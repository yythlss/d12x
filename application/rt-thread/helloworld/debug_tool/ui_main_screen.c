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
#include "ui_main_screen.h"
#include "ui_serial_screen.h"
#include "ui_can_screen.h"
#include "ui_scope_screen.h"

#define DBG_MAIN_CARD_W  300
#define DBG_MAIN_CARD_H  64
#define DBG_MAIN_GAP     16

/* 主题色 */
#define CLR_TITLE_BAR lv_color_hex(0x243356) /* 深蓝标题栏 */
#define CLR_BG lv_color_hex(0xF0F3F7) /* 浅灰背景 */
#define CLR_SERIAL lv_color_hex(0x2E8B57) /* 串口-绿 */
#define CLR_CAN lv_color_hex(0x1E6FB8) /* CAN-蓝 */
#define CLR_ANALOG lv_color_hex(0xE07B24) /* 模拟-橙 */
#define CLR_CARD_BG lv_color_hex(0xFFFFFF) /* 卡片白 */
#define CLR_TEXT_TITLE lv_color_hex(0xFFFFFF)
#define CLR_TEXT_DARK lv_color_hex(0x333A45)

static lv_obj_t *g_serial_screen;
static lv_obj_t *g_can_screen;
static lv_obj_t *g_scope_screen;
static lv_obj_t *g_main_screen;

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
    default:
        break;
    }
}

/* 创建一张功能卡片按钮；ref 为 NULL 时放在顶部第一张，否则在其正下方 */
static lv_obj_t *create_menu_card(lv_obj_t *parent, const char *title,
                                  const char *subtitle, lv_color_t accent,
                                  int id, lv_obj_t *ref)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_size(card, DBG_MAIN_CARD_W, DBG_MAIN_CARD_H);
    if (ref == NULL)
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 76);
    else
        lv_obj_align_to(card, ref, LV_ALIGN_OUT_BOTTOM_MID, 0, DBG_MAIN_GAP);

    /* 卡片样式：白底圆角 + 阴影 + 左侧色条 */
    lv_obj_set_style_bg_color(card, CLR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 12, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_border_width(card, 0, 0);

    /* 左侧色条（一个窄的彩色对象作装饰） */
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_size(bar, 8, DBG_MAIN_CARD_H - 16);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(bar, accent, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_border_width(bar, 0, 0);

    /* 标题 + 副标题 */
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, CLR_TEXT_DARK, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 28, -10);

    lv_obj_t *lbl_sub = lv_label_create(card);
    lv_label_set_text(lbl_sub, subtitle);
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x8A93A1), 0);
    lv_obj_align(lbl_sub, LV_ALIGN_LEFT_MID, 28, 16);

    lv_obj_set_user_data(card, (void *)(intptr_t)id);
    lv_obj_add_event_cb(card, main_btn_cb, LV_EVENT_CLICKED, NULL);
    return card;
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

    /* 功能卡片 */
    lv_obj_t *card0 = create_menu_card(g_main_screen, "Serial Debug", "Modbus-RTU / UART", CLR_SERIAL, 0, NULL);
    lv_obj_t *card1 = create_menu_card(g_main_screen, "CAN Debug", "CAN bus monitor", CLR_CAN, 1, card0);
    lv_obj_t *card2 = create_menu_card(g_main_screen, "Analog Scope", "ADC curve", CLR_ANALOG, 2, card1);
    (void)card2;

    return g_main_screen;
}

lv_obj_t *ui_main_screen_get(void)
{
    if (g_main_screen == NULL)
        return ui_main_screen_create();
    return g_main_screen;
}