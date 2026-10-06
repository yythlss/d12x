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

/* --------------------------------------------------- RS485 扫描（第 7 步）
 * 扫描请求通过信号量投递给轮询线程，避免与仪表盘轮询并发占用 uart1，
 * 也避免写请求的应答被下一轮轮询「抢走」导致帧错乱。 */
static struct rt_semaphore  g_rs485_sem;
static dbg_rs485_req_t      g_rs485_req;        /* 单缓冲：UI 串行使用（一次只发一个） */
static dbg_rs485_result_t   g_rs485_result;     /* 最近一次结果，供 UI 取走 */

/* ------------------------------------------- 执行器写投递（第 6 步）
 * ⚠️ 为什么执行器写也必须走信号量（与 RS485 扫描同理）：
 *   dbg_slave_write_reg() 由 LVGL 按钮回调调用，若在 LVGL 上下文直接
 *   rt_device_write(uart1)，会与轮询线程的 slave_do_once() 并发占用同一条
 *   链路 → 字节交织、CRC 全错；且从机对 FC06 会回 8 字节回显，只发不收时
 *   回显滞留在接收缓冲，被下一轮 slave_recv_frame() 当成 FC03 应答读走
 *   → 长度不符 → 仪表盘误报 CRC 错（现场每点一次按钮就报一次）。
 *   故这里只投递请求并立即返回，事务（含读回显）由轮询线程串行完成。 */
static struct rt_semaphore  g_act_sem;
#if !DBG_SLAVE_LOOPBACK
/* LOOPBACK 下执行器写只改本地镜像，不投递事务，这两个变量无使用者 */
static rt_uint16_t          g_act_reg;
static rt_uint16_t          g_act_val;

/* 前置声明：定义在 dbg_slave_entry() 之后（与执行器写小节在一起），此处先引用 */
static void slave_do_actuator_write(rt_uint16_t reg, rt_uint16_t val);
#endif

/* 前置声明：定义见文件末尾，由轮询线程经信号量调用。
 * real/loopback 两个实现互斥存在，各自的声明也必须条件编译，
 * 否则切到真链路(LOOPBACK=0)时 dbg_rs485_run_real 只有隐式声明 → 编译失败。 */
static void dbg_rs485_run(const dbg_rs485_req_t *req);
#if DBG_SLAVE_LOOPBACK
static void dbg_rs485_run_loopback(const dbg_rs485_req_t *req);
#else
static void dbg_rs485_run_real(const dbg_rs485_req_t *req);
#endif

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
    /* 用「经过时间」而非「截止时刻」比较：rt_tick_t 约 49.7 天后会回绕，
     * 直接比较 rt_tick_get() < deadline 会在回绕后恒为假，导致每轮都超时。 */
    rt_tick_t start = rt_tick_get();
    rt_uint32_t wait_ticks = (rt_uint32_t)rt_tick_from_millisecond(timeout_ms);
    rt_tick_t last = rt_tick_get();

    while ((rt_uint32_t)(rt_tick_get() - start) < wait_ticks) {
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

#if !DBG_SLAVE_LOOPBACK
/* 发送一帧并收应答（带 CRC 校验）。返回 0 成功 / 1 超时 / 2 CRC错 / 3 从机异常 / 4 发送失败 */
static int slave_exchange(const rt_uint8_t *tx, int txlen, rt_uint8_t *rx, int rxmax, int *got)
{
    if (rt_device_write(g_dev, 0, tx, (rt_size_t)txlen) != (rt_size_t)txlen)
        return 4;

    *got = slave_recv_frame(rx, rxmax, SLAVE_TIMEOUT_MS);
    if (*got < 5)
        return 1;                                   /* 超时 */
    if (rx[1] & 0x80)
        return 3;                                   /* 从机异常应答 */
    if (get_crc(&rx[*got - 2]) != dbg_modbus_crc16(rx, (rt_uint32_t)(*got - 2)))
        return 2;                                   /* CRC 错 */
    return 0;
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
        /* 事务一律在此线程串行执行，保证 uart1 独占：
         *   1) 执行器写优先（用户在点按钮，延迟要低）
         *   2) 其次 RS485 扫描事务（保证应答不被下一轮轮询「抢走」导致帧错乱）
         *   3) 最后常规仪表盘轮询
         *
         * 三个信号量**共用一个等待窗口**：用剩余预算而不是各等满
         * SLAVE_POLL_MS，否则「执行器 500ms + 扫描 500ms」串行等待会把无操作时的
         * 仪表盘刷新周期从 500ms 拖到 1000ms（串口回复仍正确，只是刷新变慢）。
         * 预算耗尽且无请求时，本轮就做一次常规轮询，周期稳定在 SLAVE_POLL_MS。 */
        rt_tick_t  budget = rt_tick_from_millisecond(SLAVE_POLL_MS);
        rt_tick_t  entry  = rt_tick_get();
        int        did_io = 0;

#if !DBG_SLAVE_LOOPBACK
        if (rt_sem_take(&g_act_sem, budget) == RT_EOK) {
            slave_do_actuator_write(g_act_reg, g_act_val);
            did_io = 1;
            budget -= (rt_uint32_t)(rt_tick_get() - entry);      /* 写完接着用剩余预算 */
        }
#endif

        if (budget > 0 &&
            rt_sem_take(&g_rs485_sem, budget) == RT_EOK) {
            dbg_rs485_run(&g_rs485_req);
            did_io = 1;
        }

        /* 本轮既没写也没扫描：把剩下的预算花在常规轮询上，
         * 不足一个 tick 时直接进入下一轮（此时距上轮已至少间隔一个周期） */
        if (!did_io) {
            rt_tick_t wait = rt_tick_get() - entry;
            if (wait < budget)
                rt_thread_mdelay((rt_uint32_t)
                    ((budget - wait) * 1000u / (rt_uint32_t)RT_TICK_PER_SECOND));
            slave_do_once();
        }
    }
}

