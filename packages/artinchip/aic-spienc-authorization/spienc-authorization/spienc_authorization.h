/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: Hao Xiong <hao.xiong@artinchip.com>
 */
#ifndef _AIC_SPIENC_AUTHORIZATION_SPIE_H_
#define _AIC_SPIENC_AUTHORIZATION_SPIE_H_

#include <aic_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate random bytes.
 *
 * @param[out] buf  Pointer to the destination buffer to store the random bytes.
 * @param[in]  len  Number of random bytes to generate.
 * @return     Number of bytes generated on success, or -EINVAL if buf is NULL.
 */
int aic_rng_get_bytes(u8 *buf, unsigned int len);

/**
 * @brief Sign data using SPIENC-derived key
 *
 * @param dlen     Data length in bytes
 * @param data     Input data to encrypt
 * @param output   Output buffer for encrypted data
 * @param sign_key 32-byte sign key blob
 * @return dlen on success, -EINVAL if sign_key version mismatch,
 *         -EIO if SPI read fails
 */
int aic_spie_sk_sign(int dlen, unsigned char *data, unsigned char *output, unsigned char *sign_key);

/**
 * @brief Verify data using the verify key
 *
 * @param dlen       Data length in bytes
 * @param data       Encrypted input data
 * @param output     Output buffer for decrypted data
 * @param verify_key 32-byte verify key
 * @return dlen on success
 */
int aic_spie_vk_verify(int dlen, unsigned char *data, unsigned char *output, unsigned char *verify_key);

#ifdef __cplusplus
}
#endif

#endif /* _AIC_SPIENC_AUTHORIZATION_SPIE_H_ */
