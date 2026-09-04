/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <rtthread.h>
#include <finsh.h>
#include <string.h>
#include "nand_verify_core.h"

static void nand_verify(int argc, char *argv[])
{
    const char *dev = "data";
    int phase;

    if (argc >= 3 && !strncmp(argv[1], "dev", 4)) {
        dev = argv[2];
        argc -= 2;
        argv += 2;
    }

    if (argc >= 2 && !strncmp(argv[1], "phase", 6)) {
        if (argc < 3) {
            rt_kprintf("Usage: nand_verify phase <0-3>\n");
            return;
        }
        phase = atoi(argv[2]);
        if (phase < 0 || phase > 3) {
            rt_kprintf("phase must be 0-3\n");
            return;
        }
        if (nand_verify_init(dev)) return;
        nand_verify_phase(phase);
        nand_verify_deinit();
        return;
    }

    if (argc >= 2 && !strncmp(argv[1], "phase_scan", 11)) {
        if (nand_verify_init(dev)) return;
        nand_verify_phase_scan();
        nand_verify_deinit();
        return;
    }

    if (nand_verify_init(dev)) return;

    rt_kprintf("\n========== NAND Phase Verify ==========\n");
    nand_verify_phase_scan();
    rt_kprintf("\n========== NAND base Verify ==========\n");
    rt_kprintf("Device: %s\n", nand_verify_dev_name());
    nand_verify_single_read();
    nand_verify_single_write();
    nand_verify_data();
    nand_verify_multi_read();
    nand_verify_multi_write();
    nand_verify_erase();
    nand_verify_large_read();
    nand_verify_large_write();
    nand_verify_data();
    rt_kprintf("\n========== NAND OOB Verify ==========\n");
    nand_verify_oob();
    nand_verify_deinit();
}
MSH_CMD_EXPORT(nand_verify, NAND medium verification: nand_verify[dev name]);
