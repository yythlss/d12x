/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SPI_LCD_H__
#define __SPI_LCD_H__

#include <aic_core.h>
#include <artinchip_fb.h>

int spi_lcd_init(void);
void spi_lcd_get_info(struct aicfb_screeninfo *info);
int spi_lcd_write_cmd(unsigned int cmd, unsigned int len, const u8 *data);
int spi_lcd_set_window(unsigned int x0, unsigned int y0,
                       unsigned int x1, unsigned int y1);
int spi_lcd_flush(u8 *data, unsigned int len);
void spi_lcd_flush_region(unsigned int x, unsigned int y,
                          unsigned int w, unsigned int h,
                          u8 *data);
void spi_lcd_wait_completion(void);

void spi_lcd_backlight_on(void);

void spi_lcd_fill_rect(struct aicfb_screeninfo *info,
                       unsigned int x, unsigned int y,
                       unsigned int w, unsigned int h,
                       u8 red, u8 green, u8 blue);

#define spi_lcd_write_seq(cmd, seq...)              \
    do {                                            \
        static const u8 d[] = { seq };              \
        spi_lcd_write_cmd(cmd, ARRAY_SIZE(d), d);   \
    } while (0)

#endif /* __SPI_LCD_H__ */
