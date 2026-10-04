/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_modbus.c - Modbus-RTU 主站（异步实现）
 *
 * 工作线程流程：
 *   1. 从请求队列取一条请求
 *   2. 组 Modbus-RTU 帧（含 CRC16）
 *   3. 串口轮询发送（阻塞写完）
 *   4. 带超时接收应答（按字节数 / 超时判断帧结束）
 *   5. 解析应答 + CRC 校验
 *   6. 通过 dbg_msg 总线投递 DBG_MSG_MODBUS_RESULT 给 UI
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "dbg_modbus.h"
#include "dbg_serial.h"
#include "dbg_msg.h"

#define MODBUS_RX_TIMEOUT_MS   300
#define MODBUS_RX_TAIL_MS      20   /* 最后一个字节到帧结束的静默时间 */
#define MODBUS_QUEUE_NUM       16

static rt_mq_t  g_req_mq = RT_NULL;

/* 预期应答长度（用于等够帧）：实际用"静默时间"判帧尾，这里仅给最大兜底 */
#define MODBUS_MAX_FRAME       256

/* 用消息总线投递结果，打包格式：
 *   data[0..1]  = 结果码（小端int16）
 *   若成功：data[2] = 功能码 03 -> 后面是 count*2 字节寄存器值
 *             功能码 01 -> 后面是 N 字节线圈位图
 *             功能码 06 -> 无附加数据
 */
static void modbus_post_result(int err, rt_uint8_t fc,
                               const rt_uint8_t *payload, rt_uint16_t plen)
{
    dbg_msg_t msg;
    rt_uint16_t uerr;

    memset(&msg, 0, sizeof(msg));
    msg.type = DBG_MSG_MODBUS_RESULT;
    uerr = (rt_uint16_t)err;
    msg.data[0] = (rt_uint8_t)(uerr & 0xFF);
    msg.data[1] = (rt_uint8_t)(uerr >> 8);
    msg.data[2] = fc;
    msg.len = 3;
    if (err == MODBUS_OK && payload && plen > 0) {
        if (plen > DBG_MSG_DATA_MAX - 3)
            plen = DBG_MSG_DATA_MAX - 3;
        memcpy(&msg.data[3], payload, plen);
        msg.len = (rt_uint16_t)(3 + plen);
    }
    dbg_msg_send(&msg);
}

