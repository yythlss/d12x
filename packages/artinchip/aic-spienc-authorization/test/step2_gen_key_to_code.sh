#!/bin/bash
# Copyright (C) 2026 ArtInChip Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

echo "Make spi_aes_key.h from spi_enc_key.bin"

cp spi_enc_key.bin spi_aes.key
xxd -c 16 -i spi_aes.key > spi_aes_key.h
cat spi_aes.key > all.key
crc32 spi_aes.key all.key > crc32.txt
rm spi_aes.key

cp spi_aes_key.h ../../../../bsp/examples_bare/test-efuse/spi_aes_key.h
