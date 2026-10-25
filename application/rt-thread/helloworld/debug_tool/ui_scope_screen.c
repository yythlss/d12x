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
 *
 * 配色统一走 dbg_theme（页面自带强调色='模拟橙'，夜间自动适配）。
 */
#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include "ui_scope_screen.h"
#include "dbg_adc.h"
#include "dbg_slave.h"      /* RS485 源：读最近请求目标，显示寄存器地址 */
#include "dbg_log.h"        /* CSV 回放：列出/解析已录波形 */
#include "ui_main_screen.h"
#include "dbg_theme.h"

/* 波形显示区坐标（相对整屏） */
#define SCOPE_TOP       54
#define SCOPE_LEFT      12
#define SCOPE_W         (LV_HOR_RES - 24)   /* 480-24 = 456 */
#define SCOPE_H         150

#define SCOPE_PT_NUM    DBG_ADC_SCOPE_LEN   /* 240 个点 */
#define SCOPE_PT_MAX    4095                /* 12-bit ADC 满量程（SYNTH/GPAI） */
#define SCOPE_PT_MAX_RS485 65535            /* 16-bit 寄存器满量程（RS485 源） */

/* 页面强调色：模拟橙（日/夜两版） */
#define CLR_ACC_SCOPE_DAY    0xE07B24
#define CLR_ACC_SCOPE_NIGHT  0xE8933C

static lv_obj_t *g_scope_screen;
static lv_obj_t *g_head;
static lv_obj_t *g_lbl_title;
static lv_obj_t *g_btn_back;
static lv_obj_t *g_card;
static lv_obj_t *g_mid;
static lv_obj_t *g_line;
static lv_obj_t *g_lbl_value;
static lv_obj_t *g_lbl_ch;
static lv_obj_t *g_lbl_src;       /* SRC 按钮（可点击循环切换数据源） */
static lv_obj_t *g_lbl_src_txt;   /* SRC 按钮内文字 */
static lv_obj_t *g_btn_run;

/* 底部小按钮：主题切换时统一重刷底色/文字色 */
#define SCOPE_BTN_NUM   4
static lv_obj_t *g_btns[SCOPE_BTN_NUM];      /* -, +, Clear, Run */
static lv_obj_t *g_btn_lbls[SCOPE_BTN_NUM];
static uint32_t  g_btn_clr[SCOPE_BTN_NUM];   /* 0=中性色，1=强调色 */

/* ---------------- CSV 回放（第 4 步） ----------------
 * 标题栏 Replay 按钮触发：列出 /sdcard 下已录 CSV，选文件后把 temp/humi/lux
 * 之一动画推入波形区。回放期间暂停实时采样，避免两条数据互相覆盖。 */
#define SCOPE_REPLAY_MAX   2000
#define SCOPE_REPLAY_CSV   8
static dbg_replay_point_t  g_replay_pts[SCOPE_REPLAY_MAX];
static int                 g_replay_n;
static int                 g_replay_idx;
static int                 g_replay_ch;      /* 0=temp 1=humi 2=lux */
static int                 g_replay_active;
static lv_timer_t         *g_replay_timer;
static lv_obj_t           *g_replay_panel;
static char                g_replay_names[SCOPE_REPLAY_CSV][24];
static int                 g_replay_cnt;

/* 前向声明：scope_screen_evt（定义在下方）会调用，真正实现在回放块里 */
static void scope_replay_close(void);

/* 波形样本：adc 值（0..4095），映射到显示区高度 */
static rt_uint16_t g_samples[SCOPE_PT_NUM];
static lv_point_precise_t g_points[SCOPE_PT_NUM];
static rt_uint16_t g_sample_cnt;   /* 当前已累计的样本数 */
static rt_uint16_t g_pt_max = SCOPE_PT_MAX;  /* 当前源的量程（RS485 时为 65535） */

/* Y 方向显示量程（来自 SYNTH 满量程 3300mV / GPAI 3.3V） */
#define SCOPE_VREF_MV   3300

static uint32_t scope_accent(void)
{
    if (dbg_theme_is_night())
        return CLR_ACC_SCOPE_NIGHT;
    return CLR_ACC_SCOPE_DAY;
}

static void scope_redraw(void)
{
    int i;

    if (g_line == NULL)
        return;

    for (i = 0; i < (int)g_sample_cnt; i++) {
        lv_coord_t x = SCOPE_LEFT + 2
                       + (lv_coord_t)((SCOPE_W - 4) * i
                                      / (SCOPE_PT_NUM - 1));
        /* 样本(0..g_pt_max) -> y：量程映射到显示区，下沿为最大值 */
        lv_coord_t y = SCOPE_TOP + 8
                       + (lv_coord_t)((SCOPE_H - 16)
                                      * (g_pt_max - g_samples[i])
                                      / g_pt_max);
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

static void scope_refresh_ch_label(void);   /* 前向声明：被下方 +/- 回调调用 */

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
        scope_refresh_ch_label();
    }
}

static void scope_ch_inc_cb(lv_event_t *e)
{
    rt_uint8_t ch = dbg_adc_get_channel();

    if (ch < DBG_ADC_CH_NUM - 1) {
        dbg_adc_set_channel(ch + 1);
        scope_refresh_ch_label();
    }
}

static void scope_clear_cb(lv_event_t *e)
{
    g_sample_cnt = 0;
    memset(g_samples, 0, sizeof(g_samples));
    lv_line_set_points(g_line, g_points, 0);
}

/* ---------------- 通道标签（RS485 模式下显示寄存器地址） ---------- */
static void scope_refresh_ch_label(void)
{
    char buf[16];

    if (g_lbl_ch == NULL)
        return;

    if (dbg_adc_get_source() == DBG_ADC_SRC_RS485) {
        dbg_rs485_req_t base;
        rt_uint16_t reg;

        dbg_slave_rs485_get_last(&base);
        reg = (rt_uint16_t)(base.reg + dbg_adc_get_channel());
        snprintf(buf, sizeof(buf), "R%04X", (unsigned)reg);
    } else {
        snprintf(buf, sizeof(buf), "CH%d", dbg_adc_get_channel());
    }
    lv_label_set_text(g_lbl_ch, buf);
}

/* ---------------- 数据源切换（点 SRC 标签循环） ------------------- */
static void scope_src_cb(lv_event_t *e)
{
    int cur  = dbg_adc_get_source();
    int next;

    (void)e;
    /* SYNTH -> (GPAI 若可用) -> RS485 -> SYNTH */
    if (cur == DBG_ADC_SRC_SYNTH)
        next = dbg_adc_gpai_available() ? DBG_ADC_SRC_GPAI : DBG_ADC_SRC_RS485;
    else if (cur == DBG_ADC_SRC_GPAI)
        next = DBG_ADC_SRC_RS485;
    else
        next = DBG_ADC_SRC_SYNTH;

    if (dbg_adc_set_source(next) != RT_EOK)
        next = DBG_ADC_SRC_SYNTH;   /* GPAI 等不可用，回退 SYNTH */

    /* RS485 源仅在示波页前台采数；其它源关闭 RS485 采样 */
    dbg_adc_rs485_active((next == DBG_ADC_SRC_RS485) ? 1 : 0);

    /* 量程随源切换 */
    g_pt_max = (next == DBG_ADC_SRC_RS485) ? SCOPE_PT_MAX_RS485 : SCOPE_PT_MAX;

    /* 刷新 SRC 标签与通道标签 */
    if (g_lbl_src_txt)
        lv_label_set_text(g_lbl_src_txt, dbg_adc_source_name());
    scope_refresh_ch_label();
}

/* 示波页加载/卸载：RS485 源仅在页面前台才发采样请求，避免离台后空转占用 uart1 */
static void scope_screen_evt(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SCREEN_LOADED)
        dbg_adc_rs485_active((dbg_adc_get_source() == DBG_ADC_SRC_RS485) ? 1 : 0);
    else if (code == LV_EVENT_SCREEN_UNLOADED) {
        dbg_adc_rs485_active(0);
        scope_replay_close();          /* 离开示波页时停回放，清空浮层指针避免悬空 */
    }
}

static int g_btn_slot;  /* create 期间分配按钮槽位 */

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
                              lv_color_hex(is_accent ? scope_accent()
                                                     : th->btn_neutral), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_align(btn, align, x, y);

    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    /* 登记到重刷列表（槽位用完则忽略，不影响功能） */
    if (g_btn_slot < SCOPE_BTN_NUM) {
        slot = g_btn_slot++;
        g_btns[slot] = btn;
        g_btn_lbls[slot] = lbl;
        g_btn_clr[slot] = is_accent ? 1 : 0;
    }

    if (ret)
        *ret = btn;
}

/* 主题刷新：跟随 dbg_theme_set() 重刷全页面 */
static void scope_apply_theme(void)
{
    const dbg_theme_t *th = dbg_theme_get();
    int i;

    if (g_scope_screen == NULL)
        return;

    lv_obj_set_style_bg_color(g_scope_screen, lv_color_hex(th->bg), 0);

    if (g_head)
        lv_obj_set_style_bg_color(g_head, lv_color_hex(scope_accent()), 0);
    if (g_lbl_title)
        lv_obj_set_style_text_color(g_lbl_title, lv_color_hex(th->title_text), 0);

    /* 返回按钮：叠加半透明黑，切夜间时保留同一手法但重算不透明度 */
    if (g_btn_back) {
        lv_obj_set_style_bg_opa(g_btn_back, (lv_opa_t)th->head_btn_opa, 0);
        {
            lv_obj_t *lbl = lv_obj_get_child(g_btn_back, 0);
            if (lbl)
                lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
        }
    }

    if (g_card) {
        lv_obj_set_style_bg_color(g_card, lv_color_hex(th->card_bg), 0);
        lv_obj_set_style_border_color(g_card, lv_color_hex(th->card_border), 0);
    }

    if (g_mid)
        lv_obj_set_style_bg_color(g_mid, lv_color_hex(th->grid), 0);

    if (g_line)
        lv_obj_set_style_line_color(g_line, lv_color_hex(scope_accent()), 0);

    if (g_lbl_ch)
        lv_obj_set_style_text_color(g_lbl_ch, lv_color_hex(th->text), 0);
    if (g_lbl_src) {
        lv_obj_set_style_bg_color(g_lbl_src, lv_color_hex(th->btn_neutral), 0);
        lv_obj_set_style_bg_opa(g_lbl_src, LV_OPA_COVER, 0);
    }
    if (g_lbl_src_txt)
        lv_obj_set_style_text_color(g_lbl_src_txt, lv_color_hex(th->text), 0);
    if (g_lbl_value)
        lv_obj_set_style_text_color(g_lbl_value, lv_color_hex(scope_accent()), 0);

    for (i = 0; i < SCOPE_BTN_NUM; i++) {
        if (g_btns[i] == NULL)
            continue;
        lv_obj_set_style_bg_color(g_btns[i],
                                  lv_color_hex(g_btn_clr[i] ? scope_accent()
                                                            : th->btn_neutral),
                                  0);
        if (g_btn_lbls[i])
            lv_obj_set_style_text_color(g_btn_lbls[i],
                                        lv_color_hex(th->title_text), 0);
    }
}

/* ============================================================ CSV 回放（第 4 步）
 * 标题栏 Replay 按钮 -> 浮层列出 /sdcard 已录 CSV -> 选文件后把 temp/humi/lux
 * 之一按 30ms/点动画推入波形区。回放期间暂停实时采样，避免两条数据互相覆盖。 */

/* 通道量程：temp 0~50℃ / humi 0~100%RH / lux 0~2000lx，统一映射到 0..SCOPE_PT_MAX */
static rt_uint16_t scope_replay_scale(int ch, const dbg_replay_point_t *p)
{
    float v, range;

    if (ch == 1)      { v = p->humi; range = 100.0f; }
    else if (ch == 2) { v = p->lux;  range = 2000.0f; }
    else              { v = p->temp; range = 50.0f; }

    if (range <= 0.0f) range = 1.0f;
    {
        int val = (int)(v / range * (float)SCOPE_PT_MAX);
        if (val < 0) val = 0;
        if (val > SCOPE_PT_MAX) val = SCOPE_PT_MAX;
        return (rt_uint16_t)val;
    }
}

static void scope_replay_close(void)
{
    if (g_replay_timer != NULL) {
        lv_timer_del(g_replay_timer);
        g_replay_timer = NULL;
    }
    if (g_replay_panel != NULL) {
        lv_obj_del(g_replay_panel);
        g_replay_panel = NULL;
    }
    g_replay_active = 0;
    g_replay_idx = 0;
}

static void scope_replay_timer_cb(lv_timer_t *t)
{
    char buf[32];

    (void)t;
    if (g_replay_idx >= g_replay_n) {
        scope_replay_close();
        if (g_lbl_value)
            lv_label_set_text(g_lbl_value, "REPLAY DONE");
        return;
    }
    scope_push_sample(scope_replay_scale(g_replay_ch, &g_replay_pts[g_replay_idx]));
    g_replay_idx++;
    if (g_lbl_value) {
        snprintf(buf, sizeof(buf), "REPLAY %d/%d", g_replay_idx, g_replay_n);
        lv_label_set_text(g_lbl_value, buf);
    }
}

static void scope_replay_start(const char *name, int ch)
{
    int n;

    n = dbg_log_replay(name, g_replay_pts, SCOPE_REPLAY_MAX);
    if (n <= 0) {
        if (g_lbl_value)
            lv_label_set_text(g_lbl_value, "EMPTY");
        return;
    }
    scope_replay_close();              /* 关掉浮层（若存在） */
    g_replay_n = n;
    g_replay_idx = 0;
    g_replay_ch = ch;
    g_sample_cnt = 0;                  /* 清空实时波形 */
    memset(g_samples, 0, sizeof(g_samples));
    lv_line_set_points(g_line, g_points, 0);
    dbg_adc_run(0);                    /* 回放期间暂停实时采样 */
    update_run_btn();
    g_replay_active = 1;
    g_replay_timer = lv_timer_create(scope_replay_timer_cb, 30, NULL);
}

static void scope_replay_file_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

    if (idx >= 0 && idx < g_replay_cnt)
        scope_replay_start(g_replay_names[idx], g_replay_ch);
}

static void scope_replay_ch_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    g_replay_ch = (int)(intptr_t)lv_obj_get_user_data(btn);   /* 0/1/2 */
}

static void scope_replay_close_cb(lv_event_t *e)
{
    (void)e;
    scope_replay_close();
}

static void scope_replay_open(lv_event_t *e)
{
    const dbg_theme_t *th = dbg_theme_get();
    lv_obj_t *cap, *btn, *lbl;
    int i, y;

    (void)e;
    /* 再点一次：运行中则停，已开浮层则关 */
    if (g_replay_active) { scope_replay_close(); return; }
    if (g_replay_panel != NULL) { scope_replay_close(); return; }

    g_replay_cnt = dbg_log_list_csv(g_replay_names, SCOPE_REPLAY_CSV);

    g_replay_panel = lv_obj_create(g_scope_screen);
    lv_obj_set_size(g_replay_panel, LV_HOR_RES - 24, LV_VER_RES - 70);
    lv_obj_align(g_replay_panel, LV_ALIGN_TOP_LEFT, 12, 54);
    lv_obj_set_style_bg_color(g_replay_panel, lv_color_hex(th->card_bg), 0);
    lv_obj_set_style_bg_opa(g_replay_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_replay_panel, 10, 0);
    lv_obj_set_style_border_width(g_replay_panel, 1, 0);
    lv_obj_set_style_border_color(g_replay_panel, lv_color_hex(scope_accent()), 0);

    cap = lv_label_create(g_replay_panel);
    lv_label_set_text(cap, "Replay CSV");
    lv_obj_set_style_text_color(cap, lv_color_hex(th->text), 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 10, 8);

    btn = lv_button_create(g_replay_panel);
    lv_obj_set_size(btn, 60, 28);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -10, 8);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Close");
    lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, scope_replay_close_cb, LV_EVENT_CLICKED, NULL);

    if (g_replay_cnt == 0) {
        lbl = lv_label_create(g_replay_panel);
        lv_label_set_text(lbl, "NO FILE (record first)");
        lv_obj_set_style_text_color(lbl, lv_color_hex(th->text_dim), 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, 44);
        return;
    }

    /* 通道选择：T / H / L（默认 T） */
    {
        static const char *ch_name[3] = { "T", "H", "L" };
        int x = 10;
        cap = lv_label_create(g_replay_panel);
        lv_label_set_text(cap, "Ch:");
        lv_obj_set_style_text_color(cap, lv_color_hex(th->text_dim), 0);
        lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 10, 36);
        for (i = 0; i < 3; i++) {
            btn = lv_button_create(g_replay_panel);
            lv_obj_set_size(btn, 36, 28);
            lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x + 44, 36);
            lbl = lv_label_create(btn);
            lv_label_set_text(lbl, ch_name[i]);
            lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
            lv_obj_center(lbl);
            lv_obj_set_user_data(btn, (void *)(intptr_t)i);
            lv_obj_add_event_cb(btn, scope_replay_ch_cb, LV_EVENT_CLICKED, NULL);
            x += 44;
        }
    }

    y = 72;
    for (i = 0; i < g_replay_cnt; i++) {
        btn = lv_button_create(g_replay_panel);
        lv_obj_set_size(btn, LV_HOR_RES - 24 - 20, 30);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, y);
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, g_replay_names[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
        lv_obj_center(lbl);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, scope_replay_file_cb, LV_EVENT_CLICKED, NULL);
        y += 36;
    }
}

lv_obj_t *ui_scope_screen_create(void)
{
    const dbg_theme_t *th;
    lv_obj_t *head;
    lv_obj_t *card;
    lv_obj_t *mid;
    lv_obj_t *btn_back;
    lv_obj_t *title;
    /* 位于波形卡片正下方，留 8px 间隙 */
    static const lv_coord_t bar_y = SCOPE_TOP + SCOPE_H + 8; /* 212 */

    if (g_scope_screen != NULL)
        return g_scope_screen;

    th = dbg_theme_get();
    g_btn_slot = 0;

    g_scope_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_scope_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_scope_screen, lv_color_hex(th->bg), 0);
    lv_obj_set_style_bg_opa(g_scope_screen, LV_OPA_COVER, 0);

    /* 标题栏 */
    head = lv_obj_create(g_scope_screen);
    lv_obj_set_size(head, LV_HOR_RES, 44);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(scope_accent()), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    g_head = head;

    title = lv_label_create(head);
    lv_label_set_text(title, "Analog Scope");
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
        lv_obj_t *lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, "Back");
        lv_obj_set_style_text_color(lbl_back, lv_color_hex(th->title_text), 0);
        lv_obj_center(lbl_back);
    }
    lv_obj_add_event_cb(btn_back, scope_back_cb, LV_EVENT_CLICKED, NULL);
    g_btn_back = btn_back;

    /* 回放：列出已录 CSV 并动画推入波形区（第 4 步） */
    {
        lv_obj_t *btn_rp = lv_button_create(head);
        lv_obj_set_size(btn_rp, 64, 30);
        lv_obj_align(btn_rp, LV_ALIGN_RIGHT_MID, -72, 0);
        lv_obj_set_style_bg_color(btn_rp, lv_color_hex(th->shadow), 0);
        lv_obj_set_style_bg_opa(btn_rp, (lv_opa_t)th->head_btn_opa, 0);
        lv_obj_set_style_radius(btn_rp, 6, 0);
        lv_obj_set_style_border_width(btn_rp, 0, 0);
        {
            lv_obj_t *lbl_rp = lv_label_create(btn_rp);
            lv_label_set_text(lbl_rp, "Replay");
            lv_obj_set_style_text_color(lbl_rp, lv_color_hex(th->title_text), 0);
            lv_obj_center(lbl_rp);
        }
        lv_obj_add_event_cb(btn_rp, scope_replay_open, LV_EVENT_CLICKED, NULL);
    }

    /* 波形卡片 */
    card = lv_obj_create(g_scope_screen);
    lv_obj_set_size(card, SCOPE_W, SCOPE_H);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, SCOPE_LEFT, SCOPE_TOP);
    lv_obj_set_style_bg_color(card, lv_color_hex(th->card_bg), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(th->card_border), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    g_card = card;

    /* 中线（参考线） */
    mid = lv_obj_create(card);
    lv_obj_set_size(mid, SCOPE_W - 8, 1);
    lv_obj_align(mid, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(mid, lv_color_hex(th->grid), 0);
    lv_obj_set_style_bg_opa(mid, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mid, 0, 0);
    g_mid = mid;

    /* 波形曲线 */
    g_line = lv_line_create(card);
    lv_obj_set_style_line_width(g_line, 2, 0);
    lv_obj_set_style_line_color(g_line, lv_color_hex(scope_accent()), 0);
    lv_obj_set_style_line_rounded(g_line, true, 0);
    lv_line_set_points(g_line, g_points, 0);

    /* 通道选择：左减号 */
    make_btn(g_scope_screen, "-", 0, scope_ch_dec_cb, 40, 34,
             LV_ALIGN_TOP_LEFT, SCOPE_LEFT, bar_y, NULL);

    g_lbl_ch = lv_label_create(g_scope_screen);
    lv_label_set_text(g_lbl_ch, "CH0");
    lv_obj_set_style_text_color(g_lbl_ch, lv_color_hex(th->text), 0);
    lv_obj_align(g_lbl_ch, LV_ALIGN_TOP_LEFT, SCOPE_LEFT + 52, bar_y + 8);

    /* 通道选择：右加号 */
    make_btn(g_scope_screen, "+", 0, scope_ch_inc_cb, 40, 34,
             LV_ALIGN_TOP_LEFT, SCOPE_LEFT + 96, bar_y, NULL);

    /* 数据源（可点击循环切换：SYNTH -> GPAI -> RS485 -> SYNTH） */
    g_lbl_src = lv_button_create(g_scope_screen);
    lv_obj_set_size(g_lbl_src, 84, 26);
    lv_obj_align(g_lbl_src, LV_ALIGN_TOP_LEFT, SCOPE_LEFT + 148, bar_y + 4);
    lv_obj_set_style_bg_color(g_lbl_src, lv_color_hex(th->btn_neutral), 0);
    lv_obj_set_style_bg_opa(g_lbl_src, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_lbl_src, 6, 0);
    lv_obj_set_style_border_width(g_lbl_src, 0, 0);
    {
        lv_obj_t *t = lv_label_create(g_lbl_src);
        lv_label_set_text(t, "SYNTH");
        lv_obj_set_style_text_color(t, lv_color_hex(th->text), 0);
        lv_obj_center(t);
        g_lbl_src_txt = t;
    }
    lv_obj_add_event_cb(g_lbl_src, scope_src_cb, LV_EVENT_CLICKED, NULL);

    g_lbl_value = lv_label_create(g_scope_screen);
    lv_label_set_text(g_lbl_value, "-- mV");
    lv_obj_set_style_text_color(g_lbl_value, lv_color_hex(scope_accent()), 0);
    lv_obj_align(g_lbl_value, LV_ALIGN_TOP_LEFT, SCOPE_LEFT + 244, bar_y + 8);

    /* 清空 */
    make_btn(g_scope_screen, "Clear", 0, scope_clear_cb, 56, 34,
             LV_ALIGN_TOP_RIGHT, -72, bar_y, NULL);

    /* 运行/停止 */
    make_btn(g_scope_screen, "Run", 1, scope_run_cb, 56, 34,
             LV_ALIGN_TOP_RIGHT, -8, bar_y, &g_btn_run);

    /* 初始显示数据源 */
    if (g_lbl_src_txt)
        lv_label_set_text(g_lbl_src_txt, dbg_adc_source_name());
    scope_refresh_ch_label();

    /* 页面加载/卸载事件：RS485 源仅在示波页前台才发采样请求 */
    lv_obj_add_event_cb(g_scope_screen, scope_screen_evt,
                        LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(g_scope_screen, scope_screen_evt,
                        LV_EVENT_SCREEN_UNLOADED, NULL);

    dbg_theme_register(scope_apply_theme);

    return g_scope_screen;
}

void ui_scope_on_adc(const rt_uint8_t *data, rt_uint16_t len)
{
    rt_uint8_t  src;
    rt_uint16_t adc, mv;
    char buf[24];

    if (g_scope_screen == NULL || data == NULL || len < 6)
        return;

    src = data[DBG_ADC_MSG_CH_SRC];
    adc = (rt_uint16_t)(data[DBG_ADC_MSG_CH_ADC_LO] | (data[DBG_ADC_MSG_CH_ADC_HI] << 8));
    mv  = (rt_uint16_t)(data[DBG_ADC_MSG_CH_MV_LO]  | (data[DBG_ADC_MSG_CH_MV_HI]  << 8));

    /* 量程随源切换：RS485 寄存器为 16-bit（0..65535），其余为 12-bit ADC */
    g_pt_max = (src == DBG_ADC_SRC_RS485) ? SCOPE_PT_MAX_RS485 : SCOPE_PT_MAX;

    /* 更新曲线（仅运行中推入；停止时不刷新但保留画面） */
    if (dbg_adc_is_running())
        scope_push_sample(adc);

    /* 读数：RS485 模式显示寄存器原始值（hex），其余显示 mV */
    if (src == DBG_ADC_SRC_RS485)
        snprintf(buf, sizeof(buf), "0x%04X", (unsigned)adc);
    else
        snprintf(buf, sizeof(buf), "%d mV", (int)mv);
    lv_label_set_text(g_lbl_value, buf);

    /* 数据源/通道标签（SRC 文本仅在切换时改，这里顺手保持最新） */
    if (g_lbl_src_txt)
        lv_label_set_text(g_lbl_src_txt, dbg_adc_source_name());
    scope_refresh_ch_label();
}
