/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Blit animation framework: SPI3/4/5 show one shared sky-flyer world.
 * SPI3 advances simulation; the plane loops across all three panels.
 */

#include "blit_common.h"

struct blit_anim_cfg {
    int id;
    int effect;
};

static struct blit_anim_cfg g_blit_cfg[DISP_SLOT_COUNT];

static void blit_color_anim_thread(void *arg)
{
    struct blit_anim_cfg *cfg = arg;
    int id = cfg->id;
    int effect = cfg->effect;
    unsigned int w = 0, h = 0, size = 0;
    void *b0 = NULL, *b1 = NULL;
    uint16_t *bufs[2];
    int idx = 0;

    /*
     * Use LVGL draw resolution (half-res when LV_SPI_HALF_RES_RENDER is on).
     * lv_spi_blit() upscales via GE2D into the full-size tx buffer.
     */
    if (lv_spi_get_panel_info(id, &w, &h, &size) < 0)
        return;

    blit_lut_init();

    /*
     * Reuse the two CMA draw buffers as ping-pong frames. draw_buf is always
     * LE (never swapped in-place), so copy+swap / GE upscale to tx_buf is safe.
     */
    if (lv_spi_get_blit_buffers(id, &b0, &b1) < 0) {
        LV_LOG_ERROR("blit anim: no draw buffers for panel %d", id);
        return;
    }
    bufs[0] = b0;
    bufs[1] = b1;

    while (1) {
        uint32_t frame_start = lv_tick_get();
        uint32_t elapsed;
        uint32_t budget = 1000U / (uint32_t)BLIT_TARGET_FPS;
        uint32_t t;

        blit_link_tick_advance();
        t = blit_link_tick();

        /* Single producer for runner state. */
        if (effect == BLIT_FX_SPRITE)
            blit_runner_step(t);

        switch (effect) {
        case BLIT_FX_SPRITE:
            blit_fill_sprite(bufs[idx], w, h, t);
            break;
        case BLIT_FX_RADAR:
            blit_fill_radar(bufs[idx], w, h, t);
            break;
        case BLIT_FX_STARFIELD:
        default:
            blit_fill_starfield(bufs[idx], w, h, t);
            break;
        }
        lv_spi_blit(id, bufs[idx]);
        idx ^= 1;

        elapsed = lv_tick_elaps(frame_start);
        if (elapsed < budget)
            rt_thread_mdelay(budget - elapsed);
    }
}

void start_blit_anim(int id, int effect)
{
    if (id < 0 || id >= DISP_SLOT_COUNT)
        return;

    g_blit_cfg[id].id = id;
    g_blit_cfg[id].effect = effect;

    lv_spi_blit_claim(id);
    {
        char tname[12];
        rt_snprintf(tname, sizeof(tname), "blit_spi%c", '1' + (char)id);
        aicos_thread_create(tname, 4096, 15, blit_color_anim_thread,
                            &g_blit_cfg[id]);
    }
}
