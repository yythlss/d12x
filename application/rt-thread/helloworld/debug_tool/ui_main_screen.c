/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_main_screen.c - 调试仪主界面（双页左右滑动，2026-10-17 重写）
 *
 * 结构（480x272）：
 *   ┌──────────────────────────────────────────┐
 *   │ 顶栏 50px：[Sleep]   页面标题   BAT xx%    │  固定，不随滑动滚动
 *   ├──────────────────────────────────────────┤
 *   │ lv_tileview（208px 高，双瓦片左右滑动）      │
 *   │  瓦片0 = 环境监测（ui_env_tile_create）     │  左右滑 → 瓦片1
 *   │  瓦片1 = 应用图标网格（3x2，点按进功能页）    │  左右滑 → 瓦片0
 *   ├──────────────────────────────────────────┤
 *   │ 页面指示点 14px：● ○                       │  固定
 *   └──────────────────────────────────────────┘
 *
 * 瓦片方向语义（LVGL）：dir = 内容可被拖动的方向。
 *   瓦片0 给 LV_DIR_RIGHT：手指左滑（内容左移）露出右侧瓦片1；
 *   瓦片1 给 LV_DIR_LEFT ：手指右滑（内容右移）回到左侧瓦片0。
 *
 * 配色统一走 dbg_theme；本屏（含两个瓦片）全部走 apply 回调重刷，
 * 不占用 track 登记表（表容量 80，子页面已占 47，留给它们）。
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <rtthread.h>       /* rt_uint8_t / rt_uint16_t：子页回调签名依赖 */
#include "ui_main_screen.h"
#include "ui_serial_screen.h"
#include "ui_scope_screen.h"
#include "ui_slave_screen.h"
#include "ui_env_screen.h"
#include "ui_rs485_screen.h"
#include "ui_pwm_screen.h"
#include "dbg_power.h"
#include "dbg_theme.h"

#define HEADER_H        50      /* 顶栏高 */
#define DOTS_H          14      /* 底部指示点行高 */

/* 瓦片1 应用网格：3 列 x 2 行 */
#define APP_PAD         10
#define APP_GAP         8
#define APP_COLS        3
#define APP_NUM         6

/* 已睡眠时可点的「Wake」按钮用琥珀色吸睛，与「Sleep」常态区分开 */
#define CLR_WAKE_BTN    0xE0A030

/* 应用图标字形（用 LVGL 图元手绘，避免依赖图标字体） */
typedef enum {
    GLYPH_SER = 0,   /* 串口：> _ */
    GLYPH_ADC,       /* 示波：正弦波 */
    GLYPH_SLV,       /* 仪表盘：仪表弧 + 指针 */
    GLYPH_485,       /* 总线：三根线 + 竖干线 */
    GLYPH_PWM,       /* 方波 */
    GLYPH_ENV,       /* 温度计 */
} glyph_type_t;

typedef struct {
    const char  *title;
    const char  *sub;
    uint32_t     accent_day;
    uint32_t     accent_night;
    glyph_type_t glyph;
} app_def_t;

/* id 5 = Env：不进功能页，点按滑回瓦片0（环境监测） */
static const app_def_t g_apps[APP_NUM] = {
    { "Serial", "UART/Modbus", 0x2E8B57, 0x3FA76A, GLYPH_SER },
    { "Scope",  "ADC wave",    0xE07B24, 0xE8933C, GLYPH_ADC },
    { "Slave",  "dashboard",   0x1D9E75, 0x28B489, GLYPH_SLV },
    { "RS485",  "field bus",   0x8E44AD, 0xA463C4, GLYPH_485 },
    { "PWM",    "sq wave",     0x3B82C4, 0x5AA3DC, GLYPH_PWM },
    { "Env",    "monitor",     0x17C3A2, 0x1AD0AE, GLYPH_ENV },
};

/* 每个应用图标的运行期对象（主题切换时要重刷） */
typedef struct {
    lv_obj_t *card;
    lv_obj_t *icon;                 /* 44x44 强调色方块 */
    lv_obj_t *arc;                  /* 仪表弧（仅 GLYPH_SLV 用） */
    lv_obj_t *lbl_title;
    lv_obj_t *lbl_sub;
    lv_obj_t *lines[4];
    int       line_cnt;
    lv_obj_t *fills[3];
    int       fill_cnt;
} app_ui_t;

