/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_d12x.c - D12x 本板运行遥测采集（不接任何外设）
 *
 * 链路：本线程每 DBG_D12X_POLL_MS 采一份 -> dbg_slave_d12x_set()
 *       -> 从机轮询线程按退避节奏 FC16 写 0x0400 区 -> 星火一号 cloud_upload 以 device="d12x" 上报。
 *
 * 容错：任一数据源的底层设备未就绪（menuconfig 没开对应驱动）时，该项填 0 上报，
 *       不阻塞其它项，也不会让线程崩溃。星火一号没接上时，最新快照会保留在从机轮询侧，
 *       按指数退避（封顶 30s）周期性探活；重连后下个窗口即把"最近值"补传，不丢趋势。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "dbg_slave.h"
#include "dbg_power.h"
#include "cpu_usage.h"       /* cpu_load_average(): 0.0~100.0，来自 LPKG cpu_usage */
#include "hal_tsen.h"        /* 结温：直接调 HAL，绕开 sensor 框架的 open/SET_MODE 坑 */

#define DBG_D12X_POLL_MS   5000
/* TSEN 设备名：drv_tsen.c 通道名是 "tsen_cpu"，而 rt_hw_sensor_register() 会按
 * 传感器类型再拼前缀（sensor_name_str[TEMP]="temp_"），故最终注册名为 "temp_tsen_cpu"。
 * ⚠️ 别写成 "sensor_cpu"/"tsen_cpu"，都 find 不到，结温会恒为 0。 */
#define DBG_TSEN_DEV_NAME  "temp_tsen_cpu"

/* 开机自检：连读 3 次结温并打印原始值（默认开）。
 * 之所以要做成自动打印而不是只留 msh 命令：本机 msh 控制台在 uart1
 * （RT_CONSOLE_DEVICE_NAME="uart1"），而 uart1 同时被从机轮询线程独占
 * （DBG_SLAVE_UART_NAME="uart1"），命令行输入会被轮询吞掉、msh 实际不可用。
 * 输出不受影响，故改为启动时自动把诊断打到串口。 */
#ifndef DBG_D12X_TSEN_SELFTEST
#define DBG_D12X_TSEN_SELFTEST  1
#endif

/* CPU 占用走 LPKG cpu_usage 包（rtconfig.h 中 LPKG_USING_CPU_USAGE=y）：
 * 该包 INIT_APP_EXPORT 自动初始化，1s 周期采样空闲线程，cpu_load_average() 返回 0.0~100.0。
 * ⚠️ 不要用 rt_cpu_usage_get()——本 SDK 的 RT-Thread 内核没有这个 API，会链接失败。
 * 包未启用时自动降级为 0。 */
#ifndef DBG_D12X_REPORT_CPU
#ifdef LPKG_USING_CPU_USAGE
#define DBG_D12X_REPORT_CPU  1
#else
#define DBG_D12X_REPORT_CPU  0
#endif
#endif

/* 结温两条读取通道：
 *  1) g_tsen_ch  —— 直接调 HAL（主路径）。drv_tsen_init 只做了 clk/irq/enable，
 *     没调 hal_tsen_ch_init()，所以这里自己补一次，否则转换可能永远不完成。
 *  2) g_tsen_dev —— sensor 框架设备（回退）。注意它的 read 语义是「读几项
 *     rt_sensor_data」（aic_tsen_fetch 固定 return 1），不是字节数。 */
static struct aic_tsen_ch *g_tsen_ch  = RT_NULL;
static rt_device_t         g_tsen_dev = RT_NULL;
static rt_uint8_t          g_snap_logged = 0;  /* 首帧诊断只打印一次 */
static rt_uint8_t          g_tsen_diagged = 0; /* 结温开机自检只做一次 */

/* 读结温，单位 0.1 ℃；两条路都失败返回 0（云端会把 0 判成「无数据」红色卡片） */
static rt_int16_t dbg_tsen_read_x10(void)
{
    s32 t = 0;

    if (g_tsen_ch != RT_NULL) {
        if (hal_tsen_get_temp(g_tsen_ch, &t) == 0)
            return (rt_int16_t)t;
    }

    if (g_tsen_dev != RT_NULL) {
        struct rt_sensor_data d;
        memset(&d, 0, sizeof(d));
        if (rt_device_read(g_tsen_dev, 0, &d, 1) == 1)
            return (rt_int16_t)d.data.temp;
    }
    return 0;
}

