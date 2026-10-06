/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_rs485_screen.c - RS485 扫描界面（第 7 步）
 *
 * 思路：用户只填「目标地址 / 功能码 / 寄存器 / 数量 / 值」，点 Send 后把一次操作
 * 抽象成 dbg_rs485_req_t 提交给链路线程。真正的 Modbus 帧组装、触发网关、轮询状态、
 * 读应答全部在 dbg_slave 的轮询线程里完成（保证 uart1 独占、不与仪表盘轮询打架），
 * 结果经 DBG_MSG_RS485_RESULT 回传，这里只负责显示。
 *
 * 布局全部用 flex，不写死坐标，避免在不同分辨率/旋转下溢出（参考 ui_slave_screen
 * 的执行器卡踩过的坑）。
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ui_rs485_screen.h"
#include "dbg_slave.h"
#include "dbg_modbus.h"
#include "ui_main_screen.h"

/* 配色（与从机仪表盘 / 主菜单统一主题） */
#define CLR_TITLE_BAR   lv_color_hex(0x8E44AD)  /* RS485 紫 */
#define CLR_BG          lv_color_hex(0xF0F3F7)
#define CLR_CARD_BG     lv_color_hex(0xFFFFFF)
#define CLR_TEXT_WHITE  lv_color_hex(0xFFFFFF)
#define CLR_TEXT_DARK   lv_color_hex(0x333A45)
#define CLR_MUTED       lv_color_hex(0x8A93A1)
#define CLR_ACCENT      lv_color_hex(0x8E44AD)
#define CLR_OK          lv_color_hex(0x2E8B57)
#define CLR_ERR         lv_color_hex(0xC0392B)

#define HEADER_H        44

/* 步进字段（Addr / Reg / Qty / Value 共用一套逻辑） */
typedef struct {
    rt_int32_t  cur;
    rt_int32_t  min;
    rt_int32_t  max;
    int         hex;       /* 1 => 0x%04X 显示，0 => 十进制 */
    int         step;
    lv_obj_t   *lbl;       /* 数值标签 */
} field_t;

static lv_obj_t *g_screen;

/* 功能码选择 */
static rt_int32_t g_func = 3;           /* 3 读 / 6 写单 / 16 写多 */
static lv_obj_t  *g_btn_f3;
static lv_obj_t  *g_btn_f6;
static lv_obj_t  *g_btn_f16;
static lv_obj_t  *g_btn_send;

/* 可隐藏的行（FC06 隐藏 Qty，FC03 隐藏 Value） */
static lv_obj_t  *g_row_qty;
static lv_obj_t  *g_row_val;
static lv_obj_t  *g_lbl_val_name;       /* Value / Data 切换 */

/* 步进字段状态 */
static field_t    g_faddr = {1, 1, 247, 0, 1, RT_NULL};
static field_t    g_freg  = {0x10, 0, 0xFFFF, 1, 1, RT_NULL};
static field_t    g_fqty  = {2, 1, 125, 0, 1, RT_NULL};
static field_t    g_fval  = {0, 0, 0xFFFF, 1, 1, RT_NULL};

/* 结果与状态 */
static lv_obj_t  *g_lbl_result;
static lv_obj_t  *g_lbl_status;

/* --------------------------------------------- 小工具 */
static void field_refresh(field_t *f)
{
    char buf[16];

    if (f->hex)
        rt_snprintf(buf, sizeof(buf), "0x%04X", (rt_uint32_t)f->cur);
    else
        rt_snprintf(buf, sizeof(buf), "%d", (int)f->cur);
    if (f->lbl)
        lv_label_set_text(f->lbl, buf);
}

static void field_inc_cb(lv_event_t *e)
{
    field_t *f = (field_t *)lv_event_get_user_data(e);

    f->cur += f->step;
    if (f->cur > f->max) f->cur = f->max;
    field_refresh(f);
}

static void field_dec_cb(lv_event_t *e)
{
    field_t *f = (field_t *)lv_event_get_user_data(e);

    f->cur -= f->step;
    if (f->cur < f->min) f->cur = f->min;
    field_refresh(f);
}

