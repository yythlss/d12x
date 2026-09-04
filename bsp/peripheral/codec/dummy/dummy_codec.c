/*
 * Copyright (c) 2026 ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: xyg <yiguan.xu@artinchip.com>
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <aic_core.h>
#include "codec.h"
#include "hal_i2s_format.h"
#include "dummy_codec.h"
#include <rtdbg.h>

static struct dummy_codec_priv g_dummy_priv = { 0 };

/* Helper: convert stream enum to string */
static const char *stream_to_str(i2s_stream_t stream)
{
    switch (stream) {
        case I2S_STREAM_PLAYBACK:
            return "PLAYBACK";
        case I2S_STREAM_RECORD:
            return "RECORD";
        default:
            return "UNKNOWN";
    }
}

/* Helper: validate sample rate */
static rt_bool_t is_valid_rate(uint32_t rate)
{
    switch (rate) {
        case I2S_SAMPLE_RATE_8000:
        case I2S_SAMPLE_RATE_11025:
        case I2S_SAMPLE_RATE_16000:
        case I2S_SAMPLE_RATE_22050:
        case I2S_SAMPLE_RATE_24000:
        case I2S_SAMPLE_RATE_32000:
        case I2S_SAMPLE_RATE_44100:
        case I2S_SAMPLE_RATE_48000:
        case I2S_SAMPLE_RATE_96000:
        case I2S_SAMPLE_RATE_192000:
        case I2S_SAMPLE_RATE_256000:
            return RT_TRUE;
        default:
            return RT_FALSE;
    }
}

/* Helper: validate sample width */
static rt_bool_t is_valid_width(uint32_t width)
{
    switch (width) {
        case I2S_SAMPLE_WIDTH_8BIT:
        case I2S_SAMPLE_WIDTH_12BIT:
        case I2S_SAMPLE_WIDTH_16BIT:
        case I2S_SAMPLE_WIDTH_20BIT:
        case I2S_SAMPLE_WIDTH_24BIT:
        case I2S_SAMPLE_WIDTH_28BIT:
        case I2S_SAMPLE_WIDTH_32BIT:
            return RT_TRUE;
        default:
            return RT_FALSE;
    }
}

/* Helper: validate channel count */
static rt_bool_t is_valid_channel(uint32_t channel)
{
    return (channel >= DUMMY_CODEC_CHANNELS_MIN && channel <= DUMMY_CODEC_CHANNELS_MAX);
}

/* Helper: update min/max/avg performance counters */
static void update_perf_stats(uint32_t *count, uint32_t *total, uint32_t *max, uint32_t *min,
                              uint32_t duration)
{
    (*count)++;
    *total += duration;

    if (duration > *max)
        *max = duration;
    if (duration < *min || *min == 0)
        *min = duration;
}

static int dummy_codec_init(struct codec *codec)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;
    int ret = 0;
    uint32_t tick_start, tick_end;

    /* Parameter validation */
    if (!codec) {
        LOG_E("codec parameter is NULL");
        return -EINVAL;
    }

    /* Acquire mutex and record start time */
    tick_start = rt_tick_get();
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    LOG_I("initializing dummy codec...");

    /* Set default audio parameters */
    priv->cur_rate = I2S_SAMPLE_RATE_48000;
    priv->cur_channel = I2S_TDM_CHANNEL_2;
    priv->cur_width = I2S_SAMPLE_WIDTH_16BIT;
    priv->cur_slot_width = I2S_SAMPLE_WIDTH_16BIT;
    priv->cur_protocol = I2S_PROTOCOL_I2S;
    priv->cur_polarity = I2S_LEFT_POLARITY_LOW;
    priv->cur_mclk_nfs = 256;
    priv->cur_sclk_nfs = 64;
    priv->cur_volume = 80;
    priv->pa_enabled = 0;
    priv->cur_stream = I2S_STREAM_PLAYBACK;

    /* Update state and statistics */
    priv->initialized = RT_TRUE;
    priv->is_running = RT_FALSE;
    priv->stats.init_count++;

    /* Record performance data */
    tick_end = rt_tick_get();
    update_perf_stats(&priv->perf.init_count, &priv->perf.init_total_ticks,
                      &priv->perf.init_max_ticks, &priv->perf.init_min_ticks,
                      tick_end - tick_start);
    LOG_I("dummy codec init done, default: %dHz/%dch/%dbit (took %d ms)", priv->cur_rate,
          priv->cur_channel, priv->cur_width, (tick_end - tick_start) * 1000 / RT_TICK_PER_SECOND);

    rt_mutex_release(&priv->mutex);
    return ret;
}

