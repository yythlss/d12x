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
#include <stdint.h>
#include "ui_slave_screen.h"
#include "dbg_slave.h"
#include "dbg_log.h"
#include "ui_main_screen.h"
#include "dbg_theme.h"

/* 页面强调色：从机青绿（日/夜两版） */
#define CLR_ACC_SLAVE_DAY     0x1D9E75
#define CLR_ACC_SLAVE_NIGHT   0x28B489

/*
 * 状态色：不再写死。这些颜色是运行时按状态切换的（联动 OK / 断线 ERR），
 * 不走 track 机制的动态角色，所以显式给出日夜两版。
 */
#define CLR_OK_DAY      0x2E8B57
#define CLR_OK_NIGHT    0x3FA76A
#define CLR_ERR_DAY     0xC0392B
#define CLR_ERR_NIGHT   0xD45B45

static uint32_t clr_ok(void)   { return dbg_theme_is_night() ? CLR_OK_NIGHT  : CLR_OK_DAY; }
static uint32_t clr_err(void)  { return dbg_theme_is_night() ? CLR_ERR_NIGHT : CLR_ERR_DAY; }
static uint32_t clr_card(void) { return dbg_theme_get()->card_bg; }
static uint32_t clr_text(void) { return dbg_theme_get()->text; }
static uint32_t clr_title_text(void) { return dbg_theme_get()->title_text; }

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

/* ---- 第 8 步：CSV 记录按钮（放标题栏右侧 —— 四张卡片已经排满，没有多余空间）---- */
static lv_obj_t *g_btn_rec;
static lv_obj_t *g_lbl_rec;
static lv_obj_t *g_lbl_rows;

/* ----------------------------- 执行器（第 6 步） ----------------------------- */
#define DO_BIT_R    (1u << 0)
#define DO_BIT_G    (1u << 1)
#define DO_BIT_B    (1u << 2)
#define DO_BIT_BEEP (1u << 3)

/* 执行器控件句柄。
 * 注：这块卡内容区只有 207x56，放不下「按钮 + 独立状态标签」两套控件，
 *     所以占空比 / 灯效模式 / 速度全部直接显示在按钮文字上，不再单开标签。 */
static lv_obj_t *g_btn_r;
static lv_obj_t *g_btn_g;
static lv_obj_t *g_btn_b;
static lv_obj_t *g_btn_beep;
static lv_obj_t *g_btn_pwm;
static char      g_pwm_txt[16];

/* 灯效控件（模式 0~7 + 速度 1~9，与从机 MB_FX_* 同步） */
static lv_obj_t *g_btn_fx;
static lv_obj_t *g_btn_spd;
static rt_uint16_t g_fx_speed = 3;      /* 当前速度档 1~9 */
static char      g_fx_txt[24];

static const char *const g_fx_names[DBG_FX_MAX] =
{
    "solid", "breath", "rainbow", "chase", "cycle", "flash", "marquee", "sparkle"
};

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
    dbg_theme_track_bg(card, DBG_C_CARD);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_opa(card, (lv_opa_t)dbg_theme_get()->shadow_opa, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    dbg_theme_track_text(t, DBG_C_DIM);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    return card;
}

/* 卡片内追加一行数值标签；ref 为上一行，NULL 表示第一行 */
static lv_obj_t *add_row(lv_obj_t *card, lv_obj_t *ref, const char *text)
{
    lv_obj_t *l = lv_label_create(card);

    lv_label_set_text(l, text);
    dbg_theme_track_text(l, DBG_C_TEXT);
    if (ref == NULL)
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 2, 24);
    else
        lv_obj_align_to(l, ref, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    return l;
}

/* --------------------------------------------- 执行器控件（第 6 步） */
/* 开关按钮：按下切换对应 DO 位，并通过 Modbus 写回从机 */
static void actuator_toggle_cb(lv_event_t *e)
{
    lv_obj_t   *btn = lv_event_get_target(e);
    lv_obj_t   *lbl = lv_obj_get_child(btn, 0);
    rt_uint32_t bit = (rt_uint32_t)(uintptr_t)lv_event_get_user_data(e);
    rt_uint16_t v   = dbg_slave_get_do_ctrl();

    v ^= (rt_uint16_t)bit;
    dbg_slave_write_reg(R_DO_CTRL, v);

    if (v & bit) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(clr_ok()), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(clr_title_text()), 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(clr_card()), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(clr_text()), 0);
    }
}

