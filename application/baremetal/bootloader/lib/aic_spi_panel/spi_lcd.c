/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <aic_core.h>
#include <aic_hal.h>
#include <hal_qspi.h>
#ifdef AIC_DMA_DRV
#include <aic_dma_id.h>
#endif
#include <aic_common.h>
#include <aic_errno.h>
#include <string.h>
#include <console.h>
#include "spi_lcd.h"
#include "spi_lcd_panel.h"

#define SPI_LCD_FB_BPP           16

static qspi_master_handle spi_handle;
#ifndef SPI_LCD_QSPI_MODE
static u32 rs_g;
static u32 rs_p;
#endif
static bool inited;
static struct aicfb_screeninfo lcd_info;

static void spi_lcd_set_pixel_native(struct aicfb_screeninfo *info,
                                     unsigned int nx, unsigned int ny,
                                     u8 red, u8 green, u8 blue)
{
    uint16_t *px;
    unsigned int bpp = info->bits_per_pixel / 8;

    px = (uint16_t *)(info->framebuffer + ny * info->stride + nx * bpp);
    *px = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
}

static void spi_lcd_fill_rect_native(struct aicfb_screeninfo *info,
                                     unsigned int x, unsigned int y,
                                     unsigned int w, unsigned int h,
                                     u8 red, u8 green, u8 blue)
{
    unsigned int row, col;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++)
            spi_lcd_set_pixel_native(info, x + col, y + row,
                                     red, green, blue);
    }
}

void spi_lcd_fill_rect(struct aicfb_screeninfo *info,
                       unsigned int x, unsigned int y,
                       unsigned int w, unsigned int h,
                       u8 red, u8 green, u8 blue)
{
    if (!w || !h)
        return;

    spi_lcd_fill_rect_native(info, x, y, w, h, red, green, blue);
    aicos_dcache_clean_range((unsigned long *)info->framebuffer, info->smem_len);
    spi_lcd_flush_region(x, y, w, h, info->framebuffer);
}

#ifndef SPI_LCD_QSPI_MODE
static void spi_lcd_rs_low(void)
{
    hal_gpio_clr_output(rs_g, rs_p);
}

static void spi_lcd_rs_high(void)
{
    hal_gpio_set_output(rs_g, rs_p);
}
#endif

static void spi_lcd_cs_enable(void)
{
    hal_qspi_master_set_cs(&spi_handle, 0, true);
}

static void spi_lcd_cs_disable(void)
{
    hal_qspi_master_set_cs(&spi_handle, 0, false);
}

static int spi_lcd_spi_transfer(const u8 *tx_data, u32 len)
{
    struct qspi_transfer t;
    int ret;

    t.tx_data = (u8 *)tx_data;
    t.rx_data = NULL;
    t.data_len = len;

    ret = hal_qspi_master_transfer_sync(&spi_handle, &t);
    if (ret < 0) {
        pr_err("SPI LCD transfer failed, ret %d\n", ret);
        return ret;
    }
    return 0;
}

