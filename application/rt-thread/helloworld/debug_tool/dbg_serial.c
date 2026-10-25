/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_serial.c - 串口调试模块
 * 参考 SDK 自带 serial_com_demo 的 simple_serial.c：
 *   中断接收 -> 消息队列 -> 接收线程 -> 转发到 UI 消息总线
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "dbg_serial.h"
#include "dbg_msg.h"

#define DBG_SERIAL_DEV_NAME  "uart0"
#define DBG_SERIAL_RX_MAX    512

/* 接收中断投递的信息 */
struct dbg_serial_rxinfo {
    rt_device_t dev;
    rt_size_t   size;
};

static rt_device_t g_serial = RT_NULL;
static rt_mq_t     g_rx_mq  = RT_NULL;
static rt_uint32_t g_baud   = DBG_BAUD_115200;

/* 接收线程 */
static void dbg_serial_rx_entry(void *param)
{
    struct dbg_serial_rxinfo rx_info;
    rt_uint8_t buf[DBG_SERIAL_RX_MAX];
    int len;
    dbg_msg_t msg;

    while (1) {
        /* 等接收中断投递的消息 */
        if (rt_mq_recv(g_rx_mq, &rx_info, sizeof(rx_info), RT_WAITING_FOREVER) != RT_EOK)
            continue;

        if (rx_info.size >= DBG_SERIAL_RX_MAX)
            len = rt_device_read(rx_info.dev, 0, buf, DBG_SERIAL_RX_MAX - 1);
        else
            len = rt_device_read(rx_info.dev, 0, buf, rx_info.size);

        if (len <= 0)
            continue;

        /* 组消息投递到 UI 总线（分片，避免消息体溢出） */
        int off = 0;
        while (len > 0) {
            int chunk = (len > DBG_MSG_DATA_MAX) ? DBG_MSG_DATA_MAX : len;
            memset(&msg, 0, sizeof(msg));
            msg.type = DBG_MSG_SERIAL_RX;
            msg.len  = (rt_uint16_t)chunk;
            memcpy(msg.data, &buf[off], chunk);
            dbg_msg_send(&msg);
            off += chunk;
            len  -= chunk;
        }
    }
}

/* 接收中断回调：把设备/长度投递到队列 */
static rt_err_t dbg_serial_rx_ind(rt_device_t dev, rt_size_t size)
{
    struct dbg_serial_rxinfo info = { dev, size };

    if (g_rx_mq && rt_mq_send(g_rx_mq, &info, sizeof(info)) != RT_EOK) {
        rt_kprintf("dbg_serial: rx mq full!\n");
    }
    return RT_EOK;
}

static void dbg_serial_tx_selftest(void);

