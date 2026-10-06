/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_log.c - 第 8 步：采样数据写 SD 卡，导出 CSV
 *
 * 数据流：
 *   dbg_slave 轮询线程 ──27B 报文──▶ dbg_msg 总线 ──▶ dbg_main 分发（LVGL 线程）
 *        ──▶ dbg_log_on_data()（只入环形缓冲）──▶ dbglog 线程 ──▶ CSV 文件
 *
 * 为什么要有环形缓冲和独立线程：
 *   消息分发跑在 LVGL timer 上下文，在里面 open/write 一张慢卡会把界面卡住。
 *   这里让 UI 侧只做 memcpy，真正落盘交给低优先级的 dbglog 线程。
 *   缓冲满了丢最旧的一条 —— 宁可丢采样，也不阻塞 UI。
 */

#include <rtthread.h>
#include <string.h>
#include <dfs_posix.h>
#include <fcntl.h>
#include <dfs_fs.h>

#include "dbg_log.h"
#include "dbg_slave.h"

#define DBG_TAG "dbglog"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/*
 * 自测开关：手上没插 SD 卡时置 1。
 *   1 = 不落卡，把前几行 CSV 打到串口，用来验证「入队 → 格式化 → 出队」这条链路
 *       以及 CSV 字段顺序/小数位是否正确（LOOPBACK 的 dbg_slave 正好提供数据源）
 *   0 = 真正写 /sdcard
 *
 * 2026-10-06：软件链路已验证通过（串口 CSV 行格式/起停/行数全对），现改为 0 真落卡。
 *   注意数据源仍是 dbg_slave 的仿真值 —— 想要真实传感器读数，
 *   还要接线并把 dbg_slave.h 的 DBG_SLAVE_LOOPBACK 改成 0。两个开关互相独立。
 */
#define DBG_LOG_LOOPBACK      0

/* 自测模式下最多打印几行（再往后只计数，避免串口刷屏） */
#define DBG_LOG_LB_PRINT_ROWS 3

/* 环形缓冲：32 条 × 27 字节。从机 500ms 一轮，能扛 16 秒的写卡卡顿。 */
#define DBG_LOG_RING          32

/* 线程节拍：200ms 醒来把积累的采样一次性写掉 */
#define DBG_LOG_TICK_MS       200
#define DBG_LOG_STACK         2048
#define DBG_LOG_PRIO          20

/* 文件名序号上限，超出则回绕覆盖（正常使用到不了） */
#define DBG_LOG_MAX_SEQ       999

/* ------------------------------------------------------------- 内部状态 */
static struct
{
    rt_uint8_t buf[DBG_LOG_RING][SLAVE_PKT_LEN];
} g_ring;

static volatile rt_uint32_t g_widx;      /* 写指针（LVGL 线程推进） */
static rt_uint32_t          g_ridx;      /* 读指针（dbglog 线程推进） */

static rt_thread_t g_thread  = RT_NULL;
static int         g_fd      = -1;
static int         g_state   = DBG_LOG_ST_IDLE;
static rt_uint32_t g_rows;
static rt_tick_t   g_start_tick;
static char        g_path[24] = "-";       /* 短名，UI 显示用 */
static char        g_full_path[48];        /* 含挂载点的完整路径 */

/* 单调递增的 widx 让「满/空」判断无需额外标志：ridx == widx 即空 */
#define RING_COUNT()        (g_widx - g_ridx)
#define RING_SLOT(idx)      ((idx) % DBG_LOG_RING)

/* ------------------------------------------------------------ 小工具 */
static rt_int16_t raw_s16(const rt_uint8_t *p)
{
    return (rt_int16_t)(((rt_uint16_t)p[0] << 8) | p[1]);
}

static rt_uint16_t raw_u16(const rt_uint8_t *p)
{
    return (rt_uint16_t)(((rt_uint16_t)p[0] << 8) | p[1]);
}

/* raw 单位为 0.1，输出一位小数。避开浮点，且负号只出现在整数部分。 */
static void fmt_dec1(char *dst, int size, rt_int16_t raw)
{
    int         neg = (raw < 0);
    rt_uint16_t v   = neg ? (rt_uint16_t)(-raw) : (rt_uint16_t)raw;

    rt_snprintf(dst, size, "%s%u.%u", neg ? "-" : "", v / 10u, v % 10u);
}

