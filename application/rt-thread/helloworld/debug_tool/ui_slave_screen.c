/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_slave_screen.c - 从机（星火一号）仪表盘界面
 *
 * 布局按屏幕可用高度自动分配，不写死坐标：
 *   标题栏 / 上排两张卡（环境 + 光照）/ 下排一整张（六轴）/ 底部状态条
 *
 * ⚠️ 所有 lv_xxx 调用必须发生在 LVGL 定时器上下文（dbg_main 的消息分发里），
 *    dbg_slave 线程只负责往消息总线投递数据。
 */
#include <stdio.h>
#include <string.h>
#include "ui_slave_screen.h"
#include "dbg_slave.h"
#include "ui_main_screen.h"

/* 配色（与各功能页保持一致的主题体系） */
#define CLR_TITLE_BAR   lv_color_hex(0x1D9E75)  /* 青绿 */
#define CLR_BG          lv_color_hex(0xF0F3F7)
#define CLR_CARD_BG     lv_color_hex(0xFFFFFF)
#define CLR_TEXT_WHITE  lv_color_hex(0xFFFFFF)
#define CLR_TEXT_DARK   lv_color_hex(0x333A45)
#define CLR_MUTED       lv_color_hex(0x8A93A1)
#define CLR_OK          lv_color_hex(0x2E8B57)
#define CLR_ERR         lv_color_hex(0xC0392B)

#define HEADER_H        48
#define PAD             10
#define GAP             6
#define STATUS_H        22

static lv_obj_t *g_screen;

static lv_obj_t *g_lbl_temp;
static lv_obj_t *g_lbl_humi;
static lv_obj_t *g_lbl_lux;
static lv_obj_t *g_lbl_ps;
static lv_obj_t *g_lbl_acc;
static lv_obj_t *g_lbl_gyro;
static lv_obj_t *g_lbl_status;

/* ------------------------------------------------------------ 小工具 */
static rt_int16_t raw_s16(const rt_uint8_t *p)
{
    rt_uint16_t u = (rt_uint16_t)((p[0] << 8) | p[1]);
    return (rt_int16_t)u;
}

/* raw 为「真值 x10」，输出一位小数 */
static void fmt_dec(char *buf, int size, rt_int16_t raw, const char *unit)
{
    int neg = raw < 0;
    int abs_v = neg ? -raw : raw;

    rt_snprintf(buf, size, "%s%d.%d %s", neg ? "-" : "", abs_v / 10, abs_v % 10, unit);
}

/* 建一张白底圆角卡片，返回卡片对象；标题已放好 */
static lv_obj_t *create_card(lv_obj_t *parent, int x, int y, int w, int h,
                             const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, CLR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, CLR_MUTED, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    return card;
}

/* 卡片内追加一行数值标签；ref 为上一行，NULL 表示第一行 */
static lv_obj_t *add_row(lv_obj_t *card, lv_obj_t *ref, const char *text)
{
    lv_obj_t *l = lv_label_create(card);

    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, CLR_TEXT_DARK, 0);
    if (ref == NULL)
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 2, 24);
    else
        lv_obj_align_to(l, ref, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    return l;
}

/* -------------------------------------------------------------- 界面 */
static void slave_back_cb(lv_event_t *e)
{
    lv_screen_load_anim(ui_main_screen_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, 0);
}

