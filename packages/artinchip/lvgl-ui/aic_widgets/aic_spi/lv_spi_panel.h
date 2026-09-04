/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LV_SPI_PANEL_H
#define LV_SPI_PANEL_H

#include "aic_spi_lcd.h"

struct lv_spi_dev;
struct lv_spi_config;
struct qspi_cfg;

typedef void (*lv_spi_panel_enable_t)(struct lv_spi_dev *dev);

struct lv_spi_panel_driver {
    const char *name;
    lv_spi_panel_enable_t enable;
    unsigned int width;
    unsigned int height;
    const struct qspi_cfg *qspi_cfg;
};

extern struct lv_spi_panel_driver lv_spi_st7789;
extern struct lv_spi_panel_driver lv_spi_st77912;
extern struct lv_spi_panel_driver lv_spi_st77916;
extern struct lv_spi_panel_driver lv_spi_st77903;
extern struct lv_spi_panel_driver lv_spi_gc9d01n;
extern struct lv_spi_panel_driver lv_spi_gc9a01;

struct lv_spi_panel_driver *lv_spi_find_panel(const char *name);

void lv_spi_fill_lcd_cfg(const struct lv_spi_config *cfg, aic_spi_lcd_cfg_t *spi_cfg);

void lv_spi_resolve_geometry(const struct lv_spi_config *cfg, unsigned int fb_w,
                             unsigned int fb_h, unsigned int *width,
                             unsigned int *height);

void lv_spi_panel_enable(struct lv_spi_dev *dev);

#endif /* LV_SPI_PANEL_H */
