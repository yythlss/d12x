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
#include <rtdevice.h>
#include "dbg_power.h"

#ifdef AIC_GPAI_DRV
#include "hal_adcim.h"
#include "hal_gpai.h"
#define DBG_POWER_GPAI_DEV_NAME  "gpai"
#define DBG_POWER_VOLTAGE_SCALE  10000   /* 电压精度：1 = 0.1mV */
#endif

static rt_adc_device_t g_adc = RT_NULL;
static int             g_available = 0;
static rt_uint8_t      g_pct = 0;
static rt_uint16_t     g_mv  = 0;
static int             g_suspended = 0;

int dbg_power_init(void)
{
#ifdef AIC_GPAI_DRV
    g_adc = (rt_adc_device_t)rt_device_find(DBG_POWER_GPAI_DEV_NAME);
    if (g_adc != RT_NULL) {
        rt_adc_enable(g_adc, DBG_BAT_ADC_CH);
        g_available = 1;
        rt_kprintf("dbg_power: battery monitor on %s ch%d\n",
                   DBG_POWER_GPAI_DEV_NAME, DBG_BAT_ADC_CH);
    } else {
        rt_kprintf("dbg_power: %s not found, battery N/A "
                   "(enable AIC_USING_GPAI in menuconfig)\n", DBG_POWER_GPAI_DEV_NAME);
    }
#else
    rt_kprintf("dbg_power: AIC_GPAI_DRV not enabled, battery N/A\n");
#endif
    return RT_EOK;
}

int dbg_power_available(void)
{
    return g_available;
}

void dbg_power_sample(void)
{
#ifdef AIC_GPAI_DRV
    if (g_adc != RT_NULL) {
        struct aic_gpai_ch_info info;

        memset(&info, 0, sizeof(info));
        info.chan_id = DBG_BAT_ADC_CH;
        if (rt_adc_control(g_adc, RT_ADC_CMD_GET_CH_INFO, (void *)&info) == RT_EOK
            && info.fifo_valid_cnt > 0) {
            rt_uint16_t adc_val = info.adc_values[info.fifo_valid_cnt - 1];
            rt_uint32_t cal = hal_adcim_auto_calibration();
            int v = hal_adcim_adc2voltage(&adc_val, cal, DBG_POWER_VOLTAGE_SCALE, 3);

            if (v > 0) {
                rt_uint32_t pin_mv = (rt_uint32_t)v / 10;          /* scale=10000 → mV */
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
            }
        }
    }
#endif
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
