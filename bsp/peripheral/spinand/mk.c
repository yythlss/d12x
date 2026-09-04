/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "inc/spinand.h"
#include "inc/manufacturer.h"

#define SPINAND_MFR_MK  0xF2

/*
 * ECC Status: 4 bits across two registers
 *   Status Register C0H:  Bit5=ECCS1, Bit4=ECCS0
 *   Feature Register D0H: Bit1=ECCSE1, Bit0=ECCSE0
 *
 *   ECCS1 ECCS0 ECCSE1 ECCSE0 | Bitflips
 *   0     0     x      x       | 0
 *   0     1     0      0       | 1~2
 *   0     1     0      1       | 3~4
 *   0     1     1      0       | 5~6
 *   0     1     1      1       | 7~8
 *   1     0     0      0       | 9~10
 *   1     0     0      1       | 11~12
 *   1     0     1      0       | 13~14
 *   1     0     1      1       | 15~16
 *   1     1     x      x       | >16 (uncorrectable)
 */
#define MK_STATUS_ECC_MASK    (BIT(5) | BIT(4))
#define MK_STATUS_ECC_NO      (0)
#define MK_STATUS_ECC_LOW     (BIT(4))
#define MK_STATUS_ECC_HIGH    (BIT(5))
#define MK_STATUS_ECC_UNCOR   (BIT(5) | BIT(4))

#define MK_FEATURE_ECCSE_MASK (BIT(1) | BIT(0))

static int mksv1gilae_ecc_get_status(struct aic_spinand *flash, u8 status)
{
    u8 eccse;
    int ret;

    switch (status & MK_STATUS_ECC_MASK) {
    case MK_STATUS_ECC_NO:
        return 0;

    case MK_STATUS_ECC_UNCOR:
        return -SPINAND_ERR_ECC;

    case MK_STATUS_ECC_LOW:
    case MK_STATUS_ECC_HIGH:
        ret = spinand_get_feature(flash, 0xD0);
        if (ret < 0)
            return -SPINAND_ERR;
        eccse = (u8)ret & MK_FEATURE_ECCSE_MASK;
        break;

    default:
        return -SPINAND_ERR;
    }

    switch (status & MK_STATUS_ECC_MASK) {
    case MK_STATUS_ECC_LOW:
        switch (eccse) {
        case (0 << 0): return 2;
        case (1 << 0): return 4;
        case (2 << 0): return 6;
        case (3 << 0): return 8;
        }
        break;
    case MK_STATUS_ECC_HIGH:
        switch (eccse) {
        case (0 << 0): return 10;
        case (1 << 0): return 12;
        case (2 << 0): return 14;
        case (3 << 0): return 16;
        }
        break;
    }

    return -SPINAND_ERR;
}

/*
 * OOB layout (128B):
 *   Spare(0):  bytes 0~15   (byte 0 = BBM)
 *   Spare(1):  bytes 16~31
 *   Spare(2):  bytes 32~47
 *   Spare(3):  bytes 48~63
 *   Spare2(0): bytes 64~79  (ECC)
 *   Spare2(1): bytes 80~95  (ECC)
 *   Spare2(2): bytes 96~111 (ECC)
 *   Spare2(3): bytes 112~127 (ECC)
 */
static int mksv1gilae_ooblayout_user(struct aic_spinand *flash, int section,
                             struct aic_oob_region *region)
{
    if (section > 3)
        return -SPINAND_ERR;

    region->offset = 16 * section;
    region->length = 16;

    return 0;
}

/*
 * OOB layout (256B):
 *   User:  bytes 0~127 (8 sections x 16 bytes)
 *   ECC:   bytes 128~255
 */
static int mksv4gilae_ooblayout_user(struct aic_spinand *flash, int section,
                             struct aic_oob_region *region)
{
    if (section > 7)
        return -SPINAND_ERR;

    region->offset = 16 * section;
    region->length = 16;

    return 0;
}

static const struct aic_spinand_info mk_spinand_table[] = {
    /* MKSV1GIL-AE: 1Gb, 2048+128@64@1024 */
    { DEVID(0x0A), PAGESIZE(2048), OOBSIZE(128),
      BPL(1024), PPB(64), PLANENUM(1), DIE(0),
      "MKSV1GIL-AE MK 128MB: 2048+128@64@1024", cmd_cfg_table,
      mksv1gilae_ecc_get_status, mksv1gilae_ooblayout_user, 16},
    /* MKSV4GIL-AE: 4Gb, 4096+256@64@2048 */
    { DEVID(0x0C), PAGESIZE(4096), OOBSIZE(256),
      BPL(2048), PPB(64), PLANENUM(1), DIE(0),
      "MKSV4GIL-AE MK 512MB: 4096+256@64@2048", cmd_cfg_table,
      mksv1gilae_ecc_get_status, mksv4gilae_ooblayout_user, 16},
};

static const struct aic_spinand_info *mk_spinand_detect(struct aic_spinand *flash)
{
    u8 *id;

    if (!flash)
        return NULL;

    id = flash->id.data;

    if (id[0] != SPINAND_MFR_MK)
        return NULL;

    return spinand_match_and_init(&id[1], mk_spinand_table,
                                  ARRAY_SIZE(mk_spinand_table));
}

static int mk_spinand_init(struct aic_spinand *flash)
{
    /* Feature B0H defaults: ECC_EN=1, BUF=0, QE=0 — no init required */
    return 0;
}

static const struct spinand_manufacturer_ops mk_spinand_manuf_ops = {
    .detect = mk_spinand_detect,
    .init = mk_spinand_init,
};

const struct spinand_manufacturer mk_spinand_manufacturer = {
    .id = SPINAND_MFR_MK,
    .name = "MK",
    .ops = &mk_spinand_manuf_ops,
};
