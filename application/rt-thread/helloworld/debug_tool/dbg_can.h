/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_can.h - CAN0 调试模块
 */
#ifndef __DBG_CAN_H__
#define __DBG_CAN_H__

#include <rtthread.h>

/* CAN 帧 */
typedef struct {
    rt_uint32_t id;      /* 帧 ID（标准/扩展由 ide 决定） */
    rt_uint8_t  ide;     /* 0=标准帧, 1=扩展帧 */
    rt_uint8_t  rtr;     /* 0=数据帧, 1=远程帧 */
    rt_uint8_t  len;     /* 数据长度 0~8 */
    rt_uint8_t  data[8];
} dbg_can_frame_t;

/* 初始化 can0，启动接收线程 */
int dbg_can_init(void);

/* 设置波特率 */
int dbg_can_set_baud(rt_uint32_t baud);

/* 发送一帧（阻塞） */
int dbg_can_send(dbg_can_frame_t *frame);

/* 是否已初始化 */
int dbg_can_is_ready(void);

#endif /* __DBG_CAN_H__ */