/* 占空比显示在 PWM 按钮自身文字上（卡片放不下独立标签） */
static void ui_pwm_sync(rt_uint16_t duty)
{
    rt_snprintf(g_pwm_txt, sizeof(g_pwm_txt), "PWM %u%%", duty / 10);
    if (g_btn_pwm)
        lv_label_set_text(lv_obj_get_child(g_btn_pwm, 0), g_pwm_txt);
}

/* PWM 占空比按钮：在 0/25/50/75/100% 间循环 */
static void actuator_pwm_cb(lv_event_t *e)
{
    rt_uint16_t duty = dbg_slave_get_pwm_duty();
    static const rt_uint16_t presets[] = {0, 250, 500, 750, 1000};
    int i;

    (void)e;
    for (i = 0; i < 5; i++)
        if (presets[i] == duty) break;
    i = (i + 1) % 5;

    dbg_slave_write_reg(R_PWM_DUTY, presets[i]);
    ui_pwm_sync(presets[i]);
}

/* 灯效按钮：在 8 种模式间循环，并把当前模式/速度写回从机 */
static void ui_fx_sync(void)
{
    rt_uint16_t mode = dbg_slave_get_led_fx();

    if (mode >= DBG_FX_MAX) mode = DBG_FX_SOLID;
    if (g_fx_speed < 1 || g_fx_speed > 9) g_fx_speed = 3;

    /* FX 按钮文字 = 模式名 + 速度档（纯色模式下速度无意义，只显示模式名） */
    if (mode == DBG_FX_SOLID)
        rt_snprintf(g_fx_txt, sizeof(g_fx_txt), "%s", g_fx_names[mode]);
    else
        rt_snprintf(g_fx_txt, sizeof(g_fx_txt), "%s %u",
                    g_fx_names[mode], (unsigned)g_fx_speed);

    if (g_btn_fx)
    {
        lv_label_set_text(lv_obj_get_child(g_btn_fx, 0), g_fx_txt);
        /* 非纯色模式把 FX 按钮点亮，一眼能看出灯效正在跑 */
        if (mode != DBG_FX_SOLID)
        {
            lv_obj_set_style_bg_color(g_btn_fx, lv_color_hex(clr_ok()), 0);
            lv_obj_set_style_text_color(lv_obj_get_child(g_btn_fx, 0),
                                        lv_color_hex(clr_title_text()), 0);
        }
        else
        {
            lv_obj_set_style_bg_color(g_btn_fx, lv_color_hex(clr_card()), 0);
            lv_obj_set_style_text_color(lv_obj_get_child(g_btn_fx, 0),
                                        lv_color_hex(clr_text()), 0);
        }
    }

    if (g_btn_spd)
    {
        char spd[16];

        rt_snprintf(spd, sizeof(spd), "SPD %u", (unsigned)g_fx_speed);
        lv_label_set_text(lv_obj_get_child(g_btn_spd, 0), spd);
    }
}

static void actuator_fx_cb(lv_event_t *e)
{
    rt_uint16_t next = (rt_uint16_t)((dbg_slave_get_led_fx() + 1u) % DBG_FX_MAX);

    (void)e;
    dbg_slave_set_led_fx(next, g_fx_speed);
    ui_fx_sync();
}

static void actuator_spd_cb(lv_event_t *e)
{
    rt_uint16_t next = (rt_uint16_t)(g_fx_speed % 9u + 1u);   /* 1..9 循环 */

    (void)e;
    g_fx_speed = next;
    dbg_slave_set_led_fx(dbg_slave_get_led_fx(), next);
    ui_fx_sync();
}

/*
 * 执行器控件统一建按钮（flex 布局用，不传坐标）。
 * 按钮高度 ACT_BTN_H=22：内容区 56px 只放得下两行（22+4+22=48），再高就会溢出卡片。
 */
#define ACT_BTN_H   22

