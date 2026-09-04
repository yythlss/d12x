/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "five_disp.h"

struct fps_frame_ctx {
    lv_obj_t *img;
    int idx;
    const char *dir;
    int count;
};

static void fps_frame_timer_cb(lv_timer_t *t)
{
    struct fps_frame_ctx *ctx = t->user_data;
    char path[64];

    snprintf(path, sizeof(path), LVGL_DIR "%s/frame_%03d.jpg", ctx->dir, ctx->idx);
    lv_image_set_src(ctx->img, path);
    ctx->idx = (ctx->idx + 1) % ctx->count;
}

struct cnt_timer_ctx {
    lv_obj_t *label;
    lv_obj_t *fps_label;
    int cnt;
    disp_slot_t slot;
};

static void fps_timer_cb(lv_timer_t *t)
{
    struct cnt_timer_ctx *ctx = t->user_data;
    uint32_t fps = lv_spi_get_fps(slot_to_spi_id(ctx->slot));

    if (ctx->fps_label)
        lv_label_set_text_fmt(ctx->fps_label, "FPS %lu", (unsigned long)fps);
}

static void cnt_timer_cb(lv_timer_t *t)
{
    struct cnt_timer_ctx *ctx = t->user_data;

    lv_label_set_text_fmt(ctx->label, "%d", ctx->cnt % 1000);
    ctx->cnt++;
}

static void fps_screen_common(const char *dir, const char *initial, int count, int period,
                              disp_slot_t slot)
{
    lv_obj_t *screen;
    int32_t dw, dh;
    disp_get_size(&dw, &dh);
    screen = screen_create(dw, dh, lv_color_black());

    lv_obj_t *bg = lv_image_create(screen);

    lv_image_set_src(bg, initial);
    lv_obj_set_size(bg, dw, dh);
    lv_image_set_inner_align(bg, LV_IMAGE_ALIGN_STRETCH);

    static struct fps_frame_ctx g_fps_ctx[2];
    static int g_fps_ctx_idx;
    struct fps_frame_ctx *fps_ctx = &g_fps_ctx[g_fps_ctx_idx++];
    fps_ctx->img = bg;
    fps_ctx->idx = 0;
    fps_ctx->dir = dir;
    fps_ctx->count = count;
    lv_timer_create(fps_frame_timer_cb, period, fps_ctx);

    /* FPS number */
    lv_obj_t *num = lv_label_create(screen);
    lv_obj_set_style_text_color(num, lv_color_hex(0xffcc66), 0);
    lv_obj_set_style_text_font(num, font_scale(28), 0);
    lv_obj_set_style_text_align(num, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(num, "FPS --");
    lv_obj_align(num, LV_ALIGN_CENTER, 0, -scale_360(80));

    /* cycling counter */
    static struct cnt_timer_ctx g_cnt_ctx[2];
    static int g_cnt_ctx_idx;
    struct cnt_timer_ctx *cnt_ctx = &g_cnt_ctx[g_cnt_ctx_idx++];
    lv_obj_t *cnt = lv_label_create(screen);
    lv_obj_set_style_text_color(cnt, lv_color_hex(0x3a3a3a), 0);
    lv_obj_set_style_text_font(cnt, font_scale(28), 0);
    lv_obj_set_style_text_align(cnt, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(cnt, "0");
    lv_obj_align_to(cnt, num, LV_ALIGN_OUT_BOTTOM_MID, 0, scale_360(8));
    cnt_ctx->label = cnt;
    cnt_ctx->fps_label = num;
    cnt_ctx->cnt = 0;
    cnt_ctx->slot = slot;
    /* 1 ms keeps the screen dirty so LVGL refresh can approach panel max FPS. */
    lv_timer_create(cnt_timer_cb, 1, cnt_ctx);
    lv_timer_create(fps_timer_cb, 1000, cnt_ctx);

    lv_scr_load(screen);
}

void init_fps_screen_toy(void *user_data)
{
    LV_UNUSED(user_data);
    fps_screen_common("toy_frames", LVGL_DIR "toy_frames/frame_000.jpg",
                      TOY_FRAME_COUNT, TOY_FRAME_PERIOD,
                      DISP_SLOT_SPI1);
}

void init_fps_screen_weather(void *user_data)
{
    LV_UNUSED(user_data);
    fps_screen_common("weather_frames", LVGL_DIR "weather_frames/frame_000.jpg",
                      WEATHER_FRAME_COUNT, WEATHER_FRAME_PERIOD,
                      DISP_SLOT_SPI2);
}
