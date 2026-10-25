/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_serial_screen.c - 串口调试界面（卡片式排版）
 * 功能：串口收发显示、Modbus 测试、输入键盘
 *
 * 配色统一走 dbg_theme，本页强调色 = 串口绿。
 */
#include <stdio.h>
#include <string.h>
#include "ui_serial_screen.h"
#include "dbg_serial.h"
#include "dbg_modbus.h"
#include "ui_main_screen.h"
#include "dbg_theme.h"

#define SERIAL_LOG_MAX   1024

/* 页面强调色：串口绿（日/夜两版） */
#define CLR_ACC_SERIAL_DAY    0x2E8B57
#define CLR_ACC_SERIAL_NIGHT  0x3FA76A

static lv_obj_t *g_serial_screen;
static lv_obj_t *g_rx_label;
static lv_obj_t *g_status_label;
static lv_obj_t *g_ta_send;
static lv_obj_t *g_keyboard;
static lv_obj_t *g_btn_back;
static char g_rx_log[SERIAL_LOG_MAX];

/* 前置声明（serial_kb_ready_cb 引用） */
static void serial_send_cb(lv_event_t *e);

/* 输入框聚焦/失焦时显示/隐藏键盘 */
static void serial_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        if (g_keyboard == NULL)
            g_keyboard = lv_keyboard_create(g_serial_screen);
        lv_keyboard_set_textarea(g_keyboard, g_ta_send);
        lv_obj_move_foreground(g_keyboard);
        lv_obj_clear_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);  /* 恢复为可见 */
        lv_obj_set_style_opa(g_keyboard, LV_OPA_COVER, 0);
        /* 输入框上移，避免被底部键盘遮挡 */
        lv_obj_set_style_translate_y(g_ta_send, -116, 0);
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (g_keyboard != NULL) {
            lv_obj_set_style_opa(g_keyboard, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_translate_y(g_ta_send, 0, 0);
    }
}

