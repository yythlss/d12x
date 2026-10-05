/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_slave.h - 从机（星火一号）数据采集 + 仪表盘数据源
 *
 * 职责：
 *   - 独占一路 UART 做 Modbus-RTU 主站，周期轮询从机保持寄存器
 *   - 结果打包后经 dbg_msg 总线投递 DBG_MSG_SLAVE_DATA 给仪表盘 UI
 *
 * 为什么不复用 dbg_modbus.c：
 *   1. dbg_modbus 走 dbg_serial(uart0)，而 uart0 的 TX 在 RT-Thread 下发不出数据（已知问题）
 *   2. dbg_serial 开着 INT_RX + 自有接收线程，会把从机的应答字节抢走，
 *      这里 transceiver 和接收必须在同一个线程里闭环
 *   3. 即便把 dbg_serial 改到 uart1，它也会把应答当「串口数据」转发给串口页
 *   所以这里独立实现一条精简主站链路，只复用 dbg_modbus_crc16() 和消息总线。
 *
 * 数据打包格式见 dbg_slave.c 顶部注释。
 */
#ifndef __DBG_SLAVE_H__
#define __DBG_SLAVE_H__

#include <rtthread.h>

/* 链路：默认用 uart1 (PA.2/PA.3)。
 * 若后续需要命令行，可新增 uart2 并把这里改成 "uart2"，同时注释/关闭 FINSH。 */
#define DBG_SLAVE_UART_NAME  "uart1"
#define DBG_SLAVE_BAUD       115200
#define DBG_SLAVE_ADDR       1

/*
 * 自测开关：手上还没有从机板子时置 1。
 *   1 = 不走真实 UART，本地生成一份符合协议的应答帧，
 *       其余「CRC 校验 → 解析 → 打包 → 消息总线 → UI 刷新」流程完全真实。
 *       副产品：不占用 uart1，msh 命令行照常可用。
 *   0 = 正常轮询真实从机（对接硬件时必须改回 0）
 */
#define DBG_SLAVE_LOOPBACK   1

/* 一次打包的数据长度（字节） */
#define SLAVE_PKT_LEN        27

/* 数据打包偏移 */
#define SLV_OFF_HEARTBEAT    0
#define SLV_OFF_UPTIME       2
#define SLV_OFF_ERR          4
#define SLV_OFF_TEMP         6
#define SLV_OFF_HUMI         8
#define SLV_OFF_LUX          10
#define SLV_OFF_PS           12
#define SLV_OFF_ACC_X        14
#define SLV_OFF_ACC_Y        16
#define SLV_OFF_ACC_Z        18
#define SLV_OFF_GYRO_X       20
#define SLV_OFF_GYRO_Y       22
#define SLV_OFF_GYRO_Z       24
#define SLV_OFF_STATUS       26   /* 0=OK 1=超时 2=CRC错 3=从机异常 */

/* 初始化：创建链路 + 轮询线程 */
int dbg_slave_init(void);

#endif /* __DBG_SLAVE_H__ */