/*
 * 执行器按钮统一构造。
 * 关键修复：之前 lv_obj_set_size(btn, w, ...) 把宽度写死，而标签用的是 LV_FONT_DEFAULT
 * （偏大），像 "PWM 0%"/"rainbow 9"/"SPD 3" 这种文字比按钮还宽，LVGL 默认会裁切子对象
 * → 按钮文字显示不全。现改为：
 *   1) 标签显式用 montserrat_12（已编进 lv_conf，CAN RX 日志也用这字号，可读且紧凑）；
 *   2) 宽度 = LV_SIZE_CONTENT（自适应文字，永不横向裁切）+ 调用方 w 作为最小宽度
 *      （R/G/B 仍保持方块感，FX 等保留足够容纳最长 "marquee 9" 的宽度）。
 * 两行预算（12px 下）：一行 40+40+40+52、二行 60+84+44，均 < 内容区 207，不会换行。
 */
static lv_obj_t *act_btn(lv_obj_t *parent, const char *label, int w,
                         lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *lbl = lv_label_create(btn);

    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0); /* 小字号，杜绝文字超出按钮 */
    lv_obj_set_width(btn, LV_SIZE_CONTENT);                      /* 宽度自适应文字：永不裁切 */
    lv_obj_set_height(btn, ACT_BTN_H);
    lv_obj_set_style_min_width(btn, w, 0);                       /* 调用方 w 作最小宽度（保持方块/留白） */
    lv_obj_set_style_pad_hor(btn, 6, 0);                         /* 左右留白，文字不贴边 */
    lv_obj_center(lbl);
    lv_obj_set_style_bg_color(btn, lv_color_hex(clr_card()), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(clr_text()), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}

static void act_highlight(lv_obj_t *btn, int on)
{
    if (on) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(clr_ok()), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0),
                                    lv_color_hex(clr_title_text()), 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(clr_card()), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0),
                                    lv_color_hex(clr_text()), 0);
    }
}

/*
 * 执行器卡片布局 —— 改用 flex 自动排版，不再手写坐标。
 *
 * 为什么改：手算坐标在这块卡上溢出过两处（都是「编译通过但显示错」的静默问题）：
 *   1) SPD 按钮 x=200+40=240，超出内容区宽 207 → 右侧被裁
 *   2) FX 行 y=84+24=108，超出内容区（到 y≈82）→ 压出卡片/与状态条重叠
 * 卡片实测可用区只有 207x56（227x92 减去 padding 10 与标题行），手写坐标必然算错，
 * 交给 LV_FLEX_FLOW_ROW_WRAP 自动换行最稳 —— 排不下会换行，绝不会重叠。
 *
 * 两行预算：40+40+40+52+3*4 = 184 < 207；60+84+44+2*4 = 196 < 207。
 */
static void ui_slave_build_actuator(lv_obj_t *card)
{
    lv_obj_t *cont;
    rt_uint16_t v = dbg_slave_get_do_ctrl();

    cont = lv_obj_create(card);
    lv_obj_set_width(cont, LV_PCT(100));
    lv_obj_set_height(cont, 52);
    lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_style_pad_column(cont, 4, 0);

    /* 第一行：四个开关 */
    g_btn_r    = act_btn(cont, "R",    40, actuator_toggle_cb, (void *)(uintptr_t)DO_BIT_R);
    g_btn_g    = act_btn(cont, "G",    40, actuator_toggle_cb, (void *)(uintptr_t)DO_BIT_G);
    g_btn_b    = act_btn(cont, "B",    40, actuator_toggle_cb, (void *)(uintptr_t)DO_BIT_B);
    g_btn_beep = act_btn(cont, "BEEP", 52, actuator_toggle_cb, (void *)(uintptr_t)DO_BIT_BEEP);

    act_highlight(g_btn_r,    (v & DO_BIT_R)    ? 1 : 0);
    act_highlight(g_btn_g,    (v & DO_BIT_G)    ? 1 : 0);
    act_highlight(g_btn_b,    (v & DO_BIT_B)    ? 1 : 0);
    act_highlight(g_btn_beep, (v & DO_BIT_BEEP) ? 1 : 0);

    /* 第二行：占空比 / 灯效 / 速度 —— 状态直接写在按钮文字上，省掉独立标签 */
    g_btn_pwm = act_btn(cont, "PWM 0%", 60, actuator_pwm_cb, NULL);
    g_btn_fx  = act_btn(cont, "solid",  84, actuator_fx_cb,  NULL);
    g_btn_spd = act_btn(cont, "SPD 3",  44, actuator_spd_cb, NULL);

    ui_pwm_sync(dbg_slave_get_pwm_duty());
    ui_fx_sync();
}

