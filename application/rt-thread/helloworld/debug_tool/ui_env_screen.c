/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_env_screen.c - 环境监测「瓦片」内容（主界面第 1 页）
 *
 * 2026-10-17 改为 tile 内容模块：主界面重构成 lv_tileview 双页结构
 * （左右滑动切换），本模块不再自建整屏/标题栏/返回按钮，只负责把
 * 环境数据卡 + 底部状态条构建进父 tile（见 ui_main_screen.c）。
 *
 * 布局（480x208 瓦片内）：
 *   两列 x 两行数据卡：Temp(AHT10) Humi(AHT10) Lux(AP3216C) PS(AP3216C)
 *     每卡：左侧马卡龙色条 + 指标名 + 右上在线标签 + 大号数值
 *   底部状态条：SD 卡状态 | 记录行数 | 从机运行时长 | 数据距今
 *
 * 只展示真实可测的量（与从机寄存器一一对应），没有的一律不做。
 *
 * ⚠️ 所有 lv_xxx 调用必须发生在 LVGL 定时器上下文（dbg_main 的消息分发里）。
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ui_env_screen.h"
#include "dbg_slave.h"
#include "dbg_log.h"
#include "dbg_theme.h"

/* 马卡龙色条（与小程序 app.wxss 的四色一致；色条很小，日夜同色） */
#define CLR_MAC_PURPLE      0x9B8CFF
#define CLR_MAC_BLUE        0x5FA8E8
#define CLR_MAC_ORANGE      0xF5A55C
#define CLR_MAC_GREEN       0x4FC79A

/* 状态色（运行时按链路状态切换，显式给日夜两版） */
#define CLR_OK_DAY          0x2FB56B
#define CLR_OK_NIGHT        0x3FA76A

#define ENV_PAD             10
#define ENV_GAP             6
#define ENV_STATUS_H        16
#define ENV_CARD_NUM        4

static lv_obj_t *g_tile;                     /* 父瓦片（环境页内容容器） */

static lv_obj_t *g_bar[ENV_CARD_NUM];      /* 卡片左侧马卡龙色条 */
static lv_obj_t *g_lbl_name[ENV_CARD_NUM];
static lv_obj_t *g_lbl_value[ENV_CARD_NUM];
static lv_obj_t *g_lbl_state[ENV_CARD_NUM];/* 每卡右上 Online 标签 */
static char      g_value_txt[ENV_CARD_NUM][24];

static lv_obj_t *g_lbl_status;             /* 底部状态条 */
static lv_timer_t *g_sec_timer;
static rt_uint16_t g_uptime;               /* 最近一次报文里的从机运行秒数 */
static rt_uint32_t g_last_ok_tick;         /* 最近一次有效报文（LVGL tick） */

static const char *const g_names[ENV_CARD_NUM] = { "Temp", "Humi", "Lux", "PS" };
static const uint32_t g_bar_colors[ENV_CARD_NUM] =
{
    CLR_MAC_PURPLE, CLR_MAC_BLUE, CLR_MAC_ORANGE, CLR_MAC_GREEN
};

/* ------------------------------------------------------------ 小工具 */
static rt_int16_t raw_s16(const rt_uint8_t *p)
{
    rt_uint16_t u = (rt_uint16_t)((p[0] << 8) | p[1]);
    return (rt_int16_t)u;
}

/* raw 为「真值 x10」，输出一位小数（照抄 ui_slave_screen.c） */
static void fmt_dec(char *buf, int size, rt_int16_t raw, const char *unit)
{
    int neg = raw < 0;
    int abs_v = neg ? -raw : raw;

    rt_snprintf(buf, size, "%s%d.%d %s", neg ? "-" : "", abs_v / 10, abs_v % 10, unit);
}

static uint32_t clr_ok(void)  { return dbg_theme_is_night() ? CLR_OK_NIGHT  : CLR_OK_DAY; }

/* 秒数 -> HH:MM:SS */
static void fmt_uptime(char *buf, int size, rt_uint32_t sec)
{
    rt_snprintf(buf, size, "%02u:%02u:%02u",
                (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60), (unsigned)(sec % 60));
}