static lv_obj_t   *g_main_screen;
static lv_obj_t   *g_tv;               /* tileview */
static lv_obj_t   *g_tiles[2];         /* 瓦片0=环境 瓦片1=应用 */
static lv_obj_t   *g_fn_screen[APP_NUM]; /* 功能页缓存（id5 不用） */

static lv_obj_t   *g_head;             /* 顶栏容器 */
static lv_obj_t   *g_lbl_title;        /* 顶栏页面标题 */
static lv_obj_t   *g_btn_sleep;        /* 顶栏睡眠/唤醒按钮 */
static lv_obj_t   *g_lbl_bat;          /* 顶栏电量标签 */
static lv_obj_t   *g_dots[2];          /* 底部页面指示点 */
static lv_timer_t *g_bat_timer;

static app_ui_t    g_app_ui[APP_NUM];

/* ---------------------------------------------------------------- 字形点表
 * lv_line_set_points 只存指针不拷贝，必须 static（对象存活期内有效）。
 * 坐标相对于 44x44 图标方块，字形区约 x/y ∈ [9,35]。 */
static const lv_point_precise_t g_pts_ser_a[] = { {11, 11}, {18, 18}, {11, 25} };
static const lv_point_precise_t g_pts_ser_b[] = { {19, 27}, {28, 27} };

static const lv_point_precise_t g_pts_adc[] = {
    {9, 22}, {12, 16}, {15, 14}, {18, 16}, {21, 22},
    {24, 28}, {27, 30}, {30, 28}, {33, 22}
};

static const lv_point_precise_t g_pts_slv[] = { {22, 22}, {31, 16} };

static const lv_point_precise_t g_pts_485_a[] = { {14, 12}, {34, 12} };
static const lv_point_precise_t g_pts_485_b[] = { {14, 22}, {34, 22} };
static const lv_point_precise_t g_pts_485_c[] = { {14, 32}, {34, 32} };
static const lv_point_precise_t g_pts_485_d[] = { {14, 12}, {14, 32} };

static const lv_point_precise_t g_pts_pwm[] = {
    {9, 30}, {9, 18}, {15, 18}, {15, 30}, {21, 30},
    {21, 18}, {27, 18}, {27, 30}, {33, 30}
};

/* ------------------------------------------------------------ 字形构建 */
static void glyph_add_line(app_ui_t *ui, const lv_point_precise_t *pts, uint32_t n)
{
    lv_obj_t *ln;

    if (ui->line_cnt >= (int)(sizeof(ui->lines) / sizeof(ui->lines[0])))
        return;

    ln = lv_line_create(ui->icon);
    lv_line_set_points(ln, pts, n);
    lv_obj_set_style_line_width(ln, 2, 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    lv_obj_set_style_line_color(ln, lv_color_hex(dbg_theme_get()->title_text), 0);
    ui->lines[ui->line_cnt++] = ln;
}

static void glyph_add_fill(app_ui_t *ui, int x, int y, int w, int h, int radius)
{
    lv_obj_t *o;

    if (ui->fill_cnt >= (int)(sizeof(ui->fills) / sizeof(ui->fills[0])))
        return;

    o = lv_obj_create(ui->icon);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(dbg_theme_get()->title_text), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    ui->fills[ui->fill_cnt++] = o;
}

static void glyph_build(app_ui_t *ui, glyph_type_t type)
{
    switch (type) {
    case GLYPH_SER:
        glyph_add_line(ui, g_pts_ser_a, 3);
        glyph_add_line(ui, g_pts_ser_b, 2);
        break;
    case GLYPH_ADC:
        glyph_add_line(ui, g_pts_adc, 9);
        break;
    case GLYPH_SLV: {
        /* 仪表弧：135°->405°（270° 底版，缺口朝下，rotation 保持 0，
         * rotation 会叠加到角度上，设了会把缺口转到顶部） */
        lv_obj_t *arc = lv_arc_create(ui->icon);
        lv_obj_set_size(arc, 26, 26);
        lv_obj_set_pos(arc, 9, 9);
        lv_arc_set_bg_angles(arc, 135, 405);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 70);
        lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN | LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN | LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, 0, LV_PART_KNOB);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        ui->arc = arc;
        glyph_add_line(ui, g_pts_slv, 2);
        break;
    }
    case GLYPH_485:
        glyph_add_line(ui, g_pts_485_a, 2);
        glyph_add_line(ui, g_pts_485_b, 2);
        glyph_add_line(ui, g_pts_485_c, 2);
        glyph_add_line(ui, g_pts_485_d, 2);
        glyph_add_fill(ui, 12, 10, 5, 5, 2);
        glyph_add_fill(ui, 12, 20, 5, 5, 2);
        glyph_add_fill(ui, 12, 30, 5, 5, 2);
        break;
    case GLYPH_PWM:
        glyph_add_line(ui, g_pts_pwm, 9);
        break;
    case GLYPH_ENV:
        /* 温度计：竖杆 + 底部圆泡 */
        glyph_add_fill(ui, 19, 9, 7, 18, 3);
        glyph_add_fill(ui, 17, 26, 11, 11, 5);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ 页面切换 */
static void tile_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_current_target(e);
    lv_obj_t *act = lv_tileview_get_tile_active(tv);
    const dbg_theme_t *th = dbg_theme_get();
    int idx = (act == g_tiles[1]) ? 1 : 0;
    int i;

    if (g_lbl_title != NULL)
        lv_label_set_text(g_lbl_title, idx ? "Function Apps" : "Env Monitor");

    for (i = 0; i < 2; i++) {
        if (g_dots[i] != NULL)
            lv_obj_set_style_bg_color(g_dots[i],
                                      lv_color_hex(i == idx ? th->text : th->card_border), 0);
    }
}

