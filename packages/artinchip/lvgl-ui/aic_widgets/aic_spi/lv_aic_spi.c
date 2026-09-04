/*
 * Copyright (C) 2025-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Huahui <huahui.mai@artinchip.com>
 *           Zhengcun <zhengcun.chen@artinchip.com>
 */

#include <stdatomic.h>
#include "lv_aic_spi.h"
#include "aic_iopoll.h"

#define TE_TIMEOUT_MS   100

#define ALIGN_8B(x) (((x) + (7)) & ~(7))

#if defined(LV_SPI_HALF_RES_RENDER)
#define LV_SPI_DRAW_DIV  2
#else
#define LV_SPI_DRAW_DIV  1
#endif

/* ---- QSPI panel (4-lane; qspi_cfg comes from panel driver) ---- */
#define QSPI_PANEL_CFG(bus, panel_name, bl, hz) \
    [bus - 1] = { \
        .panel = panel_name, \
        .spi_cfg = { \
            .bus_name  = "qspi" #bus, \
            .dev_name  = "qspi" #bus "dev", \
            .max_hz    = hz, \
            .mode      = RT_SPI_MODE_0 | RT_SPI_MSB, \
            .bus_width = 4, \
        }, \
        .bl_pin        = bl, \
        .bl_low_active = false, \
    }

/* ---- SPI panel (1-lane, with rs_pin) ---- */
#define SPI_PANEL_CFG(bus, panel_name, rs, bl, hz) \
    [bus - 1] = { \
        .panel = panel_name, \
        .spi_cfg = { \
            .bus_name  = "spi" #bus, \
            .dev_name  = "spi" #bus "dev", \
            .max_hz    = hz, \
            .mode      = RT_SPI_MODE_0 | RT_SPI_MSB, \
            .bus_width = 1, \
            .rs_pin    = rs, \
        }, \
        .bl_pin        = bl, \
        .bl_low_active = false, \
    }

#if defined(AIC_LVGL_FIVE_DISP_DEMO)
static const struct lv_spi_config spi_configs[] = {
    QSPI_PANEL_CFG(1, "st77916", "PD.7", 100000000),
    QSPI_PANEL_CFG(2, "st77916", "PD.7", 100000000),
    SPI_PANEL_CFG (3, "gc9a01", "PD.17", "PD.7", 100000000),
    SPI_PANEL_CFG (4, "gc9a01", "PD.23", "PD.7", 100000000),
    SPI_PANEL_CFG (5, "gc9a01", "PD.27", "PD.7", 100000000),
};
#elif defined LV_SPI_BUS_WIDTH_1
static const struct lv_spi_config spi_configs[] = {
    [0] = {
        .panel = LV_SPI_PANEL_NAME,
        .spi_cfg = {
            .bus_name = "spi1",
            .dev_name = "spi1dev",
            .max_hz = 25000000,
            .mode = RT_SPI_MODE_0 | RT_SPI_MSB,
            .bus_width = 1,
            .rs_pin = "PD.11",
        },
        .bl_pin = "PE.12",
        .bl_low_active = false,
    },
};
#elif defined LV_SPI_BUS_WIDTH_4
static const struct lv_spi_config spi_configs[] = {
    [0] = {
        .panel = LV_SPI_PANEL_NAME,
        .spi_cfg = {
            .bus_name = "qspi1",
            .dev_name = "qspi1dev",
            .max_hz = 50000000,
            .mode = RT_SPI_MODE_0 | RT_SPI_MSB,
            .bus_width = 4,
        },
        .bl_pin = "PE.12",
        .bl_low_active = false,
    },
    [1] = {
        .panel = LV_SPI_PANEL_NAME,
        .spi_cfg = {
            .bus_name = "qspi2",
            .dev_name = "qspi2dev",
            .max_hz = 50000000,
            .mode = RT_SPI_MODE_0 | RT_SPI_MSB,
            .bus_width = 4,
        },
        .bl_pin = "PE.12",
        .bl_low_active = false,
    },
};
#endif /* LV_SPI_BUS_WIDTH_4 */

#define LV_SPI_DEV_NUM    ARRAY_SIZE(spi_configs)

/* Array to hold all SPI display devices */
static struct lv_spi_dev **g_spi_devs = NULL;

static struct disp_timing g_timing[LV_SPI_DEV_NUM];
static uint32_t g_render_ts;   /* cross-display LVGL render tracking */

/* ---- per-panel FPS tracking (shared by LVGL and blit paths) ---- */

static uint32_t g_spi_fps_cnt[LV_SPI_DEV_NUM];
static uint32_t g_spi_fps_val[LV_SPI_DEV_NUM];

static void spi_fps_tick(int id)
{
    if (id >= 0 && id < LV_SPI_DEV_NUM)
        g_spi_fps_cnt[id]++;
}

static void spi_fps_timer_cb(lv_timer_t *t)
{
    static uint32_t last_tick;
    uint32_t dt = lv_tick_elaps(last_tick);
    int i;

    last_tick = lv_tick_get();
    if (dt == 0U)
        dt = 1U;

    for (i = 0; i < LV_SPI_DEV_NUM; i++) {
        g_spi_fps_val[i] = (uint32_t)((uint64_t)g_spi_fps_cnt[i] * 1000U / dt);
        g_spi_fps_cnt[i] = 0;
    }
}

uint32_t lv_spi_get_fps(int id)
{
    if (id >= 0 && id < LV_SPI_DEV_NUM)
        return g_spi_fps_val[id];
    return 0;
}

void lv_spi_get_timing(int id, struct disp_timing *out)
{
	if (id >= 0 && id < LV_SPI_DEV_NUM && out) {
		*out = g_timing[id];
		memset(&g_timing[id], 0, sizeof(g_timing[id]));
	}
}

static struct lv_spi_dev *lv_find_spi_dev(lv_display_t *disp)
{
    int i;

    for (i = 0; i < LV_SPI_DEV_NUM; i++) {
        if (g_spi_devs[i]->disp == disp) {
            return g_spi_devs[i];
        }
    }

    return NULL;
}

void lv_spi_write_buffer(struct lv_spi_dev *spi_dev, unsigned int cmd,
                         unsigned int len, const u8 *data)
{
    aic_spi_lcd_write_cmd(spi_dev->dev, cmd, len, data);
}

/*
 * Copy LVGL RGB565 framebuffer to the SPI tx buffer while swapping each
 * pixel's byte order (little-endian -> panel big-endian) in a single pass.
 * Replaces a separate memcpy + in-place swap (two full-frame passes) on the
 * fast path, halving the CPU memory traffic of this stage.
 */
__attribute__((unused)) static void aic_copy_swap_rgb565(void *dst, const void *src, uint32_t px_cnt)
{
    const uint32_t *s = src;
    uint32_t *d = dst;
    uint32_t cnt = px_cnt >> 1; /* two RGB565 pixels per u32 */

    while (cnt >= 8) {
        d[0] = ((s[0] & 0xff00ff00) >> 8) | ((s[0] & 0x00ff00ff) << 8);
        d[1] = ((s[1] & 0xff00ff00) >> 8) | ((s[1] & 0x00ff00ff) << 8);
        d[2] = ((s[2] & 0xff00ff00) >> 8) | ((s[2] & 0x00ff00ff) << 8);
        d[3] = ((s[3] & 0xff00ff00) >> 8) | ((s[3] & 0x00ff00ff) << 8);
        d[4] = ((s[4] & 0xff00ff00) >> 8) | ((s[4] & 0x00ff00ff) << 8);
        d[5] = ((s[5] & 0xff00ff00) >> 8) | ((s[5] & 0x00ff00ff) << 8);
        d[6] = ((s[6] & 0xff00ff00) >> 8) | ((s[6] & 0x00ff00ff) << 8);
        d[7] = ((s[7] & 0xff00ff00) >> 8) | ((s[7] & 0x00ff00ff) << 8);
        s += 8;
        d += 8;
        cnt -= 8;
    }

    while (cnt--) {
        *d = ((*s & 0xff00ff00) >> 8) | ((*s & 0x00ff00ff) << 8);
        d++;
        s++;
    }

    if (px_cnt & 0x1) {
        const uint16_t *s16 = src;
        uint16_t *d16 = dst;
        uint32_t e = px_cnt - 1;
        d16[e] = ((s16[e] & 0xff00) >> 8) | ((s16[e] & 0x00ff) << 8);
    }
}

static void lv_disp_data_blt(struct lv_spi_dev *spi_dev, unsigned char *tx_dest)
{
    extern enum mpp_pixel_format lv_fmt_to_mpp_fmt(lv_color_format_t cf);
    struct mpp_ge *ge2d_dev = spi_dev->ge2d_dev;
    lv_disp_t *disp = spi_dev->disp;
    uint32_t src_buf = (uint32_t)(ulong)spi_dev->data;
    uint32_t dest_buf = (uint32_t)(ulong)tx_dest;
    int32_t src_width = lv_display_get_horizontal_resolution(disp);
    int32_t src_height = lv_display_get_vertical_resolution(disp);
    int32_t panel_w = (int32_t)spi_dev->panel_w;
    int32_t panel_h = (int32_t)spi_dev->panel_h;
    lv_color_format_t cf = lv_display_get_color_format(disp);
    int32_t src_stride = lv_draw_buf_width_to_stride(src_width, cf);
    int32_t dst_stride = (int32_t)ALIGN_8B(panel_w * 2);
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    enum mpp_pixel_format fmt = lv_fmt_to_mpp_fmt(cf);
    bool upscale = (panel_w != src_width) || (panel_h != src_height);
    int32_t dst_w;
    int32_t dst_h;

    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        dst_w = panel_w;
        dst_h = panel_h;
    } else {
        dst_w = panel_h;
        dst_h = panel_w;
    }

    /*
     * Fast path: no upscale, no rotation, RGB565 → skip GE2D, copy + byte-swap
     * in one pass. The single-pass copy treats both buffers as contiguous, so it
     * is only valid when neither source nor destination carries row padding.
     * Fall back to the GE2D path (which honors strides / scaler) otherwise.
     */
    if (!upscale && rotation == LV_DISPLAY_ROTATION_0 && fmt == MPP_FMT_RGB_565 &&
        src_stride == src_width * 2 && dst_stride == src_width * 2) {
        aic_copy_swap_rgb565((void *)(ulong)dest_buf, (void *)(ulong)src_buf,
                             (uint32_t)(src_width * src_height));
    } else {
        struct ge_bitblt blt = { 0 };

        blt.src_buf.buf_type    = MPP_PHY_ADDR;
        blt.src_buf.phy_addr[0] = src_buf;
        blt.src_buf.stride[0]   = src_stride;
        blt.src_buf.size.width  = src_width;
        blt.src_buf.size.height = src_height;
        blt.src_buf.format      = fmt;

        blt.dst_buf.buf_type    = MPP_PHY_ADDR;
        blt.dst_buf.phy_addr[0] = dest_buf;
        blt.dst_buf.stride[0]   = dst_stride;
        blt.dst_buf.format      = MPP_FMT_RGB_565;
        blt.dst_buf.size.width  = dst_w;
        blt.dst_buf.size.height = dst_h;
        blt.ctrl.dither_en = 1;

        switch (rotation) {
        case LV_DISPLAY_ROTATION_0:
            blt.ctrl.flags = MPP_ROTATION_0;
            break;
        case LV_DISPLAY_ROTATION_90:
            /* LV_DISP_ROT_90 means display rotate 90 degrees counterclockwise,
             * so set degree to MPP_ROTATION_270
            */
            blt.ctrl.flags = MPP_ROTATION_270;
            break;
        case LV_DISPLAY_ROTATION_180:
            blt.ctrl.flags = MPP_ROTATION_180;
            break;
        case LV_DISPLAY_ROTATION_270:
            blt.ctrl.flags = MPP_ROTATION_90;
            break;
        default:
            break;
        }

        int ret = mpp_ge_bitblt(ge2d_dev, &blt);
        if (ret < 0) {
            LV_LOG_ERROR("mpp ge bitblt fail\n");
            return;
        }

        ret = mpp_ge_emit(ge2d_dev);
        if (ret < 0) {
            LV_LOG_ERROR("mpp ge emit fail\n");
            return;
        }

        ret = mpp_ge_sync(ge2d_dev);
        if (ret < 0) {
            LV_LOG_ERROR("mpp ge sync fail\n");
            return;
        }

        /* GE2D output is native RGB565; swap to panel byte order in place */
        lv_draw_sw_rgb565_swap(tx_dest, (uint32_t)(panel_w * panel_h));
    }

    aicos_dcache_clean_invalid_range((ulong *)tx_dest,
                                     (ulong)ALIGN_UP(spi_dev->tx_len, CACHE_LINE_SIZE));
}

static void te_input_irq_handler(void *args)
{
    struct lv_spi_dev *spi_dev = args;

    aicos_wqueue_wakeup(spi_dev->te_queue);
}

static int te_input_pin_cfg(struct lv_spi_dev *spi_dev)
{
    if (spi_dev->te_queue && spi_dev->te_pin) {
        rt_pin_mode(spi_dev->te_pin, PIN_MODE_INPUT_PULLUP);
        rt_pin_attach_irq(spi_dev->te_pin, PIN_IRQ_MODE_RISING_FALLING,
                        te_input_irq_handler, spi_dev);

        rt_pin_irq_enable(spi_dev->te_pin, PIN_IRQ_ENABLE);
    }

    return 0;
}

static void spi_dev_backlight_on(struct lv_spi_dev *spi_dev)
{
    const struct lv_spi_config *config = spi_dev->config;

    if (spi_dev->bl_en)
        return;

    aic_spi_lcd_backlight_gpio_enable(config->bl_pin, config->bl_low_active, true);
    spi_dev->bl_en = true;
    te_input_pin_cfg(spi_dev);
}

static void disp_thread(void *arg)
{
    struct lv_spi_dev *spi_dev = arg;
    uint32_t spi_start = 0;
    bool first = true;

    while (1) {
        uint32_t t0, t1;
        unsigned char *tx_dest;
        unsigned int wr;

        aicos_sem_take(spi_dev->display_sem, AICOS_WAIT_FOREVER);

        wr = spi_dev->tx_wr_idx;
        tx_dest = spi_dev->tx_buf[wr];

        t0 = lv_tick_get();
        lv_disp_data_blt(spi_dev, tx_dest);
        t1 = lv_tick_get();
        g_timing[spi_dev->id].ge2d_ms += t1 - t0;

        aicos_sem_give(spi_dev->sync_ready);

        if (!first) {
            aic_spi_lcd_wait_completion(spi_dev->dev);
            t1 = lv_tick_get();
            g_timing[spi_dev->id].spi_ms += t1 - spi_start;
        }
        first = false;

        if (spi_dev->bl_en && spi_dev->te_queue) {
            int ret = aicos_wqueue_wait(spi_dev->te_queue, TE_TIMEOUT_MS);
            if (ret < 0)
                LV_LOG_ERROR("SPI wait TE irq timeout, ret: %d\n", ret);
        }

        spi_start = lv_tick_get();
        aic_spi_lcd_flush(spi_dev->dev, tx_dest, spi_dev->tx_len);
        spi_dev->tx_wr_idx ^= 1U;

        spi_dev_backlight_on(spi_dev);

        g_timing[spi_dev->id].frame_count++;

        spi_fps_tick(spi_dev->id);
    }
}

static struct lv_spi_dev *lv_spi_setup(int id, int panel_w, int panel_h, int draw_fb_size)
{
    const struct lv_spi_config *config = &spi_configs[id];
    aic_spi_lcd_cfg_t spi_cfg;
    struct lv_spi_dev *spi = NULL;
    int tx_size;

    spi = rt_malloc(sizeof(struct lv_spi_dev));
    if (!spi) {
        LV_LOG_ERROR("malloc spi dev failed\n");
        return NULL;
    }
    rt_memset(spi, 0, sizeof(*spi));

    spi->id = id;
    spi->panel_w = (unsigned int)panel_w;
    spi->panel_h = (unsigned int)panel_h;
    spi->fb_size = (unsigned int)draw_fb_size;
    spi->power_on = false;

    if (config->te_pin) {
        spi->te_pin = rt_pin_get(config->te_pin);
        if (spi->te_pin < 0) {
            LV_LOG_ERROR("get spi te pin %s failed\n", config->te_pin);
            rt_free(spi);
            return NULL;
        }

        spi->te_queue = aicos_wqueue_create();
    }

    spi->display_sem = aicos_sem_create(0);
    spi->sync_ready = aicos_sem_create(0);
    spi->primed = false;
    spi->bl_en = false;
    spi->disp_thread_on = false;

    spi->ge2d_dev = mpp_ge_open();
    spi->config = config;

    lv_spi_fill_lcd_cfg(config, &spi_cfg);

    spi->dev = aic_spi_lcd_init(&spi_cfg);
    if (!spi->dev) {
        pr_err("aic spi lcd init failed\n");
        rt_free(spi);
        return NULL;
    }

    /* Full panel RGB565 buffers for SPI (GE upscale target when half-res draw) */
    tx_size = panel_w * panel_h * 2;
    spi->tx_len = tx_size;
    spi->tx_wr_idx = 0;
    for (int i = 0; i < 2; i++) {
        spi->tx_buf[i] = aicos_malloc_align(MEM_CMA, tx_size, CACHE_LINE_SIZE);
        if (!spi->tx_buf[i]) {
            LV_LOG_ERROR("malloc display buf %d failed\n", i);
            while (--i >= 0)
                aicos_free_align(MEM_CMA, spi->tx_buf[i]);
            rt_free(spi);
            return NULL;
        }
        aicos_dcache_clean_invalid_range((ulong *)spi->tx_buf[i],
                                         (ulong)ALIGN_UP(tx_size, CACHE_LINE_SIZE));
    }

    return spi;
}

void lv_spi_screen_enable(struct lv_spi_dev *spi_dev)
{
    aicos_thread_t thid = NULL;

    lv_spi_panel_enable(spi_dev);

    /* Direct-blit panels drive SPI from the producer thread; no disp_thread. */
    if (spi_dev->blit_owned)
        return;

    thid = aicos_thread_create(spi_dev->config->spi_cfg.bus_name,
                               8192, 20 + spi_dev->id, disp_thread, spi_dev);
    if (thid == NULL) {
        LV_LOG_ERROR("Failed to create display thread\n");
        return;
    }
    spi_dev->disp_thread_on = true;
}

static lv_color_format_t lv_display_fmt(enum mpp_pixel_format cf)
{
    lv_color_format_t fmt = LV_COLOR_FORMAT_ARGB8888;
    switch(cf) {
        case MPP_FMT_RGB_565:
            fmt = LV_COLOR_FORMAT_RGB565;
            break;
        case MPP_FMT_RGB_888:
            fmt = LV_COLOR_FORMAT_RGB888;
            break;
        case MPP_FMT_ARGB_8888:
            fmt = LV_COLOR_FORMAT_ARGB8888;
            break;
        case MPP_FMT_XRGB_8888:
            fmt = LV_COLOR_FORMAT_XRGB8888;
            break;
        default:
            LV_LOG_ERROR("unsupported format:%d", cf);
            break;
    }
    return fmt;
}

static void spi_dev_poweron(struct lv_spi_dev *spi_dev)
{
    if (!spi_dev->power_on) {
        lv_spi_screen_enable(spi_dev);
        spi_dev->power_on = true;
    }
}

void lv_spi_flush(lv_display_t *disp, u8 *data)
{
    struct lv_spi_dev *spi_dev = lv_find_spi_dev(disp);

    if (!spi_dev)
        return;

    /* LVGL render time: delta from previous display's flush to this one */
    if (g_render_ts)
        g_timing[spi_dev->id].lvgl_ms += lv_tick_get() - g_render_ts;
    g_render_ts = lv_tick_get();

    if (spi_dev->primed) {
        uint32_t t0 = lv_tick_get();
        aicos_sem_take(spi_dev->sync_ready, AICOS_WAIT_FOREVER);
        g_timing[spi_dev->id].sync_wait_ms += lv_tick_elaps(t0);
    } else {
        spi_dev->primed = true;
    }

    spi_dev->data = data;

    aicos_sem_give(spi_dev->display_sem);
}

static void spi_dev_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    lv_draw_buf_t *disp_buf = lv_display_get_buf_active(disp);
    struct lv_spi_dev *spi_dev = lv_find_spi_dev(disp);
    (void)px_map;

    if (!spi_dev)
        return;

    /* Panel reserved for the direct blit path: never drive it from LVGL. */
    if (spi_dev->blit_owned) {
        lv_display_flush_ready(disp);
        return;
    }

    if (lv_disp_flush_is_last(disp)) {
        spi_dev_poweron(spi_dev);
        aicos_dcache_clean_invalid_range((ulong *)disp_buf->data,
            (ulong)ALIGN_UP(spi_dev->fb_size, CACHE_LINE_SIZE));
        lv_spi_flush(disp, disp_buf->data);
    }

    lv_display_flush_ready(disp);
}

int lv_spi_blit_claim(int id)
{
    struct lv_spi_dev *spi_dev;

    if (!g_spi_devs || id < 0 || id >= (int)LV_SPI_DEV_NUM)
        return -1;

    spi_dev = g_spi_devs[id];
    if (!spi_dev)
        return -1;

    spi_dev->blit_owned = true;

    /*
     * Stop LVGL from refreshing this panel: drop its refr timer and ignore
     * invalidation so the blit producer is the sole owner.
     */
    if (spi_dev->disp) {
        lv_display_delete_refr_timer(spi_dev->disp);
        lv_display_enable_invalidation(spi_dev->disp, false);
    }

    return 0;
}

int lv_spi_is_blit_owned(int id)
{
    if (!g_spi_devs || id < 0 || id >= (int)LV_SPI_DEV_NUM)
        return 0;
    if (!g_spi_devs[id])
        return 0;

    return g_spi_devs[id]->blit_owned ? 1 : 0;
}

int lv_spi_get_blit_buffers(int id, void **buf0, void **buf1)
{
    struct lv_spi_dev *spi_dev;

    if (!g_spi_devs || id < 0 || id >= (int)LV_SPI_DEV_NUM)
        return -1;

    spi_dev = g_spi_devs[id];
    if (!spi_dev || !spi_dev->draw_buf[0] || !spi_dev->draw_buf[1])
        return -1;

    if (buf0)
        *buf0 = spi_dev->draw_buf[0];
    if (buf1)
        *buf1 = spi_dev->draw_buf[1];

    return 0;
}

int lv_spi_get_panel_info(int id, unsigned int *width, unsigned int *height,
                          unsigned int *buf_size)
{
    struct lv_spi_dev *spi_dev;

    if (!g_spi_devs || id < 0 || id >= (int)LV_SPI_DEV_NUM)
        return -1;

    spi_dev = g_spi_devs[id];
    if (!spi_dev || !spi_dev->disp)
        return -1;

    if (width)
        *width = lv_display_get_horizontal_resolution(spi_dev->disp);
    if (height)
        *height = lv_display_get_vertical_resolution(spi_dev->disp);
    if (buf_size)
        *buf_size = spi_dev->fb_size;

    return 0;
}

int lv_spi_get_phys_panel_info(int id, unsigned int *width, unsigned int *height)
{
    struct lv_spi_dev *spi_dev;

    if (!g_spi_devs || id < 0 || id >= (int)LV_SPI_DEV_NUM)
        return -1;

    spi_dev = g_spi_devs[id];
    if (!spi_dev)
        return -1;

    if (width)
        *width = spi_dev->panel_w;
    if (height)
        *height = spi_dev->panel_h;

    return 0;
}

/*
 * Direct buffer-to-panel path: copy+byte-swap draw_buf to an internal tx_buf,
 * then send via async SPI DMA.  Works entirely in the caller's context — no
 * disp_thread involvement.
 */
int lv_spi_blit(int id, void *buf)
{
    struct lv_spi_dev *spi_dev;
    uint32_t t0, t1;
    unsigned int wr;
    unsigned char *tx_dest;

    if (!g_spi_devs || id < 0 || id >= (int)LV_SPI_DEV_NUM || !buf)
        return -1;

    spi_dev = g_spi_devs[id];
    if (!spi_dev)
        return -1;

    spi_dev_poweron(spi_dev);

    aicos_dcache_clean_invalid_range((ulong *)buf,
                                     (ulong)ALIGN_UP(spi_dev->fb_size, CACHE_LINE_SIZE));

    wr = spi_dev->tx_wr_idx;
    tx_dest = spi_dev->tx_buf[wr];
    spi_dev->data = buf;

    t0 = lv_tick_get();
    lv_disp_data_blt(spi_dev, tx_dest);
    t1 = lv_tick_get();
    g_timing[spi_dev->id].ge2d_ms += t1 - t0;

    /* Wait for previous async SPI transfer to finish so its tx_buf slot is free. */
    if (spi_dev->primed) {
        aic_spi_lcd_wait_completion(spi_dev->dev);
        t1 = lv_tick_get();
        g_timing[spi_dev->id].spi_ms += t1 - spi_dev->spi_start;
    } else {
        spi_dev->primed = true;
    }

    spi_dev->spi_start = lv_tick_get();
    aic_spi_lcd_flush(spi_dev->dev, tx_dest, spi_dev->tx_len);
    spi_dev->tx_wr_idx ^= 1U;

    spi_dev_backlight_on(spi_dev);

    g_timing[spi_dev->id].frame_count++;

    spi_fps_tick(spi_dev->id);

    return 0;
}

int lv_spi_display_init(int use_frame_buffer)
{
    void *buf1 = NULL, *buf2 = NULL;
    struct aicfb_screeninfo info;
    struct mpp_fb *fb = NULL;
    int panel_w, panel_h, draw_w, draw_h, draw_fb_size;
    int i;

    fb = mpp_fb_open();
    if (!fb) {
        LV_LOG_ERROR("open mpp fb failed");
        return -1;
    }
    mpp_fb_ioctl(fb, AICFB_GET_SCREENINFO, &info);

    lv_color_format_t cf = lv_display_fmt(info.format);
    if (cf == LV_COLOR_FORMAT_UNKNOWN)
        return -1;

    g_spi_devs = lv_malloc_zeroed(sizeof(struct lv_spi_dev *) * LV_SPI_DEV_NUM);
    if (!g_spi_devs) {
        LV_LOG_ERROR("malloc spi devs array failed");
        return -1;
    }

    for (i = 0; i < LV_SPI_DEV_NUM; i++) {
        lv_spi_resolve_geometry(&spi_configs[i], info.width, info.height,
                                (unsigned int *)&panel_w, (unsigned int *)&panel_h);

        panel_w = ALIGN_8B(panel_w);
        draw_w = panel_w / LV_SPI_DRAW_DIV;
        draw_h = panel_h / LV_SPI_DRAW_DIV;
        draw_fb_size = draw_w * draw_h * lv_color_format_get_size(cf);

        struct lv_spi_dev *spi_dev = lv_spi_setup(i, panel_w, panel_h, draw_fb_size);
        if (!spi_dev) {
            LV_LOG_ERROR("spi setup failed for device %d", i);
            goto err;
        }
        g_spi_devs[i] = spi_dev;

        if (i == 0 && use_frame_buffer) {
            buf1 = info.framebuffer;
#ifdef AIC_PAN_DISPLAY
            buf2 = info.framebuffer + info.smem_len;
#else
            buf2 = NULL;
#endif
        } else {
            buf1 = aicos_malloc_align(MEM_CMA, draw_fb_size, CACHE_LINE_SIZE);
            if (!buf1) {
                LV_LOG_ERROR("malloc display buf1 failed");
                goto err;
            }
            buf2 = aicos_malloc_align(MEM_CMA, draw_fb_size, CACHE_LINE_SIZE);
            if (!buf2) {
                LV_LOG_ERROR("malloc display buf2 failed");
                goto err;
            }
        }

        lv_display_t *disp = lv_display_create(draw_w, draw_h);
        lv_display_set_color_format(disp, cf);
        lv_display_set_flush_cb(disp, spi_dev_flush);

        lv_display_set_buffers(disp, buf1, buf2, draw_fb_size, LV_DISPLAY_RENDER_MODE_DIRECT);
        spi_dev->draw_buf[0] = buf1;
        spi_dev->draw_buf[1] = buf2;
#if defined(LV_SPI_HALF_RES_RENDER)
        LV_LOG_INFO("SPI%d draw %dx%d -> panel %dx%d (GE upscale)",
                    i + 1, draw_w, draw_h, panel_w, panel_h);
#endif
#if defined(LV_DISPLAY_ROTATE_EN) && (defined(AIC_LVGL_DOUBLE_DISP_DEMO) || defined(AIC_LVGL_FIVE_DISP_DEMO))
        lv_display_set_rotation(disp, LV_SECOND_ROTATE_DEGREE / 90);
#elif defined(LV_DISPLAY_ROTATE_EN) && defined(LV_USE_SPI_REPLACE_MIPI_DBI)
        lv_display_set_rotation(disp, LV_ROTATE_DEGREE / 90);
#endif

        spi_dev->disp = disp;
    }

    /* Start per-panel FPS tracking timer (shared by LVGL and blit paths). */
    lv_timer_create(spi_fps_timer_cb, 1000, NULL);

    return 0;

err:
    if (g_spi_devs) {
        for (i = 0; i < LV_SPI_DEV_NUM; i++) {
            if (g_spi_devs[i]) {
                if (i == 0 && use_frame_buffer)
                    continue;

                if (g_spi_devs[i]->tx_buf[0])
                    aicos_free_align(MEM_CMA, g_spi_devs[i]->tx_buf[0]);
                if (g_spi_devs[i]->tx_buf[1])
                    aicos_free_align(MEM_CMA, g_spi_devs[i]->tx_buf[1]);

                if (g_spi_devs[i]->ge2d_dev)
                    mpp_ge_close(g_spi_devs[i]->ge2d_dev);

                rt_free(g_spi_devs[i]);
            }
        }
        lv_free(g_spi_devs);
        g_spi_devs = NULL;
    }

    if (fb)
        mpp_fb_close(fb);

    LV_LOG_ERROR("create lv spi display failed");
    return -1;
}

static int lv_spi_fps(int argc, char **argv)
{
    LV_UNUSED(argc);
    LV_UNUSED(argv);

    rt_kprintf("%-4s  %4s  %5s  %5s  %5s  %5s  %s\n",
               "Port", "FPS", "LVGL", "GE2D", "SPI", "Sync", "Mode");
    rt_kprintf("----  ----  -----  -----  -----  -----  ----\n");

    for (int i = 0; i < LV_SPI_DEV_NUM; i++) {
        struct disp_timing tm;
        int blit = lv_spi_is_blit_owned(i);
        uint32_t fps_val = lv_spi_get_fps(i);
        uint32_t n;

        lv_spi_get_timing(i, &tm);
        n = tm.frame_count ? tm.frame_count : 1U;

        if (blit) {
            rt_kprintf("SPI%d  %3lu    -    %3lums  %3lums    -     blit\n",
                       i + 1, (unsigned long)fps_val,
                       (unsigned long)(tm.ge2d_ms / n),
                       (unsigned long)(tm.spi_ms / n));
        } else {
            rt_kprintf("SPI%d  %3lu  %3lums  %3lums  %3lums  %3lums   lvgl\n",
                       i + 1, (unsigned long)fps_val,
                       (unsigned long)(tm.lvgl_ms / n),
                       (unsigned long)(tm.ge2d_ms / n),
                       (unsigned long)(tm.spi_ms / n),
                       (unsigned long)(tm.sync_wait_ms / n));
        }
    }
    return 0;
}
MSH_CMD_EXPORT(lv_spi_fps, print FPS of all SPI displays);
