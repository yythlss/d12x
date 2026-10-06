/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_slave.c - 从机（星火一号）Modbus-RTU 主站 + 数据打包
 *
 * 一次请求读回 0x0000~0x0025 共 38 个寄存器，覆盖：
 *   心跳 / 运行秒数 / 错误位 / 温湿度 / 光强 / 接近 / 六轴
 * 打包成 27 字节投递给仪表盘 UI（见 dbg_slave.h 的偏移定义）。
 *
 * ⚠️ 重要：轮询循环里绝对不要用 rt_kprintf 打日志。
 *   链路默认跑在 uart1 上，而 uart1 同时是 console —— 任何日志都会顺着 TX
 *   灌进从机，被当成帧头不明的报文；虽然从机会 CRC 丢弃，但白白占用链路、
 *   并且会让「偶尔超时」变成常态。调试信息请显示到屏上。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "dbg_slave.h"
#include "dbg_msg.h"
#include "dbg_modbus.h"     /* 复用 dbg_modbus_crc16() */

/* ------------------------------------------------------- 从机寄存器地图 */
#define R_HEARTBEAT     0x00
#define R_UPTIME        0x01
#define R_ERR           0x02
#define R_TEMP          0x10    /* 0.1 °C */
#define R_HUMI          0x11    /* 0.1 %  */
#define R_LUX           0x12    /* 0.1 lux*/
#define R_PS            0x13
#define R_ACC_X         0x20
#define R_ACC_Y         0x21
#define R_ACC_Z         0x22
#define R_GYRO_X        0x23
#define R_GYRO_Y        0x24
#define R_GYRO_Z        0x25

#define SLAVE_REG_START 0x0000
#define SLAVE_REG_QTY   0x0026  /* 38 个，读到 0x0025 */

/* ------------------------------------------------------------ 链路参数 */
#define SLAVE_POLL_MS      500
#define SLAVE_TIMEOUT_MS   250
#define SLAVE_T35_TICKS    5
#define SLAVE_FRAME_MAX    256

/* 请求帧固定 8 字节，应答 = 5 + 38*2 = 81 字节 */
#define SLAVE_RSP_LEN      (5 + SLAVE_REG_QTY * 2)

#if !DBG_SLAVE_LOOPBACK
static rt_device_t          g_dev    = RT_NULL;
static struct rt_semaphore  g_rx_sem;
#endif

/* 最近一帧的现场，供 msh 命令 dbg_slave_dump 诊断（短接 TX/RX 时尤其有用） */
static rt_uint8_t           g_last_rx[32];
static int                  g_last_len;
static rt_uint8_t           g_last_status;

/* 执行器本地镜像（第 6 步）：LOOPBACK 时直接反映 UI 操作；
 * 真实链路下由从机物理执行，主机不回读 DO_CTRL，故以本地镜像作为 UI 显示状态。 */
static rt_uint16_t          g_do_ctrl = 0;
static rt_uint16_t          g_pwm_duty = 0;
static rt_uint16_t          g_led_fx = 0;        /* 当前灯效模式（0=纯色） */
static rt_uint16_t          g_led_fx_arg = 3;    /* 灯效速度/参数 1~9 */

/* ------------------------------------------------------------ 小工具 */
static void put16(rt_uint8_t *p, rt_uint16_t v)
{
    p[0] = (rt_uint8_t)(v >> 8);
    p[1] = (rt_uint8_t)(v & 0xFF);
}

static rt_uint16_t get16(const rt_uint8_t *p)
{
    return (rt_uint16_t)((p[0] << 8) | p[1]);
}

/* RTU 的 CRC 是低字节在前，和数据字段的大端正好相反 */
static void put_crc(rt_uint8_t *p, rt_uint16_t v)
{
    p[0] = (rt_uint8_t)(v & 0xFF);
    p[1] = (rt_uint8_t)(v >> 8);
}

static rt_uint16_t get_crc(const rt_uint8_t *p)
{
    return (rt_uint16_t)(p[0] | (p[1] << 8));
}

#if !DBG_SLAVE_LOOPBACK
/* -------------------------------------------------------- 接收回调 */
static rt_err_t slave_rx_ind(rt_device_t dev, rt_size_t size)
{
    rt_sem_release(&g_rx_sem);
    return RT_EOK;
}