static int spi_lcd_qspi_init(void)
{
    struct qspi_master_config cfg;
#ifndef SPI_LCD_QSPI_MODE
    long pin;
#endif

    int ret;

    memset(&cfg, 0, sizeof(cfg));
    cfg.idx = SPI_LCD_BUS_IDX;
#ifdef SPI_LCD_CLK_IN_HZ
    cfg.clk_in_hz = SPI_LCD_CLK_IN_HZ;
#else
    cfg.clk_in_hz = 200000000;
#endif
#if SPI_LCD_BUS_IDX == 0
    cfg.clk_id = CLK_QSPI0;
#elif SPI_LCD_BUS_IDX == 1
    cfg.clk_id = CLK_QSPI1;
#elif SPI_LCD_BUS_IDX == 2
    cfg.clk_id = CLK_QSPI2;
#elif SPI_LCD_BUS_IDX == 3
    cfg.clk_id = CLK_QSPI3;
#else
    cfg.clk_id = CLK_QSPI1;
#endif
    cfg.bit_mode = false;
    cfg.cpol = HAL_QSPI_CPOL_ACTIVE_HIGH;
    cfg.cpha = HAL_QSPI_CPHA_FIRST_EDGE;
    cfg.cs_polarity = HAL_QSPI_CS_POL_VALID_LOW;
    cfg.max_hz = SPI_LCD_MAX_HZ;

    ret = hal_qspi_master_init(&spi_handle, &cfg);
    if (ret) {
        pr_err("hal_qspi_master_init failed, ret %d\n", ret);
        return ret;
    }

    /* Set the actual bus clock frequency (init only sets source clock) */
    hal_qspi_master_set_bus_freq(&spi_handle, SPI_LCD_MAX_HZ);

    /* Configure DMA for this QSPI bus (v1x dynamic DMA needs port_id) */
#ifdef AIC_DMA_DRV
    {
        struct qspi_master_dma_config dmacfg;

        memset(&dmacfg, 0, sizeof(dmacfg));
#if SPI_LCD_BUS_IDX == 0
        dmacfg.port_id = DMA_ID_SPI0;
#elif SPI_LCD_BUS_IDX == 1
        dmacfg.port_id = DMA_ID_SPI1;
#elif SPI_LCD_BUS_IDX == 2
        dmacfg.port_id = DMA_ID_SPI2;
#elif SPI_LCD_BUS_IDX == 3
        dmacfg.port_id = DMA_ID_SPI3;
#else
        dmacfg.port_id = DMA_ID_SPI1;
#endif
        hal_qspi_master_dma_config(&spi_handle, &dmacfg);
    }
#endif

    hal_qspi_master_set_bus_width(&spi_handle, HAL_QSPI_BUS_WIDTH_SINGLE);

#ifndef SPI_LCD_QSPI_MODE
    /* Configure RS pin (not used in QSPI 4-wire mode) */
    pin = hal_gpio_name2pin(SPI_LCD_RS_PIN);
    if (pin < 0) {
        pr_err("Invalid RS pin: %s\n", SPI_LCD_RS_PIN);
        return -EINVAL;
    }
    rs_g = GPIO_GROUP(pin);
    rs_p = GPIO_GROUP_PIN(pin);
    hal_gpio_set_func(rs_g, rs_p, 1);
    hal_gpio_set_bias_pull(rs_g, rs_p, 0);
    hal_gpio_set_drive_strength(rs_g, rs_p, 3);
    hal_gpio_direction_output(rs_g, rs_p);
    hal_gpio_clr_output(rs_g, rs_p);
#endif

    return 0;
}

#ifdef SPI_LCD_QSPI_MODE
static u32 qspi_data_bus_width(u32 data_lines)
{
    return (data_lines >= 4) ? HAL_QSPI_BUS_WIDTH_QUAD :
           (data_lines >= 2) ? HAL_QSPI_BUS_WIDTH_DUAL :
                               HAL_QSPI_BUS_WIDTH_SINGLE;
}
#endif

static void spi_lcd_rgb565_swap(u8 *data, unsigned int len)
{
    unsigned int i;
    u16 *px = (u16 *)data;

    for (i = 0; i < len / 2; i++)
        px[i] = (px[i] << 8) | (px[i] >> 8);
}

