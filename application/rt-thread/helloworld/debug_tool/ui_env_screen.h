/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_env_screen.h - 环境监测瓦片（主界面第 1 页的内容）
 * 数据来源：dbg_slave 线程 -> dbg_msg 总线 -> LVGL timer -> ui_env_on_data()
 */
#ifndef __UI_ENV_SCREEN_H__
#define __UI_ENV_SCREEN_H__

#include <rtthread.h>
#include "lvgl.h"

/*
 * 构建环境监测瓦片内容（parent = 主界面 tileview 的第 1 个 tile）。
 * 内部缓存，重复调用返回同一个 tile。返回 parent 本身。
 */
lv_obj_t *ui_env_tile_create(lv_obj_t *parent);

/* 由 LVGL 定时器回调调用（不可在其他线程直接调用） */
void ui_env_on_data(const rt_uint8_t *data, rt_uint16_t len);

#endif /* __UI_ENV_SCREEN_H__ */
