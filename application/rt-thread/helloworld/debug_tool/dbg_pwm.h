/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_pwm.h - D12x 板载 PWM 测试信号（第 4 步：PWM 测量）
 *
 * 板载 PWM 控制器产生一路可调方波（频率 / 占空比），用作现场调试的测试激励，
 * 或环回至定时器输入捕获引脚做自测。这里给出「设定即显示」的接口：
 * 设定值由驱动写进硬件，get 返回最近一次设定（硬件无回读寄存器，显示设定值；
 * 若要真读回，把输出脚环回至定时器输入捕获引脚并接 timer 捕获驱动）。
 *
 * 默认设备名 "pwm0" / 通道 0；若板级没注册该 PWM 或没配引脚，find 返回 NULL，
 * 本模块自动禁用（不崩、不阻塞其它功能），按实际硬件改 DBG_PWM_DEV_NAME 即可。
 */
#ifndef __DBG_PWM_H__
#define __DBG_PWM_H__

#include <rtthread.h>

/* 初始化：打开 PWM 设备（找不到则禁用） */
int dbg_pwm_init(void);

/* 设定输出：freq_hz 1~1MHz；duty_permil 0~1000（千分比，500 = 50%） */
int dbg_pwm_set(rt_uint32_t freq_hz, rt_uint32_t duty_permil);

/* 当前设定（回读显示用） */
rt_uint32_t dbg_pwm_get_freq(void);
rt_uint32_t dbg_pwm_get_duty(void);

/* 关闭输出（rt_pwm_disable）；再次 set 会重新 enable */
int dbg_pwm_disable(void);

/* 是否正在输出（1=输出中，0=已关 / 设备不可用） */
int dbg_pwm_is_enabled(void);

#endif /* __DBG_PWM_H__ */
