/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_modbus.h - Modbus-RTU 主站（异步实现）
 *
 * 设计：
 *  - 上层通过 dbg_modbus_xxx_start() 发起请求，函数只把请求投递到内部
 *    队列立即返回，不阻塞调用方（可在 LVGL 回调里安全调用）。
 *  - 模块内部有独立工作线程：取请求 -> 发送 -> 带超时接收 -> 解析，
 *    完成后通过 dbg_msg 总线投递 DBG_MSG_MODBUS_RESULT 给 UI。
 */
#ifndef __DBG_MODBUS_H__
#define __DBG_MODBUS_H__

#include <rtthread.h>

/* Modbus 功能码（常用） */
#define MODBUS_FC_READ_COILS       0x01
#define MODBUS_FC_READ_DISCRETE    0x02
#define MODBUS_FC_READ_HOLD_REGS   0x03
#define MODBUS_FC_READ_INPUT_REGS  0x04
#define MODBUS_FC_WRITE_SINGLE_COIL    0x05
#define MODBUS_FC_WRITE_SINGLE_REG     0x06
#define MODBUS_FC_WRITE_MULTI_REGS     0x10

/* 请求结果 */
#define MODBUS_OK          0
#define MODBUS_ERR_TIMEOUT (-1)
#define MODBUS_ERR_CRC     (-2)
#define MODBUS_ERR_EXC     (-3)
#define MODBUS_ERR_SEND    (-4)
#define MODBUS_ERR_PARAM   (-5)

/* 最多返回寄存器值字节数 */
#define MODBUS_DATA_MAX    64

/* Modbus 请求类型 */
typedef enum {
    MODBUS_REQ_READ_HOLD = 0,   /* FC03 读保持寄存器 */
    MODBUS_REQ_WRITE_REG,       /* FC06 写单寄存器 */
    MODBUS_REQ_READ_COILS,      /* FC01 读线圈 */
} dbg_modbus_req_type_t;

/* 一次请求 */
typedef struct {
    dbg_modbus_req_type_t type;
    rt_uint8_t  addr;          /* 从站地址 1~247 */
    rt_uint16_t addr_hi;       /* 起始地址/寄存器地址 */
    rt_uint16_t count_hi;      /* 数量 或 写入值 */
} dbg_modbus_req_t;

/* CRC16 (poly 0xA001, init 0xFFFF)，dbg_slave 也复用它 */
rt_uint16_t dbg_modbus_crc16(const rt_uint8_t *data, rt_uint32_t len);

/* 初始化 Modbus 工作线程（内部创建消息队列 + 线程） */
int dbg_modbus_init(void);

/* 异步发起读保持寄存器(FC03)：立即返回，结果经消息总线返回 */
int dbg_modbus_start_read_hold(rt_uint8_t addr, rt_uint16_t start, rt_uint16_t count);

/* 异步发起写单寄存器(FC06)：立即返回 */
int dbg_modbus_start_write_reg(rt_uint8_t addr, rt_uint16_t reg, rt_uint16_t value);

/* 异步发起读线圈(FC01)：立即返回 */
int dbg_modbus_start_read_coils(rt_uint8_t addr, rt_uint16_t start, rt_uint16_t count);

#endif /* __DBG_MODBUS_H__ */