/* -------------------------------------------------------- 执行器写（第 6 步） */
#if !DBG_SLAVE_LOOPBACK
/* 轮询线程侧：真正发一帧 FC06 并**读掉回显**（仅轮询线程调用）。
 * 从机会把请求原样回显 8 字节，必须收掉，否则会污染下一轮 FC03 解析。 */
static void slave_do_actuator_write(rt_uint16_t reg, rt_uint16_t val)
{
    rt_uint8_t  tx[8];
    rt_uint8_t  rx[SLAVE_FRAME_MAX];
    rt_uint16_t crc;
    int         got;

    tx[0] = DBG_SLAVE_ADDR;
    tx[1] = MODBUS_FC_WRITE_SINGLE_REG;
    put16(&tx[2], reg);
    put16(&tx[4], val);
    crc = dbg_modbus_crc16(tx, 6);
    put_crc(&tx[6], crc);

    /* 返回值语义同 slave_exchange：0 成功 / 1 超时 / 2 CRC错 / 3 异常 / 4 发送失败。
     * 写失败不重试——本地镜像已同步，UI 靠镜像维持高亮，下一次操作会覆盖。 */
    slave_exchange(tx, sizeof(tx), rx, SLAVE_FRAME_MAX, &got);
}
#endif /* !DBG_SLAVE_LOOPBACK */

