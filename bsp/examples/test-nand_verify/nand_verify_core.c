/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/mtd_nand.h>
#include <aic_common.h>
#include <aic_core.h>
#include <drv_qspi.h>
#include "aic_time.h"
#include "nand_verify_core.h"

static struct rt_mtd_nand_device *g_nand = NULL;
static uint8_t *read_ptr = NULL;
static uint8_t *write_ptr = NULL;
static int g_test_block = -1;

static void show_speed(char *msg, u32 len, u32 us)
{
    u32 tmp, speed;

    tmp = 1000 * len;
    tmp = tmp / us;
    tmp = 1000 * tmp;
    speed = tmp / 1024;
    rt_kprintf("%s: %d byte, %d us -> %d KB/s\n", msg, len, us, speed);
}

static int find_good_block(void)
{
    int b;

    for (b = (int)(g_nand->block_end - g_nand->block_start) - 1; b >= 0; b--) {
        if (!rt_mtd_nand_check_block(g_nand, b))
            return b;
    }
    return -1;
}

int nand_verify_init(const char *dev_name)
{
    if (g_nand) {
        nand_verify_deinit();
    }

    g_nand = RT_MTD_NAND_DEVICE(rt_device_find(dev_name));
    if (!g_nand) {
        rt_kprintf("no nand device found: %s\n", dev_name);
        return -1;
    }

    read_ptr = rt_malloc_align(g_nand->page_size + g_nand->oob_size, CACHE_LINE_SIZE);
    write_ptr = rt_malloc_align(g_nand->page_size + g_nand->oob_size, CACHE_LINE_SIZE);
    if (!read_ptr || !write_ptr) {
        rt_kprintf("out of memory\n");
        nand_verify_deinit();
        return -1;
    }

    g_test_block = find_good_block();
    if (g_test_block < 0) {
        rt_kprintf("no good block found\n");
        nand_verify_deinit();
        return -1;
    }

    rt_kprintf("device: %s, page=%d B, block=%d KB, test_block=%d\n",
        dev_name, (int)g_nand->page_size,
        (int)(g_nand->page_size * g_nand->pages_per_block) / 1024,
        g_test_block);

    return 0;
}

void nand_verify_deinit(void)
{
    if (write_ptr) {
        rt_free_align(write_ptr);
        write_ptr = NULL;
    }
    if (read_ptr) {
        rt_free_align(read_ptr);
        read_ptr = NULL;
    }
    g_nand = NULL;
    g_test_block = -1;
}

const char *nand_verify_dev_name(void)
{
    return g_nand ? g_nand->parent.parent.name : "N/A";
}

uint32_t nand_verify_page_size(void)
{
    return g_nand ? g_nand->page_size : 0;
}

uint32_t nand_verify_block_size(void)
{
    return g_nand ? g_nand->page_size * g_nand->pages_per_block : 0;
}

uint32_t nand_verify_capacity(void)
{
    if (!g_nand) return 0;
    return (uint32_t)(g_nand->block_end - g_nand->block_start)
        * g_nand->page_size * g_nand->pages_per_block;
}

/* --- single page --- */

int nand_verify_single_read(void)
{
    int page;
    uint64_t start_us;

    if (!g_nand) return -1;

    page = g_test_block * g_nand->pages_per_block;
    start_us = aic_get_time_us();
    if (rt_mtd_nand_read(g_nand, page, read_ptr, g_nand->page_size, NULL, 0)) {
        rt_kprintf("single page read failed\n");
        return -1;
    }
    show_speed("single page read", g_nand->page_size,
        (u32)(aic_get_time_us() - start_us));
    return 0;
}

int nand_verify_single_write(void)
{
    int page;
    uint64_t start_us;

    if (!g_nand) return -1;

    page = g_test_block * g_nand->pages_per_block;
    memset(write_ptr, 0xA5, g_nand->page_size);

    rt_mtd_nand_erase_block(g_nand, g_test_block);

    start_us = aic_get_time_us();
    if (rt_mtd_nand_write(g_nand, page, write_ptr, g_nand->page_size, NULL, 0)) {
        rt_kprintf("single page write failed\n");
        return -1;
    }
    show_speed("single page write", g_nand->page_size,
        (u32)(aic_get_time_us() - start_us));
    return 0;
}

