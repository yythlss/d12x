/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLIT_COMMON_H
#define BLIT_COMMON_H

#include "five_disp.h"

/* ---- inline utilities (called in pixel loops) ---- */

static inline uint16_t blit_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline uint8_t blit_lerp8(uint8_t a, uint8_t b, int t)
{
    return (uint8_t)((int)a + ((((int)b - (int)a) * t + 128) >> 8));
}

static inline uint8_t blit_clamp_u8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

static inline int blit_bayer8(int x, int y)
{
    static const uint8_t m[64] = {
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21,
    };

    return (int)m[((y & 7) << 3) | (x & 7)];
}

static inline int blit_dither_q5(int v, int th)
{
    return (v * 1984 + th * 255 + 8160) / 16320;
}

static inline int blit_dither_q6(int v, int th)
{
    return (v * 4032 + th * 255 + 16320) / 32640;
}

static inline uint16_t blit_rgb565_dither(int r, int g, int b, int x, int y)
{
    int th = blit_bayer8(x, y);
    int r5 = blit_dither_q5(r, th);
    int g6 = blit_dither_q6(g, th);
    int b5 = blit_dither_q5(b, th);

    if (r5 > 31)
        r5 = 31;
    if (g6 > 63)
        g6 = 63;
    if (b5 > 31)
        b5 = 31;

    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

/* ---- shared globals ---- */

extern uint8_t g_sinlut[256];

int  blit_sin8(int x);
void blit_lut_init(void);
int  blit_atan2_8(int y, int x);

struct blit_static_bg {
    uint16_t   *pixels;
    unsigned int w;
    unsigned int h;
};

void blit_static_bg_free(struct blit_static_bg *bg);
uint16_t *blit_static_bg_get(struct blit_static_bg *bg, unsigned int w,
                             unsigned int h);
void blit_static_bg_copy(uint16_t *buf, const struct blit_static_bg *bg);

typedef void (*blit_bg_build_fn)(uint16_t *buf, unsigned int w, unsigned int h);

void blit_paint_static_bg(uint16_t *buf, struct blit_static_bg *bg,
                          unsigned int w, unsigned int h,
                          blit_bg_build_fn build);

uint32_t blit_rand(void);

/* Shared frame clock for multi-panel demos */
struct blit_link {
    uint32_t tick;
};

extern struct blit_link g_blit_link;

void blit_link_tick_advance(void);
uint32_t blit_link_tick(void);

enum blit_panel {
    BLIT_PANEL_SPI3 = 0,
    BLIT_PANEL_SPI4 = 1,
    BLIT_PANEL_SPI5 = 2,
};

/* Cross-panel sky flyer (plane loops across SPI3/4/5) */
void blit_runner_step(uint32_t t);
void blit_fill_runner(uint16_t *buf, unsigned int w, unsigned int h,
                      uint32_t t, int panel);

/* draw primitives */
void blit_fill_rect(uint16_t *buf, unsigned int w, unsigned int h,
                    int x0, int y0, int rw, int rh, uint16_t c);
void blit_fill_circle(uint16_t *buf, unsigned int w, unsigned int h,
                      int cx, int cy, int r, uint16_t c);
void blit_draw_line(uint16_t *buf, unsigned int w, unsigned int h,
                    int x0, int y0, int x1, int y1, uint16_t c);
void blit_draw_circle(uint16_t *buf, unsigned int w, unsigned int h,
                      int cx, int cy, int r, uint16_t c);
void blit_draw_digit(uint16_t *buf, unsigned int w, unsigned int h,
                     int x0, int y0, int d, uint16_t c);
void blit_fill_ellipse(uint16_t *buf, unsigned int w, unsigned int h,
                       int cx, int cy, int rx, int ry, uint16_t c);

/* effect fillers — SPI3/4/5 all draw the shared runner world */
void blit_fill_sprite(uint16_t *buf, unsigned int w, unsigned int h,
                      uint32_t t);
void blit_fill_radar(uint16_t *buf, unsigned int w, unsigned int h,
                     uint32_t t);
void blit_fill_starfield(uint16_t *buf, unsigned int w, unsigned int h,
                         uint32_t t);

#endif /* BLIT_COMMON_H */
