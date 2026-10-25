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
#include "dbg_slave.h"      /* RS485 数据源：复用网关请求/结果接口 */
#include "dbg_modbus.h"     /* MODBUS_FC_READ_HOLD_REGS */

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

/* RS485 数据源：仅在示波页前台且数据源为 RS485 时发请求；
 * g_rs485_pending 保证同一时刻最多一个在途请求，波形的刷新节奏由 RS485
 * 往返时间自然决定（不堆积请求、不拖垮轮询线程）。 */
static volatile int g_rs485_active  = 0;
static volatile int g_rs485_pending = 0;

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
        } else if (g_running && g_source == DBG_ADC_SRC_RS485) {
            /* RS485 源：每个采样周期发一次读请求（无在途请求时才发），
             * 应答回到 dbg_adc_on_rs485_result() 后投递一个波形样本。
             * rate 由 RS485 往返时间决定，至多一个在途请求，不会堆积。 */
            if (g_rs485_active && !g_rs485_pending) {
                dbg_rs485_req_t base;
                dbg_rs485_req_t req;

                dbg_slave_rs485_get_last(&base);
                memset(&req, 0, sizeof(req));
                req.func = MODBUS_FC_READ_HOLD_REGS;
                req.addr = base.addr;
                req.reg  = (rt_uint16_t)(base.reg + g_chan);
                req.qty  = 1;          /* 单寄存器 = 一个样本 */

                if (dbg_slave_rs485_request(&req) == RT_EOK)
                    g_rs485_pending = 1;
            }
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
    } else if (src == DBG_ADC_SRC_RS485) {
        /* RS485 源不依赖 GPAI，只要从机轮询线程已起即可（dbg_slave_init 默认起）。
         * 切到 RS485 时清掉在途请求，避免上一页遗留的请求被误消费。 */
        g_rs485_pending = 0;
        g_source = DBG_ADC_SRC_RS485;
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
    if (g_source == DBG_ADC_SRC_GPAI)
        return "GPAI";
    if (g_source == DBG_ADC_SRC_RS485)
        return "RS485";
    return "SYNTH";
}

int dbg_adc_gpai_available(void)
{
#ifdef AIC_GPAI_DRV
    return (g_gpai != RT_NULL) ? 1 : 0;
#else
    return 0;
#endif
}

void dbg_adc_rs485_active(int on)
{
    rt_mutex_take(g_adc_lock, RT_WAITING_FOREVER);
    g_rs485_active = on ? 1 : 0;
    if (!on)
        g_rs485_pending = 0;   /* 离台即丢弃在途请求，避免误消费/空转 */
    rt_mutex_release(g_adc_lock);
}

/* 由消息总线在 DBG_MSG_RS485_RESULT 时调用：只消费示波自己发出的请求。
 * 复用 dbg_slave_rs485_get_result() 取回完整应答（含 rsp 字节，消息体只带状态）。 */
void dbg_adc_on_rs485_result(void)
{
    dbg_rs485_result_t res;
    rt_uint16_t reg_val;

    if (!g_rs485_pending)      /* 不是示波发出的请求（属于扫描页），放行给扫描页 */
        return;
    g_rs485_pending = 0;

    if (dbg_slave_rs485_get_result(&res) != RT_EOK)
        return;
    if (res.status != DBG_RS485_ST_OK || res.in_len < 5)
        return;
    if (res.rsp[1] != MODBUS_FC_READ_HOLD_REGS)   /* 仅处理 FC03 读应答 */
        return;
    if (res.rsp[2] < 2)        /* 至少 1 个寄存器（2 字节） */
        return;

    /* 第一个寄存器值（大端）即波形样本；寄存器为 16-bit，量程 0..65535。
     * 把同一值同时写入 mv 字段，供示波页在 RS485 模式下显示原始值。 */
    reg_val = (rt_uint16_t)((res.rsp[3] << 8) | res.rsp[4]);
    adc_post(reg_val, reg_val, (rt_uint8_t)g_chan);
}

/* 线程安全地读取指定 GPAI 通道电压（mV）。
 * 关键：与示波采样线程共用 g_adc_lock —— gpai 驱动内部无锁，
 * 两个线程同时 rt_adc_control(GET_CH_INFO) 抽同一个FIFO 会竞态。 */
int dbg_adc_read_mv(rt_uint8_t ch, rt_uint16_t *mv)
{
    if (mv == RT_NULL)
        return -RT_EINVAL;
    *mv = 0;

#ifdef AIC_GPAI_DRV
    if (g_gpai != RT_NULL && ch < DBG_ADC_CH_NUM) {
        struct aic_gpai_ch_info info;
        rt_uint16_t adc_val;
        rt_uint32_t cal;
        int v = 0;

        rt_mutex_take(g_adc_lock, RT_WAITING_FOREVER);
        memset(&info, 0, sizeof(info));
        info.chan_id = ch;
        if (rt_adc_control(g_gpai, RT_ADC_CMD_GET_CH_INFO, (void *)&info) == RT_EOK
            && info.fifo_valid_cnt > 0) {
            adc_val = info.adc_values[info.fifo_valid_cnt - 1];
            cal = hal_adcim_auto_calibration();
            v = hal_adcim_adc2voltage(&adc_val, cal, DBG_ADC_VOLTAGE_SCALE, 3);
            if (v > 0)
                *mv = (rt_uint16_t)((rt_uint32_t)v / 10);  /* scale=10000 -> mV */
        }
        rt_mutex_release(g_adc_lock);
        return RT_EOK;
    }
#endif
    return -RT_ERROR;
}