/* --- multi page (one block) --- */

int nand_verify_multi_read(void)
{
    uint32_t i, n, base;
    uint64_t start_us;

    if (!g_nand) return -1;

    n = g_nand->pages_per_block;
    base = g_test_block * n;

    start_us = aic_get_time_us();
    for (i = 0; i < n; i++) {
        if (rt_mtd_nand_read(g_nand, base + i, read_ptr, g_nand->page_size, NULL, 0)) {
            rt_kprintf("multi page read failed at page %d\n", (int)i);
            return -1;
        }
    }
    show_speed("multi page read", n * g_nand->page_size,
        (u32)(aic_get_time_us() - start_us));
    return 0;
}

int nand_verify_multi_write(void)
{
    uint32_t i, n, base;
    uint64_t start_us;

    if (!g_nand) return -1;

    n = g_nand->pages_per_block;
    base = g_test_block * n;

    memset(write_ptr, 0xA5, g_nand->page_size);
    rt_mtd_nand_erase_block(g_nand, g_test_block);

    start_us = aic_get_time_us();
    for (i = 0; i < n; i++) {
        if (rt_mtd_nand_write(g_nand, base + i, write_ptr, g_nand->page_size, NULL, 0)) {
            rt_kprintf("multi page write failed at page %d\n", (int)i);
            return -1;
        }
    }
    show_speed("multi page write", n * g_nand->page_size,
        (u32)(aic_get_time_us() - start_us));
    return 0;
}

/* --- block erase --- */

int nand_verify_erase(void)
{
    uint64_t start_us;
    u32 size;

    if (!g_nand) return -1;

    size = g_nand->page_size * g_nand->pages_per_block;

    start_us = aic_get_time_us();
    if (rt_mtd_nand_erase_block(g_nand, g_test_block)) {
        rt_kprintf("block erase failed\n");
        return -1;
    }
    show_speed("erase 1 block", size, (u32)(aic_get_time_us() - start_us));
    return 0;
}

/* --- 1MB R/W --- */

static int count_good_blocks(void)
{
    int i, cnt = 0;

    for (i = (int)(g_nand->block_end - g_nand->block_start) - 1; i >= 0; i--) {
        if (!rt_mtd_nand_check_block(g_nand, i))
            cnt++;
    }
    return cnt;
}

int nand_verify_large_read(void)
{
    u32 size = 0, block_size;
    u32 target = 1024 * 1024;
    int j, b;
    uint64_t start_us;

    if (!g_nand) return -1;

    block_size = g_nand->page_size * g_nand->pages_per_block;
    if (count_good_blocks() * block_size < target) {
        rt_kprintf("not enough good blocks for 1MB read\n");
        return -1;
    }

    start_us = aic_get_time_us();
    for (b = g_nand->block_end - g_nand->block_start - 1; b >= 0 && size < target; b--) {
        if (rt_mtd_nand_check_block(g_nand, b))
            continue;
        for (j = 0; j < (int)g_nand->pages_per_block && size < target; j++) {
            if (rt_mtd_nand_read(g_nand, b * g_nand->pages_per_block + j,
                read_ptr, g_nand->page_size, NULL, 0)) {
                rt_kprintf("1MB read failed\n");
                return -1;
            }
            size += g_nand->page_size;
        }
    }
    show_speed("1MB read", size, (u32)(aic_get_time_us() - start_us));
    return 0;
}

int nand_verify_large_write(void)
{
    u32 size = 0, block_size;
    u32 target = 1024 * 1024;
    int j, b;
    uint64_t start_us;

    if (!g_nand) return -1;

    block_size = g_nand->page_size * g_nand->pages_per_block;
    if (count_good_blocks() * block_size < target) {
        rt_kprintf("not enough good blocks for 1MB write\n");
        return -1;
    }

    memset(write_ptr, 0xA5, g_nand->page_size);

    for (b = g_nand->block_end - g_nand->block_start - 1; b >= 0 && size < target; b--) {
        if (rt_mtd_nand_check_block(g_nand, b))
            continue;
        rt_mtd_nand_erase_block(g_nand, b);
        size += block_size;
    }

    size = 0;
    start_us = aic_get_time_us();
    for (b = g_nand->block_end - g_nand->block_start - 1; b >= 0 && size < target; b--) {
        if (rt_mtd_nand_check_block(g_nand, b))
            continue;
        for (j = 0; j < (int)g_nand->pages_per_block && size < target; j++) {
            if (rt_mtd_nand_write(g_nand, b * g_nand->pages_per_block + j,
                write_ptr, g_nand->page_size, NULL, 0)) {
                rt_kprintf("1MB write failed\n");
                return -1;
            }
            size += g_nand->page_size;
        }
    }
    show_speed("1MB write", size, (u32)(aic_get_time_us() - start_us));
    return 0;
}