/* ------------------------------------------------------------ 状态刷新 */
/* 四张卡右上角的在线标签：链路 OK 时全部 Online（青绿），异常时置灰 */
static void env_state_refresh(rt_uint8_t link_ok)
{
    int i;
    uint32_t c = link_ok ? clr_ok() : dbg_theme_get()->text_dim;
    const char *txt = link_ok ? "Online" : "--";

    for (i = 0; i < ENV_CARD_NUM; i++)
    {
        if (g_lbl_state[i] == NULL)
            continue;
        lv_label_set_text(g_lbl_state[i], txt);
        lv_obj_set_style_text_color(g_lbl_state[i], lv_color_hex(c), 0);
    }
}

/* 底部状态条：SD 卡 + 行数 + 从机运行时长 + 数据距今 */
static void env_status_refresh(void)
{
    char up[12];
    int st = dbg_log_state();
    const char *sd = (st == DBG_LOG_ST_RUNNING) ? "rec" :
                     (st == DBG_LOG_ST_IDLE)    ? "sd ok" : "no sd";
    rt_uint32_t ago = g_last_ok_tick ? ((rt_uint32_t)lv_tick_get() - g_last_ok_tick) / 1000 : 0;

    fmt_uptime(up, sizeof(up), g_uptime);
    if (g_lbl_status != NULL)
    {
        lv_label_set_text_fmt(g_lbl_status, "%s | rows %u | up %s | data %us ago",
                              sd, (unsigned)dbg_log_rows(), up, (unsigned)ago);
    }
}

static void env_sec_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    env_status_refresh();
}

/*
 * 主题刷新补刀：马卡龙色条是固定色（不走 track），状态标签是动态色。
 * 静态角色（卡片底/正文）已由 track 表处理，这里只补动态的部分。
 */
static void env_apply_theme(void)
{
    int i;
    uint32_t dim = dbg_theme_get()->text_dim;

    if (g_tile == NULL)
        return;

    for (i = 0; i < ENV_CARD_NUM; i++)
    {
        if (g_lbl_state[i] != NULL)
            lv_obj_set_style_text_color(g_lbl_state[i], lv_color_hex(dim), 0);
    }
    /* 下一次数据到来时会重新派成 Online 的颜色；先把旧高亮退掉 */
}

/* 建一张数据卡：色条 + 名称 + 右上状态 + 大号数值 */
static lv_obj_t *env_create_card(lv_obj_t *parent, int x, int y, int w, int h, int idx)
{
    lv_obj_t *card;
    lv_obj_t *bar;
    lv_obj_t *name;
    lv_obj_t *state;
    lv_obj_t *value;

    card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    dbg_theme_track_bg(card, DBG_C_CARD);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_opa(card, (lv_opa_t)dbg_theme_get()->shadow_opa, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    /* 左侧马卡龙色条（固定色，不随主题换） */
    bar = lv_obj_create(card);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 4, 18);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 2);
    lv_obj_set_style_bg_color(bar, lv_color_hex(g_bar_colors[idx]), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    g_bar[idx] = bar;

    name = lv_label_create(card);
    lv_label_set_text(name, g_names[idx]);
    dbg_theme_track_text(name, DBG_C_DIM);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 10, 0);
    g_lbl_name[idx] = name;

    state = lv_label_create(card);
    lv_label_set_text(state, "--");
    lv_obj_align(state, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_text_color(state, lv_color_hex(dbg_theme_get()->text_dim), 0);
    g_lbl_state[idx] = state;

    value = lv_label_create(card);
    lv_label_set_text(value, "--");
    /* 数值用 16pt：本目标 Kconfig 只开了 12/14/16，20/24 未编入；
     * 卡高从整屏版的 95px 收到 ~80px，16pt 与名称行不挤 */
    lv_obj_set_style_text_font(value, &lv_font_montserrat_16, 0);
    dbg_theme_track_text(value, DBG_C_TEXT);
    lv_obj_align(value, LV_ALIGN_LEFT_MID, 10, 8);
    g_lbl_value[idx] = value;

    return card;
}

