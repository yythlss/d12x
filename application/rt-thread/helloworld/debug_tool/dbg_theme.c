/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_theme.c - 全局统一主题管理实现（见 dbg_theme.h）
 */
#include <string.h>
#include <rtthread.h>      /* rt_kprintf */
#include "dbg_theme.h"

#define DBG_THEME_MAX_CB    8

/*
 * 日间色板：换成与小程序一致的马卡龙浅色 —— 米白背景 + 白卡片，
 * 强调色由各页面 accent 自带（青绿/紫/蓝/橙），正文深灰。
 */
static const dbg_theme_t g_day = {
    .bg           = 0xF6F5F1,
    .card_bg      = 0xFFFFFF,
    .card_border  = 0xEDEAE2,
    .head_bg      = 0x2FB8A0,
    .title_text   = 0xFFFFFF,
    .text         = 0x303540,
    .text_dim     = 0x8B90A0,
    .grid         = 0xF2EFE8,
    .btn_neutral  = 0x4A5563,
    .shadow       = 0x5C5242,
    .shadow_opa   = 18,
    .head_btn_opa = 30,
};

/*
 * 夜间色板：深墨蓝基调。刻意不用纯黑——LCD 有背光，纯黑既挡不住光漏，
 * 又会把卡片边界糊掉。层次改由明度拉开：
 *   head 0x0F131B < bg 0x141922 < card 0x1D2430
 * 标题/正文用柔白而非纯白，长时间盯着看不刺眼。
 */
static const dbg_theme_t g_night = {
    .bg           = 0x141922,
    .card_bg      = 0x1D2430,
    .card_border  = 0x2B3442,
    .head_bg      = 0x0F131B,
    .title_text   = 0xE4E9F0,
    .text         = 0xC3CBD8,
    .text_dim     = 0x78849A,
    .grid         = 0x28313E,
    .btn_neutral  = 0x2E3846,
    .shadow       = 0x000000,
    .shadow_opa   = 30,
    .head_btn_opa = 35,
};

static dbg_theme_mode_t g_mode = DBG_THEME_DAY;
static dbg_theme_apply_cb g_cbs[DBG_THEME_MAX_CB];
static int g_cb_cnt;

/* ---- 登记式换色 ----
 *
 * 容量 80。实测占用：serial 18 + slave 21 + rs485 8 = 47 条。
 * 页面只在首次点击进入时创建一次后被缓存，登记条目不会重复累加，
 * 故总量恒定；留到 80 是给后续新增控件的余量。
 * 主菜单与示波器页不依赖本表，走各自的 apply 回调重刷。
 */
#define DBG_THEME_MAX_TRACK  80

typedef enum {
    DBG_TRACK_BG = 0,
    DBG_TRACK_TEXT,
    DBG_TRACK_LINE,
} dbg_track_kind_t;

typedef struct {
    lv_obj_t         *obj;
    dbg_track_kind_t  kind;
    dbg_theme_color_t role;
    uint32_t          accent_day;
    uint32_t          accent_night;
} dbg_track_t;

static dbg_track_t g_tracks[DBG_THEME_MAX_TRACK];
static int g_track_cnt;
static int g_track_overflow;   /* 登记表满后被丢弃的条目数 */
static uint32_t g_page_accent_day, g_page_accent_night;

void dbg_theme_set_page_accent(uint32_t day, uint32_t night)
{
    g_page_accent_day = day;
    g_page_accent_night = night;
}

uint32_t dbg_theme_resolve(dbg_theme_color_t role, uint32_t accent_day,
                           uint32_t accent_night)
{
    const dbg_theme_t *th = dbg_theme_get();

    switch (role) {
    case DBG_C_BG:         return th->bg;
    case DBG_C_CARD:       return th->card_bg;
    case DBG_C_BORDER:     return th->card_border;
    case DBG_C_ACCENT:     return dbg_theme_accent(accent_day, accent_night);
    case DBG_C_TEXT:       return th->text;
    case DBG_C_TITLE_TEXT: return th->title_text;
    case DBG_C_DIM:        return th->text_dim;
    case DBG_C_NEUTRAL:    return th->btn_neutral;
    case DBG_C_GRID:       return th->grid;
    default:               return th->text;
    }
}

/* 内部：写入一条登记；重复登记同一对象+同类型则覆盖 */
static void track_add(lv_obj_t *obj, dbg_track_kind_t kind,
                      dbg_theme_color_t role)
{
    int i;

    if (obj == RT_NULL || g_track_cnt == 0x7FFFFFFF)
        return;

    for (i = 0; i < g_track_cnt; i++) {
        if (g_tracks[i].obj == obj && g_tracks[i].kind == kind) {
            g_tracks[i].role = role;
            g_tracks[i].accent_day = g_page_accent_day;
            g_tracks[i].accent_night = g_page_accent_night;
            return;
        }
    }

    if (g_track_cnt >= DBG_THEME_MAX_TRACK) {
        /* 登记表满：不再登记。此处必须报警——溢出是静默失败，
         * 表现为「夜间模式下个别控件还是亮的」，串口上要能看出来。 */
        if (g_track_overflow == 0)
            rt_kprintf("[theme] WARN: track table full (%d), "
                       "some widgets will not follow theme change. "
                       "Raise DBG_THEME_MAX_TRACK.\n", DBG_THEME_MAX_TRACK);
        g_track_overflow++;
        return;
    }

    g_tracks[g_track_cnt].obj = obj;
    g_tracks[g_track_cnt].kind = kind;
    g_tracks[g_track_cnt].role = role;
    g_tracks[g_track_cnt].accent_day = g_page_accent_day;
    g_tracks[g_track_cnt].accent_night = g_page_accent_night;
    g_track_cnt++;
}