/* --- data verify --- */

int nand_verify_data(void)
{
    int i, page;

    if (!g_nand) return -1;

    for (i = 0; i < (int)g_nand->page_size; i++)
        write_ptr[i] = (uint8_t)(i * 0x11);

    rt_mtd_nand_erase_block(g_nand, g_test_block);
    page = g_test_block * g_nand->pages_per_block;
    if (rt_mtd_nand_write(g_nand, page, write_ptr, g_nand->page_size, NULL, 0)) {
        rt_kprintf("verify write failed\n");
        return -1;
    }

    memset(read_ptr, 0, g_nand->page_size);
    if (rt_mtd_nand_read(g_nand, page, read_ptr, g_nand->page_size, NULL, 0)) {
        rt_kprintf("verify read failed\n");
        return -1;
    }

    for (i = 0; i < (int)g_nand->page_size; i++) {
        if (write_ptr[i] != read_ptr[i]) {
            rt_kprintf("data check error at byte %d\n", i);
            return -1;
        }
    }
    rt_kprintf("data check OK\n");
    return 0;
}

/* --- OOB verify --- */

int nand_verify_oob(void)
{
    int i, page, oob_free;
    uint8_t *w_oob, *r_oob;
    uint8_t w_spare[32], r_spare[32];

    if (!g_nand) return -1;

    oob_free = g_nand->oob_free;
    if (oob_free > (int)sizeof(w_spare))
        oob_free = (int)sizeof(w_spare);
    if (oob_free == 0) {
        rt_kprintf("no OOB area available\n");
        return 0;
    }

    w_oob = write_ptr + g_nand->page_size;
    r_oob = read_ptr + g_nand->page_size;
    page = g_test_block * g_nand->pages_per_block;

    /* Probe actual user OOB size — map_user fails if nbytes exceeds layout */
    {
        uint8_t probe_oob[64], probe_spare[32];
        memset(probe_spare, 0, sizeof(probe_spare));
        while (oob_free > 0) {
            memset(probe_oob, 0xFF, sizeof(probe_oob));
            if (rt_mtd_nand_map_user(g_nand, probe_oob, probe_spare, 0, oob_free) == 0)
                break;
            oob_free--;
        }
        if (oob_free == 0) {
            rt_kprintf("no OOB area available\n");
            return 0;
        }
    }

    /* Init entire OOB buffer to 0xFF so non-user bytes keep erased state */
    memset(w_oob, 0xFF, g_nand->oob_size);

    rt_mtd_nand_erase_block(g_nand, g_test_block);

    for (i = 0; i < oob_free; i++)
        w_spare[i] = (uint8_t)(i * 0x11);
    w_spare[0] = 0xFF;

    for (i = 0; i < 10; i++) {
        int p = page + i;
        int ret;

        ret = rt_mtd_nand_map_user(g_nand, w_oob, w_spare, 0, oob_free);
        if (ret) {
            rt_kprintf("OOB map_user failed at page %d\n", p);
            return -1;
        }
        ret = rt_mtd_nand_write(g_nand, p, write_ptr, g_nand->page_size,
                                w_oob, g_nand->oob_size);
        if (ret) {
            rt_kprintf("OOB write failed at page %d\n", p);
            return -1;
        }

        memset(r_oob, 0, g_nand->oob_size);
        memset(r_spare, 0, sizeof(r_spare));
        ret = rt_mtd_nand_read(g_nand, p, read_ptr, g_nand->page_size,
                               r_oob, g_nand->oob_size);
        if (ret) {
            rt_kprintf("OOB read failed at page %d\n", p);
            return -1;
        }

        ret = rt_mtd_nand_unmap_user(g_nand, r_spare, r_oob, 0, oob_free);
        if (ret) {
            rt_kprintf("OOB unmap_user failed at page %d\n", p);
            return -1;
        }
        for (int j = 0; j < oob_free; j++) {
            if (w_spare[j] != r_spare[j]) {
                rt_kprintf("OOB data check error at byte %d, page %d\n", j, p);
                return -1;
            }
        }
    }

    rt_kprintf("OOB data check OK (%d bytes x 10 pages)\n", oob_free);
    return 0;
}

