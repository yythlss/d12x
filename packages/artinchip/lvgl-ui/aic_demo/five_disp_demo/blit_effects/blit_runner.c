/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cross-panel sky flyer: a colorful plane loops across SPI3|SPI4|SPI5.
 * Background, rings, balloons and gems scroll continuously for demo play.
 */

#include "blit_common.h"

#define RW_PW        256
#define RW_WORLD     (RW_PW * 3)
#define RW_LANES     3
#define RW_NOBST     12
#define RW_NGEM      14
#define RW_NCLOUD    8

enum {
    OBS_RING    = 0,  /* fly-through hoop */
    OBS_BALLOON = 1,  /* dodge balloon cluster */
    OBS_CRYSTAL = 2,  /* tall crystal pillar */
};

struct rw_obst {
    int32_t  x;
    uint8_t  lane;
    uint8_t  kind;
    uint8_t  alive;
    uint8_t  w;
    uint8_t  hue;     /* color variant 0..5 */
};

struct rw_gem {
    int32_t  x;
    uint8_t  lane;
    uint8_t  alive;
    uint8_t  hue;
};

struct rw_cloud {
    int32_t  x;
    uint8_t  y_frac;  /* 0..255 vertical placement in sky */
    uint8_t  scale;
    uint8_t  hue;
};

struct rw_game {
    uint32_t scroll;
    uint32_t dist;
    uint16_t score;
    int32_t  plane_x;     /* world X — plane flies across all panels */
    uint8_t  lane;        /* target lane */
    int16_t  lane_pos;    /* 8.8 fixed: smooth Y between lanes */
    uint8_t  bob;         /* gentle bob timer */
    uint8_t  hit_flash;
    uint8_t  seam_flash;
    uint8_t  speed;
    uint8_t  ready;
    uint32_t step;
    int32_t  next_spawn;
    struct rw_obst  obst[RW_NOBST];
    struct rw_gem   gem[RW_NGEM];
    struct rw_cloud cloud[RW_NCLOUD];
};

static struct rw_game g_rw;

static const uint8_t g_pal_r[6] = {255, 255, 80,  60, 200, 255};
static const uint8_t g_pal_g[6] = {80,  200, 220, 120, 80,  60};
static const uint8_t g_pal_b[6] = {120, 60,  255, 255, 255, 200};

static uint16_t rw_pal(int hue, int bright)
{
    int h = hue % 6;

    if (h < 0)
        h = 0;
    return blit_rgb565(blit_clamp_u8((int)g_pal_r[h] * bright / 255),
                       blit_clamp_u8((int)g_pal_g[h] * bright / 255),
                       blit_clamp_u8((int)g_pal_b[h] * bright / 255));
}

static int rw_lane_y(unsigned int h, int lane)
{
    int top = (int)h / 5;
    int bot = (int)h * 4 / 5;
    int span = bot - top;

    return top + (lane + 1) * span / (RW_LANES + 1);
}

/* Interpolate plane Y from smoothed lane_pos (8.8 fixed). */
static int rw_plane_y(unsigned int h)
{
    int y0 = rw_lane_y(h, 0);
    int y1 = rw_lane_y(h, RW_LANES - 1);
    int span = (RW_LANES - 1) * 256;

    if (span <= 0)
        return y0;
    return y0 + (y1 - y0) * (int)g_rw.lane_pos / span;
}

static void rw_init_clouds(void)
{
    int i;

    for (i = 0; i < RW_NCLOUD; i++) {
        g_rw.cloud[i].x = (int32_t)(blit_rand() % RW_WORLD);
        g_rw.cloud[i].y_frac = (uint8_t)(20 + blit_rand() % 100);
        g_rw.cloud[i].scale = (uint8_t)(10 + blit_rand() % 18);
        g_rw.cloud[i].hue = (uint8_t)(blit_rand() % 6);
    }
}

static void rw_reset(void)
{
    int i;

    memset(&g_rw, 0, sizeof(g_rw));
    g_rw.lane = 1;
    g_rw.lane_pos = (int16_t)(1 * 256);
    g_rw.speed = 5;
    g_rw.plane_x = RW_PW / 2;          /* start on SPI3 */
    g_rw.next_spawn = RW_WORLD + 60;
    g_rw.ready = 1;

    for (i = 0; i < RW_NOBST; i++)
        g_rw.obst[i].alive = 0;
    for (i = 0; i < RW_NGEM; i++)
        g_rw.gem[i].alive = 0;
    rw_init_clouds();
}

