/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_pwm_screen.c - 板载 PWM 测试信号页（第 4 步：PWM 测量）
 *
 * 布局：标题栏 + 频率/占空比两行步进 + 状态行 + 启停按钮。
 * 底层走 dbg_pwm（默认 pwm0/ch0）；板级没注册该 PWM 时显示 N/A，仍可进页。
 * 配色走 dbg_theme，跟随 Sleep/Wake 切换。
 */
#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include "ui_pwm_screen.h"
#include "dbg_pwm.h"
#include "ui_main_screen.h"
#include "dbg_theme.h"

/* 页面强调色：PWM 橙紫（日/夜两版） */
#define CLR_ACC_PWM_DAY    0x8E44AD
#define CLR_ACC_PWM_NIGHT  0xA463C4

#define PWM_TOP      56
#define PWM_ROW_H    40
#define PWM_ROW_GAP  14
#define PWM_LEFT     20

static lv_obj_t *g_pwm_screen;
static lv_obj_t *g_head;
static lv_obj_t *g_lbl_title;
static lv_obj_t *g_btn_back;
static lv_obj_t *g_lbl_freq;
static lv_obj_t *g_lbl_duty;
static lv_obj_t *g_lbl_status;
static lv_obj_t *g_btn_run;

/* 步进按钮（主题刷新用） */
#define PWM_BTN_NUM   6
static lv_obj_t *g_btns[PWM_BTN_NUM];
static lv_obj_t *g_btn_lbls[PWM_BTN_NUM];
static uint32_t  g_btn_clr[PWM_BTN_NUM];
static int       g_btn_slot;

static uint32_t pwm_accent(void)
{
    if (dbg_theme_is_night())
        return CLR_ACC_PWM_NIGHT;
    return CLR_ACC_PWM_DAY;
}

/* 频率步进：随量级自适应（1Hz / 10 / 100 / 1k / 10k），范围 1~1MHz */
static rt_uint32_t pwm_freq_step(rt_uint32_t f, int dir)
{
    rt_uint32_t step;

    if (f < 100)         step = 1;
    else if (f < 1000)   step = 10;
    else if (f < 10000)  step = 100;
    else if (f < 100000) step = 1000;
    else                 step = 10000;

    if (dir > 0)
        f += step;
    else if (f > step)
        f -= step;
    else
        f = 1;

    if (f < 1)        f = 1;
    if (f > 1000000)  f = 1000000;
    return f;
}

static void pwm_refresh_labels(void)
{
    char buf[32];

    if (g_lbl_freq == NULL || g_lbl_duty == NULL)
        return;

    snprintf(buf, sizeof(buf), "%u Hz", (unsigned)dbg_pwm_get_freq());
    lv_label_set_text(g_lbl_freq, buf);

    snprintf(buf, sizeof(buf), "%u.%u %%",
             (unsigned)(dbg_pwm_get_duty() / 10), (unsigned)(dbg_pwm_get_duty() % 10));
    lv_label_set_text(g_lbl_duty, buf);

    if (g_lbl_status) {
        if (dbg_pwm_is_enabled())
            lv_label_set_text(g_lbl_status, "OUT: ON");
        else
            lv_label_set_text(g_lbl_status, "OUT: OFF");
        lv_obj_set_style_text_color(g_lbl_status,
                                    lv_color_hex(dbg_pwm_is_enabled() ? pwm_accent()
                                                                     : dbg_theme_get()->text_dim),
                                    0);
    }
    if (g_btn_run) {
        lv_obj_t *lbl = lv_obj_get_child(g_btn_run, 0);
        if (lbl)
            lv_label_set_text(lbl, dbg_pwm_is_enabled() ? "Stop" : "Run");
    }
}

/* ---------------- 回调 ---------------- */
static void pwm_back_cb(lv_event_t *e)
{
    (void)e;
    lv_screen_load_anim(ui_main_screen_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, 0);
}

static void pwm_freq_dec_cb(lv_event_t *e)
{
    (void)e;
    dbg_pwm_set(pwm_freq_step(dbg_pwm_get_freq(), -1), dbg_pwm_get_duty());
    pwm_refresh_labels();
}