int dbg_serial_init(const char *dev_name)
{
    struct serial_configure config;
    rt_thread_t thread;

    if (dev_name == RT_NULL)
        dev_name = DBG_SERIAL_DEV_NAME;

    g_serial = rt_device_find(dev_name);
    if (g_serial == RT_NULL) {
        rt_kprintf("dbg_serial: find %s failed!\n", dev_name);
        return -RT_ERROR;
    }

    g_rx_mq = rt_mq_create("dbg_uart", sizeof(struct dbg_serial_rxinfo),
                           32, RT_IPC_FLAG_FIFO);
    if (g_rx_mq == RT_NULL) {
        rt_kprintf("dbg_serial: create mq failed!\n");
        return -RT_ERROR;
    }

    /* ⚠️ 必须先 open 再下发配置！
     * RT-Thread 的 RT_DEVICE_CTRL_CONFIG 处理里有 `if (serial->parent.ref_count)`
     * 判断——未打开（ref_count==0）时只把配置存进 serial->config，**不会真正
     * 写进硬件寄存器**。原代码先 CONFIG 后 OPEN，导致波特率/数据位/校验被静默
     * 丢弃，uart0 仍跑在 bootloader 遗留的旧配置上：PC 按 115200 采样收到的是
     * 帧错误、被驱动丢弃，表现为「指示灯闪（检测到起始位）但任何显示模式都空白」，
     * 且只有复位（bootloader 重新初始化串口）时才涌出内容。 */
    if (rt_device_open(g_serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
        rt_kprintf("dbg_serial: open %s failed!\n", dev_name);
        return -RT_ERROR;
    }

    /* 打开后读取当前有效配置（保留 uart_index/function），只改波特率 */
    if (rt_device_control(g_serial, RT_SERIAL_GET_CONFIG, &config) != RT_EOK) {
        rt_kprintf("dbg_serial: get config failed!\n");
        return -RT_ERROR;
    }
    config.baud_rate = DBG_BAUD_115200;
    config.uart_index = 0;   /* uart0 硬件索引 */
    if (rt_device_control(g_serial, RT_DEVICE_CTRL_CONFIG, &config) != RT_EOK) {
        rt_kprintf("dbg_serial: config failed!\n");
        return -RT_ERROR;
    }
    g_baud = DBG_BAUD_115200;

    rt_device_set_rx_indicate(g_serial, dbg_serial_rx_ind);

    thread = rt_thread_create("dbg_serial", dbg_serial_rx_entry, RT_NULL,
                              4096, 22, 10);
    if (thread == RT_NULL) {
        rt_kprintf("dbg_serial: create thread failed!\n");
        return -RT_ERROR;
    }
    rt_thread_startup(thread);

    dbg_serial_tx_selftest();

    return RT_EOK;
}

/*
 * 开机 TX 自检：连发 3 帧「纯可打印 ASCII + CRLF」。
 *
 * 之所以做成开机自动打印而不是只留 msh 命令：本机 msh 控制台在 uart1
 * （RT_CONSOLE_DEVICE_NAME="uart1"），而 uart1 同时被从机轮询线程独占，
 * 命令行输入会被轮询吞掉、msh 实际不可用（输出正常但无法交互）。
 *
 * 判读：
 *   - 串口助手出现 3 行 "D12X UART0 TX TEST #n ABCabc012" -> 发送通路完好，
 *     之前"收不到"是载荷内容问题（发了不可打印字节）。
 *   - 指示灯闪但仍无内容 -> 问题在 PC 侧（选错 COM / 未真正打开 / 暂停显示 / 流控 / 编码）。
 */
static void dbg_serial_tx_selftest(void)
{
    struct serial_configure cfg;
    char  buf[64];
    int   i;

    memset(&cfg, 0, sizeof(cfg));
    if (rt_device_control(g_serial, RT_SERIAL_GET_CONFIG, &cfg) != RT_EOK) {
        rt_kprintf("dbg_serial: selftest: get config failed\n");
        return;
    }

    rt_kprintf("dbg_serial: selftest dev=%s baud=%d data=%d stop=%d parity=%d\n",
               DBG_SERIAL_DEV_NAME, (int)cfg.baud_rate,
               (int)cfg.data_bits, (int)cfg.stop_bits, (int)cfg.parity);

    for (i = 1; i <= 3; i++) {
        rt_size_t wr;
        int n;

        n = rt_snprintf(buf, sizeof(buf), "D12X TX TEST #%d ABCabc012\r\n", i);
        if (n <= 0)
            continue;

        /* A) 发到 uart0 —— 串口调试页所接的那路（PA.0/PA.1），
         *    PC 串口助手应看到本行文本。 */
        wr = rt_device_write(g_serial, 0, (const void *)buf, (rt_size_t)n);

        /* B) 同时发一份到控制台（RT_CONSOLE_DEVICE_NAME="uart1"），
         *    用于区分：到底哪一路串口连到了 PC。 */
        rt_kprintf("dbg_serial: UART0 write frame%d len=%d ret=%d | console: %s",
                   i, n, (int)wr, buf);

        rt_thread_mdelay(1000);
    }
}

int dbg_serial_set_baud(rt_uint32_t baud)
{
    struct serial_configure config;

    if (g_serial == RT_NULL)
        return -RT_ERROR;

    if (rt_device_control(g_serial, RT_SERIAL_GET_CONFIG, &config) != RT_EOK)
        return -RT_ERROR;

    config.baud_rate = baud;
    if (rt_device_control(g_serial, RT_DEVICE_CTRL_CONFIG, &config) != RT_EOK)
        return -RT_ERROR;

    g_baud = baud;
    return RT_EOK;
}

int dbg_serial_send(const rt_uint8_t *data, rt_uint32_t len)
{
    rt_size_t wr;

    if (g_serial == RT_NULL) {
        rt_kprintf("dbg_serial: send fail, dev NULL\n");
        return -RT_ERROR;
    }
    if (data == RT_NULL || len == 0)
        return -RT_ERROR;

    wr = rt_device_write(g_serial, 0, data, len);
    if (wr != len) {
        rt_kprintf("dbg_serial: write ret=%d expect=%d\n", (int)wr, (int)len);
        return -RT_EIO;
    }
    return RT_EOK;
}

rt_uint32_t dbg_serial_get_baud(void)
{
    return g_baud;
}

rt_uint32_t dbg_serial_recv_timeout(rt_uint8_t *buf, rt_uint32_t max_len,
                                    rt_uint32_t timeout_ms)
{
    rt_int32_t tick;
    rt_uint32_t total = 0;
    rt_size_t  rd;

    if (g_serial == RT_NULL || buf == RT_NULL || max_len == 0)
        return 0;

    tick = rt_tick_get();
    while (total < max_len) {
        /* 轮询读取设备缓冲区（已开 INT_RX 中断填充 FIFO） */
        rd = rt_device_read(g_serial, 0, buf + total, max_len - total);
        total += (rt_uint32_t)rd;

        if (total > 0) {
            /* 收到数据后继续等待一小段，凑满一帧再返回 */
            if (rt_tick_get() - tick >= rt_tick_from_millisecond(10))
                break;
        } else {
            if (rt_tick_get() - tick >= rt_tick_from_millisecond(timeout_ms))
                break;
            rt_thread_mdelay(2);
        }
    }
    return total;
}
