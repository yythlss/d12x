/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_serial_screen.h - 串口调试界面
 */
#ifndef __UI_SERIAL_SCREEN_H__
#define __UI_SERIAL_SCREEN_H__

#include "lvgl.h"

/* 创建串口调试界面 */
lv_obj_t *ui_serial_screen_create(void);

/* 收到串口数据时调用（在 LVGL 上下文） */
void ui_serial_on_rx(const rt_uint8_t *data, rt_uint16_t len);

/* 收到 Modbus 结果时调用（在 LVGL 上下文）
 * 打包格式见 dbg_modbus.c modbus_post_result */
void ui_serial_on_modbus_result(const rt_uint8_t *data, rt_uint16_t len);

/* 更新状态栏（波特率等） */
void ui_serial_update_status(void);

#endif /* __UI_SERIAL_SCREEN_H__ */