#if DBG_D12X_TSEN_SELFTEST
/* 连读 3 次并打印：看 raw(原始 ADC) 有没有变化，即可判断转换是否在跑 */
static void dbg_tsen_selftest(void)
{
    s32 t = 0;
    int rc, i;

    if (g_tsen_ch == RT_NULL) {
        rt_kprintf("dbg_d12x: TSEN self-test skipped (no channel)\n");
        return;
    }

    rt_kprintf("dbg_d12x: TSEN self-test ch=%s slope=%d offset=%d\n",
               (const char *)g_tsen_ch->name, g_tsen_ch->slope, g_tsen_ch->offset);
    for (i = 0; i < 3; i++) {
        t = 0;
        rc = hal_tsen_get_temp(g_tsen_ch, &t);
        rt_kprintf("dbg_d12x:   [%d] ret=%d temp=%d (=%.1fC) raw=%u\n",
                   i, rc, (int)t, t / 10.0, (unsigned)g_tsen_ch->latest_data);
        if (i != 2)
            rt_thread_mdelay(1000);
    }
}
#endif

static void dbg_d12x_collect(void)
{
    dbg_d12x_snap_t snap;
    rt_size_t       total = 0, used = 0, max_used = 0;

    memset(&snap, 0, sizeof(snap));

#if DBG_D12X_TSEN_SELFTEST
    if (!g_tsen_diagged) {
        g_tsen_diagged = 1;
        dbg_tsen_selftest();
    }
#endif

    /* 1. 结温：单位 0.1 ℃，与云端 d12x_temp 的 scale 对齐 */
    snap.temp = dbg_tsen_read_x10();

    /* 2. 电量：复用 dbg_power（片内 GPAI 分压网络），单位 0.1 %
     *    当前未接电池、外接电源供电 -> dbg_power 不可用，按满电 100% 上报（不再显示 0） */
    if (dbg_power_available()) {
        snap.bat = (rt_uint16_t)dbg_power_get_pct() * 10;
    } else {
        snap.bat = 1000;                     /* 0.1 % 单位 -> 100.0 % */
    }

    /* 3. 剩余内存：rt_memory_info，字节 -> KB
     *    total/used 显式清零：万一 rt_memory_info 未赋值，也不会打印/参与运算垃圾值。
     *    首帧打印一次原始堆统计，用于确认「剩余内存显示 0」的真实原因。 */
    rt_memory_info(&total, &used, &max_used);
    if (total > used) {
        rt_uint32_t free_kb = (rt_uint32_t)((total - used) / 1024u);
        if (free_kb > 0xFFFF)
            free_kb = 0xFFFF;                /* uint16 饱和，勿被截断 */
        snap.mem = (rt_uint16_t)free_kb;
    }

    /* 4. CPU 占用：cpu_load_average() 0.0~100.0 -> 0.1 % 定点 */
#if DBG_D12X_REPORT_CPU
    {
        float load = cpu_load_average();
        if (load < 0.0f)   load = 0.0f;
        if (load > 100.0f) load = 100.0f;
        snap.cpu = (rt_uint16_t)(load * 10.0f);
    }
#endif

    /* 5. 运行秒数 */
    snap.uptime = (rt_uint16_t)(rt_tick_get() / RT_TICK_PER_SECOND);

    /* 首帧一次性诊断：整份快照 + 原始堆统计打到串口。
     * 用途：云端把「值为 0 的字段」判成无数据(红色卡片)，所以哪项是 0 一眼可见。 */
    if (!g_snap_logged) {
        g_snap_logged = 1;
        rt_kprintf("dbg_d12x: snap temp=%d(0.1C) bat=%d(0.1%%) cpu=%d(0.1%%) "
                   "mem=%uKB up=%us | heap total=%u used=%u max=%u\n",
                   (int)snap.temp, (int)snap.bat, (int)snap.cpu,
                   (unsigned)snap.mem, (unsigned)snap.uptime,
                   (unsigned)total, (unsigned)used, (unsigned)max_used);
    }

    dbg_slave_d12x_set(&snap);
}

static void dbg_d12x_entry(void *param)
{
    (void)param;

    rt_thread_mdelay(2000);   /* 等 slave / power 模块先初始化 */

    while (1) {
        dbg_d12x_collect();
        rt_thread_mdelay(DBG_D12X_POLL_MS);
    }
}