int spi_lcd_write_cmd(unsigned int cmd, unsigned int len, const u8 *data)
{
#ifdef SPI_LCD_QSPI_MODE
    /* LVGL-compatible: (cmd_content & 0xFFFF00FF) | (cmd << 8) */
    u32 addr = (panel_info.cmd_content & 0xFFFF00FF) | (cmd << 8);
    u8 addr_buf[4];

    addr_buf[0] = (addr >> 24) & 0xFF;
    addr_buf[1] = (addr >> 16) & 0xFF;
    addr_buf[2] = (addr >>  8) & 0xFF;
    addr_buf[3] =  addr        & 0xFF;

    spi_lcd_cs_enable();
    hal_qspi_master_set_bus_width(&spi_handle, HAL_QSPI_BUS_WIDTH_SINGLE);
    spi_lcd_spi_transfer(addr_buf, 4);

    if (len) {
        hal_qspi_master_set_bus_width(&spi_handle,
                        qspi_data_bus_width(panel_info.qspi_lines));
        spi_lcd_spi_transfer(data, len);
    }
    spi_lcd_cs_disable();
#else
    u8 cmd_byte = (u8)cmd;

    spi_lcd_cs_enable();

    spi_lcd_rs_low();
    spi_lcd_spi_transfer(&cmd_byte, 1);

    if (len) {
        spi_lcd_rs_high();
        spi_lcd_spi_transfer(data, len);
    }

    spi_lcd_cs_disable();
#endif
    return 0;
}

int spi_lcd_set_window(unsigned int x0, unsigned int y0,
                       unsigned int x1, unsigned int y1)
{
    u8 column[4], row[4];
    unsigned int px0 = x0 + panel_info.x_offset;
    unsigned int py0 = y0 + panel_info.y_offset;
    unsigned int px1 = x1 + panel_info.x_offset;
    unsigned int py1 = y1 + panel_info.y_offset;

    column[0] = (px0 >> 8) & 0xFF;
    column[1] = px0 & 0xFF;
    column[2] = (px1 >> 8) & 0xFF;
    column[3] = px1 & 0xFF;

    row[0] = (py0 >> 8) & 0xFF;
    row[1] = py0 & 0xFF;
    row[2] = (py1 >> 8) & 0xFF;
    row[3] = py1 & 0xFF;

    spi_lcd_write_cmd(0x2A, sizeof(column), column);
    spi_lcd_write_cmd(0x2B, sizeof(row), row);

    return 0;
}

int spi_lcd_flush(u8 *data, unsigned int len)
{
#ifdef SPI_LCD_QSPI_MODE
    u32 addr = panel_info.flush_content;
    u8 addr_buf[4];

    spi_lcd_rgb565_swap(data, len);

    addr_buf[0] = (addr >> 24) & 0xFF;
    addr_buf[1] = (addr >> 16) & 0xFF;
    addr_buf[2] = (addr >>  8) & 0xFF;
    addr_buf[3] =  addr        & 0xFF;

    spi_lcd_cs_enable();
    hal_qspi_master_set_bus_width(&spi_handle, HAL_QSPI_BUS_WIDTH_SINGLE);
    spi_lcd_spi_transfer(addr_buf, 4);

    hal_qspi_master_set_bus_width(&spi_handle,
                    qspi_data_bus_width(panel_info.data_lines));
    spi_lcd_spi_transfer(data, len);
    spi_lcd_cs_disable();
#else
    u8 cmd = 0x2C;

    spi_lcd_rgb565_swap(data, len);

    spi_lcd_cs_enable();

    spi_lcd_rs_low();
    spi_lcd_spi_transfer(&cmd, 1);

    spi_lcd_rs_high();
    spi_lcd_spi_transfer(data, len);

    spi_lcd_cs_disable();
#endif
    return 0;
}

