/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_adc.c - 模拟量采集模块（模拟量示波数据源）
 *
 * 默认使用"自测信号源"(SYNTH)：一个内部定时线程产生正弦/方波样本，
 * 经 dbg_msg 总线投递给示波界面——无需任何外设即可在板载屏上
 * 验证"示波界面 + 线程 + 消息队列"的完整链路。
 *
 * 若编译配置启用了片内 GPAI（AIC_GPAI_DRV），可切到 DBG_ADC_SRC_GPAI
 * 读取真实 ADC 通道电压；GPAI 未启用或设备找不到时自动回退 SYNTH。
 */
#include <rtthread.h>
#include <string.h>
#include "dbg_adc.h"
#include "dbg_msg.h"

#ifdef AIC_GPAI_DRV
#include <rtdevice.h>
#include "hal_adcim.h"
#include "hal_gpai.h"
#define DBG_ADC_GPAI_DEV_NAME  "gpai"
#define DBG_ADC_VOLTAGE_SCALE  10000   /* 电压精度：1 = 0.1mV */
#endif

/* 12-bit ADC 满量程（GPAI/SYNTH 共用量程） */
#define DBG_ADC_MAX_ADC        ((1 << 12) - 1)

/* 采样周期 ms（50ms -> 20 样本/秒，供示波曲线刷新） */
#define DBG_ADC_SAMPLE_PERIOD_MS  50

static rt_thread_t g_adc_thread = RT_NULL;
static rt_mutex_t  g_adc_lock   = RT_NULL;

static volatile int g_running = 0;
static volatile int g_source  = DBG_ADC_SRC_SYNTH;
static volatile rt_uint8_t g_chan = 0;

#ifdef AIC_GPAI_DRV
static rt_adc_device_t g_gpai = RT_NULL;
#endif

/* ---------------- 自测信号源：正弦 + 可辨识方波 -------------------- */
static unsigned int g_phase;   /* 相位累加器 */

static void synth_next(rt_uint16_t *adc_val, rt_uint16_t *mv)
{
    unsigned int deg = g_phase % 360;
    /* 方波：上半周期高电平、下半周期低电平，幅度取量程 ±30%，
     * 叠加在 50% 电平上，波形起伏明显，便于无外设时直观确认示波刷新。 */
    int level = (DBG_ADC_MAX_ADC / 2)
                + (int)((DBG_ADC_MAX_ADC / 2) * 0.6f
                        * ((deg < 180) ? +1.0f : -1.0f));

    if (adc_val)
        *adc_val = (rt_uint16_t)level;
    if (mv) {
        /* 量程 3.3V -> mV 换算 */
        rt_uint32_t v = ((rt_uint32_t)level * 3300U) / (rt_uint32_t)DBG_ADC_MAX_ADC;
        *mv = (rt_uint16_t)v;
    }
    g_phase += 5;   /* 每样本前进 5°，曲线平滑移动 */
    if (g_phase >= 360u)
        g_phase -= 360u;
}

/* ---------------- 消息打包：经总线投递给示波界面 ------------------- */
static void adc_post(rt_uint16_t adc_val, rt_uint16_t mv, rt_uint8_t ch)
{
    dbg_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = DBG_MSG_ADC_VAL;
    msg.len  = 6;
    msg.data[DBG_ADC_MSG_CH_SRC]   = (rt_uint8_t)g_source;
    msg.data[DBG_ADC_MSG_CH_ADC_LO] = (rt_uint8_t)(adc_val & 0xFF);
    msg.data[DBG_ADC_MSG_CH_ADC_HI] = (rt_uint8_t)(adc_val >> 8);
    msg.data[DBG_ADC_MSG_CH_MV_LO]  = (rt_uint8_t)(mv & 0xFF);
    msg.data[DBG_ADC_MSG_CH_MV_HI]  = (rt_uint8_t)(mv >> 8);
    msg.data[DBG_ADC_MSG_CH_NO]     = ch;
    dbg_msg_send(&msg);
}

