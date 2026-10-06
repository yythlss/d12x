/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_main_screen.c - 调试仪主菜单界面（卡片式排版，参考自带 demo 风格）
 * 三个功能入口卡片：串口调试 / CAN 调试 / 模拟量(占位)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <rtthread.h>       /* rt_uint8_t / rt_uint16_t：子页回调签名依赖 */
#include "ui_main_screen.h"
#include "ui_serial_screen.h"
#include "ui_can_screen.h"
#include "ui_scope_screen.h"
#include "ui_slave_screen.h"
#include "ui_rs485_screen.h"
#include "dbg_power.h"

#define DBG_MAIN_GAP     10
#define DBG_MAIN_TOP     70      /* 第一张卡顶部距屏幕顶 */
#define DBG_MAIN_BOTTOM  12      /* 最后一张卡底部留白 */

/* 主题色 */
#define CLR_TITLE_BAR lv_color_hex(0x243356) /* 深蓝标题栏 */
#define CLR_BG lv_color_hex(0xF0F3F7) /* 浅灰背景 */
#define CLR_SERIAL lv_color_hex(0x2E8B57) /* 串口-绿 */
#define CLR_CAN lv_color_hex(0x1E6FB8) /* CAN-蓝 */
#define CLR_ANALOG lv_color_hex(0xE07B24) /* 模拟-橙 */
#define CLR_SLAVE lv_color_hex(0x1D9E75) /* 从机-青绿 */
#define CLR_RS485 lv_color_hex(0x8E44AD) /* RS485-紫 */
#define CLR_CARD_BG lv_color_hex(0xFFFFFF) /* 卡片白 */
#define CLR_TEXT_TITLE lv_color_hex(0xFFFFFF)
#define CLR_TEXT_DARK lv_color_hex(0x333A45)

static lv_obj_t *g_serial_screen;
static lv_obj_t *g_can_screen;
static lv_obj_t *g_scope_screen;
static lv_obj_t *g_slave_screen;
static lv_obj_t *g_rs485_screen;
static lv_obj_t *g_main_screen;

/* 电量 / 低功耗 UI 元素 */
static lv_obj_t *g_lbl_bat;      /* 顶栏右侧电量标签 */
static lv_obj_t *g_btn_sleep;    /* 顶栏左侧睡眠/唤醒按钮 */
static lv_timer_t *g_bat_timer;

/* 菜单卡片数量：改这里即可自动重排 */
#define MAIN_CARD_NUM    5
#define MAIN_COLS       2                       /* 两列网格，避免 5 张卡被压得太矮 */
#define MAIN_CARD_W     (((int)LV_HOR_RES - 40 - DBG_MAIN_GAP) / MAIN_COLS)  /* (480-40-10)/2 = 215 */
#define MAIN_CARD_H     58                      /* 足够放「标题+副标题」两行，不裁切 */

static void main_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);

    switch (id) {
    case 0: /* 串口调试 */
        if (g_serial_screen == NULL)
            g_serial_screen = ui_serial_screen_create();
        lv_screen_load_anim(g_serial_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, 0);
        break;
    case 1: /* CAN 调试 */
        if (g_can_screen == NULL)
            g_can_screen = ui_can_screen_create();
        lv_screen_load_anim(g_can_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, 0);
        break;
    case 2: /* 模拟量示波 */
        if (g_scope_screen == NULL)
            g_scope_screen = ui_scope_screen_create();
        lv_screen_load_anim(g_scope_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, 0);
        break;
    case 3: /* 从机仪表盘 */
        if (g_slave_screen == NULL)
            g_slave_screen = ui_slave_screen_create();
        lv_screen_load_anim(g_slave_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, 0);
        break;
    case 4: /* RS485 扫描 */
        if (g_rs485_screen == NULL)
            g_rs485_screen = ui_rs485_screen_create();
        lv_screen_load_anim(g_rs485_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, 0);
        break;
    default:
        break;
    }
}

/* 创建一张功能卡片按钮，按 (x,y) 绝对定位（两列网格） */
static lv_obj_t *create_menu_card(lv_obj_t *parent, const char *title,
                                  const char *subtitle, lv_color_t accent,
                                  int id, int x, int y)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_size(card, MAIN_CARD_W, MAIN_CARD_H);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, y);

    /* 卡片样式：白底圆角 + 阴影 + 左侧色条 */
    lv_obj_set_style_bg_color(card, CLR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 12, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    /* 左侧色条改用左边框实现：色条若做成子对象会参与 flex 排版 */
    lv_obj_set_style_border_width(card, 6, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);

    /* 文字纵向排列并居中：卡片变矮时也不会溢出 */
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* 标题 + 副标题 */
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, CLR_TEXT_DARK, 0);

    lv_obj_t *lbl_sub = lv_label_create(card);
    lv_label_set_text(lbl_sub, subtitle);
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x8A93A1), 0);

    lv_obj_set_user_data(card, (void *)(intptr_t)id);
    lv_obj_add_event_cb(card, main_btn_cb, LV_EVENT_CLICKED, NULL);
    return card;
}