static void rw_spawn_obst(int world_x)
{
    int i;

    for (i = 0; i < RW_NOBST; i++) {
        if (g_rw.obst[i].alive)
            continue;
        g_rw.obst[i].alive = 1;
        g_rw.obst[i].x = world_x;
        g_rw.obst[i].lane = (uint8_t)(blit_rand() % RW_LANES);
        g_rw.obst[i].kind = (uint8_t)(blit_rand() % 3);
        g_rw.obst[i].hue = (uint8_t)(blit_rand() % 6);
        g_rw.obst[i].w = (g_rw.obst[i].kind == OBS_CRYSTAL) ? 24 :
                         (g_rw.obst[i].kind == OBS_BALLOON) ? 36 : 40;
        return;
    }
}

static void rw_spawn_gems(int world_x, int lane)
{
    int i, n = 0;

    for (i = 0; i < RW_NGEM && n < 5; i++) {
        if (g_rw.gem[i].alive)
            continue;
        g_rw.gem[i].alive = 1;
        g_rw.gem[i].x = world_x + n * 26;
        g_rw.gem[i].lane = (uint8_t)lane;
        g_rw.gem[i].hue = (uint8_t)((lane + n) % 6);
        n++;
    }
}

static void rw_ai_lane(void)
{
    int blocked[RW_LANES] = {0, 0, 0};
    int i, best, best_d;
    int hx = g_rw.plane_x;

    for (i = 0; i < RW_NOBST; i++) {
        int dx;

        if (!g_rw.obst[i].alive)
            continue;
        /* Rings are good — fly through, don't dodge */
        if (g_rw.obst[i].kind == OBS_RING)
            continue;
        dx = g_rw.obst[i].x - hx;
        if (dx > 10 && dx < 180)
            blocked[g_rw.obst[i].lane] = 1;
    }

    if (!blocked[g_rw.lane])
        return;

    best = g_rw.lane;
    best_d = 99;
    for (i = 0; i < RW_LANES; i++) {
        int d = i - (int)g_rw.lane;

        if (d < 0)
            d = -d;
        if (!blocked[i] && d < best_d) {
            best_d = d;
            best = i;
        }
    }
    g_rw.lane = (uint8_t)best;
}

static void rw_smooth_lane(void)
{
    int target = (int)g_rw.lane * 256;
    int d = target - (int)g_rw.lane_pos;
    /* Cap step so lane changes ease in over ~20 frames instead of snapping. */
    const int step = 12;

    if (d > step)
        g_rw.lane_pos = (int16_t)(g_rw.lane_pos + step);
    else if (d < -step)
        g_rw.lane_pos = (int16_t)(g_rw.lane_pos - step);
    else
        g_rw.lane_pos = (int16_t)target;
}