/* ---------------- 采样线程 ----------------------------------------- */
static void adc_thread_entry(void *param)
{
    (void)param;

    while (1) {
        rt_mutex_take(g_adc_lock, RT_WAITING_FOREVER);

        if (g_running && g_source == DBG_ADC_SRC_GPAI) {
#ifdef AIC_GPAI_DRV
            struct aic_gpai_ch_info info;

            if (g_gpai != RT_NULL) {
                memset(&info, 0, sizeof(info));
                info.chan_id = g_chan;
                /* 轮询读取指定通道 */
                if (rt_adc_control(g_gpai, RT_ADC_CMD_GET_CH_INFO,
                                   (void *)&info) == RT_EOK
                    && info.fifo_valid_cnt > 0) {
                    rt_uint16_t adc_val = info.adc_values[info.fifo_valid_cnt - 1];
                    rt_uint16_t mv = 0;
                    rt_uint32_t cal;

                    cal = hal_adcim_auto_calibration();
                    int v = hal_adcim_adc2voltage(&adc_val, cal,
                                                  DBG_ADC_VOLTAGE_SCALE, 3);
                    if (v > 0)
                        mv = (rt_uint16_t)((rt_uint32_t)v / 10);  /* scale=10000 -> mV */
                    adc_post(adc_val, mv, g_chan);
                }
            }
#endif
        } else if (g_running) {
            rt_uint16_t adc_val = 0, mv = 0;
            synth_next(&adc_val, &mv);
            adc_post(adc_val, mv, (rt_uint8_t)g_chan);
        }

        rt_mutex_release(g_adc_lock);
        rt_thread_mdelay(DBG_ADC_SAMPLE_PERIOD_MS);
    }
}

/* ---------------- 对外接口 ----------------------------------------- */
int dbg_adc_init(void)
{
    if (g_adc_thread != RT_NULL)
        return RT_EOK;

    g_adc_lock = rt_mutex_create("dbg_adc", RT_IPC_FLAG_PRIO);
    if (g_adc_lock == RT_NULL)
        return -RT_ERROR;

#ifdef AIC_GPAI_DRV
    g_gpai = (rt_adc_device_t)rt_device_find(DBG_ADC_GPAI_DEV_NAME);
    if (g_gpai == RT_NULL) {
        /* GPAI 驱动未编译或设备未注册，自动使用自测信号源 */
        rt_kprintf("dbg_adc: %s not found, use SYNTH source.\n",
                   DBG_ADC_GPAI_DEV_NAME);
        g_source = DBG_ADC_SRC_SYNTH;
    } else {
        g_source = DBG_ADC_SRC_GPAI;
    }
#else
    g_source = DBG_ADC_SRC_SYNTH;
#endif

    g_adc_thread = rt_thread_create("dbg_adc", adc_thread_entry, RT_NULL,
                                    2048, 23, 10);
    if (g_adc_thread == RT_NULL) {
        rt_mutex_delete(g_adc_lock);
        g_adc_lock = RT_NULL;
        return -RT_ERROR;
    }
    rt_thread_startup(g_adc_thread);

    /* 默认开跑，便于直接进示波页看到曲线 */
    dbg_adc_run(1);
    rt_kprintf("dbg_adc: source=%s, channel=%u\n",
               dbg_adc_source_name(), (unsigned)g_chan);
    return RT_EOK;
}

int dbg_adc_deinit(void)
{
    g_running = 0;
    if (g_adc_thread != RT_NULL) {
        rt_thread_delete(g_adc_thread);
        g_adc_thread = RT_NULL;
    }
    if (g_adc_lock != RT_NULL) {
        rt_mutex_delete(g_adc_lock);
        g_adc_lock = RT_NULL;
    }
    return RT_EOK;
}

int dbg_adc_set_source(int src)
{
    rt_mutex_take(g_adc_lock, RT_WAITING_FOREVER);

    if (src == DBG_ADC_SRC_GPAI) {
#ifdef AIC_GPAI_DRV
        if (g_gpai == RT_NULL) {
            rt_mutex_release(g_adc_lock);
            return -RT_ERROR;      /* GPAI 不可用 */
        }
        g_source = DBG_ADC_SRC_GPAI;
#else
        rt_mutex_release(g_adc_lock);
        return -RT_ERROR;
#endif
    } else {
        g_source = DBG_ADC_SRC_SYNTH;
    }

    rt_mutex_release(g_adc_lock);
    return RT_EOK;
}

int dbg_adc_get_source(void)
{
    return (int)g_source;
}

int dbg_adc_set_channel(rt_uint8_t ch)
{
    if (ch >= DBG_ADC_CH_NUM)
        return -RT_EINVAL;

    rt_mutex_take(g_adc_lock, RT_WAITING_FOREVER);
    g_chan = ch;
    rt_mutex_release(g_adc_lock);
    return RT_EOK;
}

rt_uint8_t dbg_adc_get_channel(void)
{
    return (rt_uint8_t)g_chan;
}

int dbg_adc_run(int on)
{
    rt_mutex_take(g_adc_lock, RT_WAITING_FOREVER);
    g_running = on ? 1 : 0;
    if (on)
        g_phase = 0;   /* 重新开始时相位归零，曲线从头部重绘 */
    rt_mutex_release(g_adc_lock);
    return RT_EOK;
}

int dbg_adc_is_running(void)
{
    return (int)g_running;
}

const char *dbg_adc_source_name(void)
{
    return (g_source == DBG_ADC_SRC_GPAI) ? "GPAI" : "SYNTH";
}