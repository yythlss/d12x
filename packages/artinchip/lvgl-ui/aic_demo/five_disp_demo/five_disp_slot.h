/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef DISP_SLOT_H
#define DISP_SLOT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LVGL inserts displays at head of list, so last-created = first in list.
 * Creation order (in lv_aic_spi.c): SPI1, SPI2, SPI3, SPI4, SPI5
 * LVGL list order (lv_display_get_next): SPI5, SPI4, SPI3, SPI2, SPI1
 * Slot values must match list index.
 *
 * SPI driver id is 0-based (id + 1 == SPIn). With five panels:
 *   slot = DISP_SLOT_COUNT - 1 - spi_id
 *   spi_id = DISP_SLOT_COUNT - 1 - slot
 */
typedef enum {
    DISP_SLOT_SPI5 = 0,
    DISP_SLOT_SPI4,
    DISP_SLOT_SPI3,
    DISP_SLOT_SPI2,
    DISP_SLOT_SPI1,

    DISP_SLOT_COUNT
} disp_slot_t;

static inline int slot_to_spi_id(disp_slot_t slot)
{
    return (int)DISP_SLOT_COUNT - 1 - (int)slot;
}

static inline disp_slot_t spi_id_to_slot(int spi_id)
{
    return (disp_slot_t)((int)DISP_SLOT_COUNT - 1 - spi_id);
}

#ifdef __cplusplus
}
#endif

#endif /* DISP_SLOT_H */
