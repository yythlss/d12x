/*
 * Copyright (C) 2024-2026 ArtInChip Technology Co.,Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: Xiong Hao <hao.xiong@artinchip.com>
 */

#include <stdio.h>
#include <aic_core.h>
#include <aic_common.h>
#include <ram_param.h>

#define EFUSE_CMU_REG ((void *)0x18020904)
#define EFUSE_218_REG ((void *)0x19010218)
#define EFUSE_21C_REG ((void *)0x1901021c)

#define PSRAM_SINGLE   0
#define PSRAM_PARALLEL 1

/* PSRAM ID in Chip */
enum psram_fuse {
    PSRAM_FUSE_APS3208K = 0x00,
    PSRAM_FUSE_SCKW18_12816O = 0x01,
    PSRAM_FUSE_APS12816O = 0x02,
    PSRAM_FUSE_APS25616O = 0x03,
    PSRAM_FUSE_APS3208K_PARALLEL = 0x04,
    PSRAM_FUSE_W957D6NKR = 0x05,
    PSRAM_FUSE_W956D8NKR_PARALLEL = 0x06,
    PSRAM_FUSE_APS6408L_PARALLEL = 0x07,
    PSRAM_FUSE_W955D8MKY = 0x08,
    PSRAM_FUSE_W955D8MKY_PARALLEL = 0x09,
};

struct _psram_id {
    u8 psram_fuse_id;
    u32 psram_chip_id;
};

struct _psram_info {
    u8 mark_id;
    u8 psram_num;
    u32 psram_size;
    struct _psram_id psram_id;
};

#define PSRAM_TABLE_INFO							\
{										\
    /* markid, psram_num, psram_size, {psram_efuse_id, psram_chip_id} */	\
    /* force use the cfg0 */							\
    {0x0, 0, 0, {0, 0}},							\
    /* D122BAV 4M */								\
    {0x01, PSRAM_SINGLE, 0x400000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},		\
    /* D122BBV 8M */								\
    {0x02, PSRAM_PARALLEL, 0x800000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},	\
    /* D121BAV1 4M */								\
    {0x03, PSRAM_SINGLE, 0x400000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},		\
    /* D121BAV2 4M W955D8MKY */							\
    {0x03, PSRAM_SINGLE, 0x400000, {PSRAM_FUSE_W955D8MKY, 0x5f005f00}},	\
    /* D121BBV1 8M */								\
    {0x04, PSRAM_PARALLEL, 0x800000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},	\
    /* D121BBV2 8M W955D8MKY */							\
    {0x04, PSRAM_PARALLEL, 0x800000, {PSRAM_FUSE_W955D8MKY_PARALLEL, 0x5f005f00}},	\
    /* D122BCV1 16M SCKW18X128160AAE1 */					\
    {0x05, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_SCKW18_12816O, 0xc59ac59a}},	\
    /* D122BCV2 16M AP12816 */							\
    {0x05, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_APS12816O, 0xdd8ddd8d}},	\
    /* D121BCV1 16M AP12816 */							\
    {0x06, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_APS12816O, 0xdd8ddd8d}},	\
    /* D121BCV2 16M W957D6NKR */						\
    {0x05, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_W957D6NKR, 0x0c860c86}},	\
    /* D121BCV3 16M W956D8NKR */						\
    {0x05, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_W956D8NKR_PARALLEL, 0x860c860c}},	\
    /* D121BCV4 16M APS6408L */						\
    {0x05, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_APS6408L_PARALLEL, 0x8b8d8b8d}},	\
    /* D123BAV 4M */								\
    {0x07, PSRAM_SINGLE, 0x400000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},		\
    /* D123BBV 8M */								\
    {0x08, PSRAM_PARALLEL, 0x800000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},	\
    /* D123BCV 16M */								\
    {0x09, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_APS12816O, 0xdd8ddd8d}},	\
    /* D123BDV 32M */								\
    {0x0B, PSRAM_PARALLEL, 0x2000000, {PSRAM_FUSE_APS25616O, 0xdf8ddf8d}},	\
    /* TR230 8M */								\
    {0xA1, PSRAM_PARALLEL, 0x800000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},	\
    /* JYX58 8M */								\
    {0xB1, PSRAM_PARALLEL, 0x800000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},	\
    /* SPECIAL 8M : 2 x APS3208K */						\
    {0x04, PSRAM_PARALLEL, 0x800000, {0x04, 0x80c980c9}},			\
}

/* PSRAM_TABLE_INFO1
 * Index psram info by psram_id only, not relate to mark_id
 */
