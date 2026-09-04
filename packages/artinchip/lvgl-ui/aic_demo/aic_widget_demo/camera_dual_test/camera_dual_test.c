/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include "lvgl.h"
#include "lv_aic_camera.h"

#define CAM_W   640
#define CAM_H   480

static void move_cam_cb(lv_event_t *e);
static void auto_switch_handler(lv_event_t *e);
static void auto_phase_cb(lv_timer_t *timer);
static void enter_phase(int phase);
static void cam_create(bool do_start);
static void update_status(void);
static lv_obj_t *create_test_panel(void);

typedef enum {
    STATE_NONE,
    STATE_READY,
    STATE_STREAMING,
    STATE_PAUSED,
    STATE_STOPPED,
} cam_state_t;

static lv_obj_t *g_cam = NULL;
static cam_state_t g_state = STATE_NONE;
static bool g_auto_mode = true;
static int g_auto_phase = 0;
static int g_sub_step = 0;
static lv_timer_t *g_auto_timer = NULL;
static lv_obj_t *g_panel = NULL;

static uint32_t g_phase_start_tick;

static lv_obj_t *g_status_label;
static lv_obj_t *g_phase_label;
static lv_obj_t *g_ch_label;
static lv_obj_t *g_indicator;
static lv_obj_t *g_auto_switch;

static const char *g_test_names[] = {
    "",                          /* 0 unused */
    "Create->Open->Start",       /* 1 */
    "Switch VIN3->VIN1",         /* 2 */
    "Stop",                      /* 3 */
    "Delete",                    /* 4 */
    "Create->Open->Del(no stop)" /* 5 */
};

static const char *g_ch_names[] = { [LV_AIC_CAMERA_CH_VIN1] = "CH1", [LV_AIC_CAMERA_CH_VIN2] = "CH2" };

static lv_obj_t *common_label(lv_obj_t *parent, const char *text,
                               lv_color_t color, lv_coord_t font_h)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    if (font_h)
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    return label;
}

static void position_camera(void)
{
    if (!g_cam || !g_panel)
        return;
    lv_obj_update_layout(g_panel);
    lv_coord_t panel_h = lv_obj_get_height(g_panel);
    lv_coord_t x = (LV_HOR_RES - lv_obj_get_width(g_cam)) / 2;
    lv_coord_t y = panel_h + (LV_VER_RES - panel_h - lv_obj_get_height(g_cam)) / 2;
    lv_obj_set_pos(g_cam, x, y);
}

static void cam_create(bool do_start)
{
    if (g_cam) {
        lv_obj_del(g_cam);
        g_cam = NULL;
    }

    g_cam = lv_aic_camera_create(lv_screen_active());
    lv_obj_set_size(g_cam, CAM_W, CAM_H);
    lv_obj_set_style_bg_color(g_cam, lv_color_hex(0x181818), 0);
    lv_obj_add_flag(g_cam, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_cam, move_cam_cb, LV_EVENT_ALL, NULL);

    lv_aic_camera_set_format(g_cam, LV_AIC_CAMERA_FORMAT_NV16);
    lv_aic_camera_open(g_cam);
    position_camera();

    if (do_start) {
        lv_aic_camera_start(g_cam);
        g_state = STATE_STREAMING;
    } else {
        g_state = STATE_READY;
    }
}

static void move_cam_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev == NULL)
            return;
        lv_indev_type_t t = lv_indev_get_type(indev);
        if (t != LV_INDEV_TYPE_POINTER)
            return;

        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);
        lv_obj_set_pos(obj, lv_obj_get_x_aligned(obj) + vect.x,
                            lv_obj_get_y_aligned(obj) + vect.y);
    }
}