static int dummy_codec_start(struct codec *codec, i2s_stream_t stream)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;
    uint32_t tick_start, tick_end;

    /* Parameter validation */
    if (!codec) {
        LOG_E("codec parameter is NULL");
        return -EINVAL;
    }

    /* Acquire mutex and record start time */
    tick_start = rt_tick_get();
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);

    /* State checks */
    if (!priv->initialized) {
        LOG_E("codec not initialized, call init first");
        priv->stats.error_count++;
        rt_mutex_release(&priv->mutex);
        return -EPERM;
    }
    if (priv->is_running) {
        LOG_W("codec is already running, stream: %s", stream_to_str(priv->cur_stream));
        rt_mutex_release(&priv->mutex);
        return -EBUSY;
    }

    /* Start the stream */
    LOG_I("starting codec, stream: %s", stream_to_str(stream));
    priv->cur_stream = stream;
    priv->is_running = RT_TRUE;
    priv->stats.start_count++;

    /* Record performance data */
    tick_end = rt_tick_get();
    update_perf_stats(&priv->perf.start_count, &priv->perf.start_total_ticks,
                      &priv->perf.start_max_ticks, &priv->perf.start_min_ticks,
                      tick_end - tick_start);

    rt_mutex_release(&priv->mutex);
    return 0;
}

static int dummy_codec_stop(struct codec *codec, i2s_stream_t stream)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;
    uint32_t tick_start, tick_end;

    /* Parameter validation */
    if (!codec) {
        LOG_E("codec parameter is NULL");
        return -EINVAL;
    }

    /* Acquire mutex and record start time */
    tick_start = rt_tick_get();
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);

    /* State checks */
    if (!priv->initialized) {
        LOG_E("codec not initialized");
        priv->stats.error_count++;
        rt_mutex_release(&priv->mutex);
        return -EPERM;
    }
    if (!priv->is_running) {
        LOG_W("codec is not running");
        rt_mutex_release(&priv->mutex);
        return -EPERM;
    }

    /* Stop the stream */
    LOG_I("stopping codec, stream: %s", stream_to_str(stream));
    priv->is_running = RT_FALSE;
    priv->stats.stop_count++;

    /* Record performance data */
    tick_end = rt_tick_get();
    update_perf_stats(&priv->perf.stop_count, &priv->perf.stop_total_ticks,
                      &priv->perf.stop_max_ticks, &priv->perf.stop_min_ticks,
                      tick_end - tick_start);

    rt_mutex_release(&priv->mutex);
    return 0;
}

static int dummy_codec_set_protocol(struct codec *codec, i2s_format_t *format)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec || !format) {
        LOG_E("invalid parameter: codec=%p, format=%p", codec, format);
        return -EINVAL;
    }

    /* Apply protocol setting */
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    priv->cur_protocol = format->protocol;
    priv->stats.set_protocol_count++;
    LOG_D("set protocol: %d", format->protocol);
    rt_mutex_release(&priv->mutex);

    return 0;
}

static int dummy_codec_set_polarity(struct codec *codec, i2s_format_t *format)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec || !format) {
        LOG_E("invalid parameter: codec=%p, format=%p", codec, format);
        return -EINVAL;
    }

    /* Apply polarity setting */
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    priv->cur_polarity = format->polarity;
    priv->stats.set_polarity_count++;
    LOG_D("set polarity: %d", format->polarity);
    rt_mutex_release(&priv->mutex);

    return 0;
}

static int dummy_codec_set_channel(struct codec *codec, i2s_format_t *format)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec || !format) {
        LOG_E("invalid parameter: codec=%p, format=%p", codec, format);
        return -EINVAL;
    }

    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);

    /* Validate channel range */
    if (!is_valid_channel(format->channel)) {
        LOG_E("invalid channel %d (valid: %d-%d)", format->channel, DUMMY_CODEC_CHANNELS_MIN,
              DUMMY_CODEC_CHANNELS_MAX);
        priv->stats.param_reject_count++;
        priv->stats.error_count++;
        rt_mutex_release(&priv->mutex);
        return -EINVAL;
    }

    /* Apply channel setting */
    priv->cur_channel = format->channel;
    priv->stats.set_channel_count++;
    LOG_I("set channel: %d", format->channel);
    rt_mutex_release(&priv->mutex);

    return 0;
}