#define PSRAM_TABLE_INFO1							\
{										\
    /* markid, psram_num, psram_size, {psram_efuse_id, psram_chip_id} */	\
    /* APS3208K SINGLE */							\
    {0x0, PSRAM_SINGLE, 0x400000, {PSRAM_FUSE_APS3208K, 0x80c980c9}},		\
    /* SCKW18X128160AAE1 */							\
    {0x0, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_SCKW18_12816O, 0xc59ac59a}},	\
    /* 16M AP12816 */								\
    {0x0, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_APS12816O, 0xdd8ddd8d}},	\
    /* 16M AP6408L PARALLEL*/								\
    {0x0, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_APS6408L_PARALLEL, 0x8b8d8b8d}},	\
    /* APS3208K PARALLEL */							\
    {0x0, PSRAM_PARALLEL, 0x800000, {PSRAM_FUSE_APS3208K_PARALLEL, 0x80c980c9}},\
    /* W957D6NKR PARALLEL */						\
    {0x0, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_W957D6NKR, 0x0c860c86}},	\
    /* W956D8NKR PARALLEL */						\
    {0x0, PSRAM_PARALLEL, 0x1000000, {PSRAM_FUSE_W956D8NKR_PARALLEL, 0x860c860c}},	\
    /* W955D8MKY SINGLE */							\
    {0x0, PSRAM_SINGLE, 0x400000, {PSRAM_FUSE_W955D8MKY, 0x5f005f00}},		\
    /* W955D8MKY PARALLEL */						\
    {0x0, PSRAM_PARALLEL, 0x800000, {PSRAM_FUSE_W955D8MKY_PARALLEL, 0x5f005f00}},	\
}


struct _psram_info psram_table_info[] = PSRAM_TABLE_INFO;
struct _psram_info psram_table_info1[] = PSRAM_TABLE_INFO1;

u8 psram_get_mark_id(void)
{
    u32 fuse_218 = readl(EFUSE_218_REG);
    u8 mark_id = fuse_218 & 0xff;

    pr_debug("fuse_218(0x19010218)=0x%x, mark_id=0x%x\n", fuse_218, mark_id);
    return mark_id;
}

u8 psram_get_psram_id(void)
{
    u32 fuse_21c = readl(EFUSE_21C_REG);
    u8 psram_id = (fuse_21c & 0xff) >> 4;

    pr_debug("fuse_21c(0x1901021c)=0x%x, psram_id=0x%x\n", fuse_21c, psram_id);
    return psram_id;
}

struct _psram_info *psram_get_info(u8 mark_id, u8 psram_fuse_id)
{
    u32 len = ARRAY_SIZE(psram_table_info);

    for (int i = 0; i < len; i++) {
        if (((mark_id == psram_table_info[i].mark_id) &&
             (psram_fuse_id == psram_table_info[i].psram_id.psram_fuse_id))) {
            return &psram_table_info[i];
        }
    }

    len = ARRAY_SIZE(psram_table_info1);

    for (int i = 0; i < len; i++) {
        if (((mark_id == psram_table_info1[i].mark_id) &&
             (psram_fuse_id == psram_table_info1[i].psram_id.psram_fuse_id))) {
            return &psram_table_info1[i];
        }
    }
    pr_info("can't get the psram table, return the default info.\n");
    return &psram_table_info[0]; //if not find anyone, return the default info.
}

static struct _psram_info *psram_info = NULL;

u32 aic_get_ram_size(void)
{
    u8 mark_id, psram_id;

    if (psram_info)
        return psram_info->psram_size;

    writel(0x1100, EFUSE_CMU_REG);
    mark_id = psram_get_mark_id();
    psram_id = psram_get_psram_id();
    psram_info = psram_get_info(mark_id, psram_id);
    writel(0x0, EFUSE_CMU_REG);

    return psram_info->psram_size;
}

void aic_show_ram_size(void)
{
    /* Map psram_fuse_id to PSRAM chip name */
    static const char *const psram_ids[] = {
        [PSRAM_FUSE_APS3208K]           = "APS32",
        [PSRAM_FUSE_SCKW18_12816O]      = "SCKW18",
        [PSRAM_FUSE_APS12816O]          = "APS128",
        [PSRAM_FUSE_APS25616O]          = "APS256",
        [PSRAM_FUSE_APS3208K_PARALLEL]  = "APS32x2",
        [PSRAM_FUSE_W957D6NKR]          = "W957D6",
        [PSRAM_FUSE_W956D8NKR_PARALLEL] = "W956D8x2",
        [PSRAM_FUSE_APS6408L_PARALLEL]  = "APS64x2",
        [PSRAM_FUSE_W955D8MKY]          = "W955D8",
        [PSRAM_FUSE_W955D8MKY_PARALLEL] = "W955D8x2",
    };
    const char *psram_name = "Unknown";
    u32 fuse_id;

    if (!psram_info)
        aic_get_ram_size();

    fuse_id = psram_info->psram_id.psram_fuse_id;
    if ((fuse_id < ARRAY_SIZE(psram_ids)) && psram_ids[fuse_id])
        psram_name = psram_ids[fuse_id];

    printf("PSRAM: %s, size %d MB\n",
           psram_name, psram_info->psram_size / 1024 / 1024);
}