static void auto_switch_handler(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    g_auto_mode = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (g_auto_mode) {
        if (!g_auto_timer) {
            g_auto_timer = lv_timer_create(auto_phase_cb, 100, NULL);
        }
        enter_phase(1);
        lv_obj_set_style_text_color(g_indicator,
                                     lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        if (g_auto_timer) {
            lv_timer_del(g_auto_timer);
            g_auto_timer = NULL;
        }
        lv_obj_set_style_text_color(g_indicator,
                                     lv_palette_main(LV_PALETTE_GREY), 0);
    }
    update_status();
}

static void enter_phase(int phase)
{
    LV_LOG_WARN("auto: enter_phase %d, g_cam=%p, state=%d", phase, g_cam, g_state);
    g_auto_phase = phase;
    g_sub_step = 0;
    g_phase_start_tick = lv_tick_get();

    switch (phase) {
    case 1:
        cam_create(true);
        break;
    case 2:
        lv_aic_camera_set_channel(g_cam, LV_AIC_CAMERA_CH_VIN2);
        break;
    case 3:
        if (g_cam) {
            lv_aic_camera_stop(g_cam);
            g_state = STATE_STOPPED;
        }
        break;
    case 4:
        if (g_cam) {
            lv_obj_del(g_cam);
            g_cam = NULL;
        }
        g_state = STATE_NONE;
        break;
    case 5:
        cam_create(false);
        if (g_cam) {
            lv_obj_del(g_cam);
            g_cam = NULL;
        }
        g_state = STATE_NONE;
        break;
    }
    LV_LOG_WARN("auto: enter_phase %d done", phase);
    update_status();
}

static void auto_phase_cb(lv_timer_t *t)
{
    uint32_t elapsed = lv_tick_elaps(g_phase_start_tick);

    switch (g_auto_phase) {
    case 1:
        if (elapsed >= 3000)
            enter_phase(2);
        break;
    case 2:
        if (g_sub_step == 0 && elapsed >= 2000) {
            LV_LOG_WARN("auto: sub_step0→1, switch back to ch0");
            lv_aic_camera_set_channel(g_cam, LV_AIC_CAMERA_CH_VIN1);
            g_sub_step = 1;
            g_phase_start_tick = lv_tick_get();
            update_status();
        } else if (g_sub_step == 1 && elapsed >= 2000) {
            LV_LOG_WARN("auto: sub_step1 done, goto phase 3");
            enter_phase(3);
        }
        break;
    case 3:
        if (elapsed >= 1000)
            enter_phase(4);
        break;
    case 4:
        if (elapsed >= 500)
            enter_phase(5);
        break;
    case 5:
        if (elapsed >= 1000)
            enter_phase(1);
        break;
    }
}

static void update_status(void)
{
    char buf[64];

    switch (g_state) {
    case STATE_NONE:
        lv_label_set_text(g_status_label, "> Status: None");
        lv_obj_set_style_text_color(g_status_label,
                                     lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(g_ch_label, "");
        break;
    case STATE_READY:
        lv_label_set_text(g_status_label, "> Status: Ready");
        lv_obj_set_style_text_color(g_status_label,
                                     lv_palette_main(LV_PALETTE_BLUE), 0);
        break;
    case STATE_STREAMING:
        lv_label_set_text(g_status_label, "> Status: Running");
        lv_obj_set_style_text_color(g_status_label,
                                     lv_palette_main(LV_PALETTE_GREEN), 0);
        break;
    case STATE_PAUSED:
        lv_label_set_text(g_status_label, "> Status: Paused");
        lv_obj_set_style_text_color(g_status_label,
                                     lv_palette_main(LV_PALETTE_ORANGE), 0);
        break;
    case STATE_STOPPED:
        lv_label_set_text(g_status_label, "> Status: Stopped");
        lv_obj_set_style_text_color(g_status_label,
                                     lv_palette_main(LV_PALETTE_RED), 0);
        break;
    }

    if (g_auto_mode && g_auto_phase >= 1 && g_auto_phase <= 5) {
        snprintf(buf, sizeof(buf), "> Phase [%d/5]: %s",
                 g_auto_phase, g_test_names[g_auto_phase]);
        lv_label_set_text(g_phase_label, buf);
        lv_obj_set_style_text_color(g_phase_label,
                                     lv_palette_main(LV_PALETTE_CYAN), 0);
    } else if (!g_auto_mode) {
        lv_label_set_text(g_phase_label, "> Manual mode");
        lv_obj_set_style_text_color(g_phase_label,
                                     lv_palette_main(LV_PALETTE_GREY), 0);
    } else {
        lv_label_set_text(g_phase_label, "");
    }

    if (g_cam && g_state != STATE_NONE) {
        uint32_t ch = lv_aic_camera_get_channel(g_cam);
        snprintf(buf, sizeof(buf), "> Channel: %s (ch %d)",
                 g_ch_names[ch], (int)ch);
        lv_label_set_text(g_ch_label, buf);
        lv_obj_set_style_text_color(g_ch_label,
                                     lv_palette_main(LV_PALETTE_ORANGE), 0);
    } else if (g_state == STATE_NONE) {
        lv_label_set_text(g_ch_label, "");
    }
}

static lv_obj_t *create_test_panel(void)
{
    lv_obj_t *panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(panel, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);

    /* Row 1: title + indicator + auto switch */
    lv_obj_t *row1 = lv_obj_create(panel);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row1, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 4, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                   LV_FLEX_ALIGN_CENTER);

    common_label(row1, "Camera Switch Test",
                 lv_color_hex(0xffffff), 14);

    g_indicator = common_label(row1, " >",
                                lv_palette_main(LV_PALETTE_GREEN), 10);
    lv_obj_set_style_text_font(g_indicator, LV_FONT_DEFAULT, 0);

    lv_obj_t *spacer = lv_obj_create(row1);
    lv_obj_set_size(spacer, LV_SIZE_CONTENT, 1);
    lv_obj_set_style_flex_grow(spacer, 1, 0);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_0, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    g_auto_switch = lv_switch_create(row1);
    lv_obj_add_state(g_auto_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_auto_switch, auto_switch_handler,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Separator */
    lv_obj_t *sep1 = lv_obj_create(panel);
    lv_obj_set_size(sep1, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(sep1, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_pad_all(sep1, 0, 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_30, 0);

    /* Row 2: Status */
    lv_obj_t *row2 = lv_obj_create(panel);
    lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row2, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 4, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                   LV_FLEX_ALIGN_CENTER);

    g_status_label = common_label(row2, "> Status: --",
                                  lv_palette_main(LV_PALETTE_GREY), 10);
    g_phase_label = common_label(row2, "",
                                 lv_palette_main(LV_PALETTE_CYAN), 10);
    g_ch_label = common_label(row2, "",
                              lv_palette_main(LV_PALETTE_ORANGE), 10);

    return panel;
}

void camera_dual_test_init(void)
{
    g_panel = create_test_panel();
    update_status();

    g_auto_timer = lv_timer_create(auto_phase_cb, 100, NULL);
    enter_phase(1);
}
