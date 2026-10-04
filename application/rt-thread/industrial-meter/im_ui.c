/*
 * 便携工业调试仪 —— LVGL 仪表盘
 *
 * 布局 (480x272):
 *   +----------------------------------------------------+
 *   | 标题栏: 名称 / 在线状态                              |
 *   +-------------------------------+--------------------+
 *   | AI0 卡   AI1 卡               | INPUT  (DI 指示灯)  |
 *   | AI2 卡   AI3 卡               | OUTPUT (DO 开关)    |
 *   +----------------------------------------------------+
 *   | 状态栏: 采样周期 / 心跳 / 轮询统计                    |
 *   +----------------------------------------------------+
 */

#include <lvgl.h>
#include <rtthread.h>

#include "im_protocol.h"
#include "im_master.h"
#include "im_ui.h"

#define SCR_W           480
#define SCR_H           272

/* ---------------- 配色 ---------------- */
#define CLR_BG          0x0A111E
#define CLR_BAR         0x121C2B
#define CLR_CARD        0x18222F
#define CLR_CARD_BD     0x253144
#define CLR_TXT         0xE9F0FA
#define CLR_TXT_DIM     0x7F8FA8
#define CLR_ONLINE      0x38D996
#define CLR_OFFLINE     0xF2636B
#define CLR_IDLE        0x2A374B

static const rt_uint32_t AI_COLOR[4] = {0x2ED3EE, 0x8E95F8, 0xF7C44C, 0xF4779E};

static lv_style_t   st_card;
static lv_style_t   st_txt;
static lv_style_t   st_dim;
static lv_style_t   st_val;
static lv_style_t   st_bar_bg;

static lv_obj_t    *ai_val[4];
static lv_obj_t    *ai_bar[4];
static lv_obj_t    *di_dot[4];
static lv_obj_t    *sw_do[2];
static lv_obj_t    *dot_status;
static lv_obj_t    *lbl_status;
static lv_obj_t    *lbl_stats;

/* ---------------- 基础组件 ---------------- */
static lv_obj_t *im_panel(lv_obj_t *par, int x, int y, int w, int h)
{
    lv_obj_t *p = lv_obj_create(par);

    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_add_style(p, &st_card, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    return p;
}

static lv_obj_t *im_label(lv_obj_t *par, int x, int y, const char *txt,
                          const lv_font_t *font, rt_uint32_t color)
{
    lv_obj_t *l = lv_label_create(par);

    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);

    return l;
}

/* ---------------- 顶部标题栏 ---------------- */
static void im_build_header(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_t *dot, *pill, *l;

    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, SCR_W, 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CLR_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    im_label(bar, 14, 12, "INDUSTRIAL METER", &lv_font_montserrat_16, CLR_TXT);
    im_label(bar, 176, 15, "RS485 / MODBUS RTU", &lv_font_montserrat_14, CLR_TXT_DIM);

    /* 在线状态胶囊 */
    pill = lv_obj_create(bar);
    lv_obj_set_pos(pill, 366, 8);
    lv_obj_set_size(pill, 100, 24);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x0E1626), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, 12, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(CLR_CARD_BD), 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    dot = lv_obj_create(pill);
    lv_obj_set_pos(dot, 9, 7);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(CLR_OFFLINE), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    dot_status = dot;

    l = lv_label_create(pill);
    lv_obj_set_pos(l, 26, 4);
    lv_label_set_text(l, "OFFLINE");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(CLR_OFFLINE), 0);
    lbl_status = l;
}

/* ---------------- AI 卡片 ---------------- */
static void im_build_ai(lv_obj_t *scr, int idx, int x, int y)
{
    lv_obj_t *card = im_panel(scr, x, y, 146, 96);
    lv_obj_t *bar, *l;
    char name[8];

    name[0] = 'A';
    name[1] = 'I';
    name[2] = (char)('0' + idx);
    name[3] = 0;
    im_label(card, 12, 8, name, &lv_font_montserrat_14, CLR_TXT_DIM);

    /* 数值 */
    l = lv_label_create(card);
    lv_obj_set_pos(l, 12, 26);
    lv_label_set_text(l, "0.00");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(AI_COLOR[idx]), 0);
    ai_val[idx] = l;

    im_label(card, 88, 36, "V", &lv_font_montserrat_14, CLR_TXT_DIM);

    /* 进度条 */
    bar = lv_bar_create(card);
    lv_obj_set_pos(bar, 12, 66);
    lv_obj_set_size(bar, 122, 8);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0E1626), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(AI_COLOR[idx]), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    ai_bar[idx] = bar;
}

