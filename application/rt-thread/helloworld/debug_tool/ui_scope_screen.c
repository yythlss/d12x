/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_scope_screen.c - 模拟量示波界面（实时滚动曲线）
 *
 * 布局：标题栏 + 波形卡片 + 底部状态/操作栏。
 * 波形用 lv_line 绘制，样本存入固定长度的左移缓冲区，
 * 每收到一个 ADC 采样，把最旧样本移除、在右侧追加新点，重绘一次曲线。
 * 数据源可为 SYNTH（自测信号）或 GPAI（片内 ADC）。
 */
#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include "ui_scope_screen.h"
#include "dbg_adc.h"
#include "ui_main_screen.h"

/* 波形显示区坐标（相对整屏） */
#define SCOPE_TOP       54
#define SCOPE_LEFT      12
#define SCOPE_W         (LV_HOR_RES - 24)   /* 480-24 = 456 */
#define SCOPE_H         150

#define SCOPE_PT_NUM    DBG_ADC_SCOPE_LEN   /* 240 个点 */
#define SCOPE_PT_MAX    4095                /* 12-bit ADC 满量程 */

/* 配色（与现有页面保持一致） */
#define CLR_TITLE_BAR   lv_color_hex(0xE07B24)  /* 模拟-橙 */
#define CLR_BG          lv_color_hex(0xF0F3F7)
#define CLR_CARD_BG     lv_color_hex(0xFFFFFF)
#define CLR_TEXT_WHITE  lv_color_hex(0xFFFFFF)
#define CLR_TEXT_DARK   lv_color_hex(0x333A45)
#define CLR_CURVE       lv_color_hex(0xE07B24)
#define CLR_GRID        lv_color_hex(0xE0E4EA)
#define CLR_MIDLINE     lv_color_hex(0xB9C0CC)

static lv_obj_t *g_scope_screen;
static lv_obj_t *g_line;
static lv_obj_t *g_lbl_value;
static lv_obj_t *g_lbl_ch;
static lv_obj_t *g_lbl_src;
static lv_obj_t *g_btn_run;

/* 波形样本：adc 值（0..4095），映射到显示区高度 */
static rt_uint16_t g_samples[SCOPE_PT_NUM];
static lv_point_precise_t g_points[SCOPE_PT_NUM];
static rt_uint16_t g_sample_cnt;   /* 当前已累计的样本数 */

/* Y 方向显示量程（来自 SYNTH 满量程 3300mV / GPAI 3.3V） */
#define SCOPE_VREF_MV   3300

static void scope_redraw(void)
{
    int i;

    if (g_line == NULL)
        return;

    for (i = 0; i < (int)g_sample_cnt; i++) {
        lv_coord_t x = SCOPE_LEFT + 2
                       + (lv_coord_t)((SCOPE_W - 4) * i
                                      / (SCOPE_PT_NUM - 1));
        /* ADC(0..4095) -> y：量程映射到显示区，下沿为最大值 */
        lv_coord_t y = SCOPE_TOP + 8
                       + (lv_coord_t)((SCOPE_H - 16)
                                      * (SCOPE_PT_MAX - g_samples[i])
                                      / SCOPE_PT_MAX);
        g_points[i].x = x;
        g_points[i].y = y;
    }
    lv_line_set_points(g_line, g_points, g_sample_cnt);
}

/* 左移一位，追加新样本 */
static void scope_push_sample(rt_uint16_t adc)
{
    if (g_sample_cnt < SCOPE_PT_NUM) {
        g_samples[g_sample_cnt++] = adc;
    } else {
        memmove(&g_samples[0], &g_samples[1],
                (SCOPE_PT_NUM - 1) * sizeof(rt_uint16_t));
        g_samples[SCOPE_PT_NUM - 1] = adc;
    }
    scope_redraw();
}

/* ---------------- 底部操作栏回调 ------------------- */
static void scope_back_cb(lv_event_t *e)
{
    lv_screen_load_anim(ui_main_screen_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, 0);
}

static void update_run_btn(void)
{
    if (g_btn_run == NULL)
        return;
    lv_obj_t *lbl = lv_obj_get_child(g_btn_run, 0);
    if (lbl)
        lv_label_set_text(lbl, dbg_adc_is_running() ? "Stop" : "Run");
}

static void scope_run_cb(lv_event_t *e)
{
    dbg_adc_run(dbg_adc_is_running() ? 0 : 1);
    update_run_btn();
}

static void scope_ch_dec_cb(lv_event_t *e)
{
    rt_uint8_t ch = dbg_adc_get_channel();

    if (ch > 0) {
        dbg_adc_set_channel(ch - 1);
        char buf[16];
        snprintf(buf, sizeof(buf), "CH%d", dbg_adc_get_channel());
        lv_label_set_text(g_lbl_ch, buf);
    }
}

static void scope_ch_inc_cb(lv_event_t *e)
{
    rt_uint8_t ch = dbg_adc_get_channel();

    if (ch < DBG_ADC_CH_NUM - 1) {
        dbg_adc_set_channel(ch + 1);
        char buf[16];
        snprintf(buf, sizeof(buf), "CH%d", dbg_adc_get_channel());
        lv_label_set_text(g_lbl_ch, buf);
    }
}

static void scope_clear_cb(lv_event_t *e)
{
    g_sample_cnt = 0;
    memset(g_samples, 0, sizeof(g_samples));
    lv_line_set_points(g_line, g_points, 0);
}

