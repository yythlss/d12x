/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared world-cup match across five panels. SPI3 thread advances state.
 * 2v2 with long spacing; mid passes to striker, then shot.
 */

#include "wc_game.h"
#include "blit_common.h"
#include <string.h>

#if BLIT_TARGET_FPS >= 50
#define WC_DRIBBLE_T     12
#define WC_WIND_T        3
#define WC_KICK_T        3
#define WC_PASS_FLY_T    14
#define WC_SHOT_WIND_T   5
#define WC_SHOT_KICK_T   6
#define WC_GOAL_FRAMES   30
#define WC_BALL_SPEED    8
#define WC_PASS_SPEED    12
#define WC_RUN_SPEED     6
#define WC_PASS_DIST     160
#else
#define WC_DRIBBLE_T     9
#define WC_WIND_T        3
#define WC_KICK_T        3
#define WC_PASS_FLY_T    10
#define WC_SHOT_WIND_T   4
#define WC_SHOT_KICK_T   5
#define WC_GOAL_FRAMES   20
#define WC_BALL_SPEED    12
#define WC_PASS_SPEED    16
#define WC_RUN_SPEED     8
#define WC_PASS_DIST     160
#endif
#define WC_PASSES_GOAL   3

enum {
    WC_ST_DRIBBLE0 = 0,
    WC_ST_PASS0,
    WC_ST_DRIBBLE1,
    WC_ST_SHOT,
};

struct wc_player {
    uint8_t side;
    int16_t wx;
    uint8_t y;
    uint8_t role;         /* 0 striker, 1 mid */
    uint8_t pose;
    uint8_t run;
    uint8_t hair;
    uint8_t num;
    int16_t home_wx;
    uint8_t home_y;
    uint8_t facing_right;
};

struct wc_game {
    uint8_t  ready;
    uint8_t  phase;
    uint8_t  phase_t;
    uint8_t  score_l;
    uint8_t  score_r;
    uint8_t  cheer;
    uint8_t  passes;
    uint8_t  atk_stage;
    uint8_t  stage_t;
    uint8_t  carrier;
    int16_t  pass_tx;
    int16_t  pass_ty;
    int16_t  ball_x;
    int16_t  ball_y;
    int8_t   ball_dir;
    uint8_t  trail_hue;
    int16_t  shot_ty;     /* target Y for shot — aims at goal frame */
    struct wc_player pl[WC_NPLAYERS];
    struct wc_player gk[2];  /* [0]=blue GK (left goal), [1]=orange GK (right goal) */
};

static struct wc_game g_wc;

int wc_spi_world0(int spi_id)
{
    switch (spi_id) {
    case 0: return 0;
    case 2: return WC_SEG;
    case 3: return WC_SEG * 2;
    case 4: return WC_SEG * 3;
    case 1: return WC_SEG * 4;
    default: return 0;
    }
}

