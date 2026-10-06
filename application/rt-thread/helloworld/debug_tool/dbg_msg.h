/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_msg.h - 消息总线：线程(串口/CAN/ADC) -> 消息队列 -> LVGL UI
 */
#ifndef __DBG_MSG_H__
#define __DBG_MSG_H__

#include <rtthread.h>
#include "lvgl.h"

/* 消息类型 */
#define DBG_MSG_SERIAL_RX     0x01  /* 串口收到数据 */
#define DBG_MSG_SERIAL_TX     0x02  /* 串口发出数据 */
#define DBG_MSG_CAN_RX        0x03  /* CAN 收到报文 */
#define DBG_MSG_CAN_TX        0x04  /* CAN 发出报文 */
#define DBG_MSG_ADC_VAL       0x05  /* ADC 采样值 */
#define DBG_MSG_MODBUS_RESULT 0x06  /* Modbus 应答结果 */
#define DBG_MSG_SLAVE_DATA    0x07  /* 从机仪表盘数据（见 dbg_slave.h 偏移定义） */
#define DBG_MSG_RS485_RESULT  0x08  /* RS485 透传扫描结果（dbg_rs485_result_t，见 dbg_slave.h） */

/* 消息体大小 */
#define DBG_MSG_DATA_MAX      64

typedef struct {
    rt_uint8_t  type;
    rt_uint16_t len;
    rt_uint8_t  data[DBG_MSG_DATA_MAX];
} dbg_msg_t;

/* 初始化消息总线（创建消息队列） */
int dbg_msg_init(void);

/* 发送消息（线程侧调用，非阻塞） */
int dbg_msg_send(dbg_msg_t *msg);

/* 接收消息（LVGL timer 侧调用） */
int dbg_msg_recv(dbg_msg_t *msg);

#endif /* __DBG_MSG_H__ */
