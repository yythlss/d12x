/*
 * Copyright (C) 2025-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Huahui <huahui.mai@artinchip.com>
 */

#include <string.h>
#include <rtdevice.h>
#include <aic_core.h>
#include <drv_qspi.h>

#include "lvgl.h"
#include <mpp_fb.h>
#include <mpp_ge.h>

#include "aic_spi_lcd.h"
#include "lv_spi_panel.h"

struct lv_spi_config {
    const char *panel;
    const aic_spi_lcd_cfg_t spi_cfg;
    const char *bl_pin;
    const char *te_pin;
    bool bl_low_active;
};

struct lv_spi_dev {
    const struct lv_spi_config *config;
    aic_spi_lcd_dev_t *dev;

    /* LVGL Draw Buffer */
    unsigned char *data;
    unsigned char *draw_buf[2];   /* the two CMA buffers handed to LVGL */
    unsigned int fb_size;
    unsigned int panel_w;
    unsigned int panel_h;

    /* SPI transfer buffers (ping-pong draw targets; each frame waits DMA done) */
    unsigned char *tx_buf[2];
    unsigned char tx_wr_idx;
    unsigned int tx_len;
    struct mpp_ge *ge2d_dev;

    int te_pin;
    aicos_wqueue_t te_queue;
    aicos_sem_t display_sem;
    aicos_sem_t sync_ready;
    bool primed;       /* at least one SPI transfer has been issued */

    bool bl_en;
    bool power_on;
    bool blit_owned;   /* fed by lv_spi_blit; LVGL flush is skipped */
    bool disp_thread_on; /* LVGL path only; blit panels skip this thread */

    uint32_t spi_start; /* timestamp when async SPI flush was issued */
    lv_disp_t *disp;
    unsigned int id;
};

void lv_spi_flush(lv_disp_t *disp, u8 *data);

/*
 * Direct image-buffer-to-panel path, bypassing LVGL. "id" is 0-based
 * (id + 1 == SPIn). The buffer must match the panel format (RGB565) and
 * resolution; query the expected size with lv_spi_get_panel_info().
 *
 * The buffer is copied+byte-swapped into an internal tx_buf, then sent via
 * async SPI DMA.
 */
int lv_spi_blit(int id, void *buf);

/*
 * Reserve a display for the direct blit path: LVGL stops flushing it, so the
 * panel has a single producer (the lv_spi_blit() caller). Call before LVGL
 * starts refreshing.
 */
int lv_spi_blit_claim(int id);
int lv_spi_is_blit_owned(int id);

/*
 * Hand back the two CMA draw buffers already allocated for this display so a
 * blit producer can reuse them as ping-pong frames instead of allocating its own.
 */
int lv_spi_get_blit_buffers(int id, void **buf0, void **buf1);

int lv_spi_get_panel_info(int id, unsigned int *width, unsigned int *height,
                          unsigned int *buf_size);

int lv_spi_get_phys_panel_info(int id, unsigned int *width, unsigned int *height);

void lv_spi_write_buffer(struct lv_spi_dev *spi_dev,
                         unsigned int cmd, unsigned int len, const u8 *data);

int lv_spi_display_init(int use_frame_buffer);

#define lv_spi_write_seq(dev, cmd, seq...)                  \
    do {                                                    \
        static const u8 d[] = { seq };                      \
        lv_spi_write_buffer(dev, cmd, ARRAY_SIZE(d), d);    \
    } while(0);

void lv_spi_screen_enable(struct lv_spi_dev *spi_dev);

void lv_qspi_disp_mode_init(struct lv_spi_dev *spi_dev);

struct disp_timing {
	uint32_t lvgl_ms;
	uint32_t sync_wait_ms;
	uint32_t ge2d_ms;
	uint32_t spi_ms;
	uint32_t frame_count;
};

void lv_spi_get_timing(int id, struct disp_timing *out);

uint32_t lv_spi_get_fps(int id);