static int wc_clamp(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void wc_chase_xy(struct wc_player *p, int tx, int ty, int speed)
{
    int dx = tx - (int)p->wx;
    int dy = ty - (int)p->y;

    if (dx > speed)
        p->wx = (int16_t)(p->wx + speed);
    else if (dx < -speed)
        p->wx = (int16_t)(p->wx - speed);
    else
        p->wx = (int16_t)tx;

    if (dy > speed)
        p->y = (uint8_t)(p->y + speed);
    else if (dy < -speed)
        p->y = (uint8_t)(p->y - speed);
    else
        p->y = (uint8_t)ty;

    p->wx = (int16_t)wc_clamp((int)p->wx, 24, WC_WORLD - 24);
    p->y = (uint8_t)wc_clamp((int)p->y, 80, 176);

    if (dx > 1)
        p->facing_right = 1;
    else if (dx < -1)
        p->facing_right = 0;

    if ((dx != 0 || dy != 0) && (g_wc.phase_t & 1)) {
        p->run = (uint8_t)((p->run + 1) & 7);
        if (p->pose != WC_POSE_KICK && p->pose != WC_POSE_WINDUP &&
            p->pose != WC_POSE_CHEER)
            p->pose = WC_POSE_RUN;
    }
}

static void wc_stick_ball(const struct wc_player *p, int dir)
{
    g_wc.ball_x = (int16_t)(p->wx + dir * 12);
    g_wc.ball_y = (int16_t)(p->y + 16);
    g_wc.ball_dir = (int8_t)dir;
}

static int wc_ball_fly_to(int tx, int ty, int speed)
{
    int dx = tx - (int)g_wc.ball_x;
    int dy = ty - (int)g_wc.ball_y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    if (dx > speed)
        g_wc.ball_x = (int16_t)(g_wc.ball_x + speed);
    else if (dx < -speed)
        g_wc.ball_x = (int16_t)(g_wc.ball_x - speed);
    else
        g_wc.ball_x = (int16_t)tx;

    if (dy > speed)
        g_wc.ball_y = (int16_t)(g_wc.ball_y + speed);
    else if (dy < -speed)
        g_wc.ball_y = (int16_t)(g_wc.ball_y - speed);
    else
        g_wc.ball_y = (int16_t)ty;

    if (dx > 0)
        g_wc.ball_dir = 1;
    else if (dx < 0)
        g_wc.ball_dir = -1;

    return (adx <= speed && ady <= speed);
}

static void wc_init_players(void)
{
    /* Wide spacing: mid near own half, striker pushed forward. */
    g_wc.pl[0].side = 0;
    g_wc.pl[0].role = 0;
    g_wc.pl[0].home_wx = 210;
    g_wc.pl[0].home_y = 150;
    g_wc.pl[0].num = 9;
    g_wc.pl[0].hair = 0;

    g_wc.pl[1].side = 0;
    g_wc.pl[1].role = 1;
    g_wc.pl[1].home_wx = 70;
    g_wc.pl[1].home_y = 100;
    g_wc.pl[1].num = 7;
    g_wc.pl[1].hair = 2;

    g_wc.pl[2].side = 1;
    g_wc.pl[2].role = 0;
    g_wc.pl[2].home_wx = (int16_t)(WC_WORLD - 210);
    g_wc.pl[2].home_y = 106;
    g_wc.pl[2].num = 10;
    g_wc.pl[2].hair = 1;

    g_wc.pl[3].side = 1;
    g_wc.pl[3].role = 1;
    g_wc.pl[3].home_wx = (int16_t)(WC_WORLD - 70);
    g_wc.pl[3].home_y = 156;
    g_wc.pl[3].num = 8;
    g_wc.pl[3].hair = 3;

    /* Goalkeepers: stay near own goal */
    g_wc.gk[0].side = 0;
    g_wc.gk[0].home_wx = 56;
    g_wc.gk[0].home_y = 128;
    g_wc.gk[0].num = 1;
    g_wc.gk[0].hair = 0;

    g_wc.gk[1].side = 1;
    g_wc.gk[1].home_wx = (int16_t)(WC_WORLD - 56);
    g_wc.gk[1].home_y = 128;
    g_wc.gk[1].num = 1;
    g_wc.gk[1].hair = 1;
}

static void wc_reset_round(int attack_right)
{
    int i;

    g_wc.phase_t = 0;
    g_wc.stage_t = 0;
    g_wc.atk_stage = WC_ST_DRIBBLE0;
    g_wc.cheer = 0;
    for (i = 0; i < WC_NPLAYERS; i++) {
        g_wc.pl[i].pose = WC_POSE_RUN;
        g_wc.pl[i].wx = g_wc.pl[i].home_wx;
        g_wc.pl[i].y = g_wc.pl[i].home_y;
        g_wc.pl[i].facing_right = (g_wc.pl[i].side == 0) ? 1 : 0;
        g_wc.pl[i].run = (uint8_t)(i * 2);
    }
    /* Reset goalkeepers to home */
    for (i = 0; i < 2; i++) {
        g_wc.gk[i].pose = WC_POSE_RUN;
        g_wc.gk[i].wx = g_wc.gk[i].home_wx;
        g_wc.gk[i].y = g_wc.gk[i].home_y;
        g_wc.gk[i].facing_right = (uint8_t)(i ^ 1); /* blue faces right, orange left */
        g_wc.gk[i].run = 0;
    }
    if (attack_right) {
        g_wc.phase = WC_PHASE_ATTACK_R;
        g_wc.carrier = 1; /* blue mid */
        g_wc.ball_dir = 1;
        g_wc.trail_hue = 0;
        wc_stick_ball(&g_wc.pl[1], 1);
    } else {
        g_wc.phase = WC_PHASE_ATTACK_L;
        g_wc.carrier = 3; /* orange mid */
        g_wc.ball_dir = -1;
        g_wc.trail_hue = 1;
        wc_stick_ball(&g_wc.pl[3], -1);
    }
}

static void wc_ensure(void)
{
    if (g_wc.ready)
        return;
    memset(&g_wc, 0, sizeof(g_wc));
    g_wc.ready = 1;
    wc_init_players();
    wc_reset_round(1);
}

static void wc_next_stage(uint8_t st)
{
    g_wc.atk_stage = st;
    g_wc.stage_t = 0;
}

static void wc_shape_attack(int blue, int carrier)
{
    int dir = blue ? 1 : -1;
    int mate = blue ? (carrier == 0 ? 1 : 0) : (carrier == 2 ? 3 : 2);
    int oi0 = blue ? 2 : 0;
    int oi1 = blue ? 3 : 1;
    struct wc_player *p = &g_wc.pl[mate];

    /* Teammate stays well ahead / wide */
    wc_chase_xy(p, (int)g_wc.ball_x + dir * WC_PASS_DIST,
                blue ? 150 : 100, WC_RUN_SPEED);
    p->facing_right = blue ? 1 : 0;

    wc_chase_xy(&g_wc.pl[oi0], (int)g_wc.ball_x + dir * 80,
                90, WC_RUN_SPEED);
    wc_chase_xy(&g_wc.pl[oi1], (int)g_wc.ball_x + dir * 140,
                170, WC_RUN_SPEED);
    g_wc.pl[oi0].facing_right = blue ? 0 : 1;
    g_wc.pl[oi1].facing_right = blue ? 0 : 1;
}

/* chain[0]=mid starter, chain[1]=striker finisher */
static int wc_update_attack_chain(int blue, const uint8_t chain[2])
{
    int dir = blue ? 1 : -1;
    struct wc_player *car;
    struct wc_player *recv;
    int t = g_wc.stage_t;
    int done = 0;

    g_wc.trail_hue = blue ? 0 : 1;
    g_wc.ball_dir = (int8_t)dir;
    wc_shape_attack(blue, g_wc.carrier);
    car = &g_wc.pl[g_wc.carrier];

    switch (g_wc.atk_stage) {
    case WC_ST_DRIBBLE0:
    case WC_ST_DRIBBLE1:
        wc_chase_xy(car, (int)car->wx + dir * 36,
                    (int)car->y +
                    ((int)g_sinlut[(g_wc.phase_t * 5) & 0xFF] - 128) / 24,
                    WC_RUN_SPEED + 1);
        car->facing_right = blue ? 1 : 0;
        car->pose = WC_POSE_RUN;
        wc_stick_ball(car, dir);
        if (t >= WC_DRIBBLE_T) {
            if (g_wc.atk_stage == WC_ST_DRIBBLE0) {
                g_wc.pass_tx = (int16_t)wc_clamp((int)car->wx + dir * WC_PASS_DIST,
                                                 40, WC_WORLD - 40);
                g_wc.pass_ty = (int16_t)wc_clamp((int)g_wc.pl[chain[1]].home_y,
                                                 90, 170);
                car->pose = WC_POSE_WINDUP;
                wc_next_stage(WC_ST_PASS0);
            } else {
                car->pose = WC_POSE_WINDUP;
                wc_next_stage(WC_ST_SHOT);
            }
        }
        break;

    case WC_ST_PASS0:
        recv = &g_wc.pl[chain[1]];
        if (t < WC_WIND_T) {
            car->pose = WC_POSE_WINDUP;
            wc_stick_ball(car, dir);
        } else if (t < WC_WIND_T + WC_KICK_T) {
            car->pose = WC_POSE_KICK;
            wc_stick_ball(car, dir);
        } else {
            car->pose = WC_POSE_RUN;
            wc_chase_xy(recv, g_wc.pass_tx, g_wc.pass_ty, WC_RUN_SPEED + 2);
            if (wc_ball_fly_to(g_wc.pass_tx, g_wc.pass_ty, WC_PASS_SPEED) ||
                t >= WC_WIND_T + WC_KICK_T + WC_PASS_FLY_T) {
                g_wc.carrier = chain[1];
                g_wc.pl[g_wc.carrier].wx = g_wc.pass_tx;
                g_wc.pl[g_wc.carrier].y = (uint8_t)g_wc.pass_ty;
                wc_next_stage(WC_ST_DRIBBLE1);
            }
        }
        wc_chase_xy(recv, g_wc.pass_tx, g_wc.pass_ty, WC_RUN_SPEED + 1);
        break;

    case WC_ST_SHOT:
        if (t < WC_SHOT_WIND_T) {
            car->pose = WC_POSE_WINDUP;
            wc_stick_ball(car, dir);
        } else if (t < WC_SHOT_WIND_T + WC_SHOT_KICK_T) {
            int kick_t = t - (int)WC_SHOT_WIND_T;

            car->pose = WC_POSE_KICK;
            /* Ball rockets away from foot during kick animation */
            g_wc.ball_x = (int16_t)(car->wx + dir * (40 + kick_t * WC_BALL_SPEED * 2));
            g_wc.ball_y = (int16_t)(car->y + 18);
        } else {
            /* Pick a goal-frame target on kick release */
            static const int16_t targets[] = {
                100, 110, 120, 128, 136, 146, 156,  /* on target */
                88,  168,                              /* near miss (posts/bar) */
            };
            int pick = (int)(g_wc.phase_t * 7 + g_wc.carrier * 13) %
                       (int)(sizeof(targets) / sizeof(targets[0]));

            car->pose = WC_POSE_RUN;
            g_wc.shot_ty = targets[pick];
            done = 1;
        }
        break;

    default:
        done = 1;
        break;
    }

    g_wc.stage_t++;
    return done;
}

static void wc_update_attack_r(void)
{
    static const uint8_t chain[2] = {1, 0}; /* mid → striker */

    if (wc_update_attack_chain(1, chain)) {
        g_wc.phase = WC_PHASE_FLY_R;
        g_wc.phase_t = 0;
        g_wc.ball_dir = 1;
        g_wc.trail_hue = 0;
    }

    /* Blue GK stays near left goal; orange GK tracks ball y */
    wc_chase_xy(&g_wc.gk[0], g_wc.gk[0].home_wx, 128, WC_RUN_SPEED);
    g_wc.gk[0].facing_right = 1;
    wc_chase_xy(&g_wc.gk[1], g_wc.gk[1].home_wx,
                (int)g_wc.ball_y, WC_RUN_SPEED + 2);
    g_wc.gk[1].facing_right = 0;
}

static void wc_update_attack_l(void)
{
    static const uint8_t chain[2] = {3, 2};

    if (wc_update_attack_chain(0, chain)) {
        g_wc.phase = WC_PHASE_FLY_L;
        g_wc.phase_t = 0;
        g_wc.ball_dir = -1;
        g_wc.trail_hue = 1;
    }

    /* Orange GK stays near right goal; blue GK tracks ball y */
    wc_chase_xy(&g_wc.gk[0], g_wc.gk[0].home_wx,
                (int)g_wc.ball_y, WC_RUN_SPEED + 2);
    g_wc.gk[0].facing_right = 1;
    wc_chase_xy(&g_wc.gk[1], g_wc.gk[1].home_wx, 128, WC_RUN_SPEED);
    g_wc.gk[1].facing_right = 0;
}

static void wc_update_fly_r(void)
{
    int i;

    g_wc.ball_x = (int16_t)(g_wc.ball_x + WC_BALL_SPEED);
    /* Steer ball toward shot target Y — snap when close to avoid oscillation */
    {
        int half = (int)WC_BALL_SPEED / 2;
        int dy = (int)g_wc.shot_ty - (int)g_wc.ball_y;
        int ady = dy < 0 ? -dy : dy;

        if (ady <= half)
            g_wc.ball_y = g_wc.shot_ty;
        else if (dy > 0)
            g_wc.ball_y = (int16_t)(g_wc.ball_y + half);
        else
            g_wc.ball_y = (int16_t)(g_wc.ball_y - half);
    }
    g_wc.ball_y = (int16_t)wc_clamp((int)g_wc.ball_y, 85, 170);
    g_wc.ball_dir = 1;
    g_wc.trail_hue = 0;

    for (i = 0; i < WC_NPLAYERS; i++) {
        int ox = (g_wc.pl[i].side == 0) ? -40 - g_wc.pl[i].role * 50
                                        : 50 + g_wc.pl[i].role * 45;
        int oy = g_wc.pl[i].role ? -28 : 28;

        wc_chase_xy(&g_wc.pl[i], (int)g_wc.ball_x + ox,
                    (int)g_wc.ball_y + oy, WC_RUN_SPEED + 1);
        g_wc.pl[i].pose = WC_POSE_RUN;
    }

    /* GKs: dive if shot is on-target and near goal */
    {
        int on_target = (g_wc.shot_ty >= 96 && g_wc.shot_ty <= 160);
        int near_goal = (g_wc.ball_x > WC_WORLD - 120);

        wc_chase_xy(&g_wc.gk[0], g_wc.gk[0].home_wx, 128, WC_RUN_SPEED);
        g_wc.gk[0].facing_right = 1;
        if (on_target && near_goal) {
            wc_chase_xy(&g_wc.gk[1], (int)g_wc.ball_x - 30, g_wc.shot_ty,
                        WC_RUN_SPEED + 6);
            g_wc.gk[1].pose = WC_POSE_WINDUP;
        } else {
            wc_chase_xy(&g_wc.gk[1], g_wc.gk[1].home_wx,
                        (int)g_wc.ball_y, WC_RUN_SPEED + 2);
            g_wc.gk[1].pose = WC_POSE_RUN;
        }
        g_wc.gk[1].facing_right = 0;
    }
}

static void wc_update_fly_l(void)
{
    int i;

    g_wc.ball_x = (int16_t)(g_wc.ball_x - WC_BALL_SPEED);
    /* Steer ball toward shot target Y — snap when close to avoid oscillation */
    {
        int half = (int)WC_BALL_SPEED / 2;
        int dy = (int)g_wc.shot_ty - (int)g_wc.ball_y;
        int ady = dy < 0 ? -dy : dy;

        if (ady <= half)
            g_wc.ball_y = g_wc.shot_ty;
        else if (dy > 0)
            g_wc.ball_y = (int16_t)(g_wc.ball_y + half);
        else
            g_wc.ball_y = (int16_t)(g_wc.ball_y - half);
    }
    g_wc.ball_y = (int16_t)wc_clamp((int)g_wc.ball_y, 85, 170);
    g_wc.ball_dir = -1;
    g_wc.trail_hue = 1;

    for (i = 0; i < WC_NPLAYERS; i++) {
        int ox = (g_wc.pl[i].side == 1) ? 40 + g_wc.pl[i].role * 50
                                        : -50 - g_wc.pl[i].role * 45;
        int oy = g_wc.pl[i].role ? 28 : -28;

        wc_chase_xy(&g_wc.pl[i], (int)g_wc.ball_x + ox,
                    (int)g_wc.ball_y + oy, WC_RUN_SPEED + 1);
        g_wc.pl[i].pose = WC_POSE_RUN;
    }

    /* GKs: dive if shot is on-target and near goal */
    {
        int on_target = (g_wc.shot_ty >= 96 && g_wc.shot_ty <= 160);
        int near_goal = (g_wc.ball_x < 120);

        wc_chase_xy(&g_wc.gk[1], g_wc.gk[1].home_wx, 128, WC_RUN_SPEED);
        g_wc.gk[1].facing_right = 0;
        if (on_target && near_goal) {
            wc_chase_xy(&g_wc.gk[0], (int)g_wc.ball_x + 30, g_wc.shot_ty,
                        WC_RUN_SPEED + 6);
            g_wc.gk[0].pose = WC_POSE_WINDUP;
        } else {
            wc_chase_xy(&g_wc.gk[0], g_wc.gk[0].home_wx,
                        (int)g_wc.ball_y, WC_RUN_SPEED + 2);
            g_wc.gk[0].pose = WC_POSE_RUN;
        }
        g_wc.gk[0].facing_right = 1;
    }
}

static void wc_update_goal(void)
{
    int i;

    for (i = 0; i < WC_NPLAYERS; i++) {
        int hop = ((g_wc.phase_t + i) & 2) ? -8 : 0;

        wc_chase_xy(&g_wc.pl[i], g_wc.pl[i].home_wx,
                    wc_clamp((int)g_wc.pl[i].home_y + hop, 80, 176),
                    WC_RUN_SPEED);
        g_wc.pl[i].pose = WC_POSE_CHEER;
        g_wc.pl[i].run = (uint8_t)((g_wc.pl[i].run + 1) & 7);
    }
    /* Both keepers cheer */
    for (i = 0; i < 2; i++) {
        int hop = ((g_wc.phase_t + i * 3) & 2) ? -6 : 0;

        wc_chase_xy(&g_wc.gk[i], g_wc.gk[i].home_wx,
                    wc_clamp((int)g_wc.gk[i].home_y + hop, 80, 176),
                    WC_RUN_SPEED);
        g_wc.gk[i].pose = WC_POSE_CHEER;
        g_wc.gk[i].run = (uint8_t)((g_wc.gk[i].run + 1) & 7);
    }
    g_wc.ball_x = -1;
}

void wc_step(void)
{
    wc_ensure();
    g_wc.phase_t++;

    switch (g_wc.phase) {
    case WC_PHASE_ATTACK_R:
        wc_update_attack_r();
        break;

    case WC_PHASE_FLY_R:
        wc_update_fly_r();
        if (g_wc.ball_x >= WC_WORLD - 40) {
            g_wc.passes++;
            if (g_wc.passes >= WC_PASSES_GOAL) {
                g_wc.phase = WC_PHASE_GOAL;
                g_wc.phase_t = 0;
                g_wc.cheer = WC_GOAL_FRAMES;
                g_wc.score_r = (uint8_t)((g_wc.score_r + 1) % 10);
                g_wc.passes = 0;
            } else {
                g_wc.phase = WC_PHASE_ATTACK_L;
                g_wc.phase_t = 0;
                g_wc.stage_t = 0;
                g_wc.atk_stage = WC_ST_DRIBBLE0;
                g_wc.carrier = 3;
                g_wc.pl[3].wx = (int16_t)(WC_WORLD - 80);
                g_wc.pl[2].wx = (int16_t)(WC_WORLD - 240);
                wc_stick_ball(&g_wc.pl[3], -1);
            }
        }
        break;

    case WC_PHASE_ATTACK_L:
        wc_update_attack_l();
        break;

    case WC_PHASE_FLY_L:
        wc_update_fly_l();
        if (g_wc.ball_x <= 40) {
            g_wc.passes++;
            if (g_wc.passes >= WC_PASSES_GOAL) {
                g_wc.phase = WC_PHASE_GOAL;
                g_wc.phase_t = 0;
                g_wc.cheer = WC_GOAL_FRAMES;
                g_wc.score_l = (uint8_t)((g_wc.score_l + 1) % 10);
                g_wc.passes = 0;
            } else {
                g_wc.phase = WC_PHASE_ATTACK_R;
                g_wc.phase_t = 0;
                g_wc.stage_t = 0;
                g_wc.atk_stage = WC_ST_DRIBBLE0;
                g_wc.carrier = 1;
                g_wc.pl[1].wx = 80;
                g_wc.pl[0].wx = 240;
                wc_stick_ball(&g_wc.pl[1], 1);
            }
        }
        break;

    case WC_PHASE_GOAL:
    default:
        wc_update_goal();
        if (g_wc.cheer)
            g_wc.cheer--;
        if (g_wc.phase_t >= WC_GOAL_FRAMES)
            wc_reset_round((g_wc.score_l + g_wc.score_r) & 1);
        break;
    }
}

void wc_get_snapshot(struct wc_snapshot *out)
{
    int i;

    if (!out)
        return;
    wc_ensure();
    out->phase = g_wc.phase;
    out->score_l = g_wc.score_l;
    out->score_r = g_wc.score_r;
    out->cheer = g_wc.cheer;
    out->ball_x = g_wc.ball_x;
    out->ball_y = g_wc.ball_y;
    out->ball_dir = g_wc.ball_dir;
    out->trail_hue = g_wc.trail_hue;
    out->nplayers = WC_NPLAYERS;
    for (i = 0; i < WC_NPLAYERS; i++) {
        out->players[i].side = g_wc.pl[i].side;
        out->players[i].wx = g_wc.pl[i].wx;
        out->players[i].y = g_wc.pl[i].y;
        out->players[i].facing_right = g_wc.pl[i].facing_right;
        out->players[i].pose = g_wc.pl[i].pose;
        out->players[i].run = g_wc.pl[i].run;
        out->players[i].hair = g_wc.pl[i].hair;
        out->players[i].num = g_wc.pl[i].num % 10;
    }
    /* Goalkeeper snapshot */
    for (i = 0; i < 2; i++) {
        out->gk[i].side = g_wc.gk[i].side;
        out->gk[i].wx = g_wc.gk[i].wx;
        out->gk[i].y = g_wc.gk[i].y;
        out->gk[i].facing_right = g_wc.gk[i].facing_right;
        out->gk[i].pose = g_wc.gk[i].pose;
        out->gk[i].run = g_wc.gk[i].run;
        out->gk[i].hair = g_wc.gk[i].hair;
        out->gk[i].num = g_wc.gk[i].num % 10;
    }
}