/* 电池电量周期刷新（每 2s 采样一次，更新顶栏标签） */
static void bat_timer_cb(lv_timer_t *t)
{
    char buf[32];

    (void)t;
    dbg_power_sample();

    if (!dbg_power_available()) {
        lv_label_set_text(g_lbl_bat, "BAT N/A");
    } else {
        rt_uint16_t mv = dbg_power_get_mv();
        rt_uint8_t pct = dbg_power_get_pct();
        snprintf(buf, sizeof(buf), "BAT %d%% %dmV", (int)pct, (int)mv);
        lv_label_set_text(g_lbl_bat, buf);
    }
}

/* 睡眠按钮：切换 LIGHT 睡眠 / 唤醒（轻点屏幕也可唤醒） */
static void sleep_btn_cb(lv_event_t *e)
{
    (void)e;
    if (dbg_power_is_suspended())
        dbg_power_resume();
    else
        dbg_power_suspend();

    lv_obj_t *lbl = lv_obj_get_child(g_btn_sleep, 0);
    if (lbl)
        lv_label_set_text(lbl, dbg_power_is_suspended() ? "Wake" : "Sleep");
}

lv_obj_t *ui_main_screen_create(void)
{
    if (g_main_screen != NULL)
        return g_main_screen;

    g_main_screen = lv_obj_create(NULL);

    lv_obj_set_size(g_main_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_main_screen, CLR_BG, 0);
    lv_obj_set_style_bg_opa(g_main_screen, LV_OPA_COVER, 0);

    /* 顶部标题栏 */
    lv_obj_t *head = lv_obj_create(g_main_screen);
    lv_obj_set_size(head, LV_HOR_RES, 56);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(head, CLR_TITLE_BAR, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, "Industrial Debug Tool");
    lv_obj_set_style_text_color(title, CLR_TEXT_TITLE, 0);
    lv_obj_center(title);

    /* 顶栏左侧：睡眠/唤醒按钮（轻点屏幕亦可唤醒） */
    g_btn_sleep = lv_button_create(head);
    lv_obj_set_size(g_btn_sleep, 64, 32);
    lv_obj_align(g_btn_sleep, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(g_btn_sleep, CLR_CAN, 0);
    lv_obj_set_style_bg_opa(g_btn_sleep, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_btn_sleep, 6, 0);
    {
        lv_obj_t *lbl = lv_label_create(g_btn_sleep);
        lv_label_set_text(lbl, "Sleep");
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(g_btn_sleep, sleep_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 顶栏右侧：电量标签 */
    g_lbl_bat = lv_label_create(head);
    lv_label_set_text(g_lbl_bat, "BAT --");
    lv_obj_set_style_text_color(g_lbl_bat, CLR_TEXT_TITLE, 0);
    lv_obj_align(g_lbl_bat, LV_ALIGN_RIGHT_MID, -8, 0);

    /* 电量周期刷新定时器（每 2s） */
    dbg_power_sample();
    g_bat_timer = lv_timer_create(bat_timer_cb, 2000, NULL);

    /* 功能卡片（两列网格，5 张 = 3 行） */
    {
        static const char *titles[MAIN_CARD_NUM] = {
            "Serial Debug", "CAN Debug", "Analog Scope",
            "Slave Monitor", "RS485 Scan"
        };
        static const char *subs[MAIN_CARD_NUM] = {
            "Modbus-RTU / UART", "CAN bus monitor", "ADC curve",
            "Temp / motion via Modbus", "field device passthrough"
        };
        lv_color_t clrs[MAIN_CARD_NUM];

        /* 运行时赋值：lv_color_hex 不是编译期常量，不能用于静态数组初始化 */
        clrs[0] = CLR_SERIAL; clrs[1] = CLR_CAN; clrs[2] = CLR_ANALOG;
        clrs[3] = CLR_SLAVE;  clrs[4] = CLR_RS485;

        int i;

        for (i = 0; i < MAIN_CARD_NUM; i++) {
            int col = i % MAIN_COLS;
            int row = i / MAIN_COLS;
            int x = 20 + col * (MAIN_CARD_W + DBG_MAIN_GAP);
            int y = DBG_MAIN_TOP + row * (MAIN_CARD_H + DBG_MAIN_GAP);

            create_menu_card(g_main_screen, titles[i], subs[i], clrs[i], i, x, y);
        }
    }

    return g_main_screen;
}

lv_obj_t *ui_main_screen_get(void)
{
    if (g_main_screen == NULL)
        return ui_main_screen_create();
    return g_main_screen;
}