/* --- phase scan --- */

#define INT_TO_DELAY_MODE(x) \
    (((x) == 0) ? "auto" : \
     ((x) == 1) ? "no delay" : \
     ((x) == 2) ? "delay half cycle" : \
     ((x) == 3) ? "delay one cycle" : "unknown")

static int g_phase_ok[4];

int nand_verify_phase(uint32_t phase)
{
    struct rt_qspi_device *qspi;
    struct rt_qspi_configuration qspi_cfg;
    struct rt_qspi_message msg;
    uint8_t data_r[3] = {0x01, 0x02, 0x03};
    int ret, i, j;

    if (!g_nand) return -1;
    if (phase >= ARRAY_SIZE(g_phase_ok)) return -1;

    qspi = (struct rt_qspi_device *)rt_device_find("flash_verify");
    if (!qspi) {
        ret = aic_qspi_bus_attach_device("qspi0", "flash_verify", 0, 4, RT_NULL, RT_NULL);
        if (ret != RT_EOK) {
            rt_kprintf("attach qspi failed\n");
            return -1;
        }
        qspi = (struct rt_qspi_device *)rt_device_find("flash_verify");
        if (!qspi) return -1;
    }

    rt_memset(&qspi_cfg, 0, sizeof(qspi_cfg));
    qspi_cfg.qspi_dl_width = 4;
    qspi_cfg.parent.mode = 0;
    qspi_cfg.parent.max_hz = 50000000;

    ret = rt_qspi_configure(qspi, &qspi_cfg);
    if (ret < 0) return -1;

    rt_spi_set_rx_delay_mode((struct rt_spi_device *)qspi, phase);

    rt_memset(&msg, 0, sizeof(msg));
    msg.instruction.content = 0x01;
    msg.instruction.qspi_lines = 1;
    msg.qspi_data_lines = 1;
    msg.parent.send_buf = data_r;
    msg.parent.length = 3;
    msg.parent.cs_take = 1;
    msg.parent.cs_release = 1;

    rt_spi_take_bus((struct rt_spi_device *)qspi);
    rt_qspi_transfer_message(qspi, &msg);
    rt_spi_release_bus((struct rt_spi_device *)qspi);

    for (i = 0; i < (int)g_nand->page_size; i++)
        write_ptr[i] = (uint8_t)((i * 3 + 0x5A) & 0xFF);

    rt_mtd_nand_erase_block(g_nand, g_test_block);
    rt_mtd_nand_write(g_nand, g_test_block * g_nand->pages_per_block,
        write_ptr, g_nand->page_size, NULL, 0);

    memset(read_ptr, 0, g_nand->page_size);
    rt_mtd_nand_read(g_nand, g_test_block * g_nand->pages_per_block,
        read_ptr, g_nand->page_size, NULL, 0);

    for (j = 0; j < (int)g_nand->page_size; j++) {
        if (read_ptr[j] != write_ptr[j]) {
            g_phase_ok[phase] = 0;
            return -1;
        }
    }

    g_phase_ok[phase] = 1;
    return 0;
}

int nand_verify_phase_scan(void)
{
    int i;

    for (i = 1; i <= 3; i++) {
        nand_verify_phase(i);
    }

    rt_kprintf("\n -------------------------------------------------------------- \n");
    for (i = 1; i <= 3; i++) {
        rt_kprintf("[phase scan] delay mode: [%s] test %s!\n",
            INT_TO_DELAY_MODE(i), g_phase_ok[i] ? "Success" : "Failed");
    }
    rt_kprintf("-------------------------------------------------------------- \n");

    nand_verify_phase(0);
    return 0;
}