/* ---------------------------------------- 第 8 步：SD 卡 CSV 记录按钮 */
static void ui_rec_sync(void)
{
    int st = dbg_log_state();

    if (g_btn_rec == NULL)
        return;

    if (st == DBG_LOG_ST_RUNNING) {
        lv_label_set_text(g_lbl_rec, "STOP");
        lv_obj_set_style_bg_color(g_btn_rec, lv_color_hex(clr_ok()), 0);
        lv_obj_set_style_text_color(g_lbl_rec, lv_color_hex(clr_title_text()), 0);
    } else if (st == DBG_LOG_ST_NOCARD) {
        lv_label_set_text(g_lbl_rec, "NO SD");
        lv_obj_set_style_bg_color(g_btn_rec, lv_color_hex(clr_err()), 0);
        lv_obj_set_style_text_color(g_lbl_rec, lv_color_hex(clr_title_text()), 0);
    } else {
        lv_label_set_text(g_lbl_rec, "REC");
        lv_obj_set_style_bg_color(g_btn_rec, lv_color_hex(clr_card()), 0);
        lv_obj_set_style_text_color(g_lbl_rec, lv_color_hex(clr_text()), 0);
    }

    if (st != DBG_LOG_ST_RUNNING)
        lv_label_set_text(g_lbl_rows, "");
}

static void rec_btn_cb(lv_event_t *e)
{
    (void)e;

    dbg_log_toggle();       /* 只置请求标志；挂载/开文件/写盘/关文件都在 dbglog 线程做，不卡界面 */
    ui_rec_sync();
}

/* -------------------------------------------------------------- 界面 */
static void slave_back_cb(lv_event_t *e)
{
    lv_screen_load_anim(ui_main_screen_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, 0);
}

/*
 * 主题刷新的「动态补刀」：
 * dbg_theme_repaint() 只能处理登记过的静态角色，而本页有一批颜色是
 * 「运行时按状态切换」的（R/G/B 高亮、灯效点亮、REC/STOP/NO SD、链路状态），
 * 它们不在 track 表内，需要在这里按当前状态重新派色一次。
 */
static void slave_apply_theme(void)
{
    rt_uint16_t v = dbg_slave_get_do_ctrl();

    if (g_screen == NULL)
        return;

    act_highlight(g_btn_r,    (v & DO_BIT_R)    ? 1 : 0);
    act_highlight(g_btn_g,    (v & DO_BIT_G)    ? 1 : 0);
    act_highlight(g_btn_b,    (v & DO_BIT_B)    ? 1 : 0);
    act_highlight(g_btn_beep, (v & DO_BIT_BEEP) ? 1 : 0);

    ui_fx_sync();      /* 内含「非纯色灯效时点亮 FX 按钮」的派色 */
    ui_rec_sync();     /* 内含 REC/STOP/NO SD 三态派色 */

    /* 标题栏两个按钮的描边用「标题栏浅色」，跟着 title_text 走 */
    if (g_btn_rec != NULL)
        lv_obj_set_style_border_color(g_btn_rec,
                                      lv_color_hex(clr_title_text()), 0);

    /* 链路状态色由下一次数据刷新重新决定；这里先退回中性色，
     * 避免切主题后旧的高对比色留在屏幕上。 */
    if (g_lbl_status != NULL)
        dbg_theme_track_text(g_lbl_status, DBG_C_DIM);
}

