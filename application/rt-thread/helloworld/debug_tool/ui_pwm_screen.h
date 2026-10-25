/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_pwm_screen.h - 板载 PWM 测试信号页（第 4 步：PWM 测量）
 *
 * 界面：频率 / 占空比步进 + 启停。底层走 dbg_pwm（默认 pwm0/ch0，找不到自动禁用）。
 * 若板级没注册该 PWM，本页仍可见（显示 N/A），不崩、不阻塞其它功能。
 */
#ifndef __UI_PWM_SCREEN_H__
#define __UI_PWM_SCREEN_H__

#include "lvgl.h"

/* 创建并返回 PWM 测试页（单例）；重复调用返回已创建的实例 */
lv_obj_t *ui_pwm_screen_create(void);

/* 取已创建实例（无则创建） */
lv_obj_t *ui_pwm_screen_get(void);

#endif /* __UI_PWM_SCREEN_H__ */
