/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_power.h - 电量监测 + 低功耗
 *
 * 电量：复用片内 GPAI（rt_adc 设备名 "gpai"）。若 menuconfig 未开启
 *       AIC_USING_GPAI，则 dbg_power_available() 返回 0，UI 显示 N/A。
 *       开启后把 DBG_BAT_ADC_CH 设为电池分压网络所接的 GPAI 通道即可。
 *
 * 低功耗：基于 RT-Thread PM 组件请求 LIGHT 睡眠（空闲时 CPU 进轻睡眠，
 *         可被触摸/按键等中断唤醒）。
 */
#ifndef __DBG_POWER_H__
#define __DBG_POWER_H__

#include <rtthread.h>

/* ---------------- 电池参数（按实际硬件修改） ---------------- */
#define DBG_BAT_ADC_CH       0      /* GPAI 通道：电池分压网络接入的通道号 */
#define DBG_BAT_VDIV_NUM     255    /* 分压比 (R_up+R_down)/R_down，单位 0.01（255 = 2.55 倍） */
#define DBG_BAT_VDIV_DEN     100
#define DBG_BAT_CELLS        2      /* 串联电芯数（Li-ion 单节 4.2V 满 / 3.0V 空） */
#define DBG_BAT_CELL_FULL_MV 4200   /* 单节满电 mV */
#define DBG_BAT_CELL_EMPTY_MV 3000  /* 单节空电 mV */

/* ---------------- 接口 ---------------- */
int  dbg_power_init(void);
int  dbg_power_available(void);          /* 1=GPAI 可用，0=不可用（N/A） */
void dbg_power_sample(void);             /* 采样一次电压/百分比（UI 定时器周期调用） */
rt_uint8_t  dbg_power_get_pct(void);     /* 0~100 */
rt_uint16_t dbg_power_get_mv(void);      /* 电池组电压 mV */

/* 低功耗：请求/释放 LIGHT 睡眠 */
void dbg_power_suspend(void);
void dbg_power_resume(void);
int  dbg_power_is_suspended(void);

#endif /* __DBG_POWER_H__ */