static void rw_step_once(void)
{
    int i;
    int plane_panel, plane_panel_prev;
    int prev_x;

    if (!g_rw.ready)
        rw_reset();

    g_rw.scroll += g_rw.speed;
    g_rw.dist += g_rw.speed;
    if ((g_rw.dist / 500) + 5 < 12)
        g_rw.speed = (uint8_t)(5 + g_rw.dist / 500);
    else
        g_rw.speed = 12;

    /* Plane flies forward a bit faster than scroll → crosses panels */
    prev_x = g_rw.plane_x;
    g_rw.plane_x += g_rw.speed + 2;
    if (g_rw.plane_x >= (int32_t)g_rw.scroll + RW_WORLD)
        g_rw.plane_x = (int32_t)g_rw.scroll - 20;  /* wrap to left, loop */
    if (g_rw.plane_x < (int32_t)g_rw.scroll - 40)
        g_rw.plane_x = (int32_t)g_rw.scroll + RW_WORLD - 40;

    plane_panel = ((g_rw.plane_x - (int)g_rw.scroll) / RW_PW + 3) % 3;
    plane_panel_prev = ((prev_x - (int)g_rw.scroll) / RW_PW + 3) % 3;
    if (plane_panel != plane_panel_prev)
        g_rw.seam_flash = 18;

    g_rw.bob = (uint8_t)((g_rw.bob + 1) & 0xFF);
    if (g_rw.hit_flash)
        g_rw.hit_flash--;
    if (g_rw.seam_flash)
        g_rw.seam_flash--;

    rw_ai_lane();
    rw_smooth_lane();

    if ((int)g_rw.scroll + RW_WORLD + 40 > g_rw.next_spawn) {
        int gap = 100 + (int)(blit_rand() % 100);

        rw_spawn_obst(g_rw.next_spawn);
        if ((blit_rand() & 1) == 0)
            rw_spawn_gems(g_rw.next_spawn + 30,
                          (int)(blit_rand() % RW_LANES));
        g_rw.next_spawn += gap;
        if (g_rw.next_spawn < (int32_t)g_rw.scroll + RW_WORLD)
            g_rw.next_spawn = (int32_t)g_rw.scroll + RW_WORLD + 80;
    }

    /* Drift clouds slowly */
    for (i = 0; i < RW_NCLOUD; i++) {
        g_rw.cloud[i].x -= g_rw.speed / 2;
        if (g_rw.cloud[i].x < (int32_t)g_rw.scroll - 80)
            g_rw.cloud[i].x = (int32_t)g_rw.scroll + RW_WORLD +
                              (int)(blit_rand() % 100);
    }

    for (i = 0; i < RW_NOBST; i++) {
        int vis, panel_now, panel_prev;

        if (!g_rw.obst[i].alive)
            continue;
        vis = g_rw.obst[i].x - (int)g_rw.scroll;
        if (vis + g_rw.obst[i].w < -30) {
            g_rw.obst[i].alive = 0;
            continue;
        }
        panel_now = vis / RW_PW;
        panel_prev = (vis + g_rw.speed) / RW_PW;
        if (panel_now != panel_prev && panel_now >= 0 && panel_now < 3)
            g_rw.seam_flash = 12;

        if (g_rw.obst[i].lane == g_rw.lane) {
            int dx = g_rw.obst[i].x - g_rw.plane_x;

            if (dx > -g_rw.obst[i].w / 2 && dx < 20) {
                if (g_rw.obst[i].kind == OBS_RING) {
                    if (g_rw.score < 999)
                        g_rw.score += 5;
                } else if (g_rw.hit_flash == 0) {
                    g_rw.hit_flash = 16;
                    if (g_rw.score > 3)
                        g_rw.score -= 3;
                }
            }
        }
    }

    for (i = 0; i < RW_NGEM; i++) {
        int vis;

        if (!g_rw.gem[i].alive)
            continue;
        vis = g_rw.gem[i].x - (int)g_rw.scroll;
        if (vis < -20) {
            g_rw.gem[i].alive = 0;
            continue;
        }
        if (g_rw.gem[i].lane == g_rw.lane) {
            int dx = g_rw.gem[i].x - g_rw.plane_x;

            if (dx > -14 && dx < 16) {
                g_rw.gem[i].alive = 0;
                if (g_rw.score < 999)
                    g_rw.score += 2;
            }
        }
    }

    if ((g_rw.dist & 31u) == 0u && g_rw.score < 999)
        g_rw.score++;
}

void blit_runner_step(uint32_t t)
{
    if (!g_rw.ready)
        rw_reset();

    if (t == g_rw.step)
        return;
    if (t > g_rw.step || g_rw.step - t > 1000u) {
        g_rw.step = t;
        rw_step_once();
    }
}

static int rw_world_to_px(int world_x, int panel, unsigned int w, int *px)
{
    int vis = world_x - (int)g_rw.scroll;
    int local = vis - panel * RW_PW;

    if (local < -50 || local > RW_PW + 50)
        return 0;
    *px = local * (int)w / RW_PW;
    return 1;
}

