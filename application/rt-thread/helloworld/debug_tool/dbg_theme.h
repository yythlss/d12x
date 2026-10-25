/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_theme.h - 全局统一主题管理
 *
 * 背景：原先只有主菜单在 Sleep 时改了自己的背景色，所有子页面（串口 /
 *       示波 / 从机 / RS485）的颜色都是各自 .c 里硬编码的日间配色，
 *       切到夜间后主菜单变暗、子页面依旧亮白 —— 视觉严重割裂。
 *
 * 本模块把配色收拢到一处：
 *   1. 两套色板（DAY / NIGHT），由 dbg_theme_get() 统一返回；
 *   2. 各页面 create() 时注册一个「重新上色」回调，页面自身不再 #define 配色；
 *   3. Sleep/Wake 只需一次 dbg_theme_set()，所有已创建的页面统一跟着变。
 *
 * 颜色统一存放为 0xRRGGBB（uint32_t）。不用 lv_color_t 是两条硬性原因：
 *   - LVGL v9 的 LV_COLOR_MAKE(r,g,b) 展开为 {b, g, r}，静态初始化极易写反；
 *   - lv_color_hex() 不是编译期常量，不能填进静态结构体。
 * 页面取值统一写作 lv_color_hex(dbg_theme_get()->xxx)，语义明确不会错。
 *
 * 页面接入三步：
 *   a) create() 里用 lv_color_hex(dbg_theme_get()->xxx) 代替本地 CLR_xxx；
 *   b) 写 static void page_apply_theme(void)，把已建对象的颜色重刷一遍；
 *   c) create() 末尾 dbg_theme_register(page_apply_theme)。
 */
#ifndef __DBG_THEME_H__
#define __DBG_THEME_H__

#include <rtthread.h>
#include "lvgl.h"

typedef enum {
    DBG_THEME_DAY = 0,
    DBG_THEME_NIGHT,
} dbg_theme_mode_t;

/* 一套完整色板，均为 0xRRGGBB */
typedef struct {
    uint32_t bg;          /* 屏幕背景 */
    uint32_t card_bg;     /* 卡片/容器背景 */
    uint32_t card_border; /* 卡片描边 / 分隔线 */
    uint32_t head_bg;     /* 标题栏背景（页面強調色会覆盖） */
    uint32_t title_text;  /* 标题栏文字（浅色） */
    uint32_t text;        /* 正文文字 */
    uint32_t text_dim;    /* 次要文字 / 副标题 */
    uint32_t grid;        /* 网格线 / 参考线 */
    uint32_t btn_neutral; /* 中性按钮底色 */
    uint32_t shadow;      /* 阴影色 */
    uint32_t shadow_opa;  /* 阴影不透明度百分比 0~100 */
    uint32_t head_btn_opa;/* 标题栏按钮蒙层不透明度 0~100 */
} dbg_theme_t;

/* 已创建界面的重新上色回调 */
typedef void (*dbg_theme_apply_cb)(void);

/* -------- 生命周期 -------- */
void dbg_theme_init(void);
void dbg_theme_set(dbg_theme_mode_t mode);   /* 切换并通知所有回调 */
dbg_theme_mode_t dbg_theme_get_mode(void);   /* 当前模式 */
int  dbg_theme_is_night(void);               /* 便捷判断：1=夜间 */

/* 当前生效色板（永不为 NULL；未 init 时返回日间色板） */
const dbg_theme_t *dbg_theme_get(void);

/* 注册重新上色回调（页面 create 时调用，最多 8 个，重复注册安全） */
void dbg_theme_register(dbg_theme_apply_cb cb);

/* 按当前模式挑选强调色：两值相同视为「未指定夜间版」，自动向背景混一档 */
uint32_t dbg_theme_accent(uint32_t day, uint32_t night);

/* ============ 通用登记式换色（给 DOM 较多的子页面用） ============
 *
 * 页面不必自己保存几十个对象指针，create() 时把「谁 / 用什么颜色角色」
 * 登记进来即可，dbg_theme_set() 会自动把它们全部重刷。
 *
 *   dbg_theme_set_page_accent(0x2E8B57, 0x3FA76A);   // 先声明本页强调色
 *   lv_obj_t *lbl = lv_label_create(parent);
 *   dbg_theme_track_text(lbl, DBG_C_TEXT);           // 登记，同时立即上色
 */

/* 颜色角色：一套语义，日夜各自映射 */
typedef enum {
    DBG_C_BG         = 0,  /* 屏幕背景 */
    DBG_C_CARD,            /* 卡片/容器背景 */
    DBG_C_BORDER,          /* 描边 / 分隔线 */
    DBG_C_ACCENT,          /* 本页强调色（header / 曲线 / 主按钮） */
    DBG_C_TEXT,            /* 正文 */
    DBG_C_TITLE_TEXT,      /* 标题栏浅色文字 */
    DBG_C_DIM,             /* 次要文字 */
    DBG_C_NEUTRAL,         /* 中性按钮底色 */
    DBG_C_GRID,            /* 网格线 / 参考线 */
} dbg_theme_color_t;

/* 在页面 create() 开头调用，声明该页强调色的日/夜两版 */
void dbg_theme_set_page_accent(uint32_t day, uint32_t night);

/* 把颜色角色解析成当前模式下真正的 0xRRGGBB */
uint32_t dbg_theme_resolve(dbg_theme_color_t role, uint32_t accent_day,
                           uint32_t accent_night);

/* 登记并重刷：背景色 / 文字色（重复登记同一对象安全） */
void dbg_theme_track_bg(lv_obj_t *obj, dbg_theme_color_t role);
void dbg_theme_track_text(lv_obj_t *obj, dbg_theme_color_t role);
void dbg_theme_track_line(lv_obj_t *obj, dbg_theme_color_t role);

/* 遍历登记表重刷全部（由 dbg_theme_set() 自动调用，一般无需手动） */
void dbg_theme_repaint(void);

#endif /* __DBG_THEME_H__ */
