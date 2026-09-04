/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: Jiji.Chen <jiji.chen@artinchip.com>
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <console.h>
#include <aic_common.h>
#include <aic_errno.h>
#include <spinand_port.h>
#include <aic_utils.h>

#define SPINAND_HELP                           \
    "spinand_benchmark command:\n"             \
    "!!! DANGER: This command will erase all data of spinand.\n" \
    "  spinand_benchmark <spi_bus>\n"

#define SPEED_TEST_CNT  5
#define CONT_NUM        16
#define OOB_USER_DATA_NUM 16
#define RESULT_MAX       32

static struct aic_spinand *s_spinand_flash = NULL;
static uint64_t s_total_block = 0;
static u8 *s_read_buf = NULL, *s_write_buf = NULL;
static int s_good_blocks[SPEED_TEST_CNT];
static int s_good_block_cnt;

/* --- result collector --- */
struct result_entry {
    const char *section;
    const char *name;
    int speed_kbs;
    int pass;
};
static struct result_entry s_results[RESULT_MAX];
static int s_result_cnt;

static void result_add(const char *section, const char *name, int speed_kbs, int pass)
{
    if (s_result_cnt < RESULT_MAX) {
        s_results[s_result_cnt].section = section;
        s_results[s_result_cnt].name = name;
        s_results[s_result_cnt].speed_kbs = speed_kbs;
        s_results[s_result_cnt].pass = pass;
        s_result_cnt++;
    }
}

static u32 rand_mod(u32 n)
{
    u32 r = ((u32)rand() << 15) ^ (u32)rand();
    return r % n;
}

static int calc_speed(u32 len, u32 us)
{
    u32 tmp;
    if (us == 0) return 0;
    tmp = 1000 * len;
    tmp = tmp / us;
    tmp = 1000 * tmp;
    return (int)(tmp / 1024);
}

static void spinand_help(void)
{
    puts(SPINAND_HELP);
}

/* --- manufacturer lookup --- */
static const char *get_mf_name(u8 id)
{
    switch (id) {
    case 0xEF: return "Winbond";
    case 0xC8: return "GigaDevice";
    case 0x2C: return "Micron";
    case 0x9D: return "ISSI";
    case 0xC2: return "Macronix";
    case 0x98: return "Toshiba";
    case 0xA1: return "Foresee";
    case 0xF2: return "MK";
    case 0x5E: return "Zbit";
    case 0xE5: return "ESMT";
    case 0x68: return "Boya";
    case 0x0B: return "XTX";
    case 0x51: return "GigaDevice";
    case 0x6B: return "Etron";
    case 0xCD: return "FudanMicro";
    default:   return "Unknown";
    }
}

/* --- verify --- */
static int verify_mem(u8 *src1, u8 *src2, u32 cnt)
{
    for (int i = 0; i < (int)cnt; i++) {
        if (src1[i] != src2[i]) {
            printf("Verify fail! i=%d, 0x%02x != 0x%02x\n", i, src1[i], src2[i]);
            return -1;
        }
    }
    return 0;
}

/* --- prepare good blocks --- */
static void prepare_good_blocks(void)
{
    s_good_block_cnt = 0;
    for (u32 b = 0; b < s_total_block && s_good_block_cnt < SPEED_TEST_CNT; b++) {
        if (!spinand_block_isbad(s_spinand_flash, b))
            s_good_blocks[s_good_block_cnt++] = (int)b;
    }
    for (int i = 0; i < (int)s_spinand_flash->info->page_size; i++)
        s_write_buf[i] = (u8)(rand() & 0xFF);
}

static int do_spinand_init(int spi_bus)
{
    u32 total_page_size;

    s_spinand_flash = spinand_probe(spi_bus);
    if (s_spinand_flash == NULL) {
        printf("Failed to probe spinand flash.\n");
        return -1;
    }

    s_total_block = s_spinand_flash->info->block_per_lun
                    * s_spinand_flash->info->planes_per_lun;

    total_page_size = s_spinand_flash->info->page_size
                      + s_spinand_flash->info->oob_size;
    s_read_buf = aicos_malloc_align(0, total_page_size, CACHE_LINE_SIZE);
    s_write_buf = aicos_malloc_align(0, total_page_size, CACHE_LINE_SIZE);
    if (!s_read_buf || !s_write_buf) {
        printf("Malloc failed.\n");
        if (s_read_buf)  aicos_free_align(0, s_read_buf);
        if (s_write_buf) aicos_free_align(0, s_write_buf);
        s_read_buf = s_write_buf = NULL;
        return -1;
    }

    return 0;
}

