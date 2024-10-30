/*
 * Copyright (c) 2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author          Notes
 * 2024-09-29   junlong.chen    first implementation.
 * 2024-09-29   junlong.chen    ArtInChip
 */
#include <finsh.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "aic_core.h"
#include "aic_log.h"
#include "hal_dce.h"
#include "aic_crc32.h"
#include <hwcrypto.h>
#include <hw_crc.h>

u8 default_data[] = "It is test data";

static int software_checksum(u8 *buf, u32 size)
{
    u32 i, val, sum, rest, cnt;
	u8 *p;
	u32 *p32, *pe32;

	p = buf;
	i = 0;
	sum = 0;
	cnt = size >> 2;

	if ((unsigned long)buf & 0x3) {
		for (i = 0; i < cnt; i++) {
			p = &buf[i * 4];
			val = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
			sum += val;
		}
	} else {
		p32 = (u32 *)buf;
		pe32 = p32 + cnt;
		while (p32 < pe32) {
			sum += *p32;
			p32++;
		}
	}

	/* Calculate not 32 bit aligned part */
	rest = size - (cnt << 2);
	p = &buf[cnt * 4];
	val = 0;
	for (i = 0; i < rest; i++)
		val += (p[i] << (i * 8));
	sum += val;

	return sum;
}

static int hardware_checksum(u8 *data, int len)
{
    int ret;

    hal_dce_checksum_start(data, len);
    ret = hal_dce_checksum_wait();
    if (!ret)
        return hal_dce_checksum_result();
    else
        printf("\t%s error: time out\n", __func__);
    return 0;
}

static int hardware_crc32(u8 *data, int len)
{
    int ret;

    hal_dce_crc32_start(0, data, len);
    ret = hal_dce_crc32_wait();
    if (!ret)
        return hal_dce_crc32_result();
    else
        printf("\t%s error: time out\n", __func__);
    return 0;
}

static int hardware_rtt_crc32(u8 *data, int len)
{
    struct rt_hwcrypto_ctx *ctx;
    int result = 0;
    struct hwcrypto_crc_cfg cfg = {
        .last_val = 0,
        .poly = 0x04C11DB7,
        .width = 32,
        .xorout = 0,
        .flags = 0,
    };

    ctx = rt_hwcrypto_crc_create(rt_hwcrypto_dev_default(), HWCRYPTO_CRC_CRC32);
    rt_hwcrypto_crc_cfg(ctx, &cfg);
    result = rt_hwcrypto_crc_update(ctx, data, len);
    rt_hwcrypto_crc_destroy(ctx);
    return result;
}

void int_to_bcd(int num, int *bcd) {
    int index = 0;
    while (num != 0) {
        bcd[index] = num % 16;
        num /= 16;
        index++;
    }
}

static void usage(char *program)
{
    printf("Usage:  %s [options]: \n", program);
    printf("\tParameter 1 is the address of the data to be verified,");
    printf("\n\tparameter 2 is the length of the data to be verified \n");
    printf("\n");
    printf("Example: \n");
    printf("\t%s 0x80040000 0x100   (test data from ram)\n", program);
    printf("\t%s                    (test default data)\n", program);
}

static void cmd_test_dce(int argc, char **argv)
{
    int sw_check_sum, sw_crc32, hw_check_sum, hw_crc32, hw_rtt_crc32, data_len;
    int version[3] = {0};
    u8 *data;

    if (!(argc == 1 || argc == 3)) {
        printf("Usage error\n");
        usage(argv[0]);
        return;
    }

    if (argc == 3) {
        data = (u8 *)strtoul(argv[1], NULL, 0);
        data_len = strtoul(argv[2], NULL, 0);
    } else {
        data = default_data;
        data_len = sizeof(default_data);
    }

    if ((u32)data % 4 || data_len % 4) {
        printf("data and data_len need 4 byte alignment\n");
        return;
    }

    hal_dce_init();

    int_to_bcd(hal_get_version(), version);
    printf("\tDCE version: %d.%d.%d\n", version[2], version[1], version[0]);
    if (argc == 3)
        printf("\tdata_addr: 0x%08x, data_len: 0x%08x\n", (int)data, data_len);
    else
        printf("\tdata:%s, data_len: 0x%08x\n", data, data_len);
    sw_check_sum = software_checksum(data, data_len);
    sw_crc32 = (int)crc32(0, data, data_len);
    printf("\tsoftware: check_sum 0x%08x, crc32 0x%08x\n", sw_check_sum,
           sw_crc32);

    hw_check_sum = hardware_checksum(data, data_len);
    hw_crc32 = hardware_crc32(data, data_len);
    hw_rtt_crc32 = hardware_rtt_crc32(data, data_len);
    printf("\thardware: check_sum 0x%08x, crc32 0x%08x, rtt_crc32 0x%08x\n",
            hw_check_sum, hw_crc32, hw_rtt_crc32);

    if (sw_check_sum != hw_check_sum || sw_crc32 != hw_crc32 ||
            sw_crc32 != hw_rtt_crc32)
        printf("\tDCE calculate failed\n");
    else
        printf("\tDCE calculate OK\n");

    hal_dce_deinit();
    return;
}

MSH_CMD_EXPORT_ALIAS(cmd_test_dce, test_dce, test Dce);