/* 立即给对象上色（按当前模式） */
static void track_paint(const dbg_track_t *t)
{
    uint32_t hex;

    if (t->obj == RT_NULL)
        return;

    hex = dbg_theme_resolve(t->role, t->accent_day, t->accent_night);

    switch (t->kind) {
    case DBG_TRACK_BG:
        lv_obj_set_style_bg_color(t->obj, lv_color_hex(hex), 0);
        lv_obj_set_style_bg_opa(t->obj, LV_OPA_COVER, 0);
        break;
    case DBG_TRACK_TEXT:
        lv_obj_set_style_text_color(t->obj, lv_color_hex(hex), 0);
        break;
    case DBG_TRACK_LINE:
        lv_obj_set_style_line_color(t->obj, lv_color_hex(hex), 0);
        break;
    default:
        break;
    }
}

void dbg_theme_track_bg(lv_obj_t *obj, dbg_theme_color_t role)
{
    int i;

    track_add(obj, DBG_TRACK_BG, role);
    /* 找到刚写入的条目立即上色 */
    for (i = g_track_cnt - 1; i >= 0; i--) {
        if (g_tracks[i].obj == obj && g_tracks[i].kind == DBG_TRACK_BG) {
            track_paint(&g_tracks[i]);
            break;
        }
    }
}

void dbg_theme_track_text(lv_obj_t *obj, dbg_theme_color_t role)
{
    int i;

    track_add(obj, DBG_TRACK_TEXT, role);
    for (i = g_track_cnt - 1; i >= 0; i--) {
        if (g_tracks[i].obj == obj && g_tracks[i].kind == DBG_TRACK_TEXT) {
            track_paint(&g_tracks[i]);
            break;
        }
    }
}

void dbg_theme_track_line(lv_obj_t *obj, dbg_theme_color_t role)
{
    int i;

    track_add(obj, DBG_TRACK_LINE, role);
    for (i = g_track_cnt - 1; i >= 0; i--) {
        if (g_tracks[i].obj == obj && g_tracks[i].kind == DBG_TRACK_LINE) {
            track_paint(&g_tracks[i]);
            break;
        }
    }
}

void dbg_theme_repaint(void)
{
    int i;

    for (i = 0; i < g_track_cnt; i++) {
        if (g_tracks[i].obj != RT_NULL)
            track_paint(&g_tracks[i]);
    }
}

void dbg_theme_init(void)
{
    int i;

    for (i = 0; i < DBG_THEME_MAX_CB; i++)
        g_cbs[i] = RT_NULL;
    g_cb_cnt = 0;
    g_track_cnt = 0;
    g_page_accent_day = g_day.head_bg;
    g_page_accent_night = g_night.head_bg;
    g_mode = DBG_THEME_DAY;
}

const dbg_theme_t *dbg_theme_get(void)
{
    return (g_mode == DBG_THEME_NIGHT) ? &g_night : &g_day;
}

dbg_theme_mode_t dbg_theme_get_mode(void)
{
    return g_mode;
}

int dbg_theme_is_night(void)
{
    return (g_mode == DBG_THEME_NIGHT) ? 1 : 0;
}

void dbg_theme_register(dbg_theme_apply_cb cb)
{
    int i;

    if (cb == RT_NULL || g_cb_cnt >= DBG_THEME_MAX_CB)
        return;

    for (i = 0; i < g_cb_cnt; i++) {
        if (g_cbs[i] == cb)
            return;
    }

    g_cbs[g_cb_cnt++] = cb;
}

void dbg_theme_set(dbg_theme_mode_t mode)
{
    int i;

    if (mode != DBG_THEME_DAY && mode != DBG_THEME_NIGHT)
        return;

    g_mode = mode;

    /* 先统一重刷所有登记过的对象，再跑页面自身的回调
     * （回调可覆盖 track 的结果，例如再做半透明/动态强调色处理） */
    dbg_theme_repaint();

    /* 即使模式未变也刷一遍：可能有页面在切换之后才首次创建 */
    for (i = 0; i < g_cb_cnt; i++) {
        if (g_cbs[i])
            g_cbs[i]();
    }
}

static uint32_t hex_clamp(int v)
{
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;
    return (uint32_t)v;
}

/*
 * 强调色：夜间把日间色向背景方向混一档，避免饱和色在深色背景上「跳」。
 * 调用方可显式给夜间版；day==night 视为未提供，走自动混合（保留 30% 原色）。
 */
uint32_t dbg_theme_accent(uint32_t day, uint32_t night)
{
    const dbg_theme_t *th;
    int r, g, b;

    if (g_mode != DBG_THEME_NIGHT)
        return day;

    if (day == night) {
        th = dbg_theme_get();
        /* mix = 30% day + 70% bg */
        r = ((int)((day >> 16) & 0xFF) * 30 + (int)((th->bg >> 16) & 0xFF) * 70) / 100;
        g = ((int)((day >> 8) & 0xFF) * 30 + (int)((th->bg >> 8) & 0xFF) * 70) / 100;
        b = ((int)(day & 0xFF) * 30 + (int)(th->bg & 0xFF) * 70) / 100;
        return (hex_clamp(r) << 16) | (hex_clamp(g) << 8) | hex_clamp(b);
    }

    return night;
}