/* --- chip info --- */
static int spinand_print_info(void)
{
    u8 *id = s_spinand_flash->id.data;
    const struct aic_spinand_info *info = s_spinand_flash->info;
    u32 capacity_mb;
    u32 total_user_oob = 0;
    int section;
    struct aic_oob_region region;

    capacity_mb = (u64)info->block_per_lun * info->pages_per_eraseblock
                  * info->page_size * info->planes_per_lun / 1024 / 1024;

    printf("\n  Chip Info:\n");
    printf("    Chip:        %s\n", info->sz_description);
    printf("    JEDEC ID:    0x%02X%02X%02X%02X (%s)\n",
           id[0], id[1], id[2], id[3], get_mf_name(id[0]));
    printf("    Page:        %dB + %dB OOB\n",
           info->page_size, info->oob_size);
    printf("    Block:       %dKB (%d pages)\n",
           info->page_size * info->pages_per_eraseblock / 1024,
           info->pages_per_eraseblock);
    printf("    Blocks/LUN:  %d | Planes: %d | Die: %d\n",
           info->block_per_lun, info->planes_per_lun,
           info->is_die_select);
    printf("    Capacity:    %uMB\n", capacity_mb);
    printf("    Data Lines:  %u\n", s_spinand_flash->qspi_dl_width);
    printf("    ECC:         %d bit/%d bytes\n",
           info->ecc_strength, info->page_size);

    /* probe OOB user area */
    if (info->oob_get_user) {
        for (section = 0; section < 16; section++) {
            if (info->oob_get_user(s_spinand_flash, section, &region) != 0)
                break;
            total_user_oob += region.length;
            if (section == 0)
                printf("    OOB User:    [%d] %d~%d",
                       section, region.offset, region.offset + region.length - 1);
            else if (section < 4)
                printf(", [%d] %d~%d",
                       section, region.offset, region.offset + region.length - 1);
        }
        if (section > 4)
            printf(", ... (%d sections, %dB total)", section, total_user_oob);
        printf("\n");
    }

    printf("    ECC:         %d bit/%d bytes\n",
           info->ecc_strength, info->page_size);

    return 0;
}

/* --- bad block scan --- */
static int do_spinand_badblock_check(void)
{
    u32 bad_cnt = 0;

    printf("    scanning...\n");
    for (u32 b = 0; b < s_total_block; b++) {
        if (spinand_block_isbad(s_spinand_flash, b)) {
            printf("    bad block: %d\n", b);
            bad_cnt++;
        }
    }
    result_add("Bad Block", "Found (factory)", bad_cnt, 1);

    return 0;
}

/* --- page read speed: x5 random --- */
static int do_spinand_page_read_speed(void)
{
    u64 total_us = 0;
    int b = s_good_blocks[0];
    int base = b * s_spinand_flash->info->pages_per_eraseblock;

    spinand_block_erase(s_spinand_flash, b);
    for (int p = 0; p < (int)s_spinand_flash->info->pages_per_eraseblock; p++)
        spinand_write_page(s_spinand_flash, base + p, s_write_buf,
                           s_spinand_flash->info->page_size, NULL, 0);

    for (int i = 0; i < SPEED_TEST_CNT; i++) {
        int page = base + (rand() % s_spinand_flash->info->pages_per_eraseblock);
        u64 start_us = aic_get_time_us();
        if (spinand_read_page(s_spinand_flash, page, s_read_buf,
                              s_spinand_flash->info->page_size, NULL, 0)) {
            printf("  Page read failed at page %d\n", page);
            continue;
        }
        total_us += aic_get_time_us() - start_us;
    }
    result_add("Speed", "Page Read",
               calc_speed(s_spinand_flash->info->page_size * SPEED_TEST_CNT,
                          (u32)total_us), 1);
    return 0;
}

/* --- page write speed: x5 random --- */
static int do_spinand_page_write_speed(void)
{
    u64 total_us = 0;
    int b = s_good_blocks[0];
    int base = b * s_spinand_flash->info->pages_per_eraseblock;

    spinand_block_erase(s_spinand_flash, b);

    for (int i = 0; i < SPEED_TEST_CNT; i++) {
        int page = base + (rand() % s_spinand_flash->info->pages_per_eraseblock);
        u64 start_us = aic_get_time_us();
        if (spinand_write_page(s_spinand_flash, page, s_write_buf,
                               s_spinand_flash->info->page_size, NULL, 0)) {
            printf("  Page write failed at page %d\n", page);
            continue;
        }
        total_us += aic_get_time_us() - start_us;
    }
    result_add("Speed", "Page Write",
               calc_speed(s_spinand_flash->info->page_size * SPEED_TEST_CNT,
                          (u32)total_us), 1);
    return 0;
}

