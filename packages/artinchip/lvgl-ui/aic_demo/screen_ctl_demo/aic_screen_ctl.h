/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#ifndef AIC_SCREEN_CTL_H
#define AIC_SCREEN_CTL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*
 * Screen sleep/wake feature switch.
 * Define to enable real hardware CTL; comment out to make all API no-op stubs.
 */
#define LV_AIC_SCREEN_CTL

/*
 * DBI/DSI interface selection — define ONE of:
 *   LV_SCREEN_DBI_SPI   — SPI (QSPI) interface
 *   LV_SCREEN_DBI_I8080 — i8080 parallel interface
 *   LV_SCREEN_DSI       — MIPI DSI interface
 */
//#define LV_SCREEN_DBI_SPI
//#define LV_SCREEN_DBI_I8080
#define LV_SCREEN_DSI

/* DE flush lcd with AUTO mode (depends on TE signal) */
#define LV_SCREEN_USE_TE_FLUSH 0

#if LV_SCREEN_USE_TE_FLUSH
/* Monitor TE signal while flushing; reset LCD if TE disappears */
#define LV_SCREEN_TE_CHECK 0
#endif

/*
 * Register status polling (0 = disabled, 1 = enabled)
 * implement dbi_read_screen_register / dsi_read_screen_register hooks
 */
#define LV_SCREEN_CHECK_REGISTER_STATUS  0

/* Poll LCD register every N vsync frames */
#define LV_SCREEN_CHECK_POLLING_FRAMES (10u)

/* -------------------------------------------------------------------------- */
/* DBI-SPI specific configuration                                             */
/* -------------------------------------------------------------------------- */
#ifdef LV_SCREEN_DBI_SPI

/*
 * Case A: LV_SPI_RD_SDA = 1  —  SDA bidirectional, readback on D0
 *
 * SCL  ─┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──
 *       └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘
 * SDA   W7 W6 W5 W4 W3 W2 W1 W0 | R7 R6 R5 R4 R3 R2 R1 R0
 *
 * Case B: LV_SPI_RD_SDA = 0  —  D0 output only, readback on D1
 *
 * SCL  ─┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──
 *       └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘
 * SDA(D0)  W7 W6 W5 W4 W3 W2 W1 W0  X  X  X  X  X  X  X  X
 * SDI(D1)   X  X  X  X  X  X  X  X R7 R6 R5 R4 R3 R2 R1 R0
 */
#define LV_SPI_RD_SDA                    0

#define LV_SPI_4SDA_RD_SWITCH_PACKET_HEAD 1
/* only takes effect when LV_SPI_4SDA_RD_SWITCH_PACKET_HEAD is 1 */
#define LV_SPI_RD_PACKET_HEAD             0xDD

#define LV_SPI_RD_SWITCH_CLK              1
/* only takes effect when LV_SPI_RD_SWITCH_CLK is 1 */
#define LV_SPI_RD_PIXEL_CLK               (1500000) /* 1M Hz */
#endif

/* Public API */
void screen_ctl_init(void);

void screen_sleep_enter(void);
void screen_sleep_exit(void);
bool screen_is_sleeping(void);

void screen_set_brightness(u16 brightness);
u16  screen_get_brightness(void);

#ifdef __cplusplus
}
#endif

#endif /* AIC_SCREEN_CTL_H */
