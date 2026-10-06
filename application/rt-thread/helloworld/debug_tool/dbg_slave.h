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
 *   1. dbg_serial(uart0) 开着 INT_RX + 自有接收线程，会把从机应答字节抢走，
 *      这里收发必须在同一个线程里闭环
 *   2. 即便把 dbg_serial 改到 uart1，它也会把应答当「串口数据」转发给串口页
 *   3. 注：uart0 的 TX 本身正常（同仓库 im_master.c 用它跑 RS485），不能复用是 rx_indicate
 *      冲突，与 uart0 TX 无关
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

/* 执行器写寄存器地址（第 6 步：仅以下这些开放写） */
#define R_DO_CTRL       0x0100   /* b0 LED_R b1 LED_G b2 LED_B b3 BEEP */
#define R_PWM_DUTY      0x0101   /* 0.1 % */
#define R_LED_FX        0x0102   /* 灯效模式 0~7，见 DBG_FX_* */
#define R_LED_FX_ARG    0x0103   /* 灯效参数（速度 1~9） */

/* 灯效模式（与从机 mb_slave.c 的 MB_FX_* 一一对应，改动需两边同步） */
#define DBG_FX_SOLID        0   /* 纯色：由 R_DO_CTRL 三位 + PWM_DUTY 亮度决定 */
#define DBG_FX_BREATH       1   /* 呼吸 */
#define DBG_FX_RAINBOW      2   /* 彩虹 */
#define DBG_FX_CHASE        3   /* 跑马 */
#define DBG_FX_COLOR_CYCLE  4   /* 颜色循环 */
#define DBG_FX_FLASH        5   /* 闪烁 */
#define DBG_FX_MARQUEE      6   /* 双向扫描 */
#define DBG_FX_SPARKLE      7   /* 星光 */
#define DBG_FX_MAX          8

/* 初始化：创建链路 + 轮询线程 */
int dbg_slave_init(void);

/* 写单个保持寄存器（FC06）：驱动从机执行器。LOOPBACK 时仅更新本地镜像，不碰 uart1。
 * 返回 RT_EOK 表示已发出（或镜像已更新）。 */
int dbg_slave_write_reg(rt_uint16_t reg, rt_uint16_t val);

/* 读取执行器本地镜像（UI 初始化按钮状态用） */
rt_uint16_t dbg_slave_get_do_ctrl(void);
rt_uint16_t dbg_slave_get_pwm_duty(void);
rt_uint16_t dbg_slave_get_led_fx(void);

/* 一次性设置灯效模式 + 参数（等价于两次 FC06，但只发一帧后者自动补齐） */
int dbg_slave_set_led_fx(rt_uint16_t mode, rt_uint16_t arg);

#endif /* __DBG_SLAVE_H__ */
