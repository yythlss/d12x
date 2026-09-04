/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SPI_LCD_PANEL_H__
#define __SPI_LCD_PANEL_H__

#include <aic_common.h>

struct spi_lcd_panel_info {
    const char *name;
    u16 width;
    u16 height;
    u16 x_offset;
    u16 y_offset;
    u8  bpp;
    u32 cmd_content;
    u32 flush_content;
    u32 qspi_lines;
    u32 data_lines;
};

extern const struct spi_lcd_panel_info panel_info;
void panel_enable(void);

#endif /* __SPI_LCD_PANEL_H__ */
