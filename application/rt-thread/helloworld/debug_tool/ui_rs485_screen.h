/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_rs485_screen.h - RS485 扫描界面头文件
 */
#ifndef __UI_RS485_SCREEN_H__
#define __UI_RS485_SCREEN_H__

#include "lvgl.h"

/* 创建 RS485 扫描界面（首次调用创建，之后返回同一实例） */
lv_obj_t *ui_rs485_screen_create(void);

/* 收到 DBG_MSG_RS485_RESULT 后在 LVGL 上下文刷新结果（data/len 仅含状态，
 * 应答字节由 dbg_slave_rs485_get_result 取走） */
void ui_rs485_on_result(const rt_uint8_t *data, rt_uint16_t len);

#endif /* __UI_RS485_SCREEN_H__ */
