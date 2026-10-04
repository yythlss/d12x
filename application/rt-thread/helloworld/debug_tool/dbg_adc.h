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

/* 查询当前数据源名称（"SYNTH" / "GPAI"） */
const char *dbg_adc_source_name(void);

#endif /* __DBG_ADC_H__ */