lv_obj_t *ui_slave_screen_create(void)
{
    lv_obj_t *head, *title, *btn, *btn_lbl, *cardA, *cardB, *cardC;
    int content_top, content_h, row_h, col_w, full_w, status_y;
    int screen_w = (int)LV_HOR_RES;
    int screen_h = (int)LV_VER_RES;

    if (g_screen != NULL)
        return g_screen;

    dbg_theme_set_page_accent(CLR_ACC_SLAVE_DAY, CLR_ACC_SLAVE_NIGHT);

    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, screen_w, screen_h);
    dbg_theme_track_bg(g_screen, DBG_C_BG);

    /* 标题栏 */
    head = lv_obj_create(g_screen);
    lv_obj_set_size(head, screen_w, HEADER_H);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);
    dbg_theme_track_bg(head, DBG_C_ACCENT);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);

    title = lv_label_create(head);
    lv_label_set_text(title, "Slave Monitor");
    dbg_theme_track_text(title, DBG_C_TITLE_TEXT);
    lv_obj_center(title);

    btn = lv_button_create(head);
    lv_obj_set_size(btn, 64, 34);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 6, 0);
    dbg_theme_track_bg(btn, DBG_C_ACCENT);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(clr_title_text()), 0);
    lv_obj_add_event_cb(btn, slave_back_cb, LV_EVENT_CLICKED, NULL);

    btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "< Back");
    dbg_theme_track_text(btn_lbl, DBG_C_TITLE_TEXT);
    lv_obj_center(btn_lbl);

    /* 标题栏右侧：CSV 记录（第 8 步）。
     * 四张卡片已排满，只有标题栏还放得下 —— 按钮 + 左侧一个行数标签。 */
    g_lbl_rows = lv_label_create(head);
    lv_label_set_text(g_lbl_rows, "");
    lv_obj_set_size(g_lbl_rows, 60, 34);
    lv_obj_align(g_lbl_rows, LV_ALIGN_RIGHT_MID, -70, 0);
    dbg_theme_track_text(g_lbl_rows, DBG_C_TITLE_TEXT);
    lv_obj_set_style_text_align(g_lbl_rows, LV_TEXT_ALIGN_RIGHT, 0);

    g_btn_rec = lv_button_create(head);
    lv_obj_set_size(g_btn_rec, 56, 34);
    lv_obj_align(g_btn_rec, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_border_width(g_btn_rec, 1, 0);
    lv_obj_set_style_border_color(g_btn_rec, lv_color_hex(clr_title_text()), 0);
    lv_obj_add_event_cb(g_btn_rec, rec_btn_cb, LV_EVENT_CLICKED, NULL);

    g_lbl_rec = lv_label_create(g_btn_rec);
    lv_label_set_text(g_lbl_rec, "REC");
    lv_obj_center(g_lbl_rec);

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

    /* 下排左：六轴 */
    cardC = create_card(g_screen, PAD, content_top + row_h + GAP, col_w, row_h, "Motion");
    g_lbl_acc  = add_row(cardC, NULL, "Acc  X/Y/Z: --");
    g_lbl_gyro = add_row(cardC, g_lbl_acc, "Gyro X/Y/Z: --");

    /* 下排右：执行器（第 6 步） */
    {
        lv_obj_t *cardD = create_card(g_screen, PAD + col_w + GAP,
                                      content_top + row_h + GAP, col_w, row_h,
                                      "Actuator");
        ui_slave_build_actuator(cardD);
    }

    /* 底部状态条 */
    g_lbl_status = lv_label_create(g_screen);
    lv_label_set_text(g_lbl_status, "HB --   UP -- s   waiting...");
    lv_obj_set_size(g_lbl_status, full_w, STATUS_H);
    lv_obj_set_pos(g_lbl_status, PAD, status_y);
    dbg_theme_track_text(g_lbl_status, DBG_C_DIM);

    ui_rec_sync();      /* 第 8 步：按钮初始态（待机 = 灰色 REC） */

    dbg_theme_register(slave_apply_theme);

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
        lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(clr_err()), 0);
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
    lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(clr_ok()), 0);

    /* 第 8 步：记录中则刷新已写行数（只读一个计数，不做文件 IO） */
    if (g_lbl_rows != NULL && dbg_log_state() == DBG_LOG_ST_RUNNING)
    {
        rt_snprintf(buf, sizeof(buf), "%u rows", dbg_log_rows());
        lv_label_set_text(g_lbl_rows, buf);
    }
}
