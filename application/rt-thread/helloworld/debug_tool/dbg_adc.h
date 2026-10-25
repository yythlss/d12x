/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_adc.h - 模拟量采集模块（模拟量示波数据源）
 *
 * 本模块提供"模拟量示波"的数据来源，支持两种模式：
 *  1) DBG_ADC_SRC_SYNTH  —— 自测信号源（默认）。不依赖任何实测引脚，
 *                           由内部定时器产生正弦/方波样本，用于在无外设接入时
 *                           验证示波界面、线程与消息总线的完整链路。
 *  2) DBG_ADC_SRC_GPAI   —— 片内 GPAI 真实采集。当配置中启用 GPAI
 *                           (AIC_USING_GPAI) 时，自动优先使用真实 ADC，
 *                           读取所选通道的电压值。
 *
 * 采集样本通过 dbg_msg 消息总线(DBG_MSG_ADC_VAL)投递给 LVGL 示波界面。
 * 打包格式见本文件内 DBG_ADC_MSG_* 宏。
 */
#ifndef __DBG_ADC_H__
#define __DBG_ADC_H__

#include <rtthread.h>

/* 数据源选择 */
#define DBG_ADC_SRC_SYNTH   0   /* 软件信号发生器（无硬件可验证） */
#define DBG_ADC_SRC_GPAI    1   /* 片内 GPAI 真实采集 */
#define DBG_ADC_SRC_RS485   2   /* RS485 工业总线：经星火一号网关周期读保持寄存器 */

/* DBG_MSG_ADC_VAL 消息打包格式：
 *   data[0] = 数据源(DBG_ADC_SRC_*)  + 高 4 位保留
 *   data[1..2] = ADC 原始值（小端 u16）
 *   data[3..4] = 对应电压 mV（小端 u16）  （SYNTH 模式为换算值，GPAI 为实测）
 *   data[5]   = 当前通道号
 */
#define DBG_ADC_MSG_CH_SRC   0
#define DBG_ADC_MSG_CH_ADC_LO 1
#define DBG_ADC_MSG_CH_ADC_HI 2
#define DBG_ADC_MSG_CH_MV_LO  3
#define DBG_ADC_MSG_CH_MV_HI  4
#define DBG_ADC_MSG_CH_NO    5

/* 示波可用的通道数（与芯片 GPAI 通道数一致，SYNTH 模式通道仅作显示用） */
#define DBG_ADC_CH_NUM      6

/* 可供示波显示的最大样本数（环形） */
#define DBG_ADC_SCOPE_LEN   240

/* 数据源与采集控制 */
int  dbg_adc_init(void);                /* 创建采集线程/定时器并启动 */
int  dbg_adc_deinit(void);              /* 停止并释放资源 */

/* 采集源设置：DBG_ADC_SRC_SYNTH / DBG_ADC_SRC_GPAI
 * 返回实际生效的数据源（GPAI 未启用/失败时回退 SYNTH） */
int  dbg_adc_set_source(int src);
int  dbg_adc_get_source(void);

/* 通道选择 */
int  dbg_adc_set_channel(rt_uint8_t ch);
rt_uint8_t dbg_adc_get_channel(void);

/* 启动/停止采集（start=1 开始，0 停止） */
int  dbg_adc_run(int on);

/* 当前是否在采集 */
int  dbg_adc_is_running(void);

/* 查询当前数据源名称（"SYNTH" / "GPAI" / "RS485"） */
const char *dbg_adc_source_name(void);

/* GPAI 是否可用（驱动已编译且设备已注册）。RS485 源不依赖 GPAI，恒可用。 */
int dbg_adc_gpai_available(void);

/*
 * RS485 数据源：示波页把「最近一次 RS485 扫描请求」（由扫描页 UI 配置，见
 * dbg_slave_rs485_get_last）当作采样目标，周期读其中第一个保持寄存器的值作为
 * 一个波形样本。通道(CH0..CH5)在此模式下偏移寄存器地址(reg+chan)，便于看某段连续
 * 寄存器随时间的波形。
 *
 * 仅在示波页前台(dbg_adc_rs485_active(1))且数据源为 RS485 时才发请求，避免
 * 持续占用 uart1 把仪表盘轮询饿死。
 */
void dbg_adc_rs485_active(int on);

/* 由消息总线在收到 DBG_MSG_RS485_RESULT 时调用；自带 pending 守卫，只消费示波
 * 自己发出的请求，不会误吞扫描页的结果。 */
void dbg_adc_on_rs485_result(void);

/* 线程安全地读取指定 GPAI 通道电压（mV）。
 * 与示波采样线程共用同一把锁，避免两个线程并发 drain 同一个 gpai 设备
 * （gpai 驱动内部无锁，并发调用会产生竞态）。供 dbg_power 等模块复用。
 * 通道无效或 GPAI 未启用时返回 -RT_ERROR。*/
int  dbg_adc_read_mv(rt_uint8_t ch, rt_uint16_t *mv);

#endif /* __DBG_ADC_H__ */