static void pwm_freq_inc_cb(lv_event_t *e)
{
    (void)e;
    dbg_pwm_set(pwm_freq_step(dbg_pwm_get_freq(), +1), dbg_pwm_get_duty());
    pwm_refresh_labels();
}

static void pwm_duty_dec_cb(lv_event_t *e)
{
    rt_uint32_t d;
    (void)e;
    d = dbg_pwm_get_duty();
    if (d >= 10) d -= 10; else d = 0;
    dbg_pwm_set(dbg_pwm_get_freq(), d);
    pwm_refresh_labels();
}

static void pwm_duty_inc_cb(lv_event_t *e)
{
    rt_uint32_t d;
    (void)e;
    d = dbg_pwm_get_duty() + 10;
    if (d > 1000) d = 1000;
    dbg_pwm_set(dbg_pwm_get_freq(), d);
    pwm_refresh_labels();
}

static void pwm_run_cb(lv_event_t *e)
{
    (void)e;
    if (dbg_pwm_is_enabled())
        dbg_pwm_disable();
    else
        dbg_pwm_set(dbg_pwm_get_freq(), dbg_pwm_get_duty());  /* 重新 enable */
    pwm_refresh_labels();
}

static void make_btn(lv_obj_t *parent, const char *label, int is_accent,
                     lv_event_cb_t cb, int w, int h,
                     lv_align_t align, int x, int y, lv_obj_t **ret)
{
    const dbg_theme_t *th = dbg_theme_get();
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *lbl;
    int slot;

    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn,
                              lv_color_hex(is_accent ? pwm_accent() : th->btn_neutral), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_align(btn, align, x, y);

    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    if (g_btn_slot < PWM_BTN_NUM) {
        slot = g_btn_slot++;
        g_btns[slot] = btn;
        g_btn_lbls[slot] = lbl;
        g_btn_clr[slot] = is_accent ? 1 : 0;
    }
    if (ret)
        *ret = btn;
}

static void pwm_apply_theme(void)
{
    const dbg_theme_t *th = dbg_theme_get();
    int i;

    if (g_pwm_screen == NULL)
        return;

    lv_obj_set_style_bg_color(g_pwm_screen, lv_color_hex(th->bg), 0);

    if (g_head)
        lv_obj_set_style_bg_color(g_head, lv_color_hex(pwm_accent()), 0);
    if (g_lbl_title)
        lv_obj_set_style_text_color(g_lbl_title, lv_color_hex(th->title_text), 0);

    if (g_btn_back) {
        lv_obj_set_style_bg_color(g_btn_back, lv_color_hex(th->shadow), 0);
        lv_obj_set_style_bg_opa(g_btn_back, (lv_opa_t)th->head_btn_opa, 0);
        {
            lv_obj_t *lbl = lv_obj_get_child(g_btn_back, 0);
            if (lbl)
                lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
        }
    }

    if (g_lbl_freq)  lv_obj_set_style_text_color(g_lbl_freq, lv_color_hex(th->text), 0);
    if (g_lbl_duty)  lv_obj_set_style_text_color(g_lbl_duty, lv_color_hex(th->text), 0);

    for (i = 0; i < PWM_BTN_NUM; i++) {
        if (g_btns[i] == NULL)
            continue;
        lv_obj_set_style_bg_color(g_btns[i],
                                  lv_color_hex(g_btn_clr[i] ? pwm_accent() : th->btn_neutral),
                                  0);
        if (g_btn_lbls[i])
            lv_obj_set_style_text_color(g_btn_lbls[i], lv_color_hex(th->title_text), 0);
    }
}

