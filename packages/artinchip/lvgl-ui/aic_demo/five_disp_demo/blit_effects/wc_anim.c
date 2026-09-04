/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Five blit threads for the world-cup demo. SPI3 alone advances wc_step().
 */

#include "wc_game.h"
#include "blit_common.h"

struct wc_anim_cfg {
    int id;
};

static struct wc_anim_cfg g_wc_cfg[DISP_SLOT_COUNT];

static void wc_anim_thread(void *arg)
{
    struct wc_anim_cfg *cfg = arg;
    int id = cfg->id;
    unsigned int w = 0, h = 0, size = 0;
    void *b0 = NULL, *b1 = NULL;
    uint16_t *bufs[2];
    int idx = 0;

    if (lv_spi_get_panel_info(id, &w, &h, &size) < 0)
        return;

    blit_lut_init();

    if (lv_spi_get_blit_buffers(id, &b0, &b1) < 0) {
        LV_LOG_ERROR("wc anim: no draw buffers for panel %d", id);
        return;
    }
    bufs[0] = b0;
    bufs[1] = b1;

    while (1) {
        uint32_t frame_start = lv_tick_get();
        uint32_t elapsed;
        uint32_t budget = 1000U / (uint32_t)BLIT_TARGET_FPS;

        /* Single simulation producer on SPI3. */
        if (id == 2)
            wc_step();

        wc_fill_panel(bufs[idx], w, h, id);
        lv_spi_blit(id, bufs[idx]);
        idx ^= 1;

        elapsed = lv_tick_elaps(frame_start);
        if (elapsed < budget)
            rt_thread_mdelay(budget - elapsed);
    }
}

void start_wc_demo(void)
{
    int id;

    for (id = 0; id < DISP_SLOT_COUNT; id++) {
        char tname[12];

        g_wc_cfg[id].id = id;
        lv_spi_blit_claim(id);
        rt_snprintf(tname, sizeof(tname), "wc_spi%c", '1' + (char)id);
        aicos_thread_create(tname, 4096, 15, wc_anim_thread, &g_wc_cfg[id]);
    }
}
