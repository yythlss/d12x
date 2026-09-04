/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "lv_aic_spi.h"
#include "lv_spi_panel.h"

static struct lv_spi_panel_driver *panels[] = {
#if defined(LV_SPI_ST7789)
    &lv_spi_st7789,
#endif
#if defined(LV_SPI_ST77912)
    &lv_spi_st77912,
#endif
#if defined(LV_SPI_ST77916)
    &lv_spi_st77916,
#endif
#if defined(LV_SPI_ST77903)
    &lv_spi_st77903,
#endif
#if defined(LV_SPI_GC9D01N)
    &lv_spi_gc9d01n,
#endif
#if defined(LV_SPI_GC9A01)
    &lv_spi_gc9a01,
#endif
};

struct lv_spi_panel_driver *lv_spi_find_panel(const char *name)
{
    int i;

    if (!name)
        return NULL;

    for (i = 0; i < ARRAY_SIZE(panels); i++) {
        if (!strcmp(panels[i]->name, name))
            break;
    }

    if (i >= ARRAY_SIZE(panels))
        return NULL;

    return panels[i];
}

void lv_spi_fill_lcd_cfg(const struct lv_spi_config *cfg, aic_spi_lcd_cfg_t *spi_cfg)
{
    struct lv_spi_panel_driver *panel;

    if (!cfg || !spi_cfg)
        return;

    *spi_cfg = cfg->spi_cfg;
    if (spi_cfg->bus_width != 4)
        return;

    panel = lv_spi_find_panel(cfg->panel);
    if (panel && panel->qspi_cfg)
        spi_cfg->qspi_cfg = panel->qspi_cfg;
}

void lv_spi_resolve_geometry(const struct lv_spi_config *cfg, unsigned int fb_w,
                             unsigned int fb_h, unsigned int *width,
                             unsigned int *height)
{
    struct lv_spi_panel_driver *p;

    if (!cfg || !width || !height)
        return;

    p = lv_spi_find_panel(cfg->panel);
    if (p) {
        *width  = p->width;
        *height = p->height;
    }

    if (!*width)
        *width = fb_w;
    if (!*height)
        *height = fb_h;
}

void lv_spi_panel_enable(struct lv_spi_dev *dev)
{
    struct lv_spi_panel_driver *p;

    if (!dev || !dev->config)
        return;

    p = lv_spi_find_panel(dev->config->panel);
    if (!p) {
        LV_LOG_ERROR("unsupported panel %s",
                     dev->config->panel ? dev->config->panel : "(null)");
        return;
    }

    p->enable(dev);
}