void spi_lcd_flush_region(unsigned int x, unsigned int y,
                          unsigned int w, unsigned int h,
                          u8 *data)
{
    unsigned int fb_stride = lcd_info.stride;
    unsigned int bpp = lcd_info.bits_per_pixel / 8;
    unsigned int i;

    spi_lcd_set_window(x, y, x + w - 1, y + h - 1);
    spi_lcd_cs_enable();

#ifdef SPI_LCD_QSPI_MODE
    u32 addr = panel_info.flush_content;
    u8 addr_buf[4];

    addr_buf[0] = (addr >> 24) & 0xFF;
    addr_buf[1] = (addr >> 16) & 0xFF;
    addr_buf[2] = (addr >>  8) & 0xFF;
    addr_buf[3] =  addr        & 0xFF;

    hal_qspi_master_set_bus_width(&spi_handle, HAL_QSPI_BUS_WIDTH_SINGLE);
    spi_lcd_spi_transfer(addr_buf, 4);

    hal_qspi_master_set_bus_width(&spi_handle,
                    qspi_data_bus_width(panel_info.data_lines));
    for (i = 0; i < h; i++) {
        u8 *line = data + (y + i) * fb_stride + x * bpp;
        spi_lcd_rgb565_swap(line, w * bpp);
        spi_lcd_spi_transfer(line, w * bpp);
    }
#else
    u8 cmd = 0x2C;

    spi_lcd_rs_low();
    spi_lcd_spi_transfer(&cmd, 1);

    spi_lcd_rs_high();
    for (i = 0; i < h; i++) {
        u8 *line = data + (y + i) * fb_stride + x * bpp;
        spi_lcd_rgb565_swap(line, w * bpp);
        spi_lcd_spi_transfer(line, w * bpp);
    }
#endif
    spi_lcd_cs_disable();

}

void spi_lcd_wait_completion(void)
{
    /* Synchronous transfers - nothing to wait for */
}

void spi_lcd_get_info(struct aicfb_screeninfo *info)
{
    memcpy(info, &lcd_info, sizeof(*info));
}

static void spi_lcd_reset(void)
{
    long pin;
    u32 g, p;

    pin = hal_gpio_name2pin(SPI_LCD_RST_PIN);
    if (pin < 0)
        return;

    g = GPIO_GROUP(pin);
    p = GPIO_GROUP_PIN(pin);

    hal_gpio_set_func(g, p, 1);
    hal_gpio_set_bias_pull(g, p, 0);
    hal_gpio_set_drive_strength(g, p, 3);
    hal_gpio_direction_output(g, p);

    hal_gpio_clr_output(g, p);
    aic_mdelay(10);
    hal_gpio_set_output(g, p);
    aic_mdelay(80);
}

void spi_lcd_backlight_on(void)
{
    long pin;
    u32 g, p;

    pin = hal_gpio_name2pin(SPI_LCD_BL_PIN);
    if (pin < 0)
        return;

    g = GPIO_GROUP(pin);
    p = GPIO_GROUP_PIN(pin);

    hal_gpio_set_func(g, p, 1);
    hal_gpio_set_bias_pull(g, p, 0);
    hal_gpio_set_drive_strength(g, p, 3);
    hal_gpio_direction_output(g, p);
    hal_gpio_set_output(g, p);
}

int spi_lcd_init(void)
{
    unsigned int bpp, fb_size;
    int ret;

    if (inited)
        return 0;

    ret = spi_lcd_qspi_init();
    if (ret) {
        printf("SPI LCD: qspi_init failed, ret=%d\n", ret);
        return ret;
    }

    printf("SPI LCD: panel = %s, %d x %d\n", panel_info.name,
           panel_info.width, panel_info.height);

    spi_lcd_reset();
    panel_enable();

    bpp = panel_info.bpp / 8;
    fb_size = panel_info.width * panel_info.height * bpp;

    lcd_info.width = panel_info.width;
    lcd_info.height = panel_info.height;
    lcd_info.bits_per_pixel = panel_info.bpp;
    lcd_info.stride = panel_info.width * bpp;
    lcd_info.smem_len = fb_size;
    lcd_info.format = MPP_FMT_RGB_565;
    lcd_info.framebuffer = aicos_malloc_align(0, fb_size, CACHE_LINE_SIZE);
    if (!lcd_info.framebuffer) {
        printf("SPI LCD: framebuffer alloc failed\n");
        return -ENOMEM;
    }

    inited = true;
    return 0;
}
