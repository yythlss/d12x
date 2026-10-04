/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_serial.h - 串口调试模块（基于 RT-Thread 串口设备）
 */
#ifndef __DBG_SERIAL_H__
#define __DBG_SERIAL_H__

#include <rtthread.h>

/* 常用波特率 */
enum {
    DBG_BAUD_9600   = 9600,
    DBG_BAUD_19200  = 19200,
    DBG_BAUD_38400  = 38400,
    DBG_BAUD_57600  = 57600,
    DBG_BAUD_115200 = 115200,
};

/* 初始化串口（默认 uart0, 115200 8N1），启动接收线程 */
int dbg_serial_init(const char *dev_name);

/* 设置波特率 */
int dbg_serial_set_baud(rt_uint32_t baud);

/* 发送数据（阻塞写） */
int dbg_serial_send(const rt_uint8_t *data, rt_uint32_t len);

/* 带超时接收（直接读设备，供 Modbus 同步收发使用）
 * 返回实际读取字节数；超时返回 0 */
rt_uint32_t dbg_serial_recv_timeout(rt_uint8_t *buf, rt_uint32_t max_len,
                                    rt_uint32_t timeout_ms);

/* 当前波特率 */
rt_uint32_t dbg_serial_get_baud(void);

#endif /* __DBG_SERIAL_H__ */