int dbg_slave_write_reg(rt_uint16_t reg, rt_uint16_t val)
{
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
    /* 真实链路：本地镜像立即更新（主机不回读这两格，UI 需要它维持按钮高亮），
     * 然后把帧投递给轮询线程串行收发——不在 LVGL 上下文碰 uart1。 */
    if      (reg == R_DO_CTRL)    g_do_ctrl  = val;
    else if (reg == R_PWM_DUTY)   g_pwm_duty = val;
    else if (reg == R_LED_FX)     g_led_fx   = val;
    else                          g_led_fx_arg = val;

    g_act_reg = reg;
    g_act_val = val;
    rt_sem_release(&g_act_sem);
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

/* ---------------------------------------- RS485 透传扫描（第 7 步） */
/* 组请求帧（含 2 字节 CRC 占位，网关会重算并覆盖最后 2 字节） */
static void dbg_rs485_build_req(const dbg_rs485_req_t *req, rt_uint8_t *b, int *len)
{
    int i;

    b[0] = req->addr;
    b[1] = req->func;
    put16(&b[2], req->reg);
    if (req->func == MODBUS_FC_READ_HOLD_REGS ||
        req->func == MODBUS_FC_WRITE_SINGLE_REG) {
        put16(&b[4], req->qty);          /* 读：数量；写单：值 */
        *len = 6;
    } else {                              /* FC16 写多 */
        put16(&b[4], req->qty);
        b[6] = (rt_uint8_t)(req->qty * 2);
        for (i = 0; i < (int)req->qty; i++)
            put16(&b[7 + i * 2], req->data[i]);
        *len = 7 + (int)req->qty * 2;
    }
    b[*len] = 0;                          /* 末尾补 2 字节 CRC 占位 */
    b[*len + 1] = 0;
    *len += 2;
}

/* 把结果通过消息总线回传（仅状态/长度走消息体，应答字节由 UI 调 get_result 取走） */
static void dbg_rs485_post_result(void)
{
    dbg_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = DBG_MSG_RS485_RESULT;
    msg.len  = 5;
    msg.data[0] = g_rs485_result.status;
    put16(&msg.data[1], g_rs485_result.out_len);
    put16(&msg.data[3], g_rs485_result.in_len);
    dbg_msg_send(&msg);
}

/* 扫描事务统一入口：真实/自测共用，由轮询线程调用 */
static void dbg_rs485_run(const dbg_rs485_req_t *req)
{
#if DBG_SLAVE_LOOPBACK
    dbg_rs485_run_loopback(req);
#else
    dbg_rs485_run_real(req);
#endif
}

int dbg_slave_rs485_request(const dbg_rs485_req_t *req)
{
    if (req == RT_NULL)
        return -RT_EINVAL;
    if (req->func != MODBUS_FC_READ_HOLD_REGS &&
        req->func != MODBUS_FC_WRITE_SINGLE_REG &&
        req->func != MODBUS_FC_WRITE_MULTI_REGS)
        return -RT_EINVAL;
    if (req->addr < 1 || req->addr > 247)
        return -RT_EINVAL;
    if (req->func == MODBUS_FC_READ_HOLD_REGS && (req->qty < 1 || req->qty > 125))
        return -RT_EINVAL;
    if (req->func == MODBUS_FC_WRITE_MULTI_REGS &&
        (req->qty < 1 || req->qty > DBG_RS485_FC16_MAX))
        return -RT_EINVAL;

    g_rs485_req = *req;                   /* 单缓冲，UI 串行使用（一次只发一个） */
    rt_sem_release(&g_rs485_sem);
    return RT_EOK;
}

int dbg_slave_rs485_get_result(dbg_rs485_result_t *out)
{
    if (out == RT_NULL)
        return -RT_ERROR;
    *out = g_rs485_result;
    return RT_EOK;
}

#if !DBG_SLAVE_LOOPBACK
/* 真实链路：通过星火一号的 RS485 网关完成一次透传。
 * 链路（全部走 uart1 -> 从机 FC03/FC06/FC16）：
 *   1) FC16 把请求原始字节写入 0x0380
 *   2) FC06 写 OUTLEN(0x0305) = 请求字节数
 *   3) FC06 写 CTRL(0x0300) = 1 触发网关
 *   4) FC03 轮询 ST(0x0304) 直到终态（最长 ~1.5s）
 *   5) FC03 读 0x0380 起 INLEN 字节 = 应答 */
static void dbg_rs485_run_real(const dbg_rs485_req_t *req)
{
    rt_uint8_t req_bytes[DBG_RS485_BUF_MAX + 4];
    rt_uint8_t rx[SLAVE_FRAME_MAX];
    rt_uint8_t frame[7 + 2 * DBG_RS485_BUF_MAX + 2];
    rt_uint16_t crc;
    int len, got, r, i;
    rt_uint16_t st, inlen;
    rt_uint32_t waited;

    dbg_rs485_build_req(req, req_bytes, &len);
    if (len > DBG_RS485_LOAD_MAX)
        len = DBG_RS485_LOAD_MAX;
    g_rs485_result.out_len = (rt_uint16_t)len;
    memset(g_rs485_result.rsp, 0, sizeof(g_rs485_result.rsp));
    g_rs485_result.in_len = 0;
    g_rs485_result.status = DBG_RS485_ST_TIMEOUT;   /* 默认失败 = 超时 */

    /* 1) 写请求字节到网关数据区 0x0380。
     *    关键：网关数据区「每寄存器装 1 字节（取低字节）」，但标准 FC16 要求
     *    byteCount == 2*qty。所以这里把每个请求字节放到 16 位寄存器的【低字节】、
     *    高字节填 0，byteCount 仍按标准写 2*len —— 这样既满足从机合法性检查，
     *    网关取低字节时又正好还原出原始请求帧。 */
    frame[0] = DBG_SLAVE_ADDR;
    frame[1] = MODBUS_FC_WRITE_MULTI_REGS;
    put16(&frame[2], R_RS485_DATA);
    put16(&frame[4], (rt_uint16_t)len);
    frame[6] = (rt_uint8_t)(2 * len);                /* byteCount = 2*len（标准） */
    for (i = 0; i < len; i++) {
        frame[7 + i * 2]     = 0;                    /* 高字节 = 0 */
        frame[7 + i * 2 + 1] = req_bytes[i];          /* 低字节 = 请求字节 */
    }
    crc = dbg_modbus_crc16(frame, (rt_uint32_t)(7 + 2 * len));
    put_crc(&frame[7 + 2 * len], crc);
    r = slave_exchange(frame, 7 + 2 * len + 2, rx, SLAVE_FRAME_MAX, &got);
    if (r != 0) {                                   /* 写数据区失败：网关没拿到请求 */
        dbg_rs485_post_result();
        return;
    }

    /* 2) 写 OUTLEN(0x0305) = len（网关据此取字节数，并自管补 CRC） */
    frame[0] = DBG_SLAVE_ADDR;
    frame[1] = MODBUS_FC_WRITE_SINGLE_REG;
    put16(&frame[2], R_RS485_OUTLEN);
    put16(&frame[4], (rt_uint16_t)len);
    crc = dbg_modbus_crc16(frame, 6);
    put_crc(&frame[6], crc);
    slave_exchange(frame, 8, rx, SLAVE_FRAME_MAX, &got);   /* best-effort */

    /* 3) 写 CTRL(0x0300) = 1 触发网关 */
    frame[0] = DBG_SLAVE_ADDR;
    frame[1] = MODBUS_FC_WRITE_SINGLE_REG;
    put16(&frame[2], R_RS485_CTRL);
    put16(&frame[4], 1);
    crc = dbg_modbus_crc16(frame, 6);
    put_crc(&frame[6], crc);
    slave_exchange(frame, 8, rx, SLAVE_FRAME_MAX, &got);   /* best-effort */

    /* 4) 轮询 ST（读 0x0304 起 3 寄存器：ST/OUTLEN/INLEN）直到终态 */
    st = DBG_RS485_ST_BUSY;
    inlen = 0;
    waited = 0;
    while (st == DBG_RS485_ST_IDLE || st == DBG_RS485_ST_BUSY) {
        frame[0] = DBG_SLAVE_ADDR;
        frame[1] = MODBUS_FC_READ_HOLD_REGS;
        put16(&frame[2], R_RS485_ST);
        put16(&frame[4], 3);
        crc = dbg_modbus_crc16(frame, 6);
        put_crc(&frame[6], crc);
        r = slave_exchange(frame, 8, rx, SLAVE_FRAME_MAX, &got);
        if (r == 0 && got >= 11) {
            st    = get16(&rx[3]);     /* 0x0304 ST */
            inlen = get16(&rx[7]);     /* 0x0306 INLEN */
        }
        rt_thread_mdelay(20);
        waited += 20;
        if (waited > 1500)
            break;
    }

    g_rs485_result.status = (rt_uint8_t)st;

    /* 5) 成功则读 0x0380 起 INLEN 字节（每寄存器低字节 = 应答字节） */
    if (st == DBG_RS485_ST_OK && inlen > 0) {
        if (inlen > DBG_RS485_BUF_MAX)
            inlen = DBG_RS485_BUF_MAX;
        g_rs485_result.in_len = inlen;
        frame[0] = DBG_SLAVE_ADDR;
        frame[1] = MODBUS_FC_READ_HOLD_REGS;
        put16(&frame[2], R_RS485_DATA);
        put16(&frame[4], inlen);
        crc = dbg_modbus_crc16(frame, 6);
        put_crc(&frame[6], crc);
        r = slave_exchange(frame, 8, rx, SLAVE_FRAME_MAX, &got);
        if (r == 0 && got >= (int)(5 + 2 * inlen)) {
            for (i = 0; i < (int)inlen; i++)
                g_rs485_result.rsp[i] = (rt_uint8_t)(get16(&rx[3 + i * 2]) & 0xFFu);
        } else {
            g_rs485_result.in_len = 0;   /* 读应答失败：保留 status=ok 但无数据 */
        }
    }

    dbg_rs485_post_result();
}
#endif /* !DBG_SLAVE_LOOPBACK */

#if DBG_SLAVE_LOOPBACK
/* 自测：合成一次「假现场设备」应答，镜像从机 mb_rs485_sim_rsp 的语义。
 * 证的是扫描页这条软件链路（组帧 -> 触发 -> 轮询 -> 回填 -> 解析）对，
 * 不代表真实硬件/真现场设备存在。 */
static void dbg_rs485_run_loopback(const dbg_rs485_req_t *req)
{
    rt_uint8_t rsp[DBG_RS485_BUF_MAX];
    rt_uint8_t tmp[DBG_RS485_BUF_MAX + 4];
    rt_uint16_t crc;
    int len, n = 0, i;

    dbg_rs485_build_req(req, tmp, &len);
    g_rs485_result.out_len = (rt_uint16_t)len;
    memset(g_rs485_result.rsp, 0, sizeof(g_rs485_result.rsp));
    g_rs485_result.in_len = 0;

    rsp[0] = req->addr;
    rsp[1] = req->func;
    if (req->func == MODBUS_FC_READ_HOLD_REGS) {
        rt_uint16_t q = (req->qty == 0 || req->qty > 60) ? 2 : req->qty;
        rsp[2] = (rt_uint8_t)(q * 2);
        for (i = 0; i < (int)q; i++)
            put16(&rsp[3 + i * 2], (rt_uint16_t)(0x0100 + i));   /* 假数据，便于看错位 */
        n = 3 + (int)q * 2;
    } else if (req->func == MODBUS_FC_WRITE_SINGLE_REG) {
        put16(&rsp[2], req->reg);
        put16(&rsp[4], req->qty);
        n = 6;
    } else {   /* FC16 */
        put16(&rsp[2], req->reg);
        put16(&rsp[4], req->qty);
        n = 6;
    }

    if (n + 2 <= DBG_RS485_BUF_MAX) {
        crc = dbg_modbus_crc16(rsp, (rt_uint32_t)n);
        put_crc(&rsp[n], crc);
        n += 2;
        memcpy(g_rs485_result.rsp, rsp, (rt_size_t)n);
        g_rs485_result.in_len = (rt_uint16_t)n;
        g_rs485_result.status = DBG_RS485_ST_OK;
    } else {
        g_rs485_result.status = DBG_RS485_ST_OVERFLOW;
    }

    dbg_rs485_post_result();
}
#endif /* DBG_SLAVE_LOOPBACK */

int dbg_slave_init(void)
{
    rt_thread_t tid;

    rt_sem_init(&g_rs485_sem, "slv_rs", 0, RT_IPC_FLAG_FIFO);   /* 扫描事务信号量 */
    /* 执行器写信号量：LOOPBACK 下不使用，但仍初始化以免条件编译下漏掉 */
    rt_sem_init(&g_act_sem, "slv_act", 0, RT_IPC_FLAG_FIFO);

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
