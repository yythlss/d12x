/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_can_screen.c - CAN 调试界面（卡片式排版）
 * 功能：CAN 报文收发、ID/数据输入、键盘
 */
#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include "ui_can_screen.h"
#include "dbg_can.h"
#include "ui_main_screen.h"

#define CAN_LOG_MAX   1024

/* 配色 */
#define CLR_TITLE_BAR lv_color_hex(0x1E6FB8) /* CAN 蓝 */
#define CLR_BG lv_color_hex(0xF0F3F7)
#define CLR_CARD_BG lv_color_hex(0xFFFFFF)
#define CLR_TEXT_WHITE lv_color_hex(0xFFFFFF)
#define CLR_TEXT_DARK lv_color_hex(0x333A45)
#define CLR_ACCENT lv_color_hex(0x1E6FB8)

static lv_obj_t *g_can_screen;
static lv_obj_t *g_rx_label;
static lv_obj_t *g_ta_id;
static lv_obj_t *g_keyboard;
static char g_can_log[CAN_LOG_MAX];

/* 前置声明（can_kb_ready_cb 引用） */
static void can_send_cb(lv_event_t *e);

/* ID 输入框聚焦/失焦时显示/隐藏键盘 */
static void can_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        if (g_keyboard == NULL)
            g_keyboard = lv_keyboard_create(g_can_screen);
        lv_keyboard_set_textarea(g_keyboard, g_ta_id);
        lv_obj_move_foreground(g_keyboard);
        lv_obj_clear_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN); /* 恢复为可见 */
        lv_obj_set_style_opa(g_keyboard, LV_OPA_COVER, 0);
        /* 输入框上移，避免被底部键盘遮挡 */
        lv_obj_set_style_translate_y(g_ta_id, -120, 0);
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (g_keyboard != NULL) {
            lv_obj_set_style_opa(g_keyboard, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_translate_y(g_ta_id, 0, 0);
    }
}