/* -------------------------------------------------------- 挂载与文件 */
/*
 * 确保 /sdcard 可用。
 * 本工程 board.c 的 mount_table 里有 {"sd1", "/sdcard", "elm", ...}，开机会自动挂一次；
 * 但开机没插卡的话那次会失败，且没开热插拔检测（drv_sdcard.c 整段被 #ifdef 掉了），
 * 所以这里再挂一次 —— 用户插卡后点一下 REC 就能用，不用重启。
 * 最终以「能否创建文件」为准，不依赖 dfs_mount 的返回值（已挂载时它会失败）。
 */
static int ensure_mounted(void)
{
    int fd;

#if DBG_LOG_LOOPBACK
    return RT_EOK;                       /* 自测：不碰卡 */
#endif

    dfs_mount(DBG_LOG_BLK_DEVICE, DBG_LOG_MOUNT_POINT, "elm", 0, 0);
    mkdir(DBG_LOG_MOUNT_POINT, 0);       /* 挂载点目录；已存在会失败，忽略 */

    fd = open(DBG_LOG_MOUNT_POINT "/.probe", O_CREAT | O_WRONLY, 0644);
    if (fd < 0)
        return -RT_ERROR;

    close(fd);
    return RT_EOK;
}

/* 用 O_EXCL 找第一个不存在的名字，绝不覆盖已有记录 */
static int create_log_file(void)
{
    int seq;

    for (seq = 1; seq <= DBG_LOG_MAX_SEQ; seq++)
    {
        int fd;

        rt_snprintf(g_path, sizeof(g_path), "dbg_%03d.csv", seq);
        rt_snprintf(g_full_path, sizeof(g_full_path),
                    DBG_LOG_MOUNT_POINT "/dbg_%03d.csv", seq);

        fd = open(g_full_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd >= 0)
            return fd;
    }

    rt_snprintf(g_path, sizeof(g_path), "-");
    return -1;
}

/* ------------------------------------------------------------ 落盘 */
static int write_line(const char *line, int len)
{
#if DBG_LOG_LOOPBACK
    /* 自测：只打印头几行，之后静默计数，避免串口刷屏 */
    if (g_rows < DBG_LOG_LB_PRINT_ROWS)
        rt_kprintf("[dbglog] %s", line);
    return len;
#else
    if (g_fd < 0)
        return -1;
    return (int)write(g_fd, line, len);
#endif
}

/*
 * 把一条 27 字节报文写成一行 CSV。
 * 列：t_s,temp_C,humi_pct,lux,ps,acc_x,acc_y,acc_z,gyro_x,gyro_y,gyro_z
 * t_s 是「开始记录后的秒数」，方便直接拿去画曲线。
 */
static void write_one_row(const rt_uint8_t *pkt)
{
    char        line[160];
    char        tbuf[16], hbuf[16], lbuf[16];
    int         n;
    rt_uint32_t sec;

    /* 状态非 0（超时/CRC 错/异常）的报文不记，避免脏数据混进曲线 */
    if (pkt[SLV_OFF_STATUS] != 0)
        return;

    sec = (rt_uint32_t)((rt_tick_get() - g_start_tick) / RT_TICK_PER_SECOND);

    fmt_dec1(tbuf, sizeof(tbuf), raw_s16(&pkt[SLV_OFF_TEMP]));
    fmt_dec1(hbuf, sizeof(hbuf), raw_s16(&pkt[SLV_OFF_HUMI]));
    fmt_dec1(lbuf, sizeof(lbuf), raw_s16(&pkt[SLV_OFF_LUX]));

    n = rt_snprintf(line, sizeof(line),
                    "%u,%s,%s,%s,%u,%d,%d,%d,%d,%d,%d\r\n",
                    sec, tbuf, hbuf, lbuf,
                    (unsigned)raw_u16(&pkt[SLV_OFF_PS]),
                    (int)raw_s16(&pkt[SLV_OFF_ACC_X]),
                    (int)raw_s16(&pkt[SLV_OFF_ACC_Y]),
                    (int)raw_s16(&pkt[SLV_OFF_ACC_Z]),
                    (int)raw_s16(&pkt[SLV_OFF_GYRO_X]),
                    (int)raw_s16(&pkt[SLV_OFF_GYRO_Y]),
                    (int)raw_s16(&pkt[SLV_OFF_GYRO_Z]));

    if (n > 0 && n < (int)sizeof(line))
    {
        if (write_line(line, n) > 0)
            g_rows++;
    }
}