rt_uint16_t dbg_modbus_crc16(const rt_uint8_t *data, rt_uint32_t len)
{
    rt_uint16_t crc = 0xFFFF;
    rt_uint32_t i, j;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* 发送并收应答，返回 (接收帧字节数) 或负错误码 */
static int modbus_do_transact(dbg_modbus_req_t *req)
{
    rt_uint8_t tx[MODBUS_MAX_FRAME];
    rt_uint8_t rx[MODBUS_MAX_FRAME];
    rt_uint16_t crc;
    rt_uint32_t txlen = 0;
    int got, ret;

    /* 组帧 */
    tx[0] = req->addr;
    switch (req->type) {
    case MODBUS_REQ_READ_HOLD:
    case MODBUS_REQ_READ_COILS:
        tx[1] = (req->type == MODBUS_REQ_READ_HOLD) ?
                 MODBUS_FC_READ_HOLD_REGS : MODBUS_FC_READ_COILS;
        tx[2] = (rt_uint8_t)(req->addr_hi >> 8);
        tx[3] = (rt_uint8_t)(req->addr_hi & 0xFF);
        tx[4] = (rt_uint8_t)(req->count_hi >> 8);
        tx[5] = (rt_uint8_t)(req->count_hi & 0xFF);
        txlen = 6;
        break;
    case MODBUS_REQ_WRITE_REG:
        tx[1] = MODBUS_FC_WRITE_SINGLE_REG;
        tx[2] = (rt_uint8_t)(req->addr_hi >> 8);
        tx[3] = (rt_uint8_t)(req->addr_hi & 0xFF);
        tx[4] = (rt_uint8_t)(req->count_hi >> 8);
        tx[5] = (rt_uint8_t)(req->count_hi & 0xFF);
        txlen = 6;
        break;
    default:
        modbus_post_result(MODBUS_ERR_PARAM, 0, NULL, 0);
        return MODBUS_ERR_PARAM;
    }
    crc = dbg_modbus_crc16(tx, txlen);
    tx[txlen]     = (rt_uint8_t)(crc & 0xFF);
    tx[txlen + 1] = (rt_uint8_t)(crc >> 8);
    txlen += 2;

    /* 发送（阻塞写完） */
    ret = dbg_serial_send(tx, txlen);
    if (ret != RT_EOK) {
        modbus_post_result(MODBUS_ERR_SEND, 0, NULL, 0);
        return MODBUS_ERR_SEND;
    }

    /* 接收：用静默时间判帧尾 */
    got = (int)dbg_serial_recv_timeout(rx, MODBUS_MAX_FRAME, MODBUS_RX_TIMEOUT_MS);
    if (got < 4) {                      /* 至少 addr+fc+数据+2CRC */
        modbus_post_result(MODBUS_ERR_TIMEOUT, 0, NULL, 0);
        return MODBUS_ERR_TIMEOUT;
    }

    /* 异常应答：FC 最高位置 1，长度 5 */
    if ((rx[1] & 0x80) && got == 5) {
        modbus_post_result(MODBUS_ERR_EXC, rx[1], NULL, 0);
        return MODBUS_ERR_EXC;
    }

    /* CRC 校验 */
    crc = dbg_modbus_crc16(rx, (rt_uint32_t)(got - 2));
    if (crc != (rt_uint16_t)(rx[got - 2] | (rx[got - 1] << 8))) {
        modbus_post_result(MODBUS_ERR_CRC, 0, NULL, 0);
        return MODBUS_ERR_CRC;
    }

    /* 解析并投递结果 */
    switch (rx[1]) {
    case MODBUS_FC_READ_HOLD_REGS:
    case MODBUS_FC_READ_COILS:
        if (got >= 3 + (int)rx[2] + 2)
            modbus_post_result(MODBUS_OK, rx[1], &rx[3], rx[2]);
        else {
            modbus_post_result(MODBUS_ERR_CRC, 0, NULL, 0);
            return MODBUS_ERR_CRC;
        }
        break;
    case MODBUS_FC_WRITE_SINGLE_REG:
        modbus_post_result(MODBUS_OK, rx[1], NULL, 0);
        break;
    default:
        modbus_post_result(MODBUS_ERR_CRC, 0, NULL, 0);
        return MODBUS_ERR_CRC;
    }
    return MODBUS_OK;
}

/* Modbus 工作线程 */
static void dbg_modbus_thread(void *param)
{
    dbg_modbus_req_t req;

    (void)param;
    while (1) {
        if (rt_mq_recv(g_req_mq, &req, sizeof(req), RT_WAITING_FOREVER) != RT_EOK)
            continue;

        modbus_do_transact(&req);
    }
}

int dbg_modbus_init(void)
{
    rt_thread_t thread;

    g_req_mq = rt_mq_create("modbus_req", sizeof(dbg_modbus_req_t),
                            MODBUS_QUEUE_NUM, RT_IPC_FLAG_FIFO);
    if (g_req_mq == RT_NULL) {
        rt_kprintf("dbg_modbus: create req mq failed\n");
        return -RT_ERROR;
    }

    thread = rt_thread_create("dbg_modbus", dbg_modbus_thread, RT_NULL,
                              4096, 21, 10);
    if (thread == RT_NULL) {
        rt_kprintf("dbg_modbus: create thread failed\n");
        return -RT_ERROR;
    }
    rt_thread_startup(thread);

    return RT_EOK;
}

static int modbus_enqueue(dbg_modbus_req_t *req)
{
    if (g_req_mq == RT_NULL || req == RT_NULL)
        return -RT_ERROR;
    if (rt_mq_send(g_req_mq, req, sizeof(*req)) != RT_EOK)
        return -RT_EFULL;
    return RT_EOK;
}

int dbg_modbus_start_read_hold(rt_uint8_t addr, rt_uint16_t start, rt_uint16_t count)
{
    dbg_modbus_req_t req;

    if (addr == 0 || addr > 247 || count == 0 || count > 125)
        return -RT_EINVAL;

    memset(&req, 0, sizeof(req));
    req.type = MODBUS_REQ_READ_HOLD;
    req.addr = addr;
    req.addr_hi = start;
    req.count_hi = count;
    return modbus_enqueue(&req);
}

int dbg_modbus_start_write_reg(rt_uint8_t addr, rt_uint16_t reg, rt_uint16_t value)
{
    dbg_modbus_req_t req;

    if (addr == 0 || addr > 247)
        return -RT_EINVAL;

    memset(&req, 0, sizeof(req));
    req.type = MODBUS_REQ_WRITE_REG;
    req.addr = addr;
    req.addr_hi = reg;
    req.count_hi = value;
    return modbus_enqueue(&req);
}

int dbg_modbus_start_read_coils(rt_uint8_t addr, rt_uint16_t start, rt_uint16_t count)
{
    dbg_modbus_req_t req;

    if (addr == 0 || addr > 247 || count == 0 || count > 2000)
        return -RT_EINVAL;

    memset(&req, 0, sizeof(req));
    req.type = MODBUS_REQ_READ_COILS;
    req.addr = addr;
    req.addr_hi = start;
    req.count_hi = count;
    return modbus_enqueue(&req);
}