static void rw_draw_sky(uint16_t *buf, unsigned int w, unsigned int h,
                        int panel, uint32_t t)
{
    unsigned int x, y;
    int horizon = (int)h * 2 / 3;
    /* Per-panel sky tint so the strip feels like a continuous sunset→night */
    static const uint8_t top_r[3] = {40,  90,  20};
    static const uint8_t top_g[3] = {20,  40,  30};
    static const uint8_t top_b[3] = {120, 160, 90};
    static const uint8_t mid_r[3] = {255, 255, 80};
    static const uint8_t mid_g[3] = {120, 80,  200};
    static const uint8_t mid_b[3] = {60,  140, 255};
    static const uint8_t bot_r[3] = {40,  60,  30};
    static const uint8_t bot_g[3] = {160, 100, 180};
    static const uint8_t bot_b[3] = {120, 180, 100};
    int p = panel;

    for (y = 0; y < h; y++) {
        uint16_t *row = &buf[y * w];
        uint8_t r, g, b;
        int ty;

        if ((int)y < horizon) {
            ty = horizon > 1 ? (int)y * 255 / (horizon - 1) : 0;
            r = blit_lerp8(top_r[p], mid_r[p], ty);
            g = blit_lerp8(top_g[p], mid_g[p], ty);
            b = blit_lerp8(top_b[p], mid_b[p], ty);
        } else {
            ty = (int)h > horizon + 1 ?
                 ((int)y - horizon) * 255 / ((int)h - horizon - 1) : 0;
            r = blit_lerp8(mid_r[p], bot_r[p], ty);
            g = blit_lerp8(mid_g[p], bot_g[p], ty);
            b = blit_lerp8(mid_b[p], bot_b[p], ty);
        }

        for (x = 0; x < w; x++) {
            /* Twinkling stars in upper sky */
            if ((int)y < horizon / 2 &&
                (((x * 17 + y * 31 + (int)(t / 4)) & 63) == 0)) {
                row[x] = blit_rgb565(255, 255, 220);
            } else {
                row[x] = blit_rgb565_dither(r, g, b, (int)x, (int)y);
            }
        }
    }

    /* Colorful ground hills */
    {
        int scroll = (int)(g_rw.scroll / 2);
        int i;

        for (i = 0; i < 8; i++) {
            int hx = ((i * 70 - scroll - panel * 30) % ((int)w + 90)) - 45;
            int hh = 20 + ((i * 13) & 31);
            uint16_t hc = rw_pal(i % 6, 160);

            blit_fill_ellipse(buf, w, h, hx, (int)h - 4, 40 + (i & 7) * 3,
                              hh, hc);
        }
    }

}

static void rw_draw_cloud(uint16_t *buf, unsigned int w, unsigned int h,
                          int panel, const struct rw_cloud *c)
{
    int px, py, s;

    if (!rw_world_to_px(c->x, panel, w, &px))
        return;
    py = (int)h * c->y_frac / 255;
    s = c->scale;
    blit_fill_ellipse(buf, w, h, px, py, s + 6, s / 2 + 4, rw_pal(c->hue, 230));
    blit_fill_ellipse(buf, w, h, px - s, py + 2, s, s / 2, rw_pal(c->hue, 200));
    blit_fill_ellipse(buf, w, h, px + s, py + 1, s - 2, s / 2, rw_pal(c->hue, 210));
}