/* 键盘 "Enter" 确认：执行发送 + 收起键盘 */
static void serial_kb_ready_cb(lv_event_t *e)
{
    serial_send_cb(e);

    if (g_keyboard != NULL) {
        lv_keyboard_set_textarea(g_keyboard, NULL);
        lv_obj_set_style_opa(g_keyboard, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_translate_y(g_ta_send, 0, 0);
}

static void serial_back_cb(lv_event_t *e)
{
    lv_screen_load_anim(ui_main_screen_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, 0);
}

static void serial_clear_cb(lv_event_t *e)
{
    memset(g_rx_log, 0, sizeof(g_rx_log));
    lv_label_set_text(g_rx_label, "");
}

static void serial_send_cb(lv_event_t *e)
{
    const char *text = lv_textarea_get_text(g_ta_send);
    rt_uint32_t len;

    if (text == NULL || text[0] == '\0')
        return;

    len = (rt_uint32_t)strlen(text);
    dbg_serial_send((const rt_uint8_t *)text, len);

    /* 补 CRLF：PC 串口助手按行缓冲，缺换行字节不会刷屏，只有复位
     * （bootloader 输出带换行）时才把缓冲行一起冲出来。自检帧自带
     * \r\n 能立即显示就是铁证。这里补真实 0x0D 0x0A，每次 Send 都
     * 形成完整的一行，PC 助手即时显示，无需复位。 */
    dbg_serial_send((const rt_uint8_t *)"\r\n", 2);
}

/* Modbus 测试：异步读从站1保持寄存器（不阻塞 UI） */
static void modbus_test_cb(lv_event_t *e)
{
    int ret;

    ret = dbg_modbus_start_read_hold(1, 0, 4);
    if (ret == RT_EOK)
        lv_label_set_text(g_status_label, "Modbus: busy...");
    else
        lv_label_set_text(g_status_label, "Modbus: req fail");
}

/* 实心按钮（主色填充 + 浅色文字） */
static void style_solid_btn(lv_obj_t *btn, int w, int h)
{
    lv_obj_set_size(btn, w, h);
    dbg_theme_track_bg(btn, DBG_C_ACCENT);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 6, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
}

/* 次要按钮（卡片底 + 描边 + 彩色文字） */
static void style_ghost_btn(lv_obj_t *btn, int w, int h, int use_accent_text)
{
    lv_obj_set_size(btn, w, h);
    dbg_theme_track_bg(btn, DBG_C_CARD);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_color(btn,
        lv_color_hex(dbg_theme_resolve(DBG_C_ACCENT, CLR_ACC_SERIAL_DAY,
                                       CLR_ACC_SERIAL_NIGHT)), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    (void)use_accent_text;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *label,
                          lv_event_cb_t cb, int w, int h, lv_align_t align,
                          int x, int y, int solid)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *lbl;

    if (solid)
        style_solid_btn(btn, w, h);
    else
        style_ghost_btn(btn, w, h, 1);
    lv_obj_align(btn, align, x, y);

    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    if (solid)
        dbg_theme_track_text(lbl, DBG_C_TITLE_TEXT);
    else
        dbg_theme_track_text(lbl, DBG_C_ACCENT);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/*
 * 页面自身的补刀：标题栏返回按钮是「黑色半透明蒙层」，不属于常规角色，
 * 由本回调在每次切主题时重算不透明度。
 */
static void serial_apply_theme(void)
{
    const dbg_theme_t *th = dbg_theme_get();

    if (g_btn_back != NULL)
        lv_obj_set_style_bg_opa(g_btn_back, (lv_opa_t)th->head_btn_opa, 0);
}

lv_obj_t *ui_serial_screen_create(void)
{
    lv_obj_t *head;
    lv_obj_t *title;
    lv_obj_t *btn_back;
    lv_obj_t *status_bar;
    lv_obj_t *rx_card;
    lv_obj_t *rx_title;

    dbg_theme_set_page_accent(CLR_ACC_SERIAL_DAY, CLR_ACC_SERIAL_NIGHT);

    g_serial_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_serial_screen, LV_HOR_RES, LV_VER_RES);
    dbg_theme_track_bg(g_serial_screen, DBG_C_BG);

    /* 顶部标题栏 */
    head = lv_obj_create(g_serial_screen);
    lv_obj_set_size(head, LV_HOR_RES, 44);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    dbg_theme_track_bg(head, DBG_C_ACCENT);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);

    title = lv_label_create(head);
    lv_label_set_text(title, "Serial Debug");
    dbg_theme_track_text(title, DBG_C_TITLE_TEXT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    /* 返回按钮（标题栏右侧）：叠加半透明蒙层 */
    btn_back = lv_button_create(head);
    lv_obj_set_size(btn_back, 56, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -8, 0);
    dbg_theme_track_bg(btn_back, DBG_C_NEUTRAL);
    lv_obj_set_style_bg_opa(btn_back,
        (lv_opa_t)dbg_theme_get()->head_btn_opa, 0);
    lv_obj_set_style_radius(btn_back, 6, 0);
    {
        lv_obj_t *lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, "Back");
        dbg_theme_track_text(lbl_back, DBG_C_TITLE_TEXT);
        lv_obj_center(lbl_back);
    }
    lv_obj_add_event_cb(btn_back, serial_back_cb, LV_EVENT_CLICKED, NULL);
    g_btn_back = btn_back;

    /* 状态栏 */
    status_bar = lv_obj_create(g_serial_screen);
    lv_obj_set_size(status_bar, LV_HOR_RES - 24, 28);
    lv_obj_align(status_bar, LV_ALIGN_TOP_LEFT, 12, 52);
    dbg_theme_track_bg(status_bar, DBG_C_CARD);
    lv_obj_set_style_radius(status_bar, 8, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);

    g_status_label = lv_label_create(status_bar);
    lv_label_set_text(g_status_label, "baud=115200");
    dbg_theme_track_text(g_status_label, DBG_C_TEXT);
    lv_obj_align(g_status_label, LV_ALIGN_LEFT_MID, 10, 0);

    /* 接收区卡片 */
    rx_card = lv_obj_create(g_serial_screen);
    lv_obj_set_size(rx_card, LV_HOR_RES - 24, 128);
    lv_obj_align(rx_card, LV_ALIGN_TOP_LEFT, 12, 88);
    dbg_theme_track_bg(rx_card, DBG_C_CARD);
    lv_obj_set_style_radius(rx_card, 10, 0);
    lv_obj_set_style_border_width(rx_card, 1, 0);
    lv_obj_set_style_border_color(rx_card,
        lv_color_hex(dbg_theme_get()->card_border), 0);

    rx_title = lv_label_create(rx_card);
    lv_label_set_text(rx_title, "RX");
    dbg_theme_track_text(rx_title, DBG_C_ACCENT);
    lv_obj_align(rx_title, LV_ALIGN_TOP_LEFT, 8, 4);

    g_rx_label = lv_label_create(rx_card);
    lv_label_set_text(g_rx_label, "");
    lv_obj_set_style_text_font(g_rx_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(g_rx_label, LV_HOR_RES - 40);
    lv_label_set_long_mode(g_rx_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_rx_label, LV_ALIGN_TOP_LEFT, 8, 22);
    dbg_theme_track_text(g_rx_label, DBG_C_TEXT);

    /* 底部操作区（Send / Clear 在右下角，不压 RX 卡片） */
    make_btn(g_serial_screen, "Send", serial_send_cb,
             68, 40, LV_ALIGN_BOTTOM_RIGHT, -84, -8, 1);
    make_btn(g_serial_screen, "Clear", serial_clear_cb,
             68, 40, LV_ALIGN_BOTTOM_RIGHT, -8, -8, 0);
    /* Modbus Test 放在标题栏右侧（Back 按钮左边），避免压住 RX 卡片 */
    make_btn(g_serial_screen, "Modbus Test", modbus_test_cb,
             100, 34, LV_ALIGN_TOP_RIGHT, -72, 5, 0);

    /* 发送输入框 */
    g_ta_send = lv_textarea_create(g_serial_screen);
    lv_obj_set_size(g_ta_send, LV_HOR_RES - 24 - 170, 40);
    lv_obj_align(g_ta_send, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    lv_textarea_set_placeholder_text(g_ta_send, "send text...");
    dbg_theme_track_bg(g_ta_send, DBG_C_CARD);
    dbg_theme_track_text(g_ta_send, DBG_C_TEXT);
    lv_obj_set_style_radius(g_ta_send, 8, 0);
    lv_obj_set_style_border_width(g_ta_send, 1, 0);
    lv_obj_set_style_border_color(g_ta_send,
        lv_color_hex(dbg_theme_get()->card_border), 0);
    lv_obj_add_event_cb(g_ta_send, serial_ta_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(g_ta_send, serial_ta_event_cb, LV_EVENT_DEFOCUSED, NULL);

    /* 键盘：初始隐藏，输入框聚焦时显示 */
    g_keyboard = lv_keyboard_create(g_serial_screen);
    lv_keyboard_set_textarea(g_keyboard, g_ta_send);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_keyboard, serial_kb_ready_cb, LV_EVENT_READY, NULL);

    dbg_theme_register(serial_apply_theme);

    return g_serial_screen;
}

void ui_serial_on_rx(const rt_uint8_t *data, rt_uint16_t len)
{
    rt_uint16_t i;
    char hex[4];
    int cur_len;

    if (g_rx_label == NULL || data == NULL || len == 0)
        return;

    cur_len = (int)strlen(g_rx_log);

    /* 每条接收独立成行: 前一条已存在则先换行, 再加分隔符 */
    if (cur_len == 0) {
        strncpy(g_rx_log, "---  ", SERIAL_LOG_MAX - 1);
        cur_len = 5;
    } else {
        if (cur_len < SERIAL_LOG_MAX - 8) {
            strncpy(g_rx_log + cur_len, "\n---  ", SERIAL_LOG_MAX - cur_len - 1);
            cur_len += 6;
        }
    }
    g_rx_log[cur_len] = '\0';

    /* 追加本次数据(hex) */
    for (i = 0; i < len; i++) {
        if (cur_len >= SERIAL_LOG_MAX - 4)
            break;
        snprintf(hex, sizeof(hex), "%02X ", data[i]);
        strncpy(g_rx_log + cur_len, hex, 4);
        cur_len += (int)strlen(hex);
        g_rx_log[cur_len] = '\0';
    }
    lv_label_set_text(g_rx_label, g_rx_log);
}

void ui_serial_update_status(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "baud=%d", (int)dbg_serial_get_baud());
    if (g_status_label)
        lv_label_set_text(g_status_label, buf);
}

/* Modbus 结果接收（在 LVGL 上下文）
 * 打包格式：data[0..1] 结果码(小端,有符号16位)  data[2] 功能码  data[3..] 数据 */
void ui_serial_on_modbus_result(const rt_uint8_t *data, rt_uint16_t len)
{
    rt_int16_t err;
    char buf[80];
    rt_uint16_t i;
    int pos;

    if (g_status_label == NULL || data == NULL || len < 3)
        return;

    err = (rt_int16_t)(data[0] | (data[1] << 8));
    if (err != 0) {
        snprintf(buf, sizeof(buf), "Modbus err=%d", (int)err);
        lv_label_set_text(g_status_label, buf);
        return;
    }

    switch (data[2]) {
    case MODBUS_FC_READ_HOLD_REGS:
        pos = 0;
        buf[0] = '\0';
        for (i = 3; i + 1 < len && pos < (int)sizeof(buf) - 20; i += 2, pos += 8) {
            snprintf(buf + pos, sizeof(buf) - pos, "%02X%02X ",
                     data[i], data[i + 1]);
        }
        lv_label_set_text_fmt(g_status_label, "Regs: %s", buf);
        break;
    case MODBUS_FC_READ_COILS:
        lv_label_set_text_fmt(g_status_label, "Coils: %d bytes", data[3]);
        break;
    case MODBUS_FC_WRITE_SINGLE_REG:
        lv_label_set_text(g_status_label, "Modbus: write OK");
        break;
    default:
        lv_label_set_text(g_status_label, "Modbus: ok");
        break;
    }
}