/* 一行 flex 容器（横向排 label + 控件） */
static lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);

    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 28);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    return row;
}

static lv_obj_t *small_btn(lv_obj_t *parent, const char *label, lv_event_cb_t cb,
                           void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *lbl = lv_label_create(btn);

    lv_obj_set_size(btn, 34, 24);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    lv_obj_set_style_bg_color(btn, CLR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_color(btn, CLR_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_text_color(lbl, CLR_TEXT_DARK, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}

/* 通用步进器：label + [-] 数值 [+] */
static lv_obj_t *make_stepper(lv_obj_t *parent, const char *label_text, field_t *f)
{
    lv_obj_t *row = make_row(parent);
    lv_obj_t *lbl = lv_label_create(row);

    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, CLR_TEXT_DARK, 0);
    small_btn(row, "-", field_dec_cb, f);
    f->lbl = lv_label_create(row);
    field_refresh(f);
    lv_obj_set_style_text_color(f->lbl, CLR_TEXT_DARK, 0);
    small_btn(row, "+", field_inc_cb, f);
    return row;
}

/* ------------------------------------------- 功能码选择回调 */
static void func_highlight(void)
{
    lv_obj_t *btns[3] = {g_btn_f3, g_btn_f6, g_btn_f16};
    int       active  = (g_func == 3) ? 0 : (g_func == 6 ? 1 : 2);
    int       i;

    for (i = 0; i < 3; i++) {
        lv_obj_t *lbl = lv_obj_get_child(btns[i], 0);
        if (i == active) {
            lv_obj_set_style_bg_color(btns[i], CLR_ACCENT, 0);
            lv_obj_set_style_text_color(lbl, CLR_TEXT_WHITE, 0);
        } else {
            lv_obj_set_style_bg_color(btns[i], CLR_CARD_BG, 0);
            lv_obj_set_style_text_color(lbl, CLR_TEXT_DARK, 0);
        }
    }
}

static void func_cb(lv_event_t *e)
{
    g_func = (rt_int32_t)(intptr_t)lv_event_get_user_data(e);
    func_highlight();

    if (g_func == 3) {
        /* 读：需要 Qty，不需要 Value */
        lv_obj_clear_flag(g_row_qty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_row_val, LV_OBJ_FLAG_HIDDEN);
        if (g_btn_send) lv_label_set_text(lv_obj_get_child(g_btn_send, 0), "READ");
    } else if (g_func == 6) {
        /* 写单：需要 Value，不需要 Qty */
        lv_obj_add_flag(g_row_qty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_row_val, LV_OBJ_FLAG_HIDDEN);
        if (g_lbl_val_name) lv_label_set_text(g_lbl_val_name, "Value");
        if (g_btn_send) lv_label_set_text(lv_obj_get_child(g_btn_send, 0), "WRITE");
    } else {
        /* 写多：Qty + Data 都需要 */
        lv_obj_clear_flag(g_row_qty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_row_val, LV_OBJ_FLAG_HIDDEN);
        if (g_lbl_val_name) lv_label_set_text(g_lbl_val_name, "Data");
        if (g_btn_send) lv_label_set_text(lv_obj_get_child(g_btn_send, 0), "WRITE N");
    }
}

/* ----------------------------------------------- Send 回调 */
static void send_cb(lv_event_t *e)
{
    dbg_rs485_req_t req;
    int i;

    (void)e;
    memset(&req, 0, sizeof(req));
    req.func = (rt_uint8_t)g_func;
    req.addr = (rt_uint8_t)g_faddr.cur;
    req.reg  = (rt_uint16_t)g_freg.cur;

    if (g_func == 6) {
        req.qty = (rt_uint16_t)g_fval.cur;        /* 写单：Value 即要写的值 */
    } else {
        req.qty = (rt_uint16_t)g_fqty.cur;
        /* FC16：把 Value 作为「统一值」写入 qty 个寄存器（扫描/批量写常用；
         * 需要逐寄存器不同值时，可在 dbg_rs485_req_t.data 里逐位填，这里先给统一值） */
        for (i = 0; i < (int)req.qty && i < DBG_RS485_FC16_MAX; i++)
            req.data[i] = (rt_uint16_t)g_fval.cur;
    }

    if (dbg_slave_rs485_request(&req) == RT_EOK) {
        if (g_lbl_status) lv_label_set_text(g_lbl_status, "RS485: sending...");
        if (g_lbl_status) lv_obj_set_style_text_color(g_lbl_status, CLR_MUTED, 0);
    } else {
        if (g_lbl_status) lv_label_set_text(g_lbl_status, "RS485: bad params");
        if (g_lbl_status) lv_obj_set_style_text_color(g_lbl_status, CLR_ERR, 0);
    }
}

/* ----------------------------------------------------- 界面 */
static void rs485_back_cb(lv_event_t *e)
{
    (void)e;
    lv_screen_load_anim(ui_main_screen_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, 0);
}

lv_obj_t *ui_rs485_screen_create(void)
{
    lv_obj_t *head, *title, *btn_back, *lbl_back;
    lv_obj_t *content, *row, *lbl, *func_btn[3];
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
    lv_label_set_text(title, "RS485 Scan");
    lv_obj_set_style_text_color(title, CLR_TEXT_WHITE, 0);
    lv_obj_center(title);

    btn_back = lv_button_create(head);
    lv_obj_set_size(btn_back, 56, 30);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_30, 0);
    lv_obj_set_style_radius(btn_back, 6, 0);
    lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "<-");
    lv_obj_set_style_text_color(lbl_back, CLR_TEXT_WHITE, 0);
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, rs485_back_cb, LV_EVENT_CLICKED, NULL);

    /* 内容区：纵向 flex，标题栏之下到底部 */
    content = lv_obj_create(g_screen);
    lv_obj_set_size(content, screen_w - 16, screen_h - HEADER_H - 8);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 8, HEADER_H + 4);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 6, 0);

    /* 功能码选择行 */
    row = make_row(content);
    lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Func:");
    lv_obj_set_style_text_color(lbl, CLR_TEXT_DARK, 0);
    func_btn[0] = lv_button_create(row);
    func_btn[1] = lv_button_create(row);
    func_btn[2] = lv_button_create(row);
    g_btn_f3 = func_btn[0]; g_btn_f6 = func_btn[1]; g_btn_f16 = func_btn[2];
    {
        const char *names[3] = {"Read 03", "Write 06", "W-Mul 16"};
        int i;
        for (i = 0; i < 3; i++) {
            lv_obj_t *bl = lv_label_create(func_btn[i]);
            lv_obj_set_size(func_btn[i], 72, 24);
            lv_label_set_text(bl, names[i]);
            lv_obj_center(bl);
            lv_obj_set_style_radius(func_btn[i], 6, 0);
            lv_obj_set_style_border_width(func_btn[i], 1, 0);
            lv_obj_set_style_border_color(func_btn[i], CLR_ACCENT, 0);
            lv_obj_add_event_cb(func_btn[i], func_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)(i == 0 ? 3 : (i == 1 ? 6 : 16)));
        }
    }
    func_highlight();

    /* 步进字段行 */
    make_stepper(content, "Addr", &g_faddr);
    make_stepper(content, "Reg ", &g_freg);
    g_row_qty = make_stepper(content, "Qty ", &g_fqty);
    g_row_val = make_stepper(content, "Data", &g_fval);
    g_lbl_val_name = lv_obj_get_child(g_row_val, 0);   /* 第一个子对象是 label */
    lv_obj_add_flag(g_row_val, LV_OBJ_FLAG_HIDDEN);    /* 默认 FC03 不显示 Value */

    /* Send 按钮（整行宽） */
    g_btn_send = lv_button_create(content);
    lv_obj_set_width(g_btn_send, LV_PCT(100));
    lv_obj_set_height(g_btn_send, 40);
    lv_obj_set_style_bg_color(g_btn_send, CLR_ACCENT, 0);
    lv_obj_set_style_bg_opa(g_btn_send, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_btn_send, 8, 0);
    {
        lv_obj_t *bl = lv_label_create(g_btn_send);
        lv_label_set_text(bl, "READ");
        lv_obj_set_style_text_color(bl, CLR_TEXT_WHITE, 0);
        lv_obj_center(bl);
        lv_obj_add_event_cb(g_btn_send, send_cb, LV_EVENT_CLICKED, NULL);
    }

    /* 结果区（只读，占剩余空间，可滚动） */
    g_lbl_result = lv_label_create(content);
    lv_obj_set_width(g_lbl_result, LV_PCT(100));
    lv_label_set_long_mode(g_lbl_result, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(g_lbl_result, 1);
    lv_label_set_text(g_lbl_result, "");
    lv_obj_set_style_text_font(g_lbl_result, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(g_lbl_result, CLR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(g_lbl_result, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_lbl_result, 8, 0);
    lv_obj_set_style_radius(g_lbl_result, 8, 0);
    lv_obj_set_style_border_color(g_lbl_result, lv_color_hex(0xDCE1E8), 0);
    lv_obj_set_style_border_width(g_lbl_result, 1, 0);

    /* 状态条 */
    g_lbl_status = lv_label_create(content);
    lv_label_set_text(g_lbl_status, "ready");
    lv_obj_set_style_text_color(g_lbl_status, CLR_MUTED, 0);

    return g_screen;
}

/* ------------------------------------------- 结果刷新（LVGL 线程） */
void ui_rs485_on_result(const rt_uint8_t *data, rt_uint16_t len)
{
    dbg_rs485_result_t res;
    char buf[512];
    int  pos, i;
    const char *stxt;

    (void)data;
    (void)len;
    if (g_lbl_result == NULL)
        return;
    if (dbg_slave_rs485_get_result(&res) != RT_EOK)
        return;

    switch (res.status) {
    case DBG_RS485_ST_IDLE:    stxt = "idle";    break;
    case DBG_RS485_ST_BUSY:    stxt = "busy";    break;
    case DBG_RS485_ST_OK:      stxt = "OK";      break;
    case DBG_RS485_ST_TIMEOUT: stxt = "timeout"; break;
    case DBG_RS485_ST_CRC_ERR: stxt = "crc-err"; break;
    case DBG_RS485_ST_OVERFLOW:stxt = "toolong"; break;
    default:                   stxt = "?";       break;
    }

    pos = rt_snprintf(buf, sizeof(buf), "status=%s  tx=%u rx=%u\n",
                      stxt, res.out_len, res.in_len);

    if (res.status == DBG_RS485_ST_OK && res.in_len > 0) {
        pos += rt_snprintf(buf + pos, sizeof(buf) - pos, "raw:");
        for (i = 0; i < (int)res.in_len && pos < (int)sizeof(buf) - 8; i++)
            pos += rt_snprintf(buf + pos, sizeof(buf) - pos, " %02X", res.rsp[i]);

        /* FC03 读应答：第 3 字节是字节数，之后每 2 字节一个寄存器 */
        if (res.rsp[1] == MODBUS_FC_READ_HOLD_REGS && res.in_len >= 5) {
            int nb = res.rsp[2];
            pos += rt_snprintf(buf + pos, sizeof(buf) - pos, "\ndecoded:");
            for (i = 0; (i * 2 + 1) < nb && (pos < (int)sizeof(buf) - 14); i++) {
                rt_uint16_t v = (rt_uint16_t)((res.rsp[3 + i * 2] << 8) |
                                             res.rsp[4 + i * 2]);
                pos += rt_snprintf(buf + pos, sizeof(buf) - pos,
                                   " [%d]=%u", (int)(g_freg.cur + i), v);
            }
        }
    }

    lv_label_set_text(g_lbl_result, buf);

    if (g_lbl_status) {
        lv_label_set_text(g_lbl_status, stxt);
        lv_obj_set_style_text_color(g_lbl_status,
            (res.status == DBG_RS485_ST_OK) ? CLR_OK : CLR_ERR, 0);
    }
}