static void make_btn(lv_obj_t *parent, const char *label, lv_color_t color,
                     lv_event_cb_t cb, int w, int h,
                     lv_align_t align, int x, int y, lv_obj_t **ret)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_align(btn, align, x, y);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, CLR_TEXT_WHITE, 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    if (ret)
        *ret = btn;
}

lv_obj_t *ui_scope_screen_create(void)
{
    if (g_scope_screen != NULL)
        return g_scope_screen;

    g_scope_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_scope_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_scope_screen, CLR_BG, 0);
    lv_obj_set_style_bg_opa(g_scope_screen, LV_OPA_COVER, 0);

    /* 标题栏 */
    lv_obj_t *head = lv_obj_create(g_scope_screen);
    lv_obj_set_size(head, LV_HOR_RES, 44);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(head, CLR_TITLE_BAR, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, "Analog Scope");
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
    lv_obj_add_event_cb(btn_back, scope_back_cb, LV_EVENT_CLICKED, NULL);

    /* 波形卡片 */
    lv_obj_t *card = lv_obj_create(g_scope_screen);
    lv_obj_set_size(card, SCOPE_W, SCOPE_H);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, SCOPE_LEFT, SCOPE_TOP);
    lv_obj_set_style_bg_color(card, CLR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xDCE1E8), 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    /* 中线（参考线） */
    lv_obj_t *mid = lv_obj_create(card);
    lv_obj_set_size(mid, SCOPE_W - 8, 1);
    lv_obj_align(mid, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(mid, CLR_MIDLINE, 0);
    lv_obj_set_style_bg_opa(mid, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mid, 0, 0);

    /* 波形曲线 */
    g_line = lv_line_create(card);
    lv_obj_set_style_line_width(g_line, 2, 0);
    lv_obj_set_style_line_color(g_line, CLR_CURVE, 0);
    lv_obj_set_style_line_rounded(g_line, true, 0);
    lv_line_set_points(g_line, g_points, 0);

    /* 底部状态栏 */
    static const lv_coord_t bar_y = SCOPE_TOP + SCOPE_H + 8; /* 212 */

    /* 通道选择：左减号 */
    make_btn(g_scope_screen, "-", CLR_TEXT_DARK, scope_ch_dec_cb, 40, 34,
             LV_ALIGN_TOP_LEFT, SCOPE_LEFT, bar_y, NULL);

    g_lbl_ch = lv_label_create(g_scope_screen);
    lv_label_set_text(g_lbl_ch, "CH0");
    lv_obj_set_style_text_color(g_lbl_ch, CLR_TEXT_DARK, 0);
    lv_obj_align(g_lbl_ch, LV_ALIGN_TOP_LEFT, SCOPE_LEFT + 52, bar_y + 8);

    /* 通道选择：右加号 */
    make_btn(g_scope_screen, "+", CLR_TEXT_DARK, scope_ch_inc_cb, 40, 34,
             LV_ALIGN_TOP_LEFT, SCOPE_LEFT + 96, bar_y, NULL);

    /* 数据源 + 当前值 */
    g_lbl_src = lv_label_create(g_scope_screen);
    lv_label_set_text(g_lbl_src, "SRC: -");
    lv_obj_set_style_text_color(g_lbl_src, CLR_TEXT_DARK, 0);
    lv_obj_align(g_lbl_src, LV_ALIGN_TOP_LEFT, SCOPE_LEFT + 150, bar_y + 8);

    g_lbl_value = lv_label_create(g_scope_screen);
    lv_label_set_text(g_lbl_value, "-- mV");
    lv_obj_set_style_text_color(g_lbl_value, CLR_TITLE_BAR, 0);
    lv_obj_align(g_lbl_value, LV_ALIGN_TOP_LEFT, SCOPE_LEFT + 300, bar_y + 8);

    /* 清空 */
    make_btn(g_scope_screen, "Clear", CLR_TEXT_DARK, scope_clear_cb, 56, 34,
             LV_ALIGN_TOP_RIGHT, -72, bar_y, NULL);

    /* 运行/停止 */
    make_btn(g_scope_screen, "Run", CLR_TITLE_BAR, scope_run_cb, 56, 34,
             LV_ALIGN_TOP_RIGHT, -8, bar_y, &g_btn_run);

    /* 初始显示数据源 */
    char buf[24];
    snprintf(buf, sizeof(buf), "SRC: %s", dbg_adc_source_name());
    lv_label_set_text(g_lbl_src, buf);

    return g_scope_screen;
}

void ui_scope_on_adc(const rt_uint8_t *data, rt_uint16_t len)
{
    rt_uint16_t adc, mv;
    char buf[24];

    if (g_scope_screen == NULL || data == NULL || len < 6)
        return;

    adc = (rt_uint16_t)(data[DBG_ADC_MSG_CH_ADC_LO] | (data[DBG_ADC_MSG_CH_ADC_HI] << 8));
    mv  = (rt_uint16_t)(data[DBG_ADC_MSG_CH_MV_LO]  | (data[DBG_ADC_MSG_CH_MV_HI]  << 8));
    (void)mv;

    /* 更新曲线（仅运行中推入；停止时不刷新但保留画面） */
    if (dbg_adc_is_running())
        scope_push_sample(adc);

    /* 更新读数 */
    snprintf(buf, sizeof(buf), "%d mV (%d)", (int)mv, (int)adc);
    lv_label_set_text(g_lbl_value, buf);

    /* 数据源变化时刷新 */
    char s[24];
    snprintf(s, sizeof(s), "SRC: %s", dbg_adc_source_name());
    lv_label_set_text(g_lbl_src, s);
}