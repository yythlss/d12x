/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <efuse.h>
#include <console.h>
#include <aic_utils.h>
#include <aic_crc32.h>
#include "spi_aes_key.h"
#if defined(LPKG_USING_DFS_ELMFAT)
#include <dfs_file.h>
#include <dfs_elm.h>
#endif
#include <mmc.h>
#include <block_dev.h>

#if defined(AIC_CHIP_D12X)
static uint32_t crc32_val = 0;

int write_efuse(char *msg, u32 offset, const void *val, u32 size)
{
#if defined(AIC_SID_BURN_SIMULATED)
    printf("eFuse %s:\n", msg);
    hexdump((unsigned char *)val, size, 1);
    return size;
#else
    return efuse_program(offset, val, size);
#endif
}

int burn_spienc_key(void)
{
    u32 offset = 0xFFFF;
    int ret;

    offset = 0x20;

    if (spi_aes_key_len != 16) {
        printf("SPI ENC AES key length is not equal 16 bytes.\n ");
        return -1;
    }

    ret = write_efuse("spi_aes.key", offset, (const void *)spi_aes_key, spi_aes_key_len);
    if (ret <= 0) {
        printf("Write SPI ENC AES key error.\n");
        return -1;
    }

    return 0;
}

int check_spienc_key(void)
{
    u32 offset = 0xFFFF;
    u8 data[256];
    int ret;

    offset = 0x20;
    ret = efuse_read(offset, (void *)data, 16);
    if (ret <= 0) {
        printf("Read efuse error.\n");
        return -1;
    }
    crc32_val = crc32(crc32_val, data, 16);
    printf("SPI ENC KEY crc32 value:0x%x\n", (u32)crc32(0, data, 16));

    return 0;
}

#if !defined(AIC_SID_BURN_DEBUG_MODE)
int burn_spienc_key_read_write_disable_bits(void)
{
    u32 offset, val;
    int ret;

    offset = 0;
    val = 0;
    val = 0x0F000F00; // SPIENC Key Read/Write disable
    ret = write_efuse("spienc key r/w dis", offset, (const void *)&val, 4);
    if (ret <= 0) {
        printf("Write r/w disable bit efuse error.\n");
        return -1;
    }

    return 0;
}
#endif

int check_spienc_key_read_write_disable_bits(void)
{
    u32 offset, val, mskval;
    int ret;

    offset = 0;
    mskval = 0xF00;
    ret = efuse_read(offset, (void *)&val, 4);
    if (ret <= 0) {
        printf("Read r/w disable bit efuse error.\n");
        return -1;
    }

    if ((val & mskval) == mskval)
        printf("SPI ENC Key is read DISABLED\n");
    else
        printf("SPI ENC Key is NOT read disabled\n");
    if (((val>>16) & 0xF00) == 0xF00)
        printf("SPI ENC Key is write DISABLED\n");
    else
        printf("SPI ENC Key is NOT write disabled\n");

    return 0;
}

int save_crc32_to_sdcard(void)
{
    int ret = 0;
#if defined(LPKG_USING_DFS_ELMFAT) && defined(AICUPG_SDCARD_ENABLE)
    struct dfs_fd fd = { 0 };
    char *devname = NULL, *file_buf = NULL;
    char str_chipid[64] = { 0 }, chipid[16] = { 0 };
    int i, length = 0;

    dfs_init();
    elm_init();

    ret = mmc_init(1);
    if (ret) {
        printf("sdmc 1 init failed.\n");
        return ret;
    }

    devname = block_get_name_by_id(1);

    if (dfs_mount(devname, "/", "elm", 0, DEVICE_TYPE_SDMC_DISK) < 0) {
        pr_err("Failed to mount %s with FatFS\n", devname);
        ret = -1;
        goto err2;
    } else {
        pr_info("mount %s ok\n", devname);
    }

    file_buf = (char *)aicos_malloc_align(0, 2048, CACHE_LINE_SIZE);
    if (!file_buf) {
        pr_err("Error, malloc buf failed.\n");
        ret = -1;
        goto err2;
    }
    memset((void *)file_buf, 0, 2048);

    if (dfs_file_open(&fd, "burn.log", O_CREAT | O_WRONLY | O_APPEND) < 0) {
        pr_err("Open burn.log failed.\n");
        ret = -1;
        goto err2;
    }

    if (efuse_read_chip_id(chipid)) {
        pr_err("Read chipid failed.\n");
        ret = -1;
        goto err1;
    }

    for (i = 0; i < sizeof(chipid); i++) {
        sprintf(&str_chipid[2 * i], "%02x", chipid[i]);
    }
    sprintf(file_buf, "chipid:%s, crc32:0x%x\n", str_chipid, (u32)crc32_val);

    length = dfs_file_write(&fd, file_buf, strlen(file_buf));
    if (length != strlen(file_buf)) {
        pr_err("Write file data failed, errno=%d\n", length);
        ret = -1;
        goto err1;
    }

err1:
    dfs_file_close(&fd);

err2:
    if (file_buf)
        aicos_free_align(0, file_buf);
    dfs_unmount("/");
#endif

    return ret;
}


static int cmd_efuse_do_spienc(int argc, char **argv)
{
    int ret;
    crc32_val = 0;

    efuse_init();
    efuse_write_enable();

    ret = burn_spienc_key();
    if (ret) {
        efuse_write_disable();
        printf("Error\n");
        return -1;
    }

    ret = check_spienc_key();
    if (ret) {
        efuse_write_disable();
        printf("Error\n");
        return -1;
    }

#if !defined(AIC_SID_BURN_DEBUG_MODE)
    ret = burn_spienc_key_read_write_disable_bits();
    if (ret) {
        efuse_write_disable();
        printf("Error\n");
        return -1;
    }

    ret = check_spienc_key_read_write_disable_bits();
    if (ret) {
        efuse_write_disable();
        printf("Error\n");
        return -1;
    }
#endif

    efuse_write_disable();
    printf("\n");
    printf("Write SPI ENC eFuse done.\n");
    printf("All key crc32 value are 0x%x.\n", (u32)crc32_val);
    save_crc32_to_sdcard();
#if defined(AIC_SID_BURN_DEBUG_MODE)
    printf("WARNING: The debug mode, the key is visible to the CPU.\n");
#endif
#if defined(AIC_SID_BURN_SIMULATED)
    printf("WARNING: This is a dry run to check the eFuse content, key is not burn to eFuse yet.\n");
#endif
#if !defined(AIC_SID_CONTINUE_BOOT_BURN_AFTER)
    while (1)
        continue;
#endif
    return 0;
}

CONSOLE_CMD(efuse_spienc_d12x_hwauth, cmd_efuse_do_spienc, "eFuse burn for d12x hwauth");
#endif