/* --- block erase speed: write-then-erase x5 random --- */
static int do_spinand_erase_speed(void)
{
    u64 total_us = 0;
    u32 size = s_spinand_flash->info->page_size
               * s_spinand_flash->info->pages_per_eraseblock;

    for (int i = 0; i < SPEED_TEST_CNT; i++) {
        int b = s_good_blocks[rand() % s_good_block_cnt];
        int base = b * s_spinand_flash->info->pages_per_eraseblock;

        for (int p = 0; p < (int)s_spinand_flash->info->pages_per_eraseblock; p++)
            spinand_write_page(s_spinand_flash, base + p, s_write_buf,
                               s_spinand_flash->info->page_size, NULL, 0);

        u64 start_us = aic_get_time_us();
        if (spinand_block_erase(s_spinand_flash, b)) {
            printf("  Block erase failed at block %d\n", b);
            continue;
        }
        total_us += aic_get_time_us() - start_us;
    }
    result_add("Speed", "Block Erase",
               calc_speed(size * SPEED_TEST_CNT, (u32)total_us), 1);
    return 0;
}

/* --- continuous read speed: x1, 16 pages --- */
static int do_spinand_contread(void)
{
    u8 *r_buf, *w_buf;
    u32 b, page;
    int ret = 0, i;
    u64 start_us;

#ifndef AIC_SPINAND_CONT_READ
    result_add("Speed", "Cont Read (N/A)", -1, 1);
    return 0;
#endif

    do {
        b = rand_mod((u32)s_total_block);
        if (!spinand_block_isbad(s_spinand_flash, b))
            ret = spinand_block_erase(s_spinand_flash, b);
    } while (ret);

    r_buf = aicos_malloc_align(0, s_spinand_flash->info->page_size * CONT_NUM,
                                CACHE_LINE_SIZE);
    w_buf = aicos_malloc_align(0, s_spinand_flash->info->page_size * CONT_NUM,
                                CACHE_LINE_SIZE);
    if (!r_buf || !w_buf) {
        result_add("Speed", "Cont Read", -1, 0);
        goto cont_err;
    }

    for (i = 0; i < (int)(s_spinand_flash->info->page_size * CONT_NUM); i++)
        w_buf[i] = (u8)(rand() & 0xFF);

    page = b * s_spinand_flash->info->pages_per_eraseblock;
    for (i = 0; i < CONT_NUM; i++)
        spinand_write_page(s_spinand_flash, page + i, w_buf,
                           s_spinand_flash->info->page_size, NULL, 0);

    start_us = aic_get_time_us();
    spinand_continuous_read(s_spinand_flash, page, r_buf,
                            s_spinand_flash->info->page_size * CONT_NUM);
    result_add("Speed", "Cont Read",
               calc_speed(s_spinand_flash->info->page_size * CONT_NUM,
                          (u32)(aic_get_time_us() - start_us)), 1);

cont_err:
    if (r_buf) aicos_free_align(0, r_buf);
    if (w_buf) aicos_free_align(0, w_buf);
    return ret;
}

/* --- OOB verify --- */
static int do_spinand_oob_verify(void)
{
    u8 r_buf[OOB_USER_DATA_NUM], w_buf[OOB_USER_DATA_NUM];
    u8 *r_oob, *w_oob;
    u32 block, page;
    int ret = 1, i;

    do {
        block = rand_mod((u32)s_total_block);
        if (!spinand_block_isbad(s_spinand_flash, block))
            ret = spinand_block_erase(s_spinand_flash, block);
    } while (ret);

    r_oob = s_read_buf + s_spinand_flash->info->page_size;
    w_oob = s_write_buf + s_spinand_flash->info->page_size;

    for (i = 1; i < OOB_USER_DATA_NUM; i++)
        w_buf[i] = (u8)(rand() & 0xFF);
    w_buf[0] = 0xFF;

    page = block * s_spinand_flash->info->pages_per_eraseblock;
    for (i = 0; i < 10; i++) {
        int p = page + i;
        ret = spinand_ooblayout_map_user(s_spinand_flash, w_oob, w_buf, 0,
                                         OOB_USER_DATA_NUM);
        if (ret) break;
        ret = spinand_write_page(s_spinand_flash, p, NULL, 0, w_oob,
                                 s_spinand_flash->info->oob_size);
        if (ret) break;
        ret = spinand_read_page(s_spinand_flash, p, NULL, 0, r_oob,
                                s_spinand_flash->info->oob_size);
        if (ret) break;
        ret = spinand_ooblayout_unmap_user(s_spinand_flash, r_buf, r_oob, 0,
                                           OOB_USER_DATA_NUM);
        if (ret) break;
        ret = verify_mem(r_buf, w_buf, OOB_USER_DATA_NUM);
        if (ret) break;
    }
    result_add("Verify", "OOB (10 pages)", -1, ret == 0);
    return ret;
}