static int dummy_codec_set_sample_width(struct codec *codec, i2s_format_t *format)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec || !format) {
        LOG_E("invalid parameter: codec=%p, format=%p", codec, format);
        return -EINVAL;
    }

    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);

    /* Validate sample width */
    if (!is_valid_width(format->width)) {
        LOG_E("invalid width %d (valid: 8/12/16/20/24/28/32)", format->width);
        priv->stats.param_reject_count++;
        priv->stats.error_count++;
        rt_mutex_release(&priv->mutex);
        return -EINVAL;
    }

    /* Apply width setting */
    priv->cur_width = format->width;
    priv->cur_slot_width = format->slot_width;
    priv->stats.set_width_count++;
    LOG_I("set sample width: width=%d, slot_width=%d", format->width, format->slot_width);
    rt_mutex_release(&priv->mutex);

    return 0;
}

static int dummy_codec_set_sample_rate(struct codec *codec, i2s_format_t *format)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;
    uint32_t tick_start, tick_end;

    /* Parameter validation */
    if (!codec || !format) {
        LOG_E("invalid parameter: codec=%p, format=%p", codec, format);
        return -EINVAL;
    }

    /* Acquire mutex and record start time */
    tick_start = rt_tick_get();
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);

    /* Validate sample rate */
    if (!is_valid_rate(format->rate)) {
        LOG_E("invalid sample rate %d Hz", format->rate);
        priv->stats.param_reject_count++;
        priv->stats.error_count++;
        rt_mutex_release(&priv->mutex);
        return -EINVAL;
    }

    /* Apply rate setting */
    priv->cur_rate = format->rate;
    priv->stats.set_rate_count++;

    /* Record performance data */
    tick_end = rt_tick_get();
    update_perf_stats(&priv->perf.set_rate_count, &priv->perf.set_rate_total_ticks,
                      &priv->perf.set_rate_max_ticks, &priv->perf.set_rate_min_ticks,
                      tick_end - tick_start);
    LOG_I("set sample rate: %d Hz (%d ms)", format->rate,
          (tick_end - tick_start) * 1000 / RT_TICK_PER_SECOND);

    rt_mutex_release(&priv->mutex);
    return 0;
}

static int dummy_codec_set_sclk(struct codec *codec, i2s_format_t *format)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec || !format) {
        LOG_E("invalid parameter: codec=%p, format=%p", codec, format);
        return -EINVAL;
    }

    /* Apply SCLK setting */
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    priv->cur_sclk_nfs = format->sclk_nfs;
    priv->stats.set_sclk_count++;
    LOG_D("set sclk: sclk_nfs=%d", format->sclk_nfs);
    rt_mutex_release(&priv->mutex);

    return 0;
}

static int dummy_codec_set_mclk(struct codec *codec, i2s_format_t *format)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec || !format) {
        LOG_E("invalid parameter: codec=%p, format=%p", codec, format);
        return -EINVAL;
    }

    /* Apply MCLK setting */
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    priv->cur_mclk_nfs = format->mclk_nfs;
    priv->stats.set_mclk_count++;
    LOG_D("set mclk: mclk_nfs=%d", format->mclk_nfs);
    rt_mutex_release(&priv->mutex);

    return 0;
}

static void dummy_codec_set_volume(struct codec *codec, uint8_t volume)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec) {
        LOG_E("codec parameter is NULL");
        return;
    }

    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);

    /* Clamp volume to valid range */
    if (volume > DUMMY_CODEC_VOLUME_MAX) {
        LOG_W("volume %d exceeds max, clamping to %d", volume, DUMMY_CODEC_VOLUME_MAX);
        volume = DUMMY_CODEC_VOLUME_MAX;
    } else if (volume < DUMMY_CODEC_VOLUME_MIN) {
        LOG_W("volume %d below min, clamping to %d", volume, DUMMY_CODEC_VOLUME_MIN);
        volume = DUMMY_CODEC_VOLUME_MIN;
    }

    /* Apply volume setting */
    priv->cur_volume = volume;
    priv->stats.set_volume_count++;
    LOG_I("set volume: %d", volume);
    rt_mutex_release(&priv->mutex);
}

static uint8_t dummy_codec_get_volume(struct codec *codec)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec) {
        LOG_E("codec parameter is NULL");
        return 0;
    }

    /* Read current volume */
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    uint8_t volume = priv->cur_volume;
    rt_mutex_release(&priv->mutex);

    return volume;
}

