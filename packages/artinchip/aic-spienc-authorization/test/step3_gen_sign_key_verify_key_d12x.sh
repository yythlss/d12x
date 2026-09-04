#!/bin/bash
# Copyright (C) 2026 ArtInChip Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

../../../../tools/scripts/spienctool genauthkey --seedver 3 \
											    --chip d12x \
												--spienckey spi_enc_key.bin \
												--outseed sign.key \
												--outverifykey verify.key

xxd -c 16 -i sign.key > d12x_hwauth.h
xxd -c 16 -i verify.key >> d12x_hwauth.h
rm -rf sign.key
rm -rf verify.key

