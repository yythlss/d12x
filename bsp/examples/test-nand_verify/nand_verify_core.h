/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __NAND_VERIFY_CORE_H__
#define __NAND_VERIFY_CORE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int nand_verify_init(const char *dev_name);
void nand_verify_deinit(void);

int nand_verify_single_read(void);
int nand_verify_single_write(void);
int nand_verify_multi_read(void);
int nand_verify_multi_write(void);
int nand_verify_erase(void);
int nand_verify_large_read(void);
int nand_verify_large_write(void);
int nand_verify_data(void);
int nand_verify_oob(void);
int nand_verify_phase(uint32_t phase);
int nand_verify_phase_scan(void);

const char *nand_verify_dev_name(void);
uint32_t nand_verify_page_size(void);
uint32_t nand_verify_block_size(void);
uint32_t nand_verify_capacity(void);

#ifdef __cplusplus
}
#endif

#endif /* __NAND_VERIFY_CORE_H__ */
