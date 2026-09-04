/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * World-cup five-panel demo: one pitch across SPI1-3-4-5-2.
 *
 * Architecture — single game world, five independent framebuffers
 * ================================================================
 *
 *   Shared game state (singleton)       5 independent framebuffers
 *   ┌──────────────────────┐           ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐
 *   │ g_wc                 │           │SPI1│ │SPI3│ │SPI4│ │SPI5│ │SPI2│
 *   │  ball_x, ball_y      │           │ w0 │ │ w0 │ │ w0 │ │ w0 │ │ w0 │
 *   │  players[0..3]       │           │ =0 │ │=256│ │=512│ │=768│ │=1024│
 *   │  gk[0..1]            │           │    │ │    │ │    │ │    │ │    │
 *   │  wx = 0 .. 1280      │           │240 │ │240 │ │240 │ │240 │ │240 │
 *   └──────────┬───────────┘           │ px │ │ px │ │ px │ │ px │ │ px │
 *              │                       └──┬─┘ └──┬─┘ └──┬─┘ └──┬─┘ └──┬─┘
 *              │ snapshot (once/frame)     ▲      ▲      ▲      ▲      ▲
 *              ├──────────────────────────┘      │      │      │      │
 *              │     5 threads, each draws its own segment
 *              │     world-to-screen: cx = (wx - world0) * panel_w / WC_SEG
 *
 *   World layout (left → right):
 *     SPI1          SPI3          SPI4          SPI5          SPI2
 *   [0 .. 255]   [256 .. 511]  [512 .. 767]  [768 .. 1023] [1024 .. 1279]
 *
 *   Only the SPI3 thread calls wc_step() to advance the simulation.
 *   All five threads call wc_get_snapshot() + wc_fill_panel() independently.
 *   Margin=40 allows partial sprites to bleed across panel seams.
 */

#ifndef WC_GAME_H
#define WC_GAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Each panel is WC_SEG wide in world space.
 * Layout left→right: SPI1 | SPI3 | SPI4 | SPI5 | SPI2
 */
#define WC_SEG       256
#define WC_NPANEL    5
#define WC_WORLD     (WC_SEG * WC_NPANEL)

#define WC_NPLAYERS  4   /* 2 blue + 2 orange */

enum wc_phase {
    WC_PHASE_ATTACK_R = 0, /* blue dribbles right then kicks */
    WC_PHASE_FLY_R,        /* ball flies right; squads chase */
    WC_PHASE_ATTACK_L,     /* orange dribbles left then kicks */
    WC_PHASE_FLY_L,
    WC_PHASE_GOAL,
};

enum wc_pose {
    WC_POSE_RUN = 0,
    WC_POSE_WINDUP,
    WC_POSE_KICK,
    WC_POSE_CHEER,
};

struct wc_player_snap {
    uint8_t side;         /* 0 blue, 1 orange */
    int16_t wx;           /* world X 0..WC_WORLD-1 */
    uint8_t y;            /* 0..255 vertical */
    uint8_t facing_right;
    uint8_t pose;
    uint8_t run;
    uint8_t hair;
    uint8_t num;
};

struct wc_snapshot {
    uint8_t  phase;
    uint8_t  score_l;
    uint8_t  score_r;
    uint8_t  cheer;
    int16_t  ball_x;      /* world X; -1 if hidden */
    int16_t  ball_y;
    int8_t   ball_dir;
    uint8_t  trail_hue;
    uint8_t  nplayers;
    struct wc_player_snap players[WC_NPLAYERS];
    struct wc_player_snap gk[2];   /* [0]=blue GK, [1]=orange GK */
};

/* World X origin of spi_id panel (0=SPI1 ..). */
int wc_spi_world0(int spi_id);

void wc_step(void);
void wc_get_snapshot(struct wc_snapshot *out);

void wc_fill_panel(uint16_t *buf, unsigned int w, unsigned int h, int spi_id);

void start_wc_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* WC_GAME_H */
