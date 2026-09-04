/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "five_disp.h"

lv_display_t *g_displays[DISP_SLOT_COUNT];

/*
 * LVGL display list newest-first. Map chain order to slot indices;
 * fails if fewer than DISP_SLOT_COUNT displays are registered.
 */
int disp_bind_all(void)
{
    lv_display_t *disp = NULL;
    int i;

    for (i = 0; i < DISP_SLOT_COUNT; i++) {
        disp = lv_display_get_next(disp);
        if (disp == NULL)
            return -1;
        g_displays[i] = disp;
    }

    return 0;
}

lv_display_t *disp_get(disp_slot_t slot)
{
    if (slot >= DISP_SLOT_COUNT)
        return NULL;

    return g_displays[slot];
}

void lv_obj_with_disp(lv_display_t *disp, disp_operation_t operation, void *user_data)
{
    lv_display_t *old_disp;

    if (disp == NULL || operation == NULL)
        return;

    old_disp = lv_display_get_default();
    lv_display_set_default(disp);
    operation(user_data);
    lv_display_set_default(old_disp);
}

/* ---- shared helpers ---- */

void disp_get_size(int32_t *w, int32_t *h)
{
    lv_display_t *d = lv_display_get_default();
    *w = lv_display_get_horizontal_resolution(d);
    *h = lv_display_get_vertical_resolution(d);
}

int32_t disp_min_dim(void)
{
    lv_display_t *d = lv_display_get_default();
    int32_t w = lv_display_get_horizontal_resolution(d);
    int32_t h = lv_display_get_vertical_resolution(d);
    return LV_MIN(w, h);
}

/*
 * Scale a pixel value designed for a 360x360 reference display to the
 * current display's shorter side.
 */
int32_t scale_360(int32_t val)
{
    int32_t dim = disp_min_dim();
    if (dim <= 0)
        return val;
    return (val * dim) / 360;
}

/* Pick the closest available montserrat font for the current display size. */
const lv_font_t *font_scale(int32_t ref_size)
{
    int32_t dim = disp_min_dim();
    int32_t scaled = (ref_size * dim) / 360;
    if (scaled < 12) scaled = 12;
    if (scaled > 28) scaled = 28;

    static const int32_t sizes[] = {12, 14, 16, 18, 20, 22, 28};
    int32_t best = 16;
    int32_t best_diff = 1000;
    int32_t n = (int32_t)(sizeof(sizes) / sizeof(sizes[0]));
    for (int32_t i = 0; i < n; i++) {
        int32_t diff = scaled > sizes[i] ? scaled - sizes[i] : sizes[i] - scaled;
        if (diff < best_diff) {
            best_diff = diff;
            best = sizes[i];
        }
    }

    switch (best) {
    case 12: return &lv_font_montserrat_12;
    case 14: return &lv_font_montserrat_14;
    case 16: return &lv_font_montserrat_16;
    case 18: return &lv_font_montserrat_18;
    case 20: return &lv_font_montserrat_20;
    case 22: return &lv_font_montserrat_22;
    case 28: return &lv_font_montserrat_28;
    default: return &lv_font_montserrat_16;
    }
}

lv_obj_t *screen_create(int32_t dw, int32_t dh, lv_color_t bg)
{
    lv_obj_t *s = lv_obj_create(NULL);

    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s, dw, dh);
    lv_obj_set_style_bg_color(s, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s, 0, LV_PART_MAIN);
    return s;
}

void ui_init(void)
{
    if (disp_bind_all() < 0) {
        LV_LOG_ERROR("disp_bind_all failed");
        return;
    }

#if FIVE_DISP_THEME == FIVE_DISP_THEME_WORLDCUP
    /*
     * World-cup theme: all five panels are direct-blit.
     * SPI1/SPI2 cartoon kickers; SPI3/4/5 ball corridor.
     */
    start_wc_demo();
#else
    lv_obj_with_disp(disp_get(DISP_SLOT_SPI1), init_fps_screen_toy, NULL);
    lv_obj_with_disp(disp_get(DISP_SLOT_SPI2), init_fps_screen_weather, NULL);

    /*
     * SPI3/4/5 — colorful sky flyer loops across three panels (no LVGL).
     * Start after LVGL screens so SPI1/SPI2 get CPU first at boot.
     */
    start_blit_anim(BLIT_ID_SPI3, BLIT_FX_SPRITE);
    start_blit_anim(BLIT_ID_SPI4, BLIT_FX_RADAR);
    start_blit_anim(BLIT_ID_SPI5, BLIT_FX_STARFIELD);
#endif

    lv_display_set_default(disp_get(DISP_SLOT_SPI1));
}
