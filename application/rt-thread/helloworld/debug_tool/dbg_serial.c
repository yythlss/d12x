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

    /* 先获取 uart0 当前有效配置（保留正确的 uart_index/function），只改波特率 */
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

    if (rt_device_open(g_serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
        rt_kprintf("dbg_serial: open %s failed!\n", dev_name);
        return -RT_ERROR;
    }

    rt_device_set_rx_indicate(g_serial, dbg_serial_rx_ind);

    thread = rt_thread_create("dbg_serial", dbg_serial_rx_entry, RT_NULL,
                              4096, 22, 10);
    if (thread == RT_NULL) {
        rt_kprintf("dbg_serial: create thread failed!\n");
        return -RT_ERROR;
    }
    rt_thread_startup(thread);

    return RT_EOK;
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
