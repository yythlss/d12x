/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ARTINCHIP_HAL_MTOP_SPEC_DEF_H_
#define _ARTINCHIP_HAL_MTOP_SPEC_DEF_H_

/* Each group is 8 bits */
#define PORT_BITMAP          0x030F0F03

#define MTOP_GROUP_MAX       4
#define MTOP_PORT_MAX        4

#define MTOP_GROUP_ID_LIST   0, 1, 2, 3
#define MTOP_GRP_NAME_LIST   "SRAM0", "SRAM1", "PSRAM", "FLASH"
#define MTOP_PRT_NAME_LIST   "CPU", "DMA", "DE", "GE"

static __attribute__((unused)) uint8_t group_id[] = { MTOP_GROUP_ID_LIST };
static __attribute__((unused)) const char *grp_name[] = { MTOP_GRP_NAME_LIST };
static __attribute__((unused)) const char *prt_name[] = { MTOP_PRT_NAME_LIST };

#endif