/* 收一帧：字节数吻合立即返回，否则靠静默时间收尾 */
static int slave_recv_frame(rt_uint8_t *buf, int max, int timeout_ms)
{
    int total = 0;
    rt_tick_t deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    rt_tick_t last = rt_tick_get();

    while (rt_tick_get() < deadline) {
        int n = (int)rt_device_read(g_dev, 0, buf + total, max - total);

        if (n > 0) {
            total += n;
            last = rt_tick_get();
            if (total >= 3 && total >= 3 + buf[2] + 2)
                return total;                   /* 长度对了，提前收工 */
            continue;
        }

        if (total > 0 && rt_tick_get() - last >= SLAVE_T35_TICKS)
            return total;                       /* 静默够久，帧结束 */

        rt_thread_mdelay(1);
    }
    return total;
}
#endif /* !DBG_SLAVE_LOOPBACK */

#if DBG_SLAVE_LOOPBACK
/*
 * 自测用：按协议就地造一份从机应答，数值随轮询次数缓慢变化，
 * 这样能确认 UI 是真在刷新，而不是显示一份静态值。
 */
static int slave_fake_response(rt_uint8_t *rx, int max)
{
    static rt_uint16_t tick = 0;
    rt_uint16_t regs[SLAVE_REG_QTY];
    rt_uint16_t crc;
    int i, len;

    if (max < SLAVE_RSP_LEN)
        return 0;

    memset(regs, 0, sizeof(regs));
    tick++;

    regs[R_HEARTBEAT] = tick;
    regs[R_UPTIME]    = (rt_uint16_t)(tick / 2);
    regs[R_ERR]       = 0;
    regs[R_TEMP]      = (rt_uint16_t)(265 + tick % 7);   /* 26.5 ~ 27.1 °C */
    regs[R_HUMI]      = (rt_uint16_t)(320 + tick % 5);   /* 32.0 ~ 32.4 %  */
    regs[R_LUX]       = (rt_uint16_t)(1480 + tick % 40); /* 148.0 ~ 151.9  */
    regs[R_PS]        = 7;
    regs[R_ACC_X]     = (rt_uint16_t)(rt_int16_t)-812;
    regs[R_ACC_Y]     = 16352;
    regs[R_ACC_Z]     = (rt_uint16_t)(rt_int16_t)-420;
    regs[R_GYRO_X]    = 12;
    regs[R_GYRO_Y]    = (rt_uint16_t)(rt_int16_t)-30;
    regs[R_GYRO_Z]    = 7;

    rx[0] = DBG_SLAVE_ADDR;
    rx[1] = MODBUS_FC_READ_HOLD_REGS;
    rx[2] = (rt_uint8_t)(SLAVE_REG_QTY * 2);
    for (i = 0; i < SLAVE_REG_QTY; i++)
        put16(&rx[3 + i * 2], regs[i]);

    len = 3 + SLAVE_REG_QTY * 2;
    crc = dbg_modbus_crc16(rx, (rt_uint32_t)len);
    put_crc(&rx[len], crc);

    return len + 2;
}
#endif /* DBG_SLAVE_LOOPBACK */

/* -------------------------------------------------------- 结果投递 */
static void slave_post(rt_uint8_t status, const rt_uint16_t *regs)
{
    dbg_msg_t msg;

    g_last_status = status;

    memset(&msg, 0, sizeof(msg));
    msg.type = DBG_MSG_SLAVE_DATA;
    msg.len  = SLAVE_PKT_LEN;

    if (regs != RT_NULL) {
        put16(&msg.data[SLV_OFF_HEARTBEAT], regs[R_HEARTBEAT]);
        put16(&msg.data[SLV_OFF_UPTIME],    regs[R_UPTIME]);
        put16(&msg.data[SLV_OFF_ERR],       regs[R_ERR]);
        put16(&msg.data[SLV_OFF_TEMP],      regs[R_TEMP]);
        put16(&msg.data[SLV_OFF_HUMI],      regs[R_HUMI]);
        put16(&msg.data[SLV_OFF_LUX],       regs[R_LUX]);
        put16(&msg.data[SLV_OFF_PS],        regs[R_PS]);
        put16(&msg.data[SLV_OFF_ACC_X],     regs[R_ACC_X]);
        put16(&msg.data[SLV_OFF_ACC_Y],     regs[R_ACC_Y]);
        put16(&msg.data[SLV_OFF_ACC_Z],     regs[R_ACC_Z]);
        put16(&msg.data[SLV_OFF_GYRO_X],    regs[R_GYRO_X]);
        put16(&msg.data[SLV_OFF_GYRO_Y],    regs[R_GYRO_Y]);
        put16(&msg.data[SLV_OFF_GYRO_Z],    regs[R_GYRO_Z]);
    }
    msg.data[SLV_OFF_STATUS] = status;

    dbg_msg_send(&msg);
}

