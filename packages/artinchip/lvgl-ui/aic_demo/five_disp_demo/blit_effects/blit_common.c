/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "blit_common.h"

/* 8-bit parabolic sine approximation, returns roughly -256..256 (no libm). */
int blit_sin8(int x)
{
    int s = (x & 0x80) ? -1 : 1;
    int q = x & 0x7F;               /* half period, 0..127 */
    int y = q * (128 - q);          /* parabola, peak 4096 at q == 64 */

    return s * (y >> 4);
}

uint8_t g_sinlut[256];
static bool g_lut_ready;

/*
 * Coarse atan2 mapped to the same 0..255 phase used by blit_sin8 / radar
 * sweeps.  Good enough for HUD lock angle; not for precise geometry.
 */
int blit_atan2_8(int y, int x)
{
    int ax = x < 0 ? -x : x;
    int ay = y < 0 ? -y : y;
    int a;

    if (ax == 0 && ay == 0)
        return 0;

    if (ax >= ay) {
        /* 0..32 in first octant-ish */
        a = (ay * 32) / (ax ? ax : 1);
    } else {
        a = 64 - (ax * 32) / (ay ? ay : 1);
    }

    if (x >= 0 && y >= 0)
        return a & 0xFF;                 /* Q1: 0..64 */
    if (x < 0 && y >= 0)
        return (128 - a) & 0xFF;         /* Q2 */
    if (x < 0 && y < 0)
        return (128 + a) & 0xFF;         /* Q3 */
    return (256 - a) & 0xFF;             /* Q4 */
}

void blit_lut_init(void)
{
    int i, v;

    if (g_lut_ready)
        return;

    for (i = 0; i < 256; i++) {
        v = 128 + (blit_sin8(i) >> 1);     /* 0..255, centered at 128 */
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        g_sinlut[i] = (uint8_t)v;
    }

    g_lut_ready = true;
}

/* ---- static background cache ---- */

void blit_static_bg_free(struct blit_static_bg *bg)
{
    if (bg->pixels) {
        rt_free(bg->pixels);
        bg->pixels = NULL;
    }
    bg->w = 0;
    bg->h = 0;
}

uint16_t *blit_static_bg_get(struct blit_static_bg *bg, unsigned int w,
                             unsigned int h)
{
    if (bg->pixels && bg->w == w && bg->h == h)
        return bg->pixels;

    blit_static_bg_free(bg);
    bg->pixels = (uint16_t *)rt_malloc(w * h * sizeof(uint16_t));
    if (!bg->pixels)
        return NULL;

    bg->w = w;
    bg->h = h;
    return bg->pixels;
}

void blit_static_bg_copy(uint16_t *buf, const struct blit_static_bg *bg)
{
    if (bg->pixels && bg->w && bg->h)
        memcpy(buf, bg->pixels, bg->w * bg->h * sizeof(uint16_t));
}

void blit_paint_static_bg(uint16_t *buf, struct blit_static_bg *bg,
                          unsigned int w, unsigned int h,
                          blit_bg_build_fn build)
{
    if (!bg->pixels || bg->w != w || bg->h != h) {
        uint16_t *cache = blit_static_bg_get(bg, w, h);

        if (cache)
            build(cache, w, h);
    }
    if (bg->pixels)
        blit_static_bg_copy(buf, bg);
    else
        build(buf, w, h);
}

/* ---- draw primitives ---- */

void blit_fill_rect(uint16_t *buf, unsigned int w, unsigned int h,
                    int x0, int y0, int rw, int rh, uint16_t c)
{
    int x, y;

    for (y = y0; y < y0 + rh; y++) {
        uint16_t *row;

        if (y < 0 || y >= (int)h)
            continue;
        row = &buf[(unsigned int)y * w];
        for (x = x0; x < x0 + rw; x++) {
            if (x < 0 || x >= (int)w)
                continue;
            row[x] = c;
        }
    }
}