lv_obj_t *ui_slave_screen_create(void)
{
    lv_obj_t *head, *title, *btn, *btn_lbl, *cardA, *cardB, *cardC;
    int content_top, content_h, row_h, col_w, full_w, status_y;
    int screen_w = (int)LV_HOR_RES;
    int screen_h = (int)LV_VER_RES;

    if (g_screen != NULL)
        return g_screen;

    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, screen_w, screen_h);
    lv_obj_set_style_bg_color(g_screen, CLR_BG, 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

    /* 标题栏 */
    head = lv_obj_create(g_screen);
    lv_obj_set_size(head, screen_w, HEADER_H);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(head, CLR_TITLE_BAR, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);

    title = lv_label_create(head);
    lv_label_set_text(title, "Slave Monitor");
    lv_obj_set_style_text_color(title, CLR_TEXT_WHITE, 0);
    lv_obj_center(title);

    btn = lv_button_create(head);
    lv_obj_set_size(btn, 64, 34);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_color(btn, CLR_TITLE_BAR, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, CLR_TEXT_WHITE, 0);
    lv_obj_add_event_cb(btn, slave_back_cb, LV_EVENT_CLICKED, NULL);

    btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "< Back");
    lv_obj_set_style_text_color(btn_lbl, CLR_TEXT_WHITE, 0);
    lv_obj_center(btn_lbl);

    /* 可用区域划分 */
    full_w      = screen_w - 2 * PAD;
    content_top = HEADER_H + GAP;
    content_h   = screen_h - content_top - STATUS_H - GAP;
    row_h       = (content_h - GAP) / 2;
    col_w       = (full_w - GAP) / 2;
    status_y    = content_top + content_h + GAP;

    /* 上排：环境 */
    cardA = create_card(g_screen, PAD, content_top, col_w, row_h, "Environment");
    g_lbl_temp = add_row(cardA, NULL, "Temp: --.- C");
    g_lbl_humi = add_row(cardA, g_lbl_temp, "Humi: --.- %");

    /* 上排：光照 */
    cardB = create_card(g_screen, PAD + col_w + GAP, content_top, col_w, row_h, "Illumination");
    g_lbl_lux = add_row(cardB, NULL, "Lux: --.- lux");
    g_lbl_ps  = add_row(cardB, g_lbl_lux, "PS: --");

    /* 下排：六轴（整行） */
    cardC = create_card(g_screen, PAD, content_top + row_h + GAP, full_w, row_h, "Motion");
    g_lbl_acc  = add_row(cardC, NULL, "Acc  X/Y/Z: --");
    g_lbl_gyro = add_row(cardC, g_lbl_acc, "Gyro X/Y/Z: --");

    /* 底部状态条 */
    g_lbl_status = lv_label_create(g_screen);
    lv_label_set_text(g_lbl_status, "HB --   UP -- s   waiting...");
    lv_obj_set_size(g_lbl_status, full_w, STATUS_H);
    lv_obj_set_pos(g_lbl_status, PAD, status_y);
    lv_obj_set_style_text_color(g_lbl_status, CLR_MUTED, 0);

    return g_screen;
}

/* ---------------------------------------------------- 数据更新（LVGL 线程） */
void ui_slave_on_data(const rt_uint8_t *data, rt_uint16_t len)
{
    char buf[48];
    rt_uint8_t status;
    rt_int16_t t, h, lux;
    rt_uint16_t hb, up;

    if (len < SLAVE_PKT_LEN || g_lbl_temp == NULL)
        return;

    status = data[SLV_OFF_STATUS];
    if (status != 0) {
        const char *txt;

        switch (status) {
        case 1: txt = "HB --    timeout, check wiring"; break;
        case 2: txt = "HB --    CRC error";            break;
        default: txt = "HB --    slave exception";      break;
        }
        lv_label_set_text(g_lbl_status, txt);
        lv_obj_set_style_text_color(g_lbl_status, CLR_ERR, 0);
        return;
    }

    t   = raw_s16(&data[SLV_OFF_TEMP]);
    h   = raw_s16(&data[SLV_OFF_HUMI]);
    lux = raw_s16(&data[SLV_OFF_LUX]);
    hb  = (rt_uint16_t)((data[SLV_OFF_HEARTBEAT] << 8) | data[SLV_OFF_HEARTBEAT + 1]);
    up  = (rt_uint16_t)((data[SLV_OFF_UPTIME] << 8) | data[SLV_OFF_UPTIME + 1]);

    fmt_dec(buf, sizeof(buf), t, "C");
    lv_label_set_text(g_lbl_temp, buf);

    fmt_dec(buf, sizeof(buf), h, "%");
    lv_label_set_text(g_lbl_humi, buf);

    fmt_dec(buf, sizeof(buf), lux, "lux");
    lv_label_set_text(g_lbl_lux, buf);

    rt_snprintf(buf, sizeof(buf), "PS: %u",
                (rt_uint16_t)((data[SLV_OFF_PS] << 8) | data[SLV_OFF_PS + 1]));
    lv_label_set_text(g_lbl_ps, buf);

    rt_snprintf(buf, sizeof(buf), "Acc  X/Y/Z: %d, %d, %d",
                raw_s16(&data[SLV_OFF_ACC_X]),
                raw_s16(&data[SLV_OFF_ACC_Y]),
                raw_s16(&data[SLV_OFF_ACC_Z]));
    lv_label_set_text(g_lbl_acc, buf);

    rt_snprintf(buf, sizeof(buf), "Gyro X/Y/Z: %d, %d, %d",
                raw_s16(&data[SLV_OFF_GYRO_X]),
                raw_s16(&data[SLV_OFF_GYRO_Y]),
                raw_s16(&data[SLV_OFF_GYRO_Z]));
    lv_label_set_text(g_lbl_gyro, buf);

    rt_snprintf(buf, sizeof(buf), "HB %u   UP %u s   link OK%s", hb, up,
#if DBG_SLAVE_LOOPBACK
                " (SIM)");      /* 自测模式：数值是本地造的，别当真数据 */
#else
                "");
#endif
    lv_label_set_text(g_lbl_status, buf);
    lv_obj_set_style_text_color(g_lbl_status, CLR_OK, 0);
}