/* 点按应用图标：进功能页；Env 图标滑回环境监测瓦片 */
static void app_click_cb(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_current_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(card);
    lv_obj_t *scr;

    if (id == 5) {
        /* Env：回到第 1 页 */
        lv_tileview_set_tile_by_index(g_tv, 0, 0, LV_ANIM_ON);
        return;
    }

    if (g_fn_screen[id] == NULL) {
        switch (id) {
        case 0: g_fn_screen[id] = ui_serial_screen_create(); break;
        case 1: g_fn_screen[id] = ui_scope_screen_create();  break;
        case 2: g_fn_screen[id] = ui_slave_screen_create();  break;
        case 3: g_fn_screen[id] = ui_rs485_screen_create();  break;
        case 4: g_fn_screen[id] = ui_pwm_screen_create();    break;
        default: break;
        }
    }

    scr = g_fn_screen[id];
    if (scr != NULL)
        lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, 0);
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

/* 睡眠按钮：切换 LIGHT 睡眠 / 唤醒（轻点屏幕亦可唤醒） */
static void sleep_btn_cb(lv_event_t *e)
{
    lv_obj_t *lbl;

    (void)e;
    if (dbg_power_is_suspended())
        dbg_power_resume();
    else
        dbg_power_suspend();

    /* 切 PS 后由 dbg_theme_set 统一重刷全部页面，这里只负责按钮文案 */
    lbl = lv_obj_get_child(g_btn_sleep, 0);
    dbg_theme_set(dbg_power_is_suspended() ? DBG_THEME_NIGHT : DBG_THEME_DAY);
    if (lbl)
        lv_label_set_text(lbl, dbg_power_is_suspended() ? "Wake" : "Sleep");
}

/* ------------------------------------------------------------ 主题刷新 */
/*
 * 整个主屏（背景/顶栏/瓦片/指示点/应用卡/图标）按当前色板重刷。
 * 由 dbg_theme_set() 回调触发，不直接被外部调用。
 * 本屏不走 track 登记表，全部在这里显式刷。
 */
