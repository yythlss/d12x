/*
 * Copyright (c) 2026 ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: xyg <yiguan.xu@artinchip.com>
 */

#ifndef __DUMMY_CODEC_H__
#define __DUMMY_CODEC_H__

/* ULOG tag and level must be defined before including any headers */
#undef LOG_TAG
#undef LOG_LVL
#define LOG_TAG "dummy_codec"
#define LOG_LVL LOG_LVL_INFO

#include "codec.h"

#define DUMMY_CODEC_RATE_MIN     I2S_SAMPLE_RATE_8000
#define DUMMY_CODEC_RATE_MAX     I2S_SAMPLE_RATE_192000
#define DUMMY_CODEC_CHANNELS_MIN I2S_TDM_CHANNEL_1
#define DUMMY_CODEC_CHANNELS_MAX I2S_TDM_CHANNEL_16
#define DUMMY_CODEC_WIDTH_MIN    I2S_SAMPLE_WIDTH_8BIT
#define DUMMY_CODEC_WIDTH_MAX    I2S_SAMPLE_WIDTH_32BIT
#define DUMMY_CODEC_VOLUME_MAX   100
#define DUMMY_CODEC_VOLUME_MIN   0

struct dummy_codec_stats {
    uint32_t init_count;
    uint32_t start_count;
    uint32_t stop_count;
    uint32_t set_rate_count;
    uint32_t set_channel_count;
    uint32_t set_width_count;
    uint32_t set_volume_count;
    uint32_t set_protocol_count;
    uint32_t set_polarity_count;
    uint32_t set_mclk_count;
    uint32_t set_sclk_count;
    uint32_t pa_power_count;
    uint32_t dump_reg_count;
    uint32_t error_count;
    uint32_t param_reject_count;
};

struct dummy_codec_perf_stats {
    uint32_t init_count;
    uint32_t init_total_ticks;
    uint32_t init_max_ticks;
    uint32_t init_min_ticks;

    uint32_t start_count;
    uint32_t start_total_ticks;
    uint32_t start_max_ticks;
    uint32_t start_min_ticks;

    uint32_t stop_count;
    uint32_t stop_total_ticks;
    uint32_t stop_max_ticks;
    uint32_t stop_min_ticks;

    uint32_t set_rate_count;
    uint32_t set_rate_total_ticks;
    uint32_t set_rate_max_ticks;
    uint32_t set_rate_min_ticks;

    uint32_t lock_wait_count;
    uint32_t lock_wait_total_ticks;
    uint32_t lock_wait_max_ticks;
    uint32_t lock_wait_min_ticks;
};

struct dummy_codec_priv {
    i2s_stream_t cur_stream;
    uint32_t cur_rate;
    uint32_t cur_channel;
    uint32_t cur_width;
    uint32_t cur_slot_width;
    uint32_t cur_protocol;
    uint32_t cur_polarity;
    uint32_t cur_mclk_nfs;
    uint32_t cur_sclk_nfs;
    uint8_t cur_volume;
    uint8_t pa_enabled;
    rt_bool_t initialized;
    rt_bool_t is_running;
    struct rt_mutex mutex;
    struct dummy_codec_stats stats;
    struct dummy_codec_perf_stats perf;
};

struct dummy_codec_priv *dummy_codec_get_instance(void);

void dummy_codec_reset_stats(void);

void dummy_codec_dump_status(void);

void dummy_codec_dump_perf(void);

void dummy_codec_reset_perf(void);

#endif /* __DUMMY_CODEC_H__ */