static void dummy_codec_pa_power(struct codec *codec, uint8_t enable)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec) {
        LOG_E("codec parameter is NULL");
        return;
    }
    /* Control PA power */
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    priv->pa_enabled = enable;
    priv->stats.pa_power_count++;
    LOG_I("PA power: %s", enable ? "ON" : "OFF");
    rt_mutex_release(&priv->mutex);
}

static void dummy_codec_dump_reg(struct codec *codec)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Parameter validation */
    if (!codec) {
        LOG_E("codec parameter is NULL");
        return;
    }
    /* Dump all codec state registers */
    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    priv->stats.dump_reg_count++;
    rt_kprintf("\n=== Dummy Codec Register Dump ===\n");
    rt_kprintf("  stream:      %s\n", stream_to_str(priv->cur_stream));
    rt_kprintf("  rate:        %d Hz\n", priv->cur_rate);
    rt_kprintf("  channel:     %d\n", priv->cur_channel);
    rt_kprintf("  width:       %d bit\n", priv->cur_width);
    rt_kprintf("  slot_width:  %d bit\n", priv->cur_slot_width);
    rt_kprintf("  protocol:    %d\n", priv->cur_protocol);
    rt_kprintf("  polarity:    %d\n", priv->cur_polarity);
    rt_kprintf("  mclk_nfs:    %d\n", priv->cur_mclk_nfs);
    rt_kprintf("  sclk_nfs:    %d\n", priv->cur_sclk_nfs);
    rt_kprintf("  volume:      %d\n", priv->cur_volume);
    rt_kprintf("  pa_enabled:  %d\n", priv->pa_enabled);
    rt_kprintf("  initialized: %d\n", priv->initialized);
    rt_kprintf("  is_running:  %d\n", priv->is_running);
    rt_mutex_release(&priv->mutex);
}

static struct codec_ops dummy_codec_ops = {
    .init = dummy_codec_init,
    .start = dummy_codec_start,
    .stop = dummy_codec_stop,
    .set_protocol = dummy_codec_set_protocol,
    .set_polarity = dummy_codec_set_polarity,
    .set_channel = dummy_codec_set_channel,
    .set_sample_width = dummy_codec_set_sample_width,
    .set_sample_rate = dummy_codec_set_sample_rate,
    .set_sclk = dummy_codec_set_sclk,
    .set_mclk = dummy_codec_set_mclk,
    .set_volume = dummy_codec_set_volume,
    .get_volume = dummy_codec_get_volume,
    .pa_power = dummy_codec_pa_power,
    .dump_reg = dummy_codec_dump_reg,
};

static struct codec dummy_codec = {
    .name = "dummy_codec",
    .i2c_name = NULL,
    .i2s_name = "i2s",
    .addr = 0x00,
    .pa = 0,
    .pa_name = NULL,
    .ops = &dummy_codec_ops,
};

/* Get the singleton codec private instance */
struct dummy_codec_priv *dummy_codec_get_instance(void)
{
    return &g_dummy_priv;
}

/* Reset all operation counters */
void dummy_codec_reset_stats(void)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    rt_memset(&priv->stats, 0, sizeof(priv->stats));
    rt_mutex_release(&priv->mutex);
    LOG_I("statistics reset");
}

/* Print current state and operation statistics */
void dummy_codec_dump_status(void)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;
    struct dummy_codec_stats *s;

    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    s = &priv->stats;
    /* Print current state */
    rt_kprintf("\n=== Dummy Codec Status ===\n");
    rt_kprintf("  stream:      %s\n", stream_to_str(priv->cur_stream));
    rt_kprintf("  config:      %dHz / %dch / %dbit\n", priv->cur_rate, priv->cur_channel,
               priv->cur_width);
    rt_kprintf("  volume:      %d\n", priv->cur_volume);
    rt_kprintf("  pa_enabled:  %d\n", priv->pa_enabled);
    rt_kprintf("  initialized: %s\n", priv->initialized ? "YES" : "NO");
    rt_kprintf("  is_running:  %s\n", priv->is_running ? "YES" : "NO");
    /* Print operation counters */
    rt_kprintf("\n=== Statistics ===\n");
    rt_kprintf("  init:             %u\n", s->init_count);
    rt_kprintf("  start:            %u\n", s->start_count);
    rt_kprintf("  stop:             %u\n", s->stop_count);
    rt_kprintf("  set_rate:         %u\n", s->set_rate_count);
    rt_kprintf("  set_channel:      %u\n", s->set_channel_count);
    rt_kprintf("  set_width:        %u\n", s->set_width_count);
    rt_kprintf("  set_volume:       %u\n", s->set_volume_count);
    rt_kprintf("  set_protocol:     %u\n", s->set_protocol_count);
    rt_kprintf("  set_polarity:     %u\n", s->set_polarity_count);
    rt_kprintf("  set_mclk:         %u\n", s->set_mclk_count);
    rt_kprintf("  set_sclk:         %u\n", s->set_sclk_count);
    rt_kprintf("  pa_power:         %u\n", s->pa_power_count);
    rt_kprintf("  dump_reg:         %u\n", s->dump_reg_count);
    rt_kprintf("  error:            %u\n", s->error_count);
    rt_kprintf("  param_reject:     %u\n", s->param_reject_count);
    rt_mutex_release(&priv->mutex);
}

