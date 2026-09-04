/*
 * Copyright (c) 2026-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Xiong Hao <hao.xiong@artinchip.com>
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aic_utils.h>
#include <aic_common.h>
#if defined(KERNEL_RTTHREAD)
#include <rtthread.h>
#elif defined(KERNEL_BAREMETAL)
#include <console.h>
#endif
#include <aic_core.h>
#include "spienc_authorization.h"
#include "test_spienc_authorization.h"


int app_hw_authorization_check(unsigned char *nonce, int nlen,
                               unsigned char *sign_key, int sign_key_len,
                               unsigned char *verify_key, int verify_key_len)
{
    uint8_t *nonce_sign = NULL, *nonce_veri = NULL;
    uint8_t sign_key_buf[sign_key_len];
    uint8_t verify_key_buf[verify_key_len];
    size_t pagesize = 4096;
    int ret = 0, rlen;

    nonce_sign = aicos_malloc_align(0, pagesize, CACHE_LINE_SIZE);
    if (nonce_sign == NULL) {
        printf("Failed to allocate inbuf.\n");
        ret = -ENOMEM;
        goto out;
    }
    nonce_veri = aicos_malloc_align(0, pagesize, CACHE_LINE_SIZE);
    if (nonce_veri == NULL) {
        printf("Failed to allocate outbuf.\n");
        ret = -ENOMEM;
        goto out;
    }

    // 1. Set key parameters
    memcpy(sign_key_buf, sign_key, sign_key_len);
    memcpy(verify_key_buf, verify_key, verify_key_len);

    hexdump_msg("Nonce", (unsigned char *)nonce, nlen, 1);
    // 2. Nonce process by sign key
    rlen = aic_spie_sk_sign(nlen, nonce, nonce_sign, sign_key_buf);
    if (rlen < 0) {
        printf("aic spie sign failed.\n");
        goto out;
    }
    hexdump_msg("Nonce sign", (unsigned char *)nonce_sign, nlen, 1);
    memset(nonce_veri, 0, pagesize);

    // 3. Nonce process by verify key
    rlen = aic_spie_vk_verify(nlen, nonce_sign, nonce_veri, verify_key_buf);
    if (rlen < 0) {
        printf("aic_spie_verify failed.\n");
        goto out;
    }

    hexdump_msg("Nonce veri", (unsigned char *)nonce_veri, nlen, 1);
    // 4. compare Nonce and nonce_veri
    if (memcmp(nonce, nonce_veri, rlen)) {
        hexdump_msg("Expect", (unsigned char *)nonce, nlen, 1);
        hexdump_msg("Got Result", (unsigned char *)nonce_veri, nlen, 1);
        printf("App stop.\n");
        printf("Authorization check is failure.\n");
        ret = -1;
    } else {
        printf("App running.\n");
        printf("Authorization check is ok.\n");
        ret = 0;
    }

out:
    if (nonce_sign)
        aicos_free_align(0, nonce_sign);
    if (nonce_veri)
        aicos_free_align(0, nonce_veri);

    return ret;
}

int aic_spienc_authorization_test(int argc, char **argv)
{
    int ret = 0;
    unsigned char nonce[16] = { 0 }, nlen = 16;

    while (1) {
        ret = aic_rng_get_bytes(nonce, 16);
        if (ret != nlen)
            pr_err("aic rng get bytes failed.\n");

        ret = app_hw_authorization_check(nonce, nlen, sign_key, sign_key_len, verify_key, verify_key_len);
        if (ret < 0) {
            printf("Application not authorization.\n");
        }

        aic_mdelay(2 * 1000);
    }

    return 0;
}
#if defined(KERNEL_RTTHREAD)
MSH_CMD_EXPORT_ALIAS(aic_spienc_authorization_test, aic_spienc_authorization_test, spienc authorization test);
#elif defined(KERNEL_BAREMETAL)
CONSOLE_CMD(aic_spienc_authorization_test, aic_spienc_authorization_test, "spienc authorization test.");
#endif
