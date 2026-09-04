/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI5 — right strip of the sky flyer world.
 */

#include "blit_common.h"

void blit_fill_starfield(uint16_t *buf, unsigned int w, unsigned int h,
                         uint32_t t)
{
    blit_fill_runner(buf, w, h, t, BLIT_PANEL_SPI5);
}
