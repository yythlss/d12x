/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_main_screen.h - 调试仪主菜单界面
 */
#ifndef __UI_MAIN_SCREEN_H__
#define __UI_MAIN_SCREEN_H__

#include "lvgl.h"

/* 创建主菜单界面（按钮回调跳转到各功能页） */
lv_obj_t *ui_main_screen_create(void);

/* 返回主菜单（复用已创建的主界面，避免重复创建） */
lv_obj_t *ui_main_screen_get(void);

#endif /* __UI_MAIN_SCREEN_H__ */
