/*
 * Copyright (c) 2022-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aicmac_macaddr.h"
#include "hal_efuse.h"
#include "aic_log.h"
#include <string.h>

#define AICMAC_CHIPID_LENGTH    6


static int aicmac_efuse_read(u32 addr, void *data, u32 size)
{
    u32 wid, wval, rest, cnt;
    u8 *pd, *pw;
    int ret;

    if (hal_efuse_wait_ready()) {
        pr_err("eFuse is not ready.\n");
        return -1;
    }

    pd = data;
    rest = size;
    while (rest > 0) {
        wid = addr >> 2;
        ret = hal_efuse_read(wid, &wval);
        if (ret)
            break;
        pw = (u8 *)&wval;
        cnt = rest;
        if (addr % 4) {
            if (rest > (4 - (addr % 4)))
                cnt = (4 - (addr % 4));
            memcpy(pd, pw + (addr % 4), cnt);
        } else {
            if (rest > 4)
                cnt = 4;
            memcpy(pd, pw, cnt);
        }
        pd += cnt;
        addr += cnt;
        rest -= cnt;
    }

    return (int)(size - rest);
}

static inline int aicmac_get_chipid(unsigned char out_chipid[AICMAC_CHIPID_LENGTH])
{
    return aicmac_efuse_read(0x10, out_chipid, AICMAC_CHIPID_LENGTH);
}

void aicmac_get_macaddr_from_chipid(int port, unsigned char out_addr[6])
{
    unsigned char chipid[AICMAC_CHIPID_LENGTH];
    unsigned char key[AICMAC_CHIPID_LENGTH] = {'a', 'i', 'c', 'k', 'e', 'y'};
    int i;

    if (!aicmac_get_chipid(chipid))
        return;

    for (i = 0; i < AICMAC_CHIPID_LENGTH; i++) {
        out_addr[AICMAC_CHIPID_LENGTH - 1 - i] = chipid[i] ^ key[i];
    }

    if (port)
        out_addr[1] ^= 0x55;
    else
        out_addr[1] ^= 0xAA;

    out_addr[0] &= 0xFE;
    out_addr[0] |= 0x02;
}