/* Print performance statistics with timing details */
void dummy_codec_dump_perf(void)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;
    struct dummy_codec_perf_stats *p = &priv->perf;

    /* Init performance */
    rt_kprintf("\n=== Performance Statistics ===\n");
    rt_kprintf("\nInit Performance:\n");
    rt_kprintf("  Count:      %u\n", p->init_count);
    if (p->init_count > 0) {
        rt_kprintf("  Average:    %u ms\n",
                   (p->init_total_ticks * 1000) / p->init_count / RT_TICK_PER_SECOND);
        rt_kprintf("  Max:        %u ms\n", p->init_max_ticks * 1000 / RT_TICK_PER_SECOND);
        rt_kprintf("  Min:        %u ms\n", p->init_min_ticks * 1000 / RT_TICK_PER_SECOND);
    }

    /* Start performance */
    rt_kprintf("\nStart Performance:\n");
    rt_kprintf("  Count:      %u\n", p->start_count);
    if (p->start_count > 0) {
        rt_kprintf("  Average:    %u ms\n",
                   (p->start_total_ticks * 1000) / p->start_count / RT_TICK_PER_SECOND);
        rt_kprintf("  Max:        %u ms\n", p->start_max_ticks * 1000 / RT_TICK_PER_SECOND);
        rt_kprintf("  Min:        %u ms\n", p->start_min_ticks * 1000 / RT_TICK_PER_SECOND);
    }

    /* Stop performance */
    rt_kprintf("\nStop Performance:\n");
    rt_kprintf("  Count:      %u\n", p->stop_count);
    if (p->stop_count > 0) {
        rt_kprintf("  Average:    %u ms\n",
                   (p->stop_total_ticks * 1000) / p->stop_count / RT_TICK_PER_SECOND);
        rt_kprintf("  Max:        %u ms\n", p->stop_max_ticks * 1000 / RT_TICK_PER_SECOND);
        rt_kprintf("  Min:        %u ms\n", p->stop_min_ticks * 1000 / RT_TICK_PER_SECOND);
    }

    /* Set rate performance */
    rt_kprintf("\nSet Rate Performance:\n");
    rt_kprintf("  Count:      %u\n", p->set_rate_count);
    if (p->set_rate_count > 0) {
        rt_kprintf("  Average:    %u ms\n",
                   (p->set_rate_total_ticks * 1000) / p->set_rate_count / RT_TICK_PER_SECOND);
        rt_kprintf("  Max:        %u ms\n", p->set_rate_max_ticks * 1000 / RT_TICK_PER_SECOND);
        rt_kprintf("  Min:        %u ms\n", p->set_rate_min_ticks * 1000 / RT_TICK_PER_SECOND);
    }
}

/* Reset all performance counters */
void dummy_codec_reset_perf(void)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    rt_mutex_take(&priv->mutex, RT_WAITING_FOREVER);
    rt_memset(&priv->perf, 0, sizeof(priv->perf));
    rt_mutex_release(&priv->mutex);
    LOG_I("performance statistics reset");
}

/* Register dummy codec device during system init */
static int dummy_codec_register(void)
{
    struct dummy_codec_priv *priv = &g_dummy_priv;

    /* Initialize private data and mutex */
    rt_memset(priv, 0, sizeof(*priv));
    if (rt_mutex_init(&priv->mutex, "dmy_mtx", RT_IPC_FLAG_FIFO) != RT_EOK) {
        LOG_E("failed to initialize mutex");
        return -1;
    }

    /* Register with codec framework */
    codec_register(&dummy_codec);
    LOG_I("dummy codec registered");

    return 0;
}

INIT_DEVICE_EXPORT(dummy_codec_register);