void blit_fill_circle(uint16_t *buf, unsigned int w, unsigned int h,
                      int cx, int cy, int r, uint16_t c)
{
    int x, y, r2 = r * r;

    for (y = -r; y <= r; y++) {
        int py = cy + y;
        int yy = y * y;
        uint16_t *row;

        if (py < 0 || py >= (int)h)
            continue;
        row = &buf[(unsigned int)py * w];
        for (x = -r; x <= r; x++) {
            int px = cx + x;

            if (px < 0 || px >= (int)w)
                continue;
            if (x * x + yy <= r2)
                row[px] = c;
        }
    }
}

void blit_draw_line(uint16_t *buf, unsigned int w, unsigned int h,
                    int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy, e2;

    for (;;) {
        if (x0 >= 0 && x0 < (int)w && y0 >= 0 && y0 < (int)h)
            buf[(unsigned int)y0 * w + (unsigned int)x0] = c;
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void blit_draw_circle(uint16_t *buf, unsigned int w, unsigned int h,
                      int cx, int cy, int r, uint16_t c)
{
    int steps = r * 6;
    int i;

    if (steps < 1)
        steps = 1;

    for (i = 0; i < steps; i++) {
        int a = i * 256 / steps;
        int x = cx + (r * blit_sin8((a + 64) & 0xFF)) / 256;
        int y = cy + (r * blit_sin8(a & 0xFF)) / 256;

        if (x >= 0 && x < (int)w && y >= 0 && y < (int)h)
            buf[(unsigned int)y * w + (unsigned int)x] = c;
    }
}

void blit_fill_ellipse(uint16_t *buf, unsigned int w, unsigned int h,
                       int cx, int cy, int rx, int ry, uint16_t c)
{
    int x, y;
    long rx2, ry2, lim;

    if (rx < 1)
        rx = 1;
    if (ry < 1)
        ry = 1;
    rx2 = (long)rx * rx;
    ry2 = (long)ry * ry;
    lim = rx2 * ry2;

    for (y = -ry; y <= ry; y++) {
        int py = cy + y;
        long yy = (long)y * y * rx2;
        uint16_t *row;

        if (py < 0 || py >= (int)h)
            continue;
        row = &buf[(unsigned int)py * w];
        for (x = -rx; x <= rx; x++) {
            int px = cx + x;

            if (px < 0 || px >= (int)w)
                continue;
            if ((long)x * x * ry2 + yy <= lim)
                row[px] = c;
        }
    }
}

static const uint8_t g_digit3x5[10][5] = {
    { 7, 5, 5, 5, 7 },  /* 0 */
    { 2, 6, 2, 2, 7 },  /* 1 */
    { 7, 1, 7, 4, 7 },  /* 2 */
    { 7, 1, 7, 1, 7 },  /* 3 */
    { 5, 5, 7, 1, 1 },  /* 4 */
    { 7, 4, 7, 1, 7 },  /* 5 */
    { 7, 4, 7, 5, 7 },  /* 6 */
    { 7, 1, 1, 1, 1 },  /* 7 */
    { 7, 5, 7, 5, 7 },  /* 8 */
    { 7, 5, 7, 1, 7 },  /* 9 */
};

void blit_draw_digit(uint16_t *buf, unsigned int w, unsigned int h,
                     int x0, int y0, int d, uint16_t c)
{
    int r, col;

    if (d < 0 || d > 9)
        return;

    for (r = 0; r < 5; r++) {
        for (col = 0; col < 3; col++) {
            if ((g_digit3x5[d][r] >> (2 - col)) & 1) {
                int px = x0 + col;
                int py = y0 + r;

                if (px >= 0 && px < (int)w && py >= 0 && py < (int)h)
                    buf[(unsigned int)py * w + (unsigned int)px] = c;
            }
        }
    }
}

/* ---- PRNG ---- */

static uint32_t g_blit_rng = 0x1234abcdu;

uint32_t blit_rand(void)
{
    g_blit_rng ^= g_blit_rng << 13;
    g_blit_rng ^= g_blit_rng >> 17;
    g_blit_rng ^= g_blit_rng << 5;
    return g_blit_rng;
}


/* ---- shared frame clock ---- */

struct blit_link g_blit_link;

void blit_link_tick_advance(void)
{
    g_blit_link.tick = lv_tick_get() / (1000U / (uint32_t)BLIT_TARGET_FPS);
}

uint32_t blit_link_tick(void)
{
    return g_blit_link.tick;
}

