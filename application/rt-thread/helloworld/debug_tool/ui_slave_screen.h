/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_slave_screen.h - 从机仪表盘界面
 * 数据来源：dbg_slave 线程 -> dbg_msg 总线 -> LVGL timer -> ui_slave_on_data()
 */
#ifndef __UI_SLAVE_SCREEN_H__
#define __UI_SLAVE_SCREEN_H__

#include <rtthread.h>
#include "lvgl.h"

/* 创建界面（内部缓存，重复调用返回同一个） */
lv_obj_t *ui_slave_screen_create(void);

/* 由 LVGL 定时器回调调用（不可在其他线程直接调用） */
void ui_slave_on_data(const rt_uint8_t *data, rt_uint16_t len);

#endif /* __UI_SLAVE_SCREEN_H__ */