/* --------------------------------------------------------- 记录线程 */
static void dbg_log_thread_entry(void *parameter)
{
    (void)parameter;

    for (;;)
    {
        if (g_state == DBG_LOG_ST_RUNNING)
        {
            /* 把缓冲里积累的采样一次性写掉 */
            while (RING_COUNT() > 0)
            {
                write_one_row(g_ring.buf[RING_SLOT(g_ridx)]);
                g_ridx++;
            }
        }
        else
        {
            /* 没在记录：丢弃，避免停止期间堆积的数据在下次开始时爆出来 */
            g_ridx = g_widx;
        }

        rt_thread_mdelay(DBG_LOG_TICK_MS);
    }
}

/* ------------------------------------------------------------- 对外 */
int dbg_log_init(void)
{
    g_widx  = 0;
    g_ridx  = 0;
    g_rows  = 0;
    g_fd    = -1;
    g_state = DBG_LOG_ST_IDLE;

    g_thread = rt_thread_create("dbglog", dbg_log_thread_entry, RT_NULL,
                                DBG_LOG_STACK, DBG_LOG_PRIO, 20);
    if (g_thread == RT_NULL)
    {
        LOG_E("create dbglog thread failed");
        return -RT_ERROR;
    }

    rt_thread_startup(g_thread);
    return RT_EOK;
}

int dbg_log_start(void)
{
    int fd;

    if (g_state == DBG_LOG_ST_RUNNING)
        return RT_EOK;

    if (ensure_mounted() != RT_EOK)
    {
        g_state = DBG_LOG_ST_NOCARD;
        LOG_W("sdcard not available (%s), cannot start", DBG_LOG_MOUNT_POINT);
        return -RT_ERROR;
    }

#if !DBG_LOG_LOOPBACK
    fd = create_log_file();
    if (fd < 0)
    {
        LOG_E("create csv file failed");
        return -RT_ERROR;
    }
    g_fd = fd;

    {
        static const char header[] =
            "t_s,temp_C,humi_pct,lux,ps,acc_x,acc_y,acc_z,gyro_x,gyro_y,gyro_z\r\n";
        write(g_fd, header, sizeof(header) - 1);
    }
#else
    rt_snprintf(g_path, sizeof(g_path), "LOOPBACK (no card)");
#endif

    /* 丢掉停止期间堆的旧数据，从干净状态开始 */
    g_ridx       = g_widx;
    g_rows       = 0;
    g_start_tick = rt_tick_get();
    g_state      = DBG_LOG_ST_RUNNING;

    LOG_I("recording started: %s", g_path);
    return RT_EOK;
}

int dbg_log_stop(void)
{
    if (g_state != DBG_LOG_ST_RUNNING)
        return RT_EOK;

    /* 先把残留的采样写完再关 */
    while (RING_COUNT() > 0)
    {
        write_one_row(g_ring.buf[RING_SLOT(g_ridx)]);
        g_ridx++;
    }

#if !DBG_LOG_LOOPBACK
    if (g_fd >= 0)
    {
        close(g_fd);        /* close 会把缓存冲进卡里，正常停止不会丢数据 */
        g_fd = -1;
    }
#endif

    g_state = DBG_LOG_ST_IDLE;
    LOG_I("recording stopped, %u rows", g_rows);
    return RT_EOK;
}

int dbg_log_toggle(void)
{
    if (g_state == DBG_LOG_ST_RUNNING)
        dbg_log_stop();
    else
        dbg_log_start();

    return g_state;
}

int dbg_log_state(void)
{
    return g_state;
}

rt_uint32_t dbg_log_rows(void)
{
    return g_rows;
}

const char *dbg_log_path(void)
{
    return g_path;
}

/*
 * 数据入口：由 dbg_main.c 的消息分发调用，跑在 LVGL 线程。
 * 只做 memcpy + 推进写指针，绝不碰文件系统。
 */
void dbg_log_on_data(const rt_uint8_t *data, rt_uint16_t len)
{
    rt_uint32_t slot;

    if (data == RT_NULL || len < SLAVE_PKT_LEN)
        return;

    if (g_state != DBG_LOG_ST_RUNNING)
        return;

    /* 满则丢最旧的一条，保证 UI 不被阻塞 */
    if (RING_COUNT() >= DBG_LOG_RING)
        g_ridx++;

    slot = RING_SLOT(g_widx);
    memcpy(g_ring.buf[slot], data, SLAVE_PKT_LEN);
    g_widx++;
}