/*
 * 构建环境监测瓦片内容（父对象 = 主界面 tileview 的第 1 个 tile）。
 * 返回父对象本身，方便主界面在主题切换时统一刷底色。
 * 内部缓存：重复调用直接返回已建结果。
 */
lv_obj_t *ui_env_tile_create(lv_obj_t *parent)
{
    int content_top, status_y, row_h, col_w;
    int tile_w, tile_h;
    int i;

    if (g_tile != NULL)
        return g_tile;

    if (parent == NULL)
        return NULL;

    g_tile = parent;
    tile_w = (int)lv_obj_get_width(parent);
    tile_h = (int)lv_obj_get_height(parent);

    /* 可用区域：两列 x 两行数据卡 + 底部状态条 */
    content_top = ENV_PAD;
    status_y    = tile_h - ENV_PAD - ENV_STATUS_H;
    row_h       = (status_y - ENV_GAP - content_top - ENV_GAP) / 2;
    col_w       = (tile_w - 2 * ENV_PAD - ENV_GAP) / 2;

    for (i = 0; i < ENV_CARD_NUM; i++)
    {
        int col = i % 2;
        int row = i / 2;

        env_create_card(parent, ENV_PAD + col * (col_w + ENV_GAP),
                        content_top + row * (row_h + ENV_GAP), col_w, row_h, i);
    }

    /* 底部状态条 */
    g_lbl_status = lv_label_create(parent);
    lv_label_set_text(g_lbl_status, "no sd | rows 0 | slave up 00:00:00");
    lv_obj_set_size(g_lbl_status, tile_w - 2 * ENV_PAD, ENV_STATUS_H);
    lv_obj_set_pos(g_lbl_status, ENV_PAD, status_y);
    lv_obj_set_style_text_font(g_lbl_status, &lv_font_montserrat_12, 0);
    dbg_theme_track_text(g_lbl_status, DBG_C_DIM);

    g_last_ok_tick = 0;
    g_uptime = 0;
    env_state_refresh(0);
    env_status_refresh();

    g_sec_timer = lv_timer_create(env_sec_timer_cb, 1000, NULL);

    dbg_theme_register(env_apply_theme);

    return g_tile;
}

/* ---------------------------------------------------- 数据更新（LVGL 线程） */
void ui_env_on_data(const rt_uint8_t *data, rt_uint16_t len)
{
    rt_int16_t t, h, lux;
    rt_uint16_t ps;
    rt_uint8_t status;

    if (len < SLAVE_PKT_LEN || g_tile == NULL || g_lbl_value[0] == NULL)
        return;

    status = data[SLV_OFF_STATUS];
    if (status != 0)
    {
        /* 链路异常：右上标签转灰，数值保留最后有效读数 */
        env_state_refresh(0);
        return;
    }

    t   = raw_s16(&data[SLV_OFF_TEMP]);
    h   = raw_s16(&data[SLV_OFF_HUMI]);
    lux = raw_s16(&data[SLV_OFF_LUX]);
    ps  = (rt_uint16_t)((data[SLV_OFF_PS] << 8) | data[SLV_OFF_PS + 1]);
    g_uptime = (rt_uint16_t)((data[SLV_OFF_UPTIME] << 8) | data[SLV_OFF_UPTIME + 1]);

    fmt_dec(g_value_txt[0], sizeof(g_value_txt[0]), t, "C");
    fmt_dec(g_value_txt[1], sizeof(g_value_txt[1]), h, "%");
    fmt_dec(g_value_txt[2], sizeof(g_value_txt[2]), lux, "lux");
    rt_snprintf(g_value_txt[3], sizeof(g_value_txt[3]), "%u", ps);

    lv_label_set_text(g_lbl_value[0], g_value_txt[0]);
    lv_label_set_text(g_lbl_value[1], g_value_txt[1]);
    lv_label_set_text(g_lbl_value[2], g_value_txt[2]);
    lv_label_set_text(g_lbl_value[3], g_value_txt[3]);

    g_last_ok_tick = (rt_uint32_t)lv_tick_get();
    env_state_refresh(1);
}
