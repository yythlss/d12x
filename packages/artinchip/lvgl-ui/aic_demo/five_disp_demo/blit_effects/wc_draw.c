/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * World-cup panel painters: pitch, cartoon squad, corridor ball.
 */

#include "wc_game.h"
#include "blit_common.h"

/* Cached pitch background — computed once, memcpy'd every frame. */
static struct blit_static_bg g_pitch_bg[DISP_SLOT_COUNT];

static uint16_t wc_c_blue(void)     { return blit_rgb565(43, 140, 255); }
static uint16_t wc_c_blue_dk(void)  { return blit_rgb565(20, 80, 180); }
static uint16_t wc_c_blue_hi(void)  { return blit_rgb565(120, 190, 255); }
static uint16_t wc_c_orange(void)   { return blit_rgb565(255, 138, 43); }
static uint16_t wc_c_orange_dk(void){ return blit_rgb565(200, 90, 20); }
static uint16_t wc_c_orange_hi(void){ return blit_rgb565(255, 190, 120); }
static uint16_t wc_c_grass(void)    { return blit_rgb565(46, 160, 70); }
static uint16_t wc_c_grass2(void)   { return blit_rgb565(36, 140, 58); }
static uint16_t wc_c_line(void)     { return blit_rgb565(240, 245, 240); }
static uint16_t wc_c_skin(void)     { return blit_rgb565(255, 214, 175); }
static uint16_t wc_c_skin_sh(void)  { return blit_rgb565(230, 170, 130); }
static uint16_t wc_c_white(void)    { return blit_rgb565(250, 250, 250); }
static uint16_t wc_c_black(void)    { return blit_rgb565(20, 20, 24); }
static uint16_t wc_c_short(void)    { return blit_rgb565(35, 45, 65); }
static uint16_t wc_c_sock(void)     { return blit_rgb565(245, 245, 250); }

static uint16_t wc_hair_color(uint8_t hair)
{
    switch (hair & 3) {
    case 0: return blit_rgb565(40, 28, 20);    /* dark brown */
    case 1: return blit_rgb565(90, 55, 25);    /* brown */
    case 2: return blit_rgb565(200, 160, 60);  /* blond */
    default: return blit_rgb565(20, 20, 25);   /* black */
    }
}

static void wc_fill_pitch(uint16_t *buf, unsigned int w, unsigned int h)
{
    unsigned int y, x;
    /* Finer grass bands for round panels (keep detail in the visible disc). */
    int stripe = (int)w / 20;

    if (stripe < 4)
        stripe = 4;

    for (y = 0; y < h; y++) {
        uint16_t *row = buf + y * w;
        int band = ((int)y / 6) & 1;
        uint16_t base = band ? wc_c_grass() : wc_c_grass2();
        uint16_t alt = band ? wc_c_grass2() : wc_c_grass();

        for (x = 0; x < w; x++)
            row[x] = (((int)x / stripe) & 1) ? base : alt;
    }

    /* Sidelines kept inside the circular safe area (~20% inset). */
    blit_fill_rect(buf, w, h, (int)w / 5, (int)h / 5, (int)w * 3 / 5, 1, wc_c_line());
    blit_fill_rect(buf, w, h, (int)w / 5, (int)h * 4 / 5, (int)w * 3 / 5, 1, wc_c_line());
}

/*
 * 3×5 digit bitmaps (same as blit_common.c g_digit3x5, duplicated here
 * so the scaled renderer is self-contained).
 */