/* -------------------------------------------------------- 一轮事务 */
static void slave_do_once(void)
{
    rt_uint8_t  tx[8];
    rt_uint8_t  rx[SLAVE_FRAME_MAX];
    rt_uint16_t regs[SLAVE_REG_QTY];
    rt_uint16_t crc;
    int got, i;

    /* 1. 组 FC03 请求 */
    tx[0] = DBG_SLAVE_ADDR;
    tx[1] = MODBUS_FC_READ_HOLD_REGS;
    put16(&tx[2], SLAVE_REG_START);
    put16(&tx[4], SLAVE_REG_QTY);
    crc = dbg_modbus_crc16(tx, 6);
    put_crc(&tx[6], crc);

#if DBG_SLAVE_LOOPBACK
    /* 2'. 自测：本地造一份应答，后面的解析流程与真实链路完全一致 */
    got = slave_fake_response(rx, SLAVE_FRAME_MAX);
#else
    /* 2. 发送 */
    if (rt_device_write(g_dev, 0, tx, sizeof(tx)) != sizeof(tx)) {
        slave_post(3, RT_NULL);
        return;
    }

    /* 3. 收应答 */
    got = slave_recv_frame(rx, SLAVE_FRAME_MAX, SLAVE_TIMEOUT_MS);
#endif

    /* 留一份现场给 dbg_slave_dump 诊断 */
    if (got > 0) {
        g_last_len = (got > (int)sizeof(g_last_rx)) ? (int)sizeof(g_last_rx) : got;
        memcpy(g_last_rx, rx, (rt_size_t)g_last_len);
    } else {
        g_last_len = 0;
    }

    if (got < 5) {
        slave_post(1, RT_NULL);                 /* 超时 */
        return;
    }

    /* 4. 异常应答 */
    if (rx[1] & 0x80) {
        slave_post(3, RT_NULL);
        return;
    }

    /* 5. CRC */
    if (get_crc(&rx[got - 2]) != dbg_modbus_crc16(rx, (rt_uint32_t)(got - 2))) {
        slave_post(2, RT_NULL);
        return;
    }

    /* 6. 提取寄存器（数据字段大端） */
    if (rx[2] != SLAVE_REG_QTY * 2 || got < SLAVE_RSP_LEN) {
        slave_post(2, RT_NULL);
        return;
    }
    for (i = 0; i < SLAVE_REG_QTY; i++)
        regs[i] = get16(&rx[3 + i * 2]);

    slave_post(0, regs);
}

static void dbg_slave_entry(void *param)
{
    (void)param;

    while (1) {
        slave_do_once();
        rt_thread_mdelay(SLAVE_POLL_MS);
    }
}

/* -------------------------------------------------------- 执行器写（第 6 步） */
int dbg_slave_write_reg(rt_uint16_t reg, rt_uint16_t val)
{
    rt_uint8_t  tx[8];
    rt_uint16_t crc;

    /* 只放行执行器相关的四个寄存器，避免 UI 误写采集区 */
    if (reg != R_DO_CTRL && reg != R_PWM_DUTY &&
        reg != R_LED_FX   && reg != R_LED_FX_ARG)
        return -RT_ERROR;

#if DBG_SLAVE_LOOPBACK
    /* 自测：不碰 uart1，仅更新本地镜像，UI 立即反映操作结果 */
    if      (reg == R_DO_CTRL)    g_do_ctrl  = val;
    else if (reg == R_PWM_DUTY)   g_pwm_duty = val;
    else if (reg == R_LED_FX)     g_led_fx   = val;
    else                          g_led_fx_arg = val;
    return RT_EOK;
#else
    tx[0] = DBG_SLAVE_ADDR;
    tx[1] = MODBUS_FC_WRITE_SINGLE_REG;
    put16(&tx[2], reg);
    put16(&tx[4], val);
    crc = dbg_modbus_crc16(tx, 6);
    put_crc(&tx[6], crc);

    if (rt_device_write(g_dev, 0, tx, sizeof(tx)) != sizeof(tx))
        return -RT_ERROR;
    /* 真实链路下也同步本地镜像：主机不回读这两格，UI 需要它维持按钮高亮 */
    if      (reg == R_DO_CTRL)    g_do_ctrl  = val;
    else if (reg == R_PWM_DUTY)   g_pwm_duty = val;
    else if (reg == R_LED_FX)     g_led_fx   = val;
    else                          g_led_fx_arg = val;
    return RT_EOK;
#endif
}

