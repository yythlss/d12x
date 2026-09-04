/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#ifdef RT_USING_PM
#include <drivers/pm.h>
#endif
#include "aic_core.h"
#include "aic_hal_clk.h"
#include "hal_tsen.h"

/* Temperature thresholds in 0.1 degrees Celsius */
#define TEMP_HIGH               900     /* 90 C */
#define TEMP_MEDIUM             700     /* 70 C */
#define DVFS_THREAD_STACK       2048
#define DVFS_THREAD_PRIO        20
#define DVFS_POLL_INTERVAL      (RT_TICK_PER_SECOND * 2)
#define DVFS_DEBOUNCE_DELAY     (RT_TICK_PER_SECOND * 1)

static rt_uint8_t g_cur_mode;

/*
 * Read current temperature and return the target run mode.
 * Returns -1 on read failure.
 */
static int dvfs_get_target_mode(struct aic_tsen_ch *chan)
{
    s32 temp;

    if (hal_tsen_get_temp(chan, &temp) != 0)
        return -1;

    rt_kprintf("temp:%3d.%d C, mode:%d\n",
               temp / 10, temp % 10, g_cur_mode);

    if (temp >= TEMP_HIGH)
        return PM_RUN_MODE_LOW_SPEED;
    else if (temp >= TEMP_MEDIUM)
        return PM_RUN_MODE_MEDIUM_SPEED;
    else
        return PM_RUN_MODE_HIGH_SPEED;
}

static void dvfs_apply_mode(int target_mode)
{
    const char *desc;

    if (target_mode == PM_RUN_MODE_LOW_SPEED) {
        desc = "Low speed (24 MHz)";
        rt_kprintf("temp >= %d C, switch to %s\n", TEMP_HIGH / 10, desc);
    } else if (target_mode == PM_RUN_MODE_MEDIUM_SPEED) {
        desc = "Medium speed (160 MHz)";
        rt_kprintf("temp >= %d C, switch to %s\n", TEMP_MEDIUM / 10, desc);
    } else {
        desc = "High speed (480 MHz)";
        rt_kprintf("temp < %d C, restore %s\n", TEMP_MEDIUM / 10, desc);
    }

    rt_pm_run_enter(target_mode);
    g_cur_mode = target_mode;
}

static void dvfs_thread_entry(void *param)
{
    struct aic_tsen_ch *chan;
    int target_mode, confirmed_mode;

    chan = hal_tsen_get_valid_ch(0);
    if (!chan) {
        rt_kprintf("tsen: CPU channel not found\n");
        return;
    }
    hal_tsen_ch_init(chan, chan->pclk_rate);
    hal_tsen_curve_fitting(chan);
    rt_kprintf("dvfs monitor started\n");

    while (1) {
        target_mode = dvfs_get_target_mode(chan);
        if (target_mode < 0)
            goto next_cycle;

        /* Only debounce when a mode change is needed */
        if (target_mode != g_cur_mode) {
            /* Wait and re-check to filter out temperature jitter */
            rt_thread_mdelay(DVFS_DEBOUNCE_DELAY);

            confirmed_mode = dvfs_get_target_mode(chan);
            if (confirmed_mode < 0)
                goto next_cycle;

            /* Apply only if both readings agree */
            if (confirmed_mode == target_mode) {
#ifdef RT_USING_PM
                dvfs_apply_mode(confirmed_mode);
#endif
            }
        }

next_cycle:
        rt_thread_mdelay(DVFS_POLL_INTERVAL);
    }
}

static int dvfs_init(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("dvfs", dvfs_thread_entry, RT_NULL,
                           DVFS_THREAD_STACK, DVFS_THREAD_PRIO, 10);
    if (!tid) {
        rt_kprintf("failed to create dvfs thread\n");
        return -1;
    }
    rt_thread_startup(tid);
    return 0;
}
INIT_APP_EXPORT(dvfs_init);