lv_obj_t *ui_pwm_screen_create(void)
{
    const dbg_theme_t *th;
    lv_obj_t *head, *title, *btn_back;
    int y_freq = PWM_TOP + 8;
    int y_duty = y_freq + PWM_ROW_H + PWM_ROW_GAP;

    if (g_pwm_screen != NULL)
        return g_pwm_screen;

    th = dbg_theme_get();
    g_btn_slot = 0;

    g_pwm_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_pwm_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_pwm_screen, lv_color_hex(th->bg), 0);
    lv_obj_set_style_bg_opa(g_pwm_screen, LV_OPA_COVER, 0);

    /* 标题栏 */
    head = lv_obj_create(g_pwm_screen);
    lv_obj_set_size(head, LV_HOR_RES, 44);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(pwm_accent()), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    g_head = head;

    title = lv_label_create(head);
    lv_label_set_text(title, "PWM Test");
    lv_obj_set_style_text_color(title, lv_color_hex(th->title_text), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);
    g_lbl_title = title;

    btn_back = lv_button_create(head);
    lv_obj_set_size(btn_back, 56, 30);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(th->shadow), 0);
    lv_obj_set_style_bg_opa(btn_back, (lv_opa_t)th->head_btn_opa, 0);
    lv_obj_set_style_radius(btn_back, 6, 0);
    lv_obj_set_style_border_width(btn_back, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(btn_back);
        lv_label_set_text(lbl, "Back");
        lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_back, pwm_back_cb, LV_EVENT_CLICKED, NULL);
    g_btn_back = btn_back;

    /* 频率行 */
    {
        lv_obj_t *cap = lv_label_create(g_pwm_screen);
        lv_label_set_text(cap, "Freq");
        lv_obj_set_style_text_color(cap, lv_color_hex(th->text), 0);
        lv_obj_align(cap, LV_ALIGN_TOP_LEFT, PWM_LEFT, y_freq + 8);

        g_lbl_freq = lv_label_create(g_pwm_screen);
        lv_label_set_text(g_lbl_freq, "-- Hz");
        lv_obj_set_style_text_color(g_lbl_freq, lv_color_hex(pwm_accent()), 0);
        lv_obj_align(g_lbl_freq, LV_ALIGN_TOP_LEFT, PWM_LEFT + 56, y_freq + 8);

        make_btn(g_pwm_screen, "-", 0, pwm_freq_dec_cb, 44, 36,
                 LV_ALIGN_TOP_LEFT, PWM_LEFT + 200, y_freq, NULL);
        make_btn(g_pwm_screen, "+", 0, pwm_freq_inc_cb, 44, 36,
                 LV_ALIGN_TOP_LEFT, PWM_LEFT + 252, y_freq, NULL);
    }

    /* 占空比行 */
    {
        lv_obj_t *cap = lv_label_create(g_pwm_screen);
        lv_label_set_text(cap, "Duty");
        lv_obj_set_style_text_color(cap, lv_color_hex(th->text), 0);
        lv_obj_align(cap, LV_ALIGN_TOP_LEFT, PWM_LEFT, y_duty + 8);

        g_lbl_duty = lv_label_create(g_pwm_screen);
        lv_label_set_text(g_lbl_duty, "-- %");
        lv_obj_set_style_text_color(g_lbl_duty, lv_color_hex(pwm_accent()), 0);
        lv_obj_align(g_lbl_duty, LV_ALIGN_TOP_LEFT, PWM_LEFT + 56, y_duty + 8);

        make_btn(g_pwm_screen, "-", 0, pwm_duty_dec_cb, 44, 36,
                 LV_ALIGN_TOP_LEFT, PWM_LEFT + 200, y_duty, NULL);
        make_btn(g_pwm_screen, "+", 0, pwm_duty_inc_cb, 44, 36,
                 LV_ALIGN_TOP_LEFT, PWM_LEFT + 252, y_duty, NULL);
    }

    /* 状态行 + 启停 */
    {
        int y_stat = y_duty + PWM_ROW_H + PWM_ROW_GAP;
        g_lbl_status = lv_label_create(g_pwm_screen);
        lv_label_set_text(g_lbl_status, "OUT: OFF");
        lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(th->text_dim), 0);
        lv_obj_align(g_lbl_status, LV_ALIGN_TOP_LEFT, PWM_LEFT, y_stat + 10);

        make_btn(g_pwm_screen, "Run", 1, pwm_run_cb, 80, 36,
                 LV_ALIGN_TOP_LEFT, PWM_LEFT + 200, y_stat, &g_btn_run);
    }

    pwm_refresh_labels();
    dbg_theme_register(pwm_apply_theme);

    return g_pwm_screen;
}

lv_obj_t *ui_pwm_screen_get(void)
{
    if (g_pwm_screen == NULL)
        return ui_pwm_screen_create();
    return g_pwm_screen;
}