/* 键盘 Enter：发送 CAN 帧 + 收起键盘 */
static void can_kb_ready_cb(lv_event_t *e)
{
    can_send_cb(e);

    if (g_keyboard != NULL) {
        lv_keyboard_set_textarea(g_keyboard, NULL);
        lv_obj_set_style_opa(g_keyboard, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_translate_y(g_ta_id, 0, 0);
}

static void can_back_cb(lv_event_t *e)
{
    lv_screen_load_anim(ui_main_screen_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, 0);
}

static void can_clear_cb(lv_event_t *e)
{
    memset(g_can_log, 0, sizeof(g_can_log));
    lv_label_set_text(g_rx_label, "");
}

/* 解析 "123:11 22 33" 格式：ID(hex) 冒号后空格分隔数据 */
static int parse_can_input(const char *s, dbg_can_frame_t *frame)
{
    const char *colon;
    int i = 0;
    char *end;

    if (s == NULL || frame == NULL)
        return -1;

    memset(frame, 0, sizeof(*frame));
    frame->ide = 0;
    frame->rtr = 0;

    colon = strchr(s, ':');
    if (colon == NULL)
        return -1;

    char id_str[16];
    int id_len = (int)(colon - s);
    if (id_len <= 0 || id_len >= (int)sizeof(id_str))
        return -1;
    memcpy(id_str, s, id_len);
    id_str[id_len] = '\0';
    frame->id = (rt_uint32_t)strtoul(id_str, &end, 16);
    if (frame->id > 0x7FF)
        frame->ide = 1;

    const char *p = colon + 1;
    while (*p && i < 8) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        frame->data[i++] = (rt_uint8_t)strtoul(p, &end, 16);
        p = end;
    }
    frame->len = (rt_uint8_t)i;

    return (frame->len > 0) ? 0 : -1;
}

static void can_send_cb(lv_event_t *e)
{
    dbg_can_frame_t frame;
    const char *text = lv_textarea_get_text(g_ta_id);
    int ret;

    ret = parse_can_input(text, &frame);
    if (ret != 0)
        return;

    ret = dbg_can_send(&frame);
    if (ret != RT_EOK) {
        lv_label_set_text(g_rx_label, "CAN send failed!");
    }
}

static void style_solid_btn(lv_obj_t *btn, lv_color_t color, int w, int h)
{
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 6, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *label, lv_color_t color,
                          lv_event_cb_t cb, int w, int h, lv_align_t align,
                          int x, int y)
{
    lv_obj_t *btn = lv_button_create(parent);
    style_solid_btn(btn, color, w, h);
    lv_obj_align(btn, align, x, y);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, CLR_TEXT_WHITE, 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

lv_obj_t *ui_can_screen_create(void)
{
    g_can_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_can_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_can_screen, CLR_BG, 0);
    lv_obj_set_style_bg_opa(g_can_screen, LV_OPA_COVER, 0);

    /* 顶部标题栏 */
    lv_obj_t *head = lv_obj_create(g_can_screen);
    lv_obj_set_size(head, LV_HOR_RES, 44);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(head, CLR_TITLE_BAR, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, "CAN Debug (can0)");
    lv_obj_set_style_text_color(title, CLR_TEXT_WHITE, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t *btn_back = lv_button_create(head);
    lv_obj_set_size(btn_back, 56, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_30, 0);
    lv_obj_set_style_radius(btn_back, 6, 0);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Back");
    lv_obj_set_style_text_color(lbl_back, CLR_TEXT_WHITE, 0);
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, can_back_cb, LV_EVENT_CLICKED, NULL);

    /* 接收列表卡片 */
    lv_obj_t *rx_card = lv_obj_create(g_can_screen);
    lv_obj_set_size(rx_card, LV_HOR_RES - 24, 160);
    lv_obj_align(rx_card, LV_ALIGN_TOP_LEFT, 12, 52);
    lv_obj_set_style_bg_color(rx_card, CLR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(rx_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rx_card, 10, 0);
    lv_obj_set_style_border_width(rx_card, 1, 0);
    lv_obj_set_style_border_color(rx_card, lv_color_hex(0xDCE1E8), 0);

    lv_obj_t *rx_title = lv_label_create(rx_card);
    lv_label_set_text(rx_title, "RX");
    lv_obj_set_style_text_color(rx_title, CLR_ACCENT, 0);
    lv_obj_align(rx_title, LV_ALIGN_TOP_LEFT, 8, 4);

    g_rx_label = lv_label_create(rx_card);
    lv_label_set_text(g_rx_label, "");
    lv_obj_set_style_text_font(g_rx_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(g_rx_label, LV_HOR_RES - 40);
    lv_label_set_long_mode(g_rx_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_rx_label, LV_ALIGN_TOP_LEFT, 8, 22);

    /* 输入提示 + 输入框 + 按钮 */
    lv_obj_t *lbl_id = lv_label_create(g_can_screen);
    lv_label_set_text(lbl_id, "ID#Data (hex):");
    lv_obj_set_style_text_color(lbl_id, CLR_TEXT_DARK, 0);
    lv_obj_align(lbl_id, LV_ALIGN_BOTTOM_LEFT, 12, -58);

    g_ta_id = lv_textarea_create(g_can_screen);
    lv_obj_set_size(g_ta_id, LV_HOR_RES - 24 - 150, 36);
    lv_obj_align(g_ta_id, LV_ALIGN_BOTTOM_LEFT, 12, -20);
    lv_textarea_set_placeholder_text(g_ta_id, "123:11 22 33");
    lv_obj_set_style_bg_color(g_ta_id, CLR_CARD_BG, 0);
    lv_obj_set_style_radius(g_ta_id, 8, 0);
    lv_obj_add_event_cb(g_ta_id, can_ta_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(g_ta_id, can_ta_event_cb, LV_EVENT_DEFOCUSED, NULL);

    /* 键盘：初始隐藏 */
    g_keyboard = lv_keyboard_create(g_can_screen);
    lv_keyboard_set_textarea(g_keyboard, g_ta_id);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_keyboard, can_kb_ready_cb, LV_EVENT_READY, NULL);

    make_btn(g_can_screen, "Send", CLR_ACCENT, can_send_cb, 64, 36,
            LV_ALIGN_BOTTOM_RIGHT, -76, -20);
    make_btn(g_can_screen, "Clear", CLR_ACCENT, can_clear_cb, 64, 36,
            LV_ALIGN_BOTTOM_RIGHT, -8, -20);

    return g_can_screen;
}

void ui_can_on_rx(const rt_uint8_t *packed, rt_uint16_t len)
{
    rt_uint32_t id;
    rt_uint8_t dlen, i;
    char line[64];
    int cur_len;

    if (g_rx_label == NULL || packed == NULL || len < 5)
        return;

    id   = packed[0] | (packed[1] << 8) | (packed[2] << 16) | (packed[3] << 24);
    dlen = packed[4];
    if (dlen > 8)
        dlen = 8;

    snprintf(line, sizeof(line), "0x%03X:", (unsigned int)id);
    for (i = 0; i < dlen; i++) {
        char hex[8];
        snprintf(hex, sizeof(hex), " %02X", packed[5 + i]);
        strncat(line, hex, sizeof(line) - strlen(line) - 1);
    }
    strncat(line, "\n", sizeof(line) - strlen(line) - 1);

    cur_len = (int)strlen(g_can_log);
    if (cur_len < CAN_LOG_MAX - (int)strlen(line)) {
        strncpy(g_can_log + cur_len, line, CAN_LOG_MAX - cur_len - 1);
        g_can_log[CAN_LOG_MAX - 1] = '\0';
    }
    lv_label_set_text(g_rx_label, g_can_log);
}