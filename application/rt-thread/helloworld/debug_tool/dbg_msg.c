/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_msg.c - 消息总线实现
 * 使用 RT-Thread 消息队列，串口/CAN 接收线程把数据投递到队列，
 * LVGL 侧用定时器轮询取消息并刷新 UI。
 */
#include <rtthread.h>
#include <string.h>
#include "dbg_msg.h"

#define DBG_MQ_MSG_NUM   64
#define DBG_MQ_BUF_SIZE  (sizeof(dbg_msg_t) * DBG_MQ_MSG_NUM)

static rt_mq_t g_dbg_mq;

int dbg_msg_init(void)
{
    g_dbg_mq = rt_mq_create("dbg_mq", sizeof(dbg_msg_t), DBG_MQ_MSG_NUM, RT_IPC_FLAG_FIFO);
    if (g_dbg_mq == RT_NULL) {
        rt_kprintf("dbg_msg: create mq failed!\n");
        return -RT_ERROR;
    }
    return RT_EOK;
}

int dbg_msg_send(dbg_msg_t *msg)
{
    if (g_dbg_mq == RT_NULL || msg == RT_NULL)
        return -RT_ERROR;

    if (rt_mq_send(g_dbg_mq, msg, sizeof(dbg_msg_t)) != RT_EOK) {
        /* 队列满，丢弃最旧的避免阻塞采集线程 */
        rt_mq_recv(g_dbg_mq, msg, sizeof(dbg_msg_t), 0);
        rt_mq_send(g_dbg_mq, msg, sizeof(dbg_msg_t));
    }
    return RT_EOK;
}

int dbg_msg_recv(dbg_msg_t *msg)
{
    if (g_dbg_mq == RT_NULL || msg == RT_NULL)
        return -RT_ERROR;

    if (rt_mq_recv(g_dbg_mq, msg, sizeof(dbg_msg_t), 0) != RT_EOK)
        return -RT_EEMPTY;

    return RT_EOK;
}
