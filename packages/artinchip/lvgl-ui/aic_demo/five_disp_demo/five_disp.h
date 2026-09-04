/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FIVE_DISP_H
#define FIVE_DISP_H

/*
 * Demo theme switch (compile-time). WORLDCUP is default; set to SKYFLYER
 * to restore the previous toy/weather + plane corridor demo.
 */
#define FIVE_DISP_THEME_SKYFLYER  0
#define FIVE_DISP_THEME_WORLDCUP  1
#ifndef FIVE_DISP_THEME
#define FIVE_DISP_THEME           FIVE_DISP_THEME_WORLDCUP
#endif

#include "aic_ui.h"
#include "five_disp_slot.h"
#include "lv_aic_spi.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <rtthread.h>
#include <finsh.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*disp_operation_t)(void *user_data);

extern lv_display_t *g_displays[DISP_SLOT_COUNT];

int disp_bind_all(void);
lv_display_t *disp_get(disp_slot_t slot);
void lv_obj_with_disp(lv_display_t *disp, disp_operation_t operation, void *user_data);

void disp_get_size(int32_t *w, int32_t *h);
int32_t disp_min_dim(void);
int32_t scale_360(int32_t val);
const lv_font_t *font_scale(int32_t ref_size);
lv_obj_t *screen_create(int32_t dw, int32_t dh, lv_color_t bg);

#define TOY_FRAME_COUNT      80
#define TOY_FRAME_PERIOD     30
#define WEATHER_FRAME_COUNT  30
#define WEATHER_FRAME_PERIOD 80

void init_fps_screen_toy(void *user_data);
void init_fps_screen_weather(void *user_data);

enum blit_effect {
    BLIT_FX_SPRITE = 0,
    BLIT_FX_RADAR,
    BLIT_FX_STARFIELD,
};
#define BLIT_ID_SPI3       2
#define BLIT_ID_SPI4       3
#define BLIT_ID_SPI5       4

/* Cap blit producer rate. World-cup aims for 60fps; sky-flyer stays lower. */
#if FIVE_DISP_THEME == FIVE_DISP_THEME_WORLDCUP
#define BLIT_TARGET_FPS    60
#else
#define BLIT_TARGET_FPS    20
#endif

void start_blit_anim(int id, int effect);

#if FIVE_DISP_THEME == FIVE_DISP_THEME_WORLDCUP
void start_wc_demo(void);
#endif

#ifdef __cplusplus
}
#endif
#endif /* FIVE_DISP_H */