static void main_apply_theme(void)
{
    const dbg_theme_t *th = dbg_theme_get();
    int suspended = dbg_power_is_suspended();
    lv_obj_t *act;
    lv_obj_t *lbl;
    int idx = 0;
    int i, j;

    if (g_main_screen == NULL)
        return;

    lv_obj_set_style_bg_color(g_main_screen, lv_color_hex(th->bg), 0);

    if (g_head != NULL)
        lv_obj_set_style_bg_color(g_head, lv_color_hex(th->head_bg), 0);

    /* 标题栏按钮：常态走色板底色，已睡眠时切琥珀「Wake」提示色 */
    if (g_btn_sleep != NULL) {
        lv_obj_set_style_bg_color(g_btn_sleep,
                                  lv_color_hex(suspended ? CLR_WAKE_BTN : th->head_bg), 0);
        lv_obj_set_style_border_color(g_btn_sleep, lv_color_hex(th->card_border), 0);
        lbl = lv_obj_get_child(g_btn_sleep, 0);
        if (lbl != NULL)
            lv_obj_set_style_text_color(lbl, lv_color_hex(th->title_text), 0);
    }

    if (g_lbl_title != NULL)
        lv_obj_set_style_text_color(g_lbl_title, lv_color_hex(th->title_text), 0);

    if (g_lbl_bat != NULL)
        lv_obj_set_style_text_color(g_lbl_bat, lv_color_hex(th->title_text), 0);

    /* 两个瓦片底色 */
    for (i = 0; i < 2; i++) {
        if (g_tiles[i] != NULL)
            lv_obj_set_style_bg_color(g_tiles[i], lv_color_hex(th->bg), 0);
    }

    /* 指示点：当前页用正文色，非当前页用卡片描边色 */
    act = (g_tv != NULL) ? lv_tileview_get_tile_active(g_tv) : NULL;
    idx = (act == g_tiles[1]) ? 1 : 0;
    for (i = 0; i < 2; i++) {
        if (g_dots[i] != NULL)
            lv_obj_set_style_bg_color(g_dots[i],
                                      lv_color_hex(i == idx ? th->text : th->card_border), 0);
    }

    /* 应用卡片 + 图标 */
    for (i = 0; i < APP_NUM; i++) {
        app_ui_t *ui = &g_app_ui[i];
        uint32_t accent;

        if (ui->card == NULL)
            continue;

        accent = dbg_theme_accent(g_apps[i].accent_day, g_apps[i].accent_night);

        lv_obj_set_style_bg_color(ui->card, lv_color_hex(th->card_bg), 0);
        lv_obj_set_style_bg_color(ui->card, lv_color_hex(th->grid), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(ui->card, lv_color_hex(th->card_border), 0);
        lv_obj_set_style_shadow_color(ui->card, lv_color_hex(th->shadow), 0);
        lv_obj_set_style_shadow_opa(ui->card, (lv_opa_t)th->shadow_opa, 0);

        if (ui->icon != NULL)
            lv_obj_set_style_bg_color(ui->icon, lv_color_hex(accent), 0);

        if (ui->arc != NULL) {
            lv_obj_set_style_arc_color(ui->arc, lv_color_hex(th->title_text), LV_PART_MAIN);
            lv_obj_set_style_arc_color(ui->arc, lv_color_hex(th->title_text), LV_PART_INDICATOR);
        }

        for (j = 0; j < ui->line_cnt; j++)
            lv_obj_set_style_line_color(ui->lines[j], lv_color_hex(th->title_text), 0);

        for (j = 0; j < ui->fill_cnt; j++)
            lv_obj_set_style_bg_color(ui->fills[j], lv_color_hex(th->title_text), 0);

        if (ui->lbl_title != NULL)
            lv_obj_set_style_text_color(ui->lbl_title, lv_color_hex(th->text), 0);
        if (ui->lbl_sub != NULL)
            lv_obj_set_style_text_color(ui->lbl_sub, lv_color_hex(th->text_dim), 0);
    }
}

/* ------------------------------------------------------------ 构建 */
/* 建一张应用卡片：强调色图标方块（手绘字形）+ 标题 + 副标题 */
static lv_obj_t *create_app_card(lv_obj_t *parent, int idx, int x, int y, int w, int h)
{
    const dbg_theme_t *th = dbg_theme_get();
    const app_def_t *app = &g_apps[idx];
    app_ui_t *ui = &g_app_ui[idx];
    uint32_t accent;

    ui->card = lv_obj_create(parent);
    lv_obj_set_size(ui->card, w, h);
    lv_obj_set_pos(ui->card, x, y);
    lv_obj_set_style_bg_color(ui->card, lv_color_hex(th->card_bg), 0);
    lv_obj_set_style_bg_opa(ui->card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui->card, 12, 0);
    lv_obj_set_style_border_width(ui->card, 1, 0);
    lv_obj_set_style_border_color(ui->card, lv_color_hex(th->card_border), 0);
    lv_obj_set_style_shadow_width(ui->card, 10, 0);
    lv_obj_set_style_shadow_color(ui->card, lv_color_hex(th->shadow), 0);
    lv_obj_set_style_shadow_opa(ui->card, (lv_opa_t)th->shadow_opa, 0);
    lv_obj_set_style_pad_all(ui->card, 0, 0);
    /* 按下反馈：grid 色在日间比卡底深一档、夜间比卡底亮一档，两套色板都自然 */
    lv_obj_set_style_bg_color(ui->card, lv_color_hex(th->grid), LV_STATE_PRESSED);

    /* 强调色图标方块 */
    accent = dbg_theme_accent(app->accent_day, app->accent_night);
    ui->icon = lv_obj_create(ui->card);
    lv_obj_set_size(ui->icon, 44, 44);
    lv_obj_set_pos(ui->icon, APP_PAD, (h - 44) / 2);
    lv_obj_set_style_bg_color(ui->icon, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(ui->icon, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui->icon, 10, 0);
    lv_obj_set_style_border_width(ui->icon, 0, 0);
    lv_obj_set_style_pad_all(ui->icon, 0, 0);
    lv_obj_clear_flag(ui->icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->icon, LV_OBJ_FLAG_SCROLLABLE);

    glyph_build(ui, app->glyph);

    /* 标题 + 副标题 */
    ui->lbl_title = lv_label_create(ui->card);
    lv_label_set_text(ui->lbl_title, app->title);
    lv_obj_set_style_text_font(ui->lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ui->lbl_title, lv_color_hex(th->text), 0);
    lv_obj_set_pos(ui->lbl_title, 62, 20);

    ui->lbl_sub = lv_label_create(ui->card);
    lv_label_set_text(ui->lbl_sub, app->sub);
    lv_obj_set_style_text_font(ui->lbl_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ui->lbl_sub, lv_color_hex(th->text_dim), 0);
    lv_obj_set_pos(ui->lbl_sub, 62, 42);

    lv_obj_set_user_data(ui->card, (void *)(intptr_t)idx);
    lv_obj_add_event_cb(ui->card, app_click_cb, LV_EVENT_CLICKED, NULL);

    return ui->card;
}

lv_obj_t *ui_main_screen_create(void)
{
    const dbg_theme_t *th;
    lv_obj_t *head;
    lv_obj_t *title;
    lv_obj_t *btn_lbl;
    lv_obj_t *bat;
    lv_obj_t *dot;
    int screen_w, screen_h;
    int tv_y, tv_h;
    int app_pad = APP_PAD;
    int app_gap = APP_GAP;
    int col_w, row_h;
    int dots_total, dot_x0, dot_y;
    int i;

    if (g_main_screen != NULL)
        return g_main_screen;

    th = dbg_theme_get();
    screen_w = (int)LV_HOR_RES;
    screen_h = (int)LV_VER_RES;
    tv_y = HEADER_H;
    tv_h = screen_h - HEADER_H - DOTS_H;

    dbg_theme_set_page_accent(th->head_bg, th->head_bg);

    g_main_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_main_screen, screen_w, screen_h);
    lv_obj_set_style_bg_color(g_main_screen, lv_color_hex(th->bg), 0);
    lv_obj_set_style_bg_opa(g_main_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_main_screen, 0, 0);
    lv_obj_set_style_radius(g_main_screen, 0, 0);
    lv_obj_set_style_border_width(g_main_screen, 0, 0);

    /* ---------------- 顶栏：Sleep + 页面标题 + BAT ---------------- */
    head = lv_obj_create(g_main_screen);
    lv_obj_set_size(head, screen_w, HEADER_H);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(th->head_bg), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    g_head = head;

    /* 睡眠/唤醒按钮（轻点屏幕亦可唤醒） */
    g_btn_sleep = lv_button_create(head);
    lv_obj_set_size(g_btn_sleep, 64, 30);
    lv_obj_align(g_btn_sleep, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(g_btn_sleep, lv_color_hex(th->head_bg), 0);
    lv_obj_set_style_bg_opa(g_btn_sleep, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_btn_sleep, 6, 0);
    lv_obj_set_style_border_width(g_btn_sleep, 1, 0);
    lv_obj_set_style_border_color(g_btn_sleep, lv_color_hex(th->card_border), 0);
    btn_lbl = lv_label_create(g_btn_sleep);
    lv_label_set_text(btn_lbl, "Sleep");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(th->title_text), 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(g_btn_sleep, sleep_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 页面标题（随瓦片切换：Env Monitor / Function Apps） */
    title = lv_label_create(head);
    lv_label_set_text(title, "Env Monitor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(th->title_text), 0);
    lv_obj_center(title);
    g_lbl_title = title;

    /* 电量标签 */
    bat = lv_label_create(head);
    lv_label_set_text(bat, "BAT --");
    lv_obj_set_style_text_color(bat, lv_color_hex(th->title_text), 0);
    lv_obj_align(bat, LV_ALIGN_RIGHT_MID, -8, 0);
    g_lbl_bat = bat;

    /* ---------------- 双瓦片：环境监测 + 应用网格 ---------------- */
    g_tv = lv_tileview_create(g_main_screen);
    lv_obj_set_size(g_tv, screen_w, tv_h);
    lv_obj_set_pos(g_tv, 0, tv_y);
    lv_obj_set_style_pad_all(g_tv, 0, 0);
    lv_obj_set_style_radius(g_tv, 0, 0);
    lv_obj_set_style_border_width(g_tv, 0, 0);
    lv_obj_set_style_bg_opa(g_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(g_tv, LV_SCROLLBAR_MODE_OFF);

    /* 瓦片0：环境监测。dir=RIGHT 表示手指左滑可露出右侧瓦片 */
    g_tiles[0] = lv_tileview_add_tile(g_tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(g_tiles[0], lv_color_hex(th->bg), 0);
    lv_obj_set_style_bg_opa(g_tiles[0], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_tiles[0], 0, 0);
    lv_obj_set_style_border_width(g_tiles[0], 0, 0);
    lv_obj_set_style_pad_all(g_tiles[0], 0, 0);
    lv_obj_clear_flag(g_tiles[0], LV_OBJ_FLAG_SCROLLABLE);

    /* 瓦片1：应用图标网格。dir=LEFT 表示手指右滑可回到左侧瓦片 */
    g_tiles[1] = lv_tileview_add_tile(g_tv, 1, 0, LV_DIR_LEFT);
    lv_obj_set_style_bg_color(g_tiles[1], lv_color_hex(th->bg), 0);
    lv_obj_set_style_bg_opa(g_tiles[1], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_tiles[1], 0, 0);
    lv_obj_set_style_border_width(g_tiles[1], 0, 0);
    lv_obj_set_style_pad_all(g_tiles[1], 0, 0);
    lv_obj_clear_flag(g_tiles[1], LV_OBJ_FLAG_SCROLLABLE);

    /* 等布局稳定后再往瓦片里填内容（pct 尺寸此时已解析） */
    lv_obj_update_layout(g_tv);

    /* 环境监测内容（数据卡 + 状态条） */
    ui_env_tile_create(g_tiles[0]);

    /* 应用图标网格：3 列 x 2 行 */
    col_w = (screen_w - 2 * app_pad - (APP_COLS - 1) * app_gap) / APP_COLS;
    row_h = (tv_h - 2 * app_pad - app_gap) / 2;
    for (i = 0; i < APP_NUM; i++) {
        int col = i % APP_COLS;
        int row = i / APP_COLS;
        int x = app_pad + col * (col_w + app_gap);
        int y = app_pad + row * (row_h + app_gap);

        create_app_card(g_tiles[1], i, x, y, col_w, row_h);
    }

    /* 瓦片切换 -> 更新标题与指示点 */
    lv_obj_add_event_cb(g_tv, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ---------------- 底部页面指示点 ---------------- */
    dots_total = 2 * 8 + 10;
    dot_x0 = (screen_w - dots_total) / 2;
    dot_y = screen_h - DOTS_H + 3;
    for (i = 0; i < 2; i++) {
        dot = lv_obj_create(g_main_screen);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_pos(dot, dot_x0 + i * (8 + 10), dot_y);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        g_dots[i] = dot;
    }
    /* 初始在第 0 页 */
    lv_obj_set_style_bg_color(g_dots[0], lv_color_hex(th->text), 0);
    lv_obj_set_style_bg_color(g_dots[1], lv_color_hex(th->card_border), 0);

    /* 电量周期刷新定时器（每 2s） */
    dbg_power_sample();
    g_bat_timer = lv_timer_create(bat_timer_cb, 2000, NULL);

    /* 登记到主题系统：Sleep/Wake 时统一跟随重刷 */
    dbg_theme_register(main_apply_theme);

    return g_main_screen;
}

lv_obj_t *ui_main_screen_get(void)
{
    if (g_main_screen == NULL)
        return ui_main_screen_create();
    return g_main_screen;
}
