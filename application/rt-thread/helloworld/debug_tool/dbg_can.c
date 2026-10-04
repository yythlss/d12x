/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_can.c - CAN0 调试模块
 * 参考 bsp/examples/test-can/test_can.c 的 RT-Thread CAN API。
 * 注意：d12x CAN0 是控制器，板上需外接 CAN 收发器(如 TJA1050)才能
 * 与总线通信；无收发器时可先用回环/自发自收验证。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "dbg_can.h"
#include "dbg_msg.h"

#define DBG_CAN_DEV_NAME   "can0"
#define DBG_CAN_BAUD       (1 * 1000 * 1000)  /* 1Mbps，可改 */

static rt_device_t g_can_dev = RT_NULL;
static struct rt_semaphore g_can_rx_sem;
static int g_can_ready = 0;

static rt_err_t dbg_can_rx_call(rt_device_t dev, rt_size_t size)
{
    rt_sem_release(&g_can_rx_sem);
    return RT_EOK;
}

static void dbg_can_rx_entry(void *param)
{
    struct rt_can_msg rxmsg = {0};
    dbg_msg_t msg;
    rt_size_t size;

    while (1) {
        rt_sem_take(&g_can_rx_sem, RT_WAITING_FOREVER);

        rxmsg.hdr = -1;
        size = rt_device_read(g_can_dev, 0, &rxmsg, sizeof(rxmsg));
        if (size == 0)
            continue;

        /* 组消息投递到 UI 总线 */
        memset(&msg, 0, sizeof(msg));
        msg.type = DBG_MSG_CAN_RX;
        msg.len  = 3 + rxmsg.len;   /* id(4)存前4字节 + len + data */

        /* 打包：4字节ID + 1字节len + 最多8字节数据 */
        msg.data[0] = (rt_uint8_t)(rxmsg.id & 0xFF);
        msg.data[1] = (rt_uint8_t)((rxmsg.id >> 8) & 0xFF);
        msg.data[2] = (rt_uint8_t)((rxmsg.id >> 16) & 0xFF);
        msg.data[3] = (rt_uint8_t)((rxmsg.id >> 24) & 0xFF);
        msg.data[4] = rxmsg.len;
        memcpy(&msg.data[5], rxmsg.data, rxmsg.len);
        msg.len = (rt_uint16_t)(5 + rxmsg.len);

        dbg_msg_send(&msg);
    }
}

int dbg_can_init(void)
{
    rt_thread_t thread;
    rt_err_t ret;

    g_can_dev = rt_device_find(DBG_CAN_DEV_NAME);
    if (g_can_dev == RT_NULL) {
        rt_kprintf("dbg_can: find %s failed!\n", DBG_CAN_DEV_NAME);
        return -RT_ERROR;
    }

    ret = rt_device_open(g_can_dev, RT_DEVICE_FLAG_INT_TX | RT_DEVICE_FLAG_INT_RX);
    if (ret != RT_EOK) {
        rt_kprintf("dbg_can: open %s failed!\n", DBG_CAN_DEV_NAME);
        return -RT_ERROR;
    }

    ret = rt_device_control(g_can_dev, RT_CAN_CMD_SET_BAUD, (void *)DBG_CAN_BAUD);
    if (ret != RT_EOK) {
        rt_kprintf("dbg_can: set baud failed!\n");
        rt_device_close(g_can_dev);
        return -RT_ERROR;
    }

    rt_device_control(g_can_dev, RT_DEVICE_CTRL_SET_INT, RT_NULL);
    rt_device_set_rx_indicate(g_can_dev, dbg_can_rx_call);

    rt_sem_init(&g_can_rx_sem, "dbg_can_rx", 0, RT_IPC_FLAG_PRIO);

    thread = rt_thread_create("dbg_can", dbg_can_rx_entry, RT_NULL,
                              2048, 21, 10);
    if (thread == RT_NULL) {
        rt_kprintf("dbg_can: create thread failed!\n");
        rt_device_close(g_can_dev);
        return -RT_ERROR;
    }
    rt_thread_startup(thread);

    g_can_ready = 1;
    rt_kprintf("dbg_can: can0 ready, baud=%d\n", DBG_CAN_BAUD);
    return RT_EOK;
}

int dbg_can_set_baud(rt_uint32_t baud)
{
    rt_err_t ret;

    if (g_can_dev == RT_NULL)
        return -RT_ERROR;

    ret = rt_device_control(g_can_dev, RT_CAN_CMD_SET_BAUD, (void *)baud);
    return (ret == RT_EOK) ? RT_EOK : -RT_ERROR;
}

int dbg_can_send(dbg_can_frame_t *frame)
{
    struct rt_can_msg msg;
    rt_size_t size;

    if (g_can_dev == RT_NULL || frame == RT_NULL)
        return -RT_ERROR;

    memset(&msg, 0, sizeof(msg));
    msg.id  = frame->id;
    msg.ide = frame->ide;
    msg.rtr = frame->rtr;
    msg.len = frame->len;
    memcpy(msg.data, frame->data, frame->len);

    size = rt_device_write(g_can_dev, 0, &msg, sizeof(msg));
    if (size != sizeof(msg)) {
        rt_kprintf("dbg_can: send failed!\n");
        return -RT_EIO;
    }
    return RT_EOK;
}

int dbg_can_is_ready(void)
{
    return g_can_ready;
}