/* ---------------- DI / DO 面板 ---------------- */
static void im_build_io(lv_obj_t *scr)
{
    static const char *di_name[4] = {"UP", "DN", "LF", "RT"};
    lv_obj_t *card, *l;
    int i;

    /* DI */
    card = im_panel(scr, 322, 46, 148, 96);
    im_label(card, 12, 8, "INPUT", &lv_font_montserrat_14, CLR_TXT_DIM);

    for (i = 0; i < 4; i++) {
        lv_obj_t *dot = lv_obj_create(card);

        lv_obj_set_pos(dot, 12 + i * 33, 34);
        lv_obj_set_size(dot, 18, 18);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(CLR_IDLE), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        di_dot[i] = dot;

        l = lv_label_create(card);
        lv_obj_set_pos(l, 8 + i * 33, 60);
        lv_obj_set_size(l, 32, 16);
        lv_label_set_text(l, di_name[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(CLR_TXT_DIM), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }

    /* DO */
    card = im_panel(scr, 322, 152, 148, 96);
    im_label(card, 12, 8, "OUTPUT", &lv_font_montserrat_14, CLR_TXT_DIM);
    im_label(card, 12, 38, "LED-R", &lv_font_montserrat_14, CLR_TXT);
    im_label(card, 12, 68, "LED-B", &lv_font_montserrat_14, CLR_TXT);

    for (i = 0; i < 2; i++) {
        lv_obj_t *sw = lv_switch_create(card);

        lv_obj_set_pos(sw, 92, 32 + i * 30);
        lv_obj_set_size(sw, 44, 24);
        lv_obj_set_style_bg_color(sw, lv_color_hex(CLR_ONLINE), LV_PART_INDICATOR | LV_STATE_CHECKED);
        sw_do[i] = sw;
    }
}

static void im_do_event_cb(lv_event_t *e)
{
    rt_uint16_t mask = 0;

    if (lv_obj_has_state(sw_do[0], LV_STATE_CHECKED))
        mask |= IM_DO_LED_R;
    if (lv_obj_has_state(sw_do[1], LV_STATE_CHECKED))
        mask |= IM_DO_LED_B;

    /* 只置请求标志, 由轮询线程真正下发, 避免卡住 UI */
    im_master_set_do(mask);
}

/* ---------------- 底部状态栏 ---------------- */
static void im_build_footer(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_t *l;

    lv_obj_set_pos(bar, 0, 250);
    lv_obj_set_size(bar, SCR_W, SCR_H - 250);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CLR_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    l = lv_label_create(bar);
    lv_obj_set_pos(l, 10, 4);
    lv_label_set_text(l, "-");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(CLR_TXT_DIM), 0);
    lbl_stats = l;
}

/* ---------------- 刷新 ---------------- */
static void im_ui_refresh(lv_timer_t *timer)
{
    struct im_status *s = im_master_status();
    rt_uint16_t di;
    int i;

    if (!s)
        return;

    /* AI */
    for (i = 0; i < 4; i++) {
        rt_uint16_t mv = im_master_reg(IM_REG_AI0_MV + i);

        lv_label_set_text_fmt(ai_val[i], "%u.%02u", mv / 1000, (mv % 1000) / 10);
        lv_bar_set_value(ai_bar[i], mv * 100 / 3300, LV_ANIM_ON);
    }

    /* DI */
    di = im_master_reg(IM_REG_DI_MAP);
    for (i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(di_dot[i],
                                  lv_color_hex((di & (1U << i)) ? CLR_ONLINE : CLR_IDLE), 0);
    }

    /* DO 开关跟随从机实际状态 */
    {
        rt_uint16_t do_map = im_master_reg(IM_REG_DO_MAP);
        rt_uint16_t bit[2] = {IM_DO_LED_R, IM_DO_LED_B};

        for (i = 0; i < 2; i++) {
            if (!!(do_map & bit[i]) != lv_obj_has_state(sw_do[i], LV_STATE_CHECKED)) {
                if (do_map & bit[i])
                    lv_obj_add_state(sw_do[i], LV_STATE_CHECKED);
                else
                    lv_obj_remove_state(sw_do[i], LV_STATE_CHECKED);
            }
        }
    }

    /* 在线状态 */
    {
        rt_uint32_t color = s->online ? CLR_ONLINE : CLR_OFFLINE;

        lv_obj_set_style_bg_color(dot_status, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(color), 0);
        lv_label_set_text(lbl_status, s->online ? "ONLINE " : "OFFLINE");
    }

    /* 状态栏 */
    lv_label_set_text_fmt(lbl_stats, "SAMPLE %ums   HB %u   POLL %u/%u   FW v%u.%02u   ERR 0x%X",
                          im_master_reg(IM_REG_SAMPLE_MS),
                          im_master_reg(IM_REG_HEARTBEAT),
                          s->poll_ok, s->poll_fail,
                          (im_master_reg(IM_REG_FW_VER) >> 8) & 0xFF,
                          im_master_reg(IM_REG_FW_VER) & 0xFF,
                          im_master_reg(IM_REG_ERR_CODE));
}

/* ---------------- 入口 ---------------- */
void im_ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* 背景 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 通用风格 */
    lv_style_init(&st_card);
    lv_style_set_bg_color(&st_card, lv_color_hex(CLR_CARD));
    lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_radius(&st_card, 12);
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_color(&st_card, lv_color_hex(CLR_CARD_BD));
    lv_style_set_pad_all(&st_card, 0);

    im_build_header(scr);
    im_build_ai(scr, 0,  10,  46);
    im_build_ai(scr, 1, 166,  46);
    im_build_ai(scr, 2,  10, 152);
    im_build_ai(scr, 3, 166, 152);
    im_build_io(scr);
    im_build_footer(scr);

    for (int i = 0; i < 2; i++)
        lv_obj_add_event_cb(sw_do[i], im_do_event_cb, LV_EVENT_VALUE_CHANGED, RT_NULL);

    lv_timer_create(im_ui_refresh, 300, RT_NULL);
}
