/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_power.c - 电量监测 + 低功耗
 *
 * 电量读取流程与示波页（dbg_adc.c）一致：经 rt_adc 设备 "gpai" 取指定通道
 * 原始值，再用 hal_adcim_adc2voltage() 换算成 mV；随后按分压比还原电池组电压，
 * 并按单节区间计算百分比。GPAI 未启用时优雅降级（available=0，UI 显示 N/A）。
 */
#include <rtthread.h>
#include <rtdevice.h>   /* rt_pm_request / PM_SLEEP_MODE_LIGHT（经 drivers/pm.h） */
#include "dbg_power.h"
#include "dbg_adc.h"   /* 线程安全的 GPAI 读电压接口（与示波共用锁） */

static int             g_available = 0;
static rt_uint8_t      g_pct = 0;
static rt_uint16_t     g_mv  = 0;
static int             g_suspended = 0;

int dbg_power_init(void)
{
    dbg_power_sample();   /* 先采一次，确定 available */
    if (g_available)
        rt_kprintf("dbg_power: battery monitor on GPAI ch%d\n", DBG_BAT_ADC_CH);
    else
        rt_kprintf("dbg_power: battery N/A (enable GPAI in menuconfig, "
                   "set DBG_BAT_ADC_CH)\n");
    return RT_EOK;
}

int dbg_power_available(void)
{
    return g_available;
}

void dbg_power_sample(void)
{
    rt_uint16_t pin_mv = 0;

    if (dbg_adc_read_mv(DBG_BAT_ADC_CH, &pin_mv) != RT_EOK) {
        g_available = 0;   /* GPAI 未启用 / 通道无效 */
        return;
    }
    if (pin_mv == 0) {
        g_available = 0;
        return;
    }

    {
        rt_uint32_t pack_mv = pin_mv * DBG_BAT_VDIV_NUM / DBG_BAT_VDIV_DEN;
        rt_uint32_t cell_mv = pack_mv / DBG_BAT_CELLS;
        rt_uint32_t pct;

        g_mv = (rt_uint16_t)pack_mv;

        if (cell_mv <= DBG_BAT_CELL_EMPTY_MV)
            pct = 0;
        else if (cell_mv >= DBG_BAT_CELL_FULL_MV)
            pct = 100;
        else
            pct = (cell_mv - DBG_BAT_CELL_EMPTY_MV) * 100
                   / (DBG_BAT_CELL_FULL_MV - DBG_BAT_CELL_EMPTY_MV);

        g_pct = (rt_uint8_t)pct;
        g_available = 1;
    }
}

rt_uint8_t dbg_power_get_pct(void)  { return g_pct; }
rt_uint16_t dbg_power_get_mv(void)  { return g_mv; }

void dbg_power_suspend(void)
{
    if (g_suspended)
        return;
    g_suspended = 1;
    /* 请求轻睡眠：系统空闲时进入 LIGHT 模式，可被触摸/按键等中断唤醒 */
    rt_pm_request(PM_SLEEP_MODE_LIGHT);
    rt_kprintf("[power] suspend -> LIGHT sleep (tap screen to wake)\n");
}

void dbg_power_resume(void)
{
    if (!g_suspended)
        return;
    rt_pm_release(PM_SLEEP_MODE_LIGHT);
    g_suspended = 0;
    rt_kprintf("[power] resume\n");
}

int dbg_power_is_suspended(void)
{
    return g_suspended;
}
