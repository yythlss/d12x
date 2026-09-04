#!/bin/bash
# Copyright (C) 2026 ArtInChip Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

dd if=/dev/urandom of=spi_enc_key.bin bs=16 count=1 2>/dev/null
# dd if=/dev/zero of=spi_enc_key.bin bs=16 count=1 2>/dev/null
dd if=/dev/urandom of=spl_chacha20_key.bin bs=32 count=1 2>/dev/null
dd if=/dev/urandom of=app_chacha20_key.bin bs=32 count=1 2>/dev/null