static void rw_draw_plane(uint16_t *buf, unsigned int w, unsigned int h,
                          int panel)
{
    int px, py;
    /* Slow, small bob — avoid the old ±8px / fast flap jitter. */
    int bob = (g_sinlut[g_rw.bob] * 2) / 255;
    int flap = (g_sinlut[(g_rw.bob + 64) & 0xFF] * 1) / 255;
    /* Coherent "sky racer" palette: silver body, azure wings, coral accents. */
    uint16_t body = blit_rgb565(230, 236, 245);
    uint16_t body_hi = blit_rgb565(250, 252, 255);
    uint16_t body_lo = blit_rgb565(170, 190, 215);
    uint16_t wing = blit_rgb565(35, 130, 245);
    uint16_t wing_hi = blit_rgb565(90, 185, 255);
    uint16_t wing_lo = blit_rgb565(20, 85, 190);
    uint16_t accent = blit_rgb565(255, 75, 110);
    uint16_t accent_hi = blit_rgb565(255, 160, 90);
    uint16_t nose = blit_rgb565(255, 95, 105);
    uint16_t glass = blit_rgb565(25, 70, 140);
    uint16_t glass_hi = blit_rgb565(80, 190, 230);
    uint16_t flame0 = blit_rgb565(255, 70, 30);
    uint16_t flame1 = blit_rgb565(255, 170, 50);
    uint16_t flame2 = blit_rgb565(255, 245, 180);

    if (!rw_world_to_px(g_rw.plane_x, panel, w, &px))
        return;

    py = rw_plane_y(h) - 8 - bob;

    /* Exhaust — warm plume behind the engine */
    blit_fill_ellipse(buf, w, h, px - 30 - flap, py + 1, 14, 5, flame0);
    blit_fill_ellipse(buf, w, h, px - 40, py + 1, 10, 4, flame1);
    blit_fill_ellipse(buf, w, h, px - 48, py + 1, 6, 2, flame2);

    /* Swept main wings (lower then upper for depth) */
    blit_fill_ellipse(buf, w, h, px - 2, py + 9 + flap, 30, 7, wing_lo);
    blit_fill_ellipse(buf, w, h, px - 8, py + 12 + flap, 20, 5, wing);
    blit_fill_ellipse(buf, w, h, px + 4, py + 7 + flap, 12, 3, wing_hi);

    blit_fill_ellipse(buf, w, h, px - 2, py - 8 - flap, 30, 7, wing);
    blit_fill_ellipse(buf, w, h, px - 8, py - 12 - flap, 20, 5, wing_hi);
    blit_fill_ellipse(buf, w, h, px + 4, py - 6 - flap, 12, 3, wing_hi);

    /* Small canards near the nose */
    blit_fill_ellipse(buf, w, h, px + 14, py - 5, 9, 3, wing_lo);
    blit_fill_ellipse(buf, w, h, px + 14, py + 5, 9, 3, wing_lo);

    /* Fuselage: long silver tube with belly shadow + top highlight */
    blit_fill_ellipse(buf, w, h, px - 2, py + 2, 24, 8, body_lo);
    blit_fill_ellipse(buf, w, h, px, py, 26, 8, body);
    blit_fill_ellipse(buf, w, h, px + 4, py - 2, 16, 5, body_hi);

    /* Coral racing stripe along the side */
    blit_fill_ellipse(buf, w, h, px + 2, py + 1, 18, 2, accent);
    blit_fill_ellipse(buf, w, h, px + 10, py + 1, 8, 2, accent_hi);

    /* Pointed nose cone */
    blit_fill_ellipse(buf, w, h, px + 22, py, 10, 5, nose);
    blit_fill_circle(buf, w, h, px + 28, py, 4, accent);

    /* Cockpit canopy */
    blit_fill_ellipse(buf, w, h, px + 6, py - 4, 9, 4, glass);
    blit_fill_ellipse(buf, w, h, px + 8, py - 5, 5, 2, glass_hi);

    /* Single vertical fin + horizontal stabilizer */
    blit_fill_rect(buf, w, h, px - 22, py - 18, 5, 18, wing);
    blit_fill_rect(buf, w, h, px - 20, py - 18, 3, 6, accent);
    blit_fill_ellipse(buf, w, h, px - 20, py, 11, 4, wing_hi);

    /* Engine nacelle at the rear */
    blit_fill_ellipse(buf, w, h, px - 22, py + 1, 7, 4, body_lo);
    blit_fill_circle(buf, w, h, px - 26, py + 1, 3, flame1);

    if (g_rw.hit_flash)
        blit_draw_circle(buf, w, h, px, py, 32, blit_rgb565(255, 40, 40));
}

static void rw_draw_obst(uint16_t *buf, unsigned int w, unsigned int h,
                         int panel, const struct rw_obst *o, uint32_t t)
{
    int px, py, pw;
    uint16_t c = rw_pal(o->hue, 255);
    uint16_t c2 = rw_pal((o->hue + 2) % 6, 220);

    if (!rw_world_to_px(o->x, panel, w, &px))
        return;

    py = rw_lane_y(h, o->lane);
    pw = o->w * (int)w / RW_PW;
    if (pw < 10)
        pw = 10;

    if (o->kind == OBS_RING) {
        int r = 14 + ((int)t & 3);
        int i;

        for (i = 0; i < 3; i++)
            blit_draw_circle(buf, w, h, px + pw / 2, py, r - i, c);
        blit_draw_circle(buf, w, h, px + pw / 2, py, r - 4, c2);
        /* sparkles */
        blit_fill_circle(buf, w, h, px + pw / 2 + r, py, 2,
                         blit_rgb565(255, 255, 200));
    } else if (o->kind == OBS_BALLOON) {
        blit_fill_circle(buf, w, h, px + 8, py - 18, 12, c);
        blit_fill_circle(buf, w, h, px + 22, py - 10, 10, c2);
        blit_fill_circle(buf, w, h, px + 10, py - 6, 9, rw_pal(o->hue + 1, 240));
        blit_draw_line(buf, w, h, px + 8, py - 6, px + 8, py + 8,
                       blit_rgb565(255, 255, 255));
        blit_draw_line(buf, w, h, px + 22, py, px + 14, py + 10,
                       blit_rgb565(255, 255, 255));
        blit_fill_circle(buf, w, h, px + 5, py - 22, 2, blit_rgb565(255, 255, 255));
    } else {
        /* Crystal tower */
        int ph = 36;
        int tip = py - ph;

        blit_fill_rect(buf, w, h, px + 4, tip + 8, pw - 8, ph - 8, c);
        blit_draw_line(buf, w, h, px + pw / 2, tip, px, tip + 14, c2);
        blit_draw_line(buf, w, h, px + pw / 2, tip, px + pw, tip + 14, c2);
        blit_fill_rect(buf, w, h, px + 6, tip + 12, pw - 12, 6,
                       blit_rgb565(255, 255, 220));
    }
}

