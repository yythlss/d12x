/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_main_screen.h - 调试仪主界面（双页左右滑动）
 *
 * 结构：固定顶栏（Sleep/Wake + 页面标题 + 电量）+ lv_tileview 双瓦片
 *   （瓦片0 = 环境监测，瓦片1 = 应用图标网格）+ 底部页面指示点。
 * 子页面（串口/示波/从机/RS485/PWM）的返回按钮统一回到本屏。
 */
#ifndef __UI_MAIN_SCREEN_H__
#define __UI_MAIN_SCREEN_H__

#include "lvgl.h"

/* 创建主界面（内部缓存，重复调用返回同一个） */
lv_obj_t *ui_main_screen_create(void);

/* 返回主菜单（复用已创建的主界面，避免重复创建） */
lv_obj_t *ui_main_screen_get(void);

#endif /* __UI_MAIN_SCREEN_H__ */