rt_uint16_t dbg_slave_get_do_ctrl(void)  { return g_do_ctrl; }
rt_uint16_t dbg_slave_get_pwm_duty(void) { return g_pwm_duty; }
rt_uint16_t dbg_slave_get_led_fx(void)   { return g_led_fx; }

int dbg_slave_set_led_fx(rt_uint16_t mode, rt_uint16_t arg)
{
    if (mode >= DBG_FX_MAX) return -RT_ERROR;
    if (arg < 1 || arg > 9) arg = 3;      /* 速度档位收敛到 1~9 */

    /* 切回纯色时，把 R_DO_CTRL 的三个颜色位清掉——纯色模式下这三个位是"常亮"标志，
     * 若沿用灯效期间写入的值，退出灯效会看到一颗常亮的意外颜色 */
    if (mode == DBG_FX_SOLID)
    {
        rt_uint16_t keep = (rt_uint16_t)(g_do_ctrl & (rt_uint16_t)~0x0007u);
        if (dbg_slave_write_reg(R_DO_CTRL, keep) != RT_EOK) return -RT_ERROR;
    }

    if (dbg_slave_write_reg(R_LED_FX_ARG, arg)  != RT_EOK) return -RT_ERROR;
    if (dbg_slave_write_reg(R_LED_FX, mode)     != RT_EOK) return -RT_ERROR;
    return RT_EOK;
}

int dbg_slave_init(void)
{
    rt_thread_t tid;

#if !DBG_SLAVE_LOOPBACK
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;

    g_dev = rt_device_find(DBG_SLAVE_UART_NAME);
    if (g_dev == RT_NULL) {
        rt_kprintf("dbg_slave: find %s failed\n", DBG_SLAVE_UART_NAME);
        return -RT_ERROR;
    }

    rt_sem_init(&g_rx_sem, "slv_rx", 0, RT_IPC_FLAG_FIFO);

    if (rt_device_open(g_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
        rt_kprintf("dbg_slave: open %s failed\n", DBG_SLAVE_UART_NAME);
        return -RT_ERROR;
    }

    cfg.baud_rate = DBG_SLAVE_BAUD;
    if (rt_device_control(g_dev, RT_DEVICE_CTRL_CONFIG, &cfg) != RT_EOK) {
        rt_kprintf("dbg_slave: config %s failed\n", DBG_SLAVE_UART_NAME);
    }

    /*
     * 接管接收回调：finsh 原本在这里挂着 rx_sem 唤醒自身去读 console，
     * 覆盖了之后命令行会失效，但应答字节不会再被 shell 吃掉。
     * 想保留命令行请改用独立串口（详见 dbg_slave.h 注释）。
     */
    rt_device_set_rx_indicate(g_dev, slave_rx_ind);
#endif /* !DBG_SLAVE_LOOPBACK */

    tid = rt_thread_create("dbg_slv", dbg_slave_entry, RT_NULL, 2048, 22, 10);
    if (tid == RT_NULL) {
        rt_kprintf("dbg_slave: create thread failed\n");
        return -RT_ERROR;
    }
    rt_thread_startup(tid);

#if DBG_SLAVE_LOOPBACK
    rt_kprintf("dbg_slave: LOOPBACK MODE (simulated slave, no UART used)\n");
#else
    rt_kprintf("dbg_slave: poll slave %d on %s @%d, read 0x%04X x%d\n",
               DBG_SLAVE_ADDR, DBG_SLAVE_UART_NAME, DBG_SLAVE_BAUD,
               SLAVE_REG_START, SLAVE_REG_QTY);
#endif
    return RT_EOK;
}

/* 诊断：打印最近一帧的字节与结果码（把 PA.2/PA.3 短接可做收发自检） */
static void dbg_slave_dump(void)
{
    char line[96];
    int pos = 0;
    int i, n = (g_last_len > 16) ? 16 : g_last_len;

    pos += rt_snprintf(line + pos, sizeof(line) - pos,
                       "[dbg_slave] status=%d got=%d:", g_last_status, g_last_len);
    for (i = 0; i < n; i++)
        pos += rt_snprintf(line + pos, sizeof(line) - pos, " %02X", g_last_rx[i]);

    rt_kprintf("%s\n", line);
}
MSH_CMD_EXPORT(dbg_slave_dump, dump last modbus frame from slave);