static void rw_draw_gem(uint16_t *buf, unsigned int w, unsigned int h,
                        int panel, const struct rw_gem *g, uint32_t t)
{
    int px, py;
    int pulse = 2 + (g_sinlut[(t * 10u + g->hue * 40u) & 0xFFu] * 3) / 255;
    uint16_t c = rw_pal(g->hue, 255);

    if (!rw_world_to_px(g->x, panel, w, &px))
        return;
    py = rw_lane_y(h, g->lane) - 10;
    /* Diamond shape via overlapping rects/circles */
    blit_fill_circle(buf, w, h, px, py, 5 + pulse, c);
    blit_fill_rect(buf, w, h, px - 1, py - 7 - pulse / 2, 3, 5 + pulse, c);
    blit_fill_circle(buf, w, h, px - 1, py - 1, 2, blit_rgb565(255, 255, 255));
}

void blit_fill_runner(uint16_t *buf, unsigned int w, unsigned int h,
                      uint32_t t, int panel)
{
    int i;
    uint16_t hud = blit_rgb565(255, 240, 100);

    if (panel < 0 || panel > 2)
        panel = 0;

    rw_draw_sky(buf, w, h, panel, t);

    for (i = 0; i < RW_NCLOUD; i++)
        rw_draw_cloud(buf, w, h, panel, &g_rw.cloud[i]);

    if (g_rw.seam_flash) {
        uint16_t portal = rw_pal((int)(t / 4) % 6, 255);
        int mid = (int)h / 2;
        int pr = 14 + g_rw.seam_flash / 2;

        blit_draw_circle(buf, w, h, 4, mid, pr, portal);
        blit_draw_circle(buf, w, h, (int)w - 5, mid, pr, portal);
        blit_fill_rect(buf, w, h, 0, mid - pr, 4, pr * 2, portal);
        blit_fill_rect(buf, w, h, (int)w - 4, mid - pr, 4, pr * 2, portal);
    }

    for (i = 0; i < RW_NGEM; i++) {
        if (g_rw.gem[i].alive)
            rw_draw_gem(buf, w, h, panel, &g_rw.gem[i], t);
    }
    for (i = 0; i < RW_NOBST; i++) {
        if (g_rw.obst[i].alive)
            rw_draw_obst(buf, w, h, panel, &g_rw.obst[i], t);
    }

    /* Plane drawn on whichever panel it currently occupies */
    rw_draw_plane(buf, w, h, panel);

    if (g_rw.hit_flash) {
        int vis = g_rw.plane_x - (int)g_rw.scroll;
        int pp = vis / RW_PW;

        if (pp == panel) {
            blit_fill_rect(buf, w, h, 0, 0, (int)w, 4, blit_rgb565(255, 60, 100));
            blit_fill_rect(buf, w, h, 0, (int)h - 4, (int)w, 4,
                           blit_rgb565(255, 60, 100));
        }
    }

    blit_draw_digit(buf, w, h, 6, 4, panel + 3, hud);
    blit_fill_rect(buf, w, h, 14, 6, 14, 2, rw_pal(panel, 255));
    {
        int s = g_rw.score;

        blit_draw_digit(buf, w, h, (int)w - 28, 4, (s / 100) % 10, hud);
        blit_draw_digit(buf, w, h, (int)w - 20, 4, (s / 10) % 10, hud);
        blit_draw_digit(buf, w, h, (int)w - 12, 4, s % 10, hud);
    }
}
