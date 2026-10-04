/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_can_screen.h - CAN 调试界面
 */
#ifndef __UI_CAN_SCREEN_H__
#define __UI_CAN_SCREEN_H__

#include "lvgl.h"

/* 创建 CAN 调试界面 */
lv_obj_t *ui_can_screen_create(void);

/* 收到 CAN 报文时调用（在 LVGL 上下文） */
void ui_can_on_rx(const rt_uint8_t *packed, rt_uint16_t len);

#endif /* __UI_CAN_SCREEN_H__ */
