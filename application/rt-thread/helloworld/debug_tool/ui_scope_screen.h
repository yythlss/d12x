/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_scope_screen.h - 模拟量示波界面（实时滚动曲线）
 *
 * 通过 dbg_msg 消息总线接收 DBG_MSG_ADC_VAL 采样值，
 * 在图上以滚动方式绘制曲线。数据源可为：
 *   - SYNTH：内部软件信号源（无外设即可看到正弦/方波滚动）
 *   - GPAI ：片内 ADC 实测电压
 */
#ifndef __UI_SCOPE_SCREEN_H__
#define __UI_SCOPE_SCREEN_H__

#include <rtthread.h>
#include "lvgl.h"

/* 创建示波界面 */
lv_obj_t *ui_scope_screen_create(void);

/* 收到 ADC 采样值（在 LVGL 上下文调用）
 * data 打包格式见 dbg_adc.h 的 DBG_ADC_MSG_* */
void ui_scope_on_adc(const rt_uint8_t *data, rt_uint16_t len);

#endif /* __UI_SCOPE_SCREEN_H__ */