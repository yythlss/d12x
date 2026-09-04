/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI4 — center strip of the sky flyer world.
 */

#include "blit_common.h"

void blit_fill_radar(uint16_t *buf, unsigned int w, unsigned int h,
                     uint32_t t)
{
    blit_fill_runner(buf, w, h, t, BLIT_PANEL_SPI4);
}