/* --- full space verify: every page --- */
static int do_spinand_full_verify(void)
{
    u32 page_count = 0, error_count = 0;
    int ret;

    for (int i = 0; i < (int)s_spinand_flash->info->page_size; i++)
        s_write_buf[i] = (u8)(rand() & 0xFF);

    for (u32 block = 0; block < s_total_block; block++) {
        if (spinand_block_isbad(s_spinand_flash, block))
            continue;
        spinand_block_erase(s_spinand_flash, block);
        u32 page = block * s_spinand_flash->info->pages_per_eraseblock;
        for (u32 p = 0; p < s_spinand_flash->info->pages_per_eraseblock; p++) {
            page_count++;
            ret = spinand_write_page(s_spinand_flash, page + p, s_write_buf,
                                     s_spinand_flash->info->page_size, NULL, 0);
            if (ret) { error_count++; continue; }
            ret = spinand_read_page(s_spinand_flash, page + p, s_read_buf,
                                    s_spinand_flash->info->page_size, NULL, 0);
            if (ret) { error_count++; continue; }
            if (verify_mem(s_write_buf, s_read_buf,
                           s_spinand_flash->info->page_size))
                error_count++;
        }
    }
    result_add("Verify", "Full Space", -1, error_count == 0);
    return 0;
}

/* --- summary table --- */
static void spinand_print_summary(void)
{
    const char *cur_section = NULL;
    int col1_w = 24, col2_w = 14;

    printf("\n");
    printf("==================================================\n");
    printf("  SPI NAND Benchmark Summary\n");
    printf("==================================================\n");

    for (int i = 0; i < s_result_cnt; i++) {
        if (s_results[i].section != cur_section) {
            cur_section = s_results[i].section;
            col1_w = 24; col2_w = 14;
            for (int j = i; j < s_result_cnt && s_results[j].section == cur_section; j++) {
                int w1 = strlen(s_results[j].name);
                int w2 = s_results[j].speed_kbs >= 0 ? 12 : 6;
                if (w1 > col1_w) col1_w = w1;
                if (w2 > col2_w) col2_w = w2;
            }
            printf("\n  %s:\n", cur_section);
            printf("  +-");
            for (int k = 0; k < col1_w; k++) printf("-");
            printf("-+-");
            for (int k = 0; k < col2_w; k++) printf("-");
            printf("-+\n");
        }
        if (s_results[i].speed_kbs >= 0 &&
            strncmp(s_results[i].section, "Speed", 5) == 0) {
            printf("  | %-*s | %*d KB/s |\n",
                   col1_w, s_results[i].name, col2_w - 6,
                   s_results[i].speed_kbs);
        } else if (s_results[i].speed_kbs >= 0) {
            printf("  | %-*s | %*d |\n",
                   col1_w, s_results[i].name, col2_w,
                   s_results[i].speed_kbs);
        } else {
            printf("  | %-*s | %*s |\n",
                   col1_w, s_results[i].name, col2_w,
                   s_results[i].pass ? "PASS" : "FAIL");
        }
        /* separator after every row */
        printf("  +-");
        for (int k = 0; k < col1_w; k++) printf("-");
        printf("-+-");
        for (int k = 0; k < col2_w; k++) printf("-");
        printf("-+\n");
    }

    printf("==================================================\n");
}

/* --- main --- */
static int do_spinand_benchmark(int argc, char *argv[])
{
    if (argc < 2) {
        spinand_help();
        return 0;
    }

    printf("\n");
    printf("=====================================================\n");

    s_result_cnt = 0;

    if (do_spinand_init(strtol(argv[1], NULL, 0)))
        goto free_buf;

    spinand_print_info();

    printf("  Bad block scan...\n");
    do_spinand_badblock_check();

    prepare_good_blocks();
    if (s_good_block_cnt < SPEED_TEST_CNT) {
        printf("Not enough good blocks (%d < %d), skip speed tests.\n",
               s_good_block_cnt, SPEED_TEST_CNT);
        goto free_buf;
    }

    printf("  Page read speed...\n");
    do_spinand_page_read_speed();
    printf("  Page write speed...\n");
    do_spinand_page_write_speed();
    printf("  Block erase speed...\n");
    do_spinand_erase_speed();
    printf("  Cont read speed...\n");
    do_spinand_contread();
    printf("  OOB verify...\n");
    do_spinand_oob_verify();
    printf("  Full space verify...\n");
    do_spinand_full_verify();

    spinand_print_summary();

free_buf:
    if (s_read_buf)
        aicos_free_align(0, s_read_buf);
    if (s_write_buf)
        aicos_free_align(0, s_write_buf);
    return 0;
}

CONSOLE_CMD(spinand_benchmark, do_spinand_benchmark, "SPI NAND flash benchmark.");