int dbg_d12x_init(void)
{
    rt_thread_t tid;

    /* 结温主路径：直接拿 HAL 通道。drv_tsen_init 没做 hal_tsen_ch_init，这里补上，
       否则 SINGLE 模式下触发转换后等不到中断（3s 超时）-> temp 恒 0。 */
    hal_tsen_enable(1);
    g_tsen_ch = hal_tsen_get_valid_ch(AIC_TSEN_SENSOR_CPU);
    if (g_tsen_ch != RT_NULL) {
        hal_tsen_ch_init(g_tsen_ch, g_tsen_ch->pclk_rate);
        /* eFuse 校准：aic_tsen_fetch() 在每次取温前都会调它来刷新 slope/offset，
         * 走 HAL 直读时必须自己补上，否则用的是 drv_tsen.c 里的硬编码默认值，
         * 温度会系统性偏离（且在校准前后看起来"恒定"）。slope/offset 是静态的，
         * 初始化时取一次即可。 */
        hal_tsen_curve_fitting(g_tsen_ch);
        rt_kprintf("dbg_d12x: TSEN ch '%s' ready (id=%d mode=%d slope=%d offset=%d)\n",
                   g_tsen_ch->name, g_tsen_ch->id, (int)g_tsen_ch->mode,
                   g_tsen_ch->slope, g_tsen_ch->offset);
    } else {
        rt_kprintf("dbg_d12x: TSEN CPU ch unavailable, temp may be 0\n");
    }

    /* 回退通道：sensor 框架设备（open 会顺带 SET_POWER -> hal_tsen_ch_init） */
    g_tsen_dev = rt_device_find(DBG_TSEN_DEV_NAME);
    if (g_tsen_dev != RT_NULL) {
        if (rt_device_open(g_tsen_dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK) {
            rt_kprintf("dbg_d12x: open %s failed (fallback disabled)\n", DBG_TSEN_DEV_NAME);
            g_tsen_dev = RT_NULL;
        }
    } else {
        rt_kprintf("dbg_d12x: %s not found (fallback disabled)\n", DBG_TSEN_DEV_NAME);
    }

    tid = rt_thread_create("dbg_d12x", dbg_d12x_entry, RT_NULL, 1024, 15, 10);
    if (tid == RT_NULL) {
        rt_kprintf("dbg_d12x: create thread failed\n");
        return -RT_ERROR;
    }
    rt_thread_startup(tid);
    rt_kprintf("dbg_d12x: poll board telemetry every %dms (relay via spark1)\n", DBG_D12X_POLL_MS);
    return RT_EOK;
}

/* 交互式探针：连读 3 次并把结温两条通道的原始值全打出来。
 * 用途：①定位「为何是 0」；②判断读数是否真的在刷新（看 raw 有没有变化）。 */
static void dbg_tsen_probe(void)
{
    s32 t = 0;
    int rc, i;

    rt_kprintf("--- dbg_tsen probe ---\n");

    rt_kprintf("HAL  ch : %s\n", g_tsen_ch ? (const char *)g_tsen_ch->name : "NULL");
    if (g_tsen_ch != RT_NULL) {
        rt_kprintf("  id=%d avail=%d mode=%d soft_mode=%d pclk=%u slope=%d offset=%d\n",
                   g_tsen_ch->id, (int)g_tsen_ch->available,
                   (int)g_tsen_ch->mode, (int)g_tsen_ch->soft_mode,
                   (unsigned)g_tsen_ch->pclk_rate,
                   g_tsen_ch->slope, g_tsen_ch->offset);
        for (i = 0; i < 3; i++) {
            t = 0;
            rc = hal_tsen_get_temp(g_tsen_ch, &t);
            rt_kprintf("  [%d] ret=%d temp=%d (=%.1f C) raw=%u data2temp=%d\n",
                       i, rc, (int)t, t / 10.0,
                       (unsigned)g_tsen_ch->latest_data,
                       (int)hal_tsen_data2temp(g_tsen_ch));
            if (i != 2)
                rt_thread_mdelay(1000);
        }
    }

    rt_kprintf("DEV     : %s -> %s\n", DBG_TSEN_DEV_NAME,
               g_tsen_dev ? "ok" : "NULL");
    if (g_tsen_dev != RT_NULL) {
        struct rt_sensor_data d;
        rt_size_t n;

        memset(&d, 0, sizeof(d));
        n = rt_device_read(g_tsen_dev, 0, &d, 1);
        rt_kprintf("  rt_device_read -> ret=%u type=%d temp=%d (=%.1f C)\n",
                   (unsigned)n, (int)d.type, (int)d.data.temp, d.data.temp / 10.0);
    }
}
MSH_CMD_EXPORT(dbg_tsen_probe, probe D12x junction temp (TSEN));