static const uint8_t wc_digit3x5[10][5] = {
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

/* Draw one 3×5 digit at scale S, centred in a cell of width cell_w. */
static void wc_score_digit(uint16_t *buf, unsigned int w, unsigned int h,
                           int x0, int y0, int d, int scale, uint16_t c)
{
    int row, col;

    if (d < 0 || d > 9)
        return;

    for (row = 0; row < 5; row++) {
        for (col = 0; col < 3; col++) {
            if ((wc_digit3x5[d][row] >> (2 - col)) & 1) {
                blit_fill_rect(buf, w, h,
                               x0 + col * scale, y0 + row * scale,
                               scale, scale, c);
            }
        }
    }
}

static void wc_draw_score(uint16_t *buf, unsigned int w, unsigned int h,
                          uint8_t sl, uint8_t sr, int emphasize_l)
{
    uint16_t c_l_val = emphasize_l ? wc_c_blue() : wc_c_white();
    uint16_t c_r_val = emphasize_l ? wc_c_white() : wc_c_orange();
    int scale = (int)w / 28;
    int cell_w, digit_w, digit_h, box_w, box_h;
    int cx, y, lx, rx, dy, gap;

    if (scale < 3)
        scale = 3;

    cell_w    = 3 * scale + scale;      /* digit + padding */
    digit_w   = 3 * scale;
    digit_h   = 5 * scale;
    gap       = scale;                  /* colon gap between cells */
    box_w     = cell_w * 2 + gap + scale * 2;
    box_h     = digit_h + scale * 3;
    cx        = (int)w / 2;
    y         = (int)h / 8;
    dy        = y + (box_h - digit_h) / 2;

    /* Dark backdrop */
    blit_fill_rect(buf, w, h, cx - box_w / 2, y, box_w, box_h,
                   blit_rgb565(12, 28, 18));
    /* Left cell (blue team) */
    blit_fill_rect(buf, w, h, cx - cell_w - gap / 2, y + scale,
                   cell_w, box_h - scale * 2, wc_c_blue_dk());
    /* Right cell (orange team) */
    blit_fill_rect(buf, w, h, cx + gap / 2, y + scale,
                   cell_w, box_h - scale * 2, wc_c_orange_dk());

    /* Digits centred in each cell */
    lx = cx - cell_w - gap / 2 + (cell_w - digit_w) / 2;
    rx = cx + gap / 2 + (cell_w - digit_w) / 2;
    wc_score_digit(buf, w, h, lx, dy, sl % 10, scale, c_l_val);
    wc_score_digit(buf, w, h, rx, dy, sr % 10, scale, c_r_val);

    /* Colon separator — scaled with the scoreboard */
    {
        int colon_sz = gap / 2;
        int colon_x = cx - colon_sz / 2;

        if (colon_sz < 2)
            colon_sz = 2;
        blit_fill_rect(buf, w, h, colon_x, dy + digit_h * 1 / 3 - colon_sz / 2,
                       colon_sz, colon_sz, wc_c_white());
        blit_fill_rect(buf, w, h, colon_x, dy + digit_h * 2 / 3 - colon_sz / 2,
                       colon_sz, colon_sz, wc_c_white());
    }
}

/* Quarter-circle corner arc at a fixed inset (inside circular safe area). */
static void wc_draw_corner_arcs(uint16_t *buf, unsigned int w, unsigned int h)
{
    int r = (int)w / 12;
    int margin = (int)w / 5;
    int steps = r * 2;
    int i;
    uint16_t c = wc_c_line();

    if (r < 3 || steps < 1)
        return;

    /* Top-left */
    for (i = 0; i <= steps; i++) {
        int a = 192 + i * 64 / steps;          /* Q2→Q3 arc */
        int x = margin + r + (r * blit_sin8((a + 64) & 0xFF)) / 256;
        int y = margin + r + (r * blit_sin8(a & 0xFF)) / 256;
        if (x >= 0 && x < (int)w && y >= 0 && y < (int)h)
            buf[(unsigned)y * w + (unsigned)x] = c;
    }
    /* Top-right */
    for (i = 0; i <= steps; i++) {
        int a = 128 + i * 64 / steps;          /* Q1→Q2 arc */
        int x = (int)w - margin - r + (r * blit_sin8((a + 64) & 0xFF)) / 256;
        int y = margin + r + (r * blit_sin8(a & 0xFF)) / 256;
        if (x >= 0 && x < (int)w && y >= 0 && y < (int)h)
            buf[(unsigned)y * w + (unsigned)x] = c;
    }
    /* Bottom-left */
    for (i = 0; i <= steps; i++) {
        int a = 0 + i * 64 / steps;            /* Q4→Q1 arc */
        int x = margin + r + (r * blit_sin8((a + 64) & 0xFF)) / 256;
        int y = (int)h - margin - r + (r * blit_sin8(a & 0xFF)) / 256;
        if (x >= 0 && x < (int)w && y >= 0 && y < (int)h)
            buf[(unsigned)y * w + (unsigned)x] = c;
    }
    /* Bottom-right */
    for (i = 0; i <= steps; i++) {
        int a = 64 + i * 64 / steps;
        int x = (int)w - margin - r + (r * blit_sin8((a + 64) & 0xFF)) / 256;
        int y = (int)h - margin - r + (r * blit_sin8(a & 0xFF)) / 256;
        if (x >= 0 && x < (int)w && y >= 0 && y < (int)h)
            buf[(unsigned)y * w + (unsigned)x] = c;
    }
}

/* Penalty arc (D shape): semi-circle outside the penalty box. */
static void wc_draw_penalty_arc(uint16_t *buf, unsigned int w, unsigned int h,
                                int left)
{
    int r = (int)w / 10;
    int cx = left ? (int)w * 4 / 5 : (int)w / 5;
    int cy = (int)h / 2;
    int steps = r * 2;
    int i;
    uint16_t c = wc_c_line();

    if (r < 3 || steps < 1)
        return;

    for (i = 0; i <= steps; i++) {
        int a = left ? (192 + i * 128 / steps) : (64 - i * 128 / steps);
        int x = cx + (r * blit_sin8((a + 64) & 0xFF)) / 256;
        int y = cy + (r * blit_sin8(a & 0xFF)) / 256;
        if (x >= 0 && x < (int)w && y >= 0 && y < (int)h)
            buf[(unsigned)y * w + (unsigned)x] = c;
    }
    /* Penalty spot */
    blit_fill_circle(buf, w, h, cx, cy, 2, wc_c_white());
}

/* Enhanced goal with visible net (posts + crossbar + grid lines). */
static void wc_draw_goal_enhanced(uint16_t *buf, unsigned int w, unsigned int h,
                                  int left)
{
    int gh = (int)h / 4;
    int gy = ((int)h - gh) / 2;
    int gw = (int)w / 12;
    int gx = left ? (int)w / 6 : (int)w - (int)w / 6 - gw;
    uint16_t post = wc_c_white();
    uint16_t net = blit_rgb565(180, 190, 195);
    int i;

    /* Posts + crossbar */
    blit_fill_rect(buf, w, h, gx, gy, 2, gh, post);
    blit_fill_rect(buf, w, h, gx + gw - 2, gy, 2, gh, post);
    blit_fill_rect(buf, w, h, gx, gy, gw, 2, post);

    /* Net: horizontal + vertical grid inside the goal mouth */
    for (i = 1; i < 6; i++)
        blit_fill_rect(buf, w, h, gx + 3, gy + i * gh / 6, gw - 6, 1, net);
    for (i = 1; i < 4; i++)
        blit_fill_rect(buf, w, h, gx + i * gw / 4, gy + 2, 1, gh - 2, net);
}

/* Compact, finer cartoon player (scaled for round panels). */
static void wc_draw_player(uint16_t *buf, unsigned int w, unsigned int h,
                           int cx, int cy, const struct wc_player_snap *p)
{
    int blue = (p->side == 0);
    int dir = p->facing_right ? 1 : -1;
    int pose = p->pose;
    int gait = p->run & 7;
    int leg_a = 0, leg_b = 0;
    int arm_a = 0, arm_b = 0;
    int body_lean = 0;
    int hop = 0;
    uint16_t jersey = blue ? wc_c_blue() : wc_c_orange();
    uint16_t jersey_dk = blue ? wc_c_blue_dk() : wc_c_orange_dk();
    uint16_t jersey_hi = blue ? wc_c_blue_hi() : wc_c_orange_hi();
    uint16_t hair = wc_hair_color(p->hair);
    int hx, hy;

    if (pose == WC_POSE_WINDUP) {
        leg_a = -6;
        leg_b = 4;
        body_lean = -dir * 2;
        arm_a = 5;
        arm_b = -4;
    } else if (pose == WC_POSE_KICK) {
        leg_a = 10;
        leg_b = -3;
        body_lean = dir * 3;
        arm_a = -3;
        arm_b = 6;
    } else if (pose == WC_POSE_CHEER) {
        hop = -4;
        arm_a = -9;
        arm_b = -9;
    } else {
        int swing = ((gait < 4) ? (int)gait : (7 - (int)gait)) - 2;

        leg_a = swing;
        leg_b = -swing;
        arm_a = -swing;
        arm_b = swing;
        hop = 0;  /* no vertical bobbing — AI y-movement is enough */
    }

    /* Only apply hop for non-run poses (cheer still hops) */
    if (pose != WC_POSE_RUN)
        cy += hop;
    hx = cx + body_lean;
    hy = cy - 12;

    blit_fill_ellipse(buf, w, h, cx, cy + 18 - hop, 9, 2, blit_rgb565(22, 70, 32));

    blit_fill_ellipse(buf, w, h, cx - dir * (3 - leg_b / 2), cy + 10, 2, 7, wc_c_short());
    blit_fill_rect(buf, w, h, cx - dir * (4 - leg_b / 2) - 1, cy + 15, 3, 4, wc_c_sock());
    blit_fill_circle(buf, w, h, cx - dir * (5 - leg_b), cy + 19, 2, wc_c_black());

    blit_fill_ellipse(buf, w, h, hx, cy + 1, 8, 9, jersey);
    blit_fill_ellipse(buf, w, h, hx + dir, cy - 1, 4, 5, jersey_hi);
    blit_fill_ellipse(buf, w, h, hx - dir, cy + 4, 5, 4, jersey_dk);
    blit_draw_digit(buf, w, h, hx - 1, cy - 1, p->num % 10, wc_c_white());

    blit_fill_ellipse(buf, w, h, cx + dir * (2 + leg_a / 2), cy + 9, 2, 7, wc_c_short());
    blit_fill_rect(buf, w, h, cx + dir * (3 + leg_a / 2) - 1, cy + 14, 3, 4, wc_c_sock());
    blit_fill_circle(buf, w, h, cx + dir * (4 + leg_a), cy + 19, 2,
                     (pose == WC_POSE_KICK) ? blit_rgb565(255, 90, 70) : wc_c_black());

    blit_fill_ellipse(buf, w, h, hx - dir * 7, cy + arm_a / 2, 2, 5, wc_c_skin());
    blit_fill_ellipse(buf, w, h, hx + dir * 6, cy + arm_b / 2, 2, 5, wc_c_skin());
    blit_fill_circle(buf, w, h, hx - dir * 7, cy + 5 + arm_a / 2, 2, wc_c_skin_sh());
    blit_fill_circle(buf, w, h, hx + dir * 6, cy + 4 + arm_b / 2, 2, wc_c_skin_sh());

    blit_fill_circle(buf, w, h, hx, hy, 8, wc_c_skin());
    blit_fill_circle(buf, w, h, hx + dir, hy + 1, 5, wc_c_skin_sh());
    blit_fill_ellipse(buf, w, h, hx, hy - 5, 7, 4, hair);

    blit_fill_circle(buf, w, h, hx - 2, hy - 1, 2, wc_c_white());
    blit_fill_circle(buf, w, h, hx + 2, hy - 1, 2, wc_c_white());
    blit_fill_circle(buf, w, h, hx - 2 + dir, hy - 1, 1, wc_c_black());
    blit_fill_circle(buf, w, h, hx + 2 + dir, hy - 1, 1, wc_c_black());

    if (pose == WC_POSE_KICK) {
        int fx = cx + dir * 12;
        int fy = cy + 18;

        blit_fill_circle(buf, w, h, fx, fy, 3, blit_rgb565(255, 245, 140));
        blit_fill_circle(buf, w, h, fx + dir * 3, fy - 1, 2, jersey);
    }

    (void)h;
}

static void wc_draw_ball(uint16_t *buf, unsigned int w, unsigned int h,
                         int cx, int cy, int trail_dir, int orange_trail)
{
    uint16_t trail = orange_trail ? wc_c_orange() : wc_c_blue();
    int i;

    for (i = 2; i >= 1; i--) {
        int tx = cx - trail_dir * i * 5;

        blit_fill_circle(buf, w, h, tx, cy, 2 + (2 - i), trail);
    }

    blit_fill_circle(buf, w, h, cx, cy, 5, wc_c_white());
    blit_fill_circle(buf, w, h, cx - 2, cy - 1, 1, wc_c_black());
    blit_fill_circle(buf, w, h, cx + 2, cy, 1, wc_c_black());
    blit_fill_circle(buf, w, h, cx, cy + 2, 1, wc_c_black());
    blit_draw_circle(buf, w, h, cx, cy, 5, blit_rgb565(170, 170, 175));
}

static void wc_draw_cheer_overlay(uint16_t *buf, unsigned int w, unsigned int h,
                                  uint8_t cheer)
{
    int i;
    uint16_t cols[4];
    int x0 = (int)w / 4;
    int y0 = (int)h / 4;
    int rw = (int)w / 2;
    int rh = (int)h / 2;

    if (!cheer || rw <= 0 || rh <= 0)
        return;

    cols[0] = wc_c_blue();
    cols[1] = wc_c_orange();
    cols[2] = wc_c_white();
    cols[3] = blit_rgb565(255, 220, 60);

    /* Confetti kept inside the circular safe disc (halved count). */
    for (i = 0; i < 5; i++) {
        int x = x0 + (int)((blit_rand() + i * 37) % (unsigned)rw);
        int y = y0 + (int)((blit_rand() + i * 19) % (unsigned)rh);

        blit_fill_rect(buf, w, h, x, y, 2, 5 + (i & 2), cols[i & 3]);
    }
}

static void wc_draw_ad_board(uint16_t *buf, unsigned int w, unsigned int h,
                             int y, uint32_t tick, int mirror)
{
    static const uint8_t ar[6] = {255, 43, 255, 40, 255, 80};
    static const uint8_t ag[6] = {80, 140, 200, 40, 138, 80};
    static const uint8_t ab[6] = {80, 255, 60, 40, 43, 200};
    int seg = 28;
    int off = (int)((tick / 4) % (uint32_t)seg);
    int x_margin = (int)w / 5;
    int usable = (int)w - 2 * x_margin;
    int i;

    if (mirror)
        off = seg - 1 - off;
    if (usable < 20)
        return;

    blit_fill_rect(buf, w, h, x_margin, y, usable, 6, blit_rgb565(18, 18, 24));
    for (i = -1; i < usable / seg + 2; i++) {
        int x = x_margin + i * seg - off;
        uint16_t c = blit_rgb565(ar[((i % 6) + 6) % 6],
                                 ag[((i % 6) + 6) % 6],
                                 ab[((i % 6) + 6) % 6]);
        int x0 = x < x_margin ? x_margin : x;
        int x1 = x + seg;

        if (x1 > x_margin + usable)
            x1 = x_margin + usable;
        if (x0 >= x1)
            continue;
        blit_fill_rect(buf, w, h, x0, y + 1, x1 - x0, 4, c);
    }
    (void)h;
}

static void wc_draw_center_mark(uint16_t *buf, unsigned int w, unsigned int h,
                                int highlight)
{
    int cx = (int)w / 2;
    int cy = (int)h / 2;
    int r = (int)((w < h ? w : h) / 5);
    uint16_t ring = highlight ? blit_rgb565(255, 240, 120) : wc_c_line();
    int i;

    blit_fill_rect(buf, w, h, cx, (int)h / 5, 1, (int)h * 3 / 5, wc_c_line());
    for (i = 0; i < (highlight ? 2 : 1); i++)
        blit_draw_circle(buf, w, h, cx, cy, r - i, ring);
    blit_fill_circle(buf, w, h, cx, cy, highlight ? 3 : 2,
                     highlight ? blit_rgb565(255, 220, 80) : wc_c_white());
    blit_fill_ellipse(buf, w, h, cx - r / 2, cy, 4, 2, wc_c_blue());
    blit_fill_ellipse(buf, w, h, cx + r / 2, cy, 4, 2, wc_c_orange());
}

static void wc_draw_bench(uint16_t *buf, unsigned int w, unsigned int h)
{
    /* Inset for round bezel. */
    int y = (int)h * 3 / 5;
    int x = (int)w / 3;

    blit_fill_rect(buf, w, h, x, y, (int)w / 3, 3, blit_rgb565(70, 55, 40));
    blit_fill_rect(buf, w, h, x, y - 5, (int)w / 3, 2, blit_rgb565(95, 75, 55));
    blit_fill_circle(buf, w, h, x + 8, y - 9, 2, wc_c_orange());
    blit_fill_circle(buf, w, h, x + 20, y - 9, 2, wc_c_blue());
}


static void wc_draw_fps(uint16_t *buf, unsigned int w, unsigned int h, int spi_id)
{
    uint32_t fps = lv_spi_get_fps(spi_id);
    char str[8];
    int len, i, row, col;
    int scale, x0, y0;
    int digit_w, digit_h, total_w, total_h;

    len = snprintf(str, sizeof(str), "%lu", (unsigned long)fps);
    if (len <= 0 || len > 5)
        return;

    /*
     * Scale digits proportional to panel size so they remain readable on
     * small round LCDs (~240×240).  Each logical pixel of the 3×5 glyph
     * becomes a scale×scale block.
     */
    scale = (int)w / 60;          /* ~4 on 240 px panel */
    if (scale < 2)
        scale = 2;

    digit_w = 3 * scale;
    digit_h = 5 * scale;
    total_w = len * digit_w + (len - 1) * scale; /* inter-digit gap */
    total_h = digit_h;

    /* Bottom-right, inside circular safe area (avoid bezel occlusion). */
    x0 = (int)w * 3 / 4 - total_w / 2;
    y0 = (int)h * 3 / 4 - total_h / 2;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;

    /* Dark backing card for contrast against pitch. */
    blit_fill_rect(buf, w, h, x0 - scale, y0 - scale,
                   total_w + 2 * scale, total_h + 2 * scale,
                   blit_rgb565(0, 0, 0));

    /* Draw each digit, scaling its 3×5 bitmap. */
    for (i = 0; i < len; i++) {
        int d = str[i] - '0';
        int dx = x0 + i * (digit_w + scale);

        if (d < 0 || d > 9)
            continue;

        for (row = 0; row < 5; row++) {
            for (col = 0; col < 3; col++) {
                if ((wc_digit3x5[d][row] >> (2 - col)) & 1) {
                    blit_fill_rect(buf, w, h,
                                   dx + col * scale, y0 + row * scale,
                                   scale, scale,
                                   blit_rgb565(255, 220, 60));
                }
            }
        }
    }
}

static void wc_draw_camera(uint16_t *buf, unsigned int w, unsigned int h)
{
    int x = (int)w * 2 / 3;
    int y = (int)h / 3;

    blit_fill_rect(buf, w, h, x + 4, y + 6, 2, 10, blit_rgb565(50, 50, 55));
    blit_fill_rect(buf, w, h, x, y + 14, 10, 2, blit_rgb565(40, 40, 45));
    blit_fill_rect(buf, w, h, x, y, 8, 6, blit_rgb565(30, 30, 35));
    blit_fill_circle(buf, w, h, x + 4, y + 3, 2, blit_rgb565(80, 180, 255));
}

/*
 * Crowd spectators — coloured dots along top and bottom edges,
 * inside the circular safe area.
 */
static void wc_draw_crowd(uint16_t *buf, unsigned int w, unsigned int h,
                          uint32_t seed)
{
    int n = 6;
    int r = (int)w / 60;    /* ~4 on 240 px — visible */
    int i;

    if (r < 2)
        r = 2;

    for (i = 0; i < n; i++) {
        int x = (int)w / 5 + (i * (int)w * 3 / 5) / (n - 1);
        int y_top = (int)h / 7;
        int y_bot = (int)h * 6 / 7;
        uint16_t col;
        /* Use only static seed (spi_id) so colours don't flash. */
        int ci = (int)((seed + (uint32_t)i * 7) & 7);

        switch (ci) {
        case 0: col = wc_c_blue();      break;
        case 1: col = wc_c_orange();    break;
        case 2: col = wc_c_white();     break;
        case 3: col = blit_rgb565(255, 220, 60); break;
        case 4: col = blit_rgb565(255, 90, 90);  break;
        case 5: col = blit_rgb565(90, 200, 90);  break;
        default: col = blit_rgb565(180, 180, 200); break;
        }
        blit_fill_circle(buf, w, h, x, y_top, r, col);
        blit_fill_circle(buf, w, h, x + (i & 1 ? r * 2 : -r * 2),
                         y_bot, r, col);
    }
}

/*
 * Goalkeeper — bigger than outfield players, with gloves and cap.
 * Drawn at screen coords (cx, cy); pose affects arm/leg positions.
 */
static void wc_draw_keeper(uint16_t *buf, unsigned int w, unsigned int h,
                           int cx, int cy, const struct wc_player_snap *p)
{
    int blue = (p->side == 0);
    int pose = p->pose;
    int scale = (int)w / 120;
    int arm_spread, dive_lean;

    if (scale < 1)
        scale = 1;

    if (pose == WC_POSE_WINDUP) {
        /* Diving save — stretch arms wide toward ball */
        arm_spread = 12 + scale;
        dive_lean = blue ? -5 : 5;
    } else if (pose == WC_POSE_CHEER) {
        arm_spread = 14;
        dive_lean = 0;
    } else {
        arm_spread = 6 + scale;
        dive_lean = 0;
    }

    cx += dive_lean;

    /* Shadow */
    blit_fill_ellipse(buf, w, h, cx, cy + 14, 10, 2, blit_rgb565(22, 70, 32));
    /* Legs */
    blit_fill_ellipse(buf, w, h, cx - 3, cy + 7, 2, 6, blit_rgb565(20, 20, 28));
    blit_fill_ellipse(buf, w, h, cx + 3, cy + 7, 2, 6, blit_rgb565(20, 20, 28));
    /* Body — keeper jersey (green for blue side, yellow for orange) */
    blit_fill_ellipse(buf, w, h, cx, cy - 1, 9, 11,
                      blue ? blit_rgb565(60, 180, 80) : blit_rgb565(200, 180, 40));
    /* Arms stretched sideways */
    blit_fill_ellipse(buf, w, h, cx - arm_spread, cy - 2, 3, 6, wc_c_skin());
    blit_fill_ellipse(buf, w, h, cx + arm_spread, cy - 2, 3, 6, wc_c_skin());
    /* Gloves */
    blit_fill_circle(buf, w, h, cx - arm_spread - 1, cy - 3, 3 + scale,
                     blit_rgb565(40, 40, 45));
    blit_fill_circle(buf, w, h, cx + arm_spread + 1, cy - 3, 3 + scale,
                     blit_rgb565(40, 40, 45));
    /* Head */
    blit_fill_circle(buf, w, h, cx, cy - 14, 9, wc_c_skin());
    /* Hair */
    blit_fill_ellipse(buf, w, h, cx, cy - 19, 7, 4,
                      wc_hair_color(p->hair));
    /* Cap visor */
    blit_fill_rect(buf, w, h, cx - 5, cy - 20, 10, 2,
                   blit_rgb565(30, 30, 35));
}

static void wc_draw_corridor_decor(uint16_t *buf, unsigned int w, unsigned int h,
                                  int spi_id, const struct wc_snapshot *s,
                                  uint32_t tick)
{
    int world0 = wc_spi_world0(spi_id);
    int world1 = world0 + WC_SEG;
    int ball_here = (s->ball_x >= world0 && s->ball_x < world1);
    int mid_y = (int)h / 2;

    /* Crowd across all corridor panels */
    wc_draw_crowd(buf, w, h, (uint32_t)spi_id);

    if (spi_id == 3) {
        wc_draw_center_mark(buf, w, h, ball_here);
    } else {
        blit_fill_rect(buf, w, h, (int)w / 5, mid_y, (int)w * 3 / 5, 1,
                       blit_rgb565(200, 230, 200));
    }

    /* Ad boards pushed to the very edges of the panel */
    wc_draw_ad_board(buf, w, h, (int)h / 8 - 2, tick, spi_id == 4);
    wc_draw_ad_board(buf, w, h, (int)h * 7 / 8, tick + 20, spi_id == 2);

    if (spi_id == 2) {
        blit_fill_rect(buf, w, h, (int)w / 4, mid_y - 18, 2, 12, wc_c_blue());
        blit_fill_rect(buf, w, h, (int)w / 4 + 2, mid_y - 18, 7, 4, wc_c_white());
    } else if (spi_id == 4) {
        wc_draw_bench(buf, w, h);
        wc_draw_camera(buf, w, h);
        blit_fill_rect(buf, w, h, (int)w * 3 / 4, mid_y - 18, 2, 12, wc_c_orange());
        blit_fill_rect(buf, w, h, (int)w * 3 / 4 - 7, mid_y - 18, 7, 4, wc_c_white());
    }

    /* Ball indicator ring when ball is on this panel */
    if (ball_here &&
        (s->phase == WC_PHASE_FLY_R || s->phase == WC_PHASE_FLY_L ||
         s->phase == WC_PHASE_ATTACK_R || s->phase == WC_PHASE_ATTACK_L)) {
        int local = (int)s->ball_x - world0;
        int cx = local * (int)w / WC_SEG;
        int cy = (int)s->ball_y * (int)h / 256;

        blit_draw_circle(buf, w, h, cx, cy, 8, blit_rgb565(255, 255, 190));
    }
}

static int wc_player_depth(const struct wc_player_snap *p)
{
    return (int)p->y;
}

static void wc_draw_actors(uint16_t *buf, unsigned int w, unsigned int h,
                           int spi_id, const struct wc_snapshot *s)
{
    int world0 = wc_spi_world0(spi_id);
    int world1 = world0 + WC_SEG;
    int margin = 40; /* allow partial sprites at panel seams */
    int order[WC_NPLAYERS];
    int n = 0;
    int i, j;

    for (i = 0; i < (int)s->nplayers; i++) {
        int wx = s->players[i].wx;

        if (wx >= world0 - margin && wx < world1 + margin)
            order[n++] = i;
    }

    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (wc_player_depth(&s->players[order[j]]) <
                wc_player_depth(&s->players[order[i]])) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    for (i = 0; i < n; i++) {
        const struct wc_player_snap *p = &s->players[order[i]];
        int local = (int)p->wx - world0;
        int cx = local * (int)w / WC_SEG;
        int cy = (int)p->y * (int)h / 256;

        if (cy < 40)
            cy = 40;
        if (cy > (int)h - 36)
            cy = (int)h - 36;
        if (cx > -30 && cx < (int)w + 30)
            wc_draw_player(buf, w, h, cx, cy, p);
    }

    if (s->ball_x >= world0 - 20 && s->ball_x < world1 + 20) {
        int local = (int)s->ball_x - world0;
        int cx = local * (int)w / WC_SEG;
        int cy = (int)s->ball_y * (int)h / 256;

        if (cy < 20)
            cy = 20;
        if (cy > (int)h - 20)
            cy = (int)h - 20;
        if (cx > -20 && cx < (int)w + 20)
            wc_draw_ball(buf, w, h, cx, cy, s->ball_dir, s->trail_hue);
    }
}

void wc_fill_panel(uint16_t *buf, unsigned int w, unsigned int h, int spi_id)
{
    struct wc_snapshot s;
    uint32_t tick = lv_tick_get() / 16U;

    wc_get_snapshot(&s);

    /*
     * Pitch never changes — render once into a static cache, then memcpy
     * every subsequent frame (avoids per-pixel grass-loop on every frame).
     */
    blit_paint_static_bg(buf, &g_pitch_bg[spi_id], w, h, wc_fill_pitch);

    if (spi_id == 0) {
        /* Left goal panel: goal, penalty box, arc, corner arcs, scoreboard */
        wc_draw_goal_enhanced(buf, w, h, 1);
        blit_fill_rect(buf, w, h, (int)w * 4 / 5, (int)h / 5, 2,
                       (int)h * 3 / 5, wc_c_line());
        wc_draw_penalty_arc(buf, w, h, 1);
        wc_draw_corner_arcs(buf, w, h);
        wc_draw_score(buf, w, h, s.score_l, s.score_r, 1);
    } else if (spi_id == 1) {
        /* Right goal panel: goal, penalty box, arc, corner arcs, scoreboard */
        wc_draw_goal_enhanced(buf, w, h, 0);
        blit_fill_rect(buf, w, h, (int)w / 5, (int)h / 5, 2,
                       (int)h * 3 / 5, wc_c_line());
        wc_draw_penalty_arc(buf, w, h, 0);
        wc_draw_corner_arcs(buf, w, h);
        wc_draw_score(buf, w, h, s.score_l, s.score_r, 0);
    } else if (spi_id >= 2 && spi_id <= 4) {
        wc_draw_corridor_decor(buf, w, h, spi_id, &s, tick);
    }

    wc_draw_actors(buf, w, h, spi_id, &s);

    /* Draw goalkeeper from world coords (each GK visible on one panel only) */
    {
        int world0 = wc_spi_world0(spi_id);
        int i;

        for (i = 0; i < 2; i++) {
            int wx = s.gk[i].wx;
            int local, cx, cy;

            if (wx < world0 - 30 || wx >= world0 + WC_SEG + 30)
                continue;

            local = wx - world0;
            cx = local * (int)w / WC_SEG;
            cy = (int)s.gk[i].y * (int)h / 256;

            if (cy < 40)
                cy = 40;
            if (cy > (int)h - 36)
                cy = (int)h - 36;
            wc_draw_keeper(buf, w, h, cx, cy, &s.gk[i]);
        }
    }

    wc_draw_cheer_overlay(buf, w, h, s.cheer);

    wc_draw_fps(buf, w, h, spi_id);
}
