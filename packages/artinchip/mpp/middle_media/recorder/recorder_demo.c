/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <fcntl.h>
#include <pthread.h>
#include <rthw.h>
#include <rtthread.h>
#include <shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>

#define LOG_DEBUG
#include "aic_recorder.h"
#include "mpp_list.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "aic_render.h"
#include "recorder_cam.h"
#include "artinchip_fb.h"
#ifdef LPKG_USING_CPU_USAGE
#include "cpu_usage.h"
#endif

#define RECORDER_RECORD_TIME 0x7FFFFFFF
#define RECORDER_OUTPUT_PATH "/sdcard/"

struct recorder_render_data {
    struct aic_video_render *render;
    int layer_id;
    int dev_id;
    struct mpp_rect dis_rect;
    bool render_init;
};

struct recorder_context {
    struct aic_recorder *recorder;
    enum aic_recorder_vin_type  vin_source_type;
    struct recorder_cam_data cam_data;
    struct recorder_render_data render_data;
    struct aic_recorder_config config;

    char output_file_path[256];
    char capture_file_path[256];
    unsigned int record_time;
    bool render_flag;
    bool record_flag;
    bool recorder_stop;
    unsigned int snapshot_count;
    struct mpp_frame frame[RECORDER_CAM_BUF_NUM];
};


static struct recorder_context *g_recorder_cxt = NULL;

static void print_help(const char* prog)
{
    printf("name: %s\n", prog);
    printf("Usage: recoder_demo [options]:\n"
        "\t-i                             video input source\n"
        "\t-t                             recoder time(s)\n"
        "\t-d                             display to screen\n"
        "\t-q                             set pic quality\n"
        "\t-r                             record data\n"
        "\t-h                             help\n\n"
        "Example1: recoder_demo -i dvp -t 60\n"
        "Example2: recoder_demo -i file -t 60\n");
}

static void print_cmd_help(const char* prog)
{
    printf("name: %s\n", prog);
    printf("Usage: recoder_demo_cmd [options]:\n"
        "\t-e                             stop recorder\n"
        "\t-s                             snap one frame\n"
        "\t-d                             get debug info\n"
        "\t-h                             help\n\n");
}

static s32 event_handle(void *app_data, s32 event, s32 data1, s32 data2)
{
    int ret = 0;
    struct recorder_context *recorder_cxt = (struct recorder_context *)app_data;
    char file_path[512] = {0};
    time_t timep;
    struct tm *p = NULL;

    switch (event) {
    case AIC_RECORDER_EVENT_NEED_NEXT_FILE:
        // set recorder file name
        time(&timep);
        p = localtime(&timep);
        snprintf(file_path, sizeof(file_path), "%s%04d%02d%02d_%02d%02d%02d.mp4",
                 recorder_cxt->output_file_path,
                 1900 + p->tm_year, 1 + p->tm_mon, p->tm_mday,
                 p->tm_hour, 1 + p->tm_min, p->tm_sec);
        aic_recorder_set_output_file_path(recorder_cxt->recorder, file_path);
        printf("set recorder file:%s\n", file_path);
        break;
    case AIC_RECORDER_EVENT_COMPLETE:
        recorder_cxt->recorder_stop = true;
        break;
    case AIC_RECORDER_EVENT_NO_SPACE:
        break;

    default:
        break;
    }
    return ret;
}

static s32 giveback_buf(void *app_data, s32 event, void *buffer)
{
    if (!app_data || !buffer) {
        loge("app_data %p or buffer %p is null", app_data, buffer);
        return -1;
    }

    switch (event) {
        case AIC_RECORDER_EVENT_GIVEBACK_FRAME:
            break;

        case AIC_RECORDER_EVENT_GIVEBACK_PACKET:
            //Reserved for user send packet to recorder
            break;

        default:
            break;
    }

    return 0;
}

static int parse_options(struct recorder_context *recoder_ctx, int cnt, char **options)
{
    int argc = cnt;
    char **argv = options;
    struct recorder_context *ctx = recoder_ctx;
    int opt;

    if (!ctx || argc == 0 || !argv) {
        loge("para error !!!");
        return -1;
    }
    optind = 0;
    while (1) {
        opt = getopt(argc, argv, "i:t:r:q:dh");
        if (opt == -1) {
            break;
        }
        switch (opt) {
        case 'i':
            if (strncmp(optarg, "dvp", 3) == 0) {
                ctx->vin_source_type = AIC_RECORDER_VIN_DVP;
            } else {
                ctx->vin_source_type = AIC_RECORDER_VIN_FILE;
            }
            break;

        case 't':
            ctx->record_time = atoi(optarg);
            break;

        case 'r':
            ctx->record_flag = atoi(optarg);
            break;

        case 'q':
            ctx->config.qfactor = atoi(optarg);
            break;

        case 'd':
            ctx->render_flag = true;
            break;

        case 'h':
        default:
            print_help(argv[0]);
            return -1;
        }
    }

    return 0;
}

static void show_cpu_usage()
{
#if defined(LPKG_USING_CPU_USAGE)
    static int index = 0;
    char data_str[64];
    float value = 0.0;

    if (index++ % 500 == 0) {
        value = cpu_load_average();
        #ifdef AIC_PRINT_FLOAT_CUSTOM
            int cpu_i;
            int cpu_frac;
            cpu_i = (int)value;
            cpu_frac = (value - cpu_i) * 100;
            snprintf(data_str, sizeof(data_str), "%d.%02d\n", cpu_i, cpu_frac);
        #else
            snprintf(data_str, sizeof(data_str), "%.2f\n", value);
        #endif
        printf("cpu_loading:%s\n",data_str);
    }
#endif
}

static int recorder_render_init(struct recorder_render_data *render_data)
{
    struct aicfb_alpha_config alpha = {0};
    rt_device_t render_dev = NULL;
    int ret = 0;

    if (!render_data) {
        loge("render_data is null");
        return -1;
    }

    // set fb power on
    render_dev = rt_device_find("aicfb");
    if (!render_dev) {
        loge("rt_device_find aicfb failed!");
        return -1;
    }
    rt_device_control(render_dev, AICFB_POWERON, 0);

    //set ui layer alpha
    alpha.layer_id = AICFB_LAYER_TYPE_UI;
    alpha.enable = 1;
    alpha.mode = 1;
    alpha.value = 0;
    rt_device_control(render_dev, AICFB_UPDATE_ALPHA_CONFIG, &alpha);

    // create video render instance
    ret = aic_video_render_create(&render_data->render);
    if (ret) {
        loge("create render failed %d.", ret);
        return ret;
    }
    render_data->layer_id = AICFB_LAYER_TYPE_VIDEO;
    render_data->dev_id = 0;
    ret = aic_video_render_init(render_data->render,
                                render_data->layer_id,
                                render_data->dev_id);
    if (ret) {
        loge("init render failed %d.", ret);
        aic_video_render_destroy(render_data->render);
        return ret;
    }

    render_data->render_init = true;
    return 0;
}

static void recorder_render_deinit(struct recorder_render_data *render_data)
{
    if (!render_data) {
        loge("render not init");
        return;
    }

    if (render_data->render && render_data->render_init) {
        aic_video_render_set_on_off(render_data->render, 0);
        aic_video_render_destroy(render_data->render);
        render_data->render = NULL;
    }
    render_data->render_init = false;
}


static int recorder_render_frame(struct recorder_render_data *render_data,
                                 struct mpp_frame *frame)
{
    if (!render_data || !render_data->render_init || !render_data->render)
        return -1;

    if (aic_video_render_rend(render_data->render, frame)) {
        loge("render failed.");
        return -1;
    }
    return 0;
}


static void do_recorder(struct recorder_context *recorder_cxt)
{
    struct recorder_cam_data *cam = &recorder_cxt->cam_data;
    struct vin_video_buf *binfo = NULL;
    struct mpp_frame *frame = NULL;
    struct dvp_out_fmt *dst = NULL;
    int index = -1;
    int ret = 0;
    int i = 0;

    if (!recorder_cxt)
        return;

#ifndef AIC_MPP_RECORDER_USING_EXTRA_FRAME
    return;
#endif

    ret = recorder_cam_dequeue_buf(&index);
    if (ret != 0)
        return;

    binfo = &cam->binfo;
    dst   = &cam->dst_fmt;
    frame = &recorder_cxt->frame[index];
    frame->id = index;
    frame->pts = aic_get_time_ms();
    frame->buf.buf_type = MPP_PHY_ADDR;
    frame->buf.size.width = dst->width;
    frame->buf.size.height = dst->height;
    frame->buf.stride[0] = dst->width;
    frame->buf.stride[1] = dst->width;
    frame->buf.stride[2] = 0;
    frame->buf.format = dst->pixelformat;
    for (i = 0; i < RECORDER_CAM_PLANE_NUM; i++) {
        frame->buf.phy_addr[i] =
            binfo->planes[index * RECORDER_CAM_PLANE_NUM + i].buf;
    }

    if (recorder_cxt->render_flag)
        recorder_render_frame(&recorder_cxt->render_data, frame);

    if (recorder_cxt->record_flag) {
        ret = aic_recorder_send_frame(recorder_cxt->recorder, frame);
        if (ret != 0) {
            recorder_cam_queue_buf(index);
            return;
        }
    }
    recorder_cam_queue_buf(index);
}

static void *recorder_thread(void *arg)
{
    struct recorder_context *recorder_cxt = (struct recorder_context *)arg;
    struct timespec start_time = { 0 }, cur_time = { 0 };

    if (!recorder_cxt)
        return NULL;

    clock_gettime(CLOCK_REALTIME, &start_time);

    while (!recorder_cxt->recorder_stop) {
        clock_gettime(CLOCK_REALTIME, &cur_time);
        if (recorder_cxt->record_time <= (cur_time.tv_sec - start_time.tv_sec)) {
            recorder_cxt->recorder_stop = true;
            printf("recorder end time coming, stop recorder.\n");
            break;
        }

        do_recorder(recorder_cxt);

        show_cpu_usage();
        usleep(10 * 1000);
    }

    if (recorder_cxt->vin_source_type == AIC_RECORDER_VIN_DVP)
        recorder_cam_deinit(&recorder_cxt->cam_data);
    if (recorder_cxt->render_flag)
        recorder_render_deinit(&recorder_cxt->render_data);

    if (recorder_cxt && recorder_cxt->recorder) {
        aic_recorder_stop(recorder_cxt->recorder);
        aic_recorder_destroy(recorder_cxt->recorder);
        recorder_cxt->recorder = NULL;
    }

    free(recorder_cxt);
    recorder_cxt = NULL;
    g_recorder_cxt = NULL;

    printf("recorder_thread exit\n");

    return NULL;
}

static void recorder_default_config(struct recorder_context *ctx)
{
    ctx->render_flag = false;
    ctx->record_flag = true;
    ctx->record_time = RECORDER_RECORD_TIME;
    snprintf(ctx->output_file_path, sizeof(ctx->output_file_path),
        "%s", RECORDER_OUTPUT_PATH);

    ctx->config.file_duration = 60;
    ctx->config.file_num = 0;
    ctx->config.file_muxer_type = AIC_MUXER_TYPE_MP4;
    ctx->config.qfactor = 80;

    ctx->config.has_video = 1;
    ctx->config.video_config.codec_type = MPP_CODEC_VIDEO_ENCODER_MJPEG;
    ctx->config.video_config.in_width = 1280;
    ctx->config.video_config.in_height = 720;
    ctx->config.video_config.out_width = 1280;
    ctx->config.video_config.out_height = 720;
    ctx->config.video_config.out_frame_rate = 30;
    ctx->config.video_config.out_bit_rate = 1024 * 1024;
    ctx->config.video_config.in_pix_fomat = MPP_FMT_NV12;

    ctx->config.has_audio = 0;
}

static void recorder_update_config(struct recorder_context *ctx)
{
    struct recorder_cam_data *cam = &ctx->cam_data;

    if (!cam->cam_init)
        return;

    ctx->config.video_config.in_width = cam->w;
    ctx->config.video_config.in_height = cam->h;
    ctx->config.video_config.out_width = cam->w;
    ctx->config.video_config.out_height = cam->h;
}

int recorder_demo_test(int argc, char *argv[])
{
    int ret = 0;
    pthread_attr_t attr;
    pthread_t thread_id;
    struct recorder_context *recorder_cxt = NULL;

    recorder_cxt = malloc(sizeof(struct recorder_context));
    if (!recorder_cxt) {
        loge("malloc error");
        return -1;
    }
    memset(recorder_cxt, 0x00, sizeof(struct recorder_context));
    recorder_default_config(recorder_cxt);
    if (parse_options(recorder_cxt, argc, argv)) {
        goto _EXIT;
    }
    g_recorder_cxt = recorder_cxt;

    //create recorder module
    recorder_cxt->recorder = aic_recorder_create();
    if (!recorder_cxt->recorder) {
        loge("aic_recorder_create error");
        goto _EXIT;
    }

    //set recorder event callback
    if (aic_recorder_set_event_callback(recorder_cxt->recorder,
                                        recorder_cxt, event_handle)) {
        loge("aic_recorder_set_event_callback error");
        goto _EXIT;
    }

    //set extra frame or packet giveback callback
    if (aic_recorder_set_giveback_buf_callback(recorder_cxt->recorder,
                                               giveback_buf)) {
        loge("aic_recorder_set_giveback_buf_callback error");
        goto _EXIT;
    }

    //initialize recorder module
    recorder_cxt->config.video_config.vin_type = recorder_cxt->vin_source_type;
    if (aic_recorder_init(recorder_cxt->recorder, &recorder_cxt->config)) {
        loge("aic_recorder_init error");
        goto _EXIT;
    }

    //initialize video input module
    if (recorder_cxt->vin_source_type == AIC_RECORDER_VIN_DVP) {
        if (recorder_cam_init(&recorder_cxt->cam_data))
            goto _EXIT;
        recorder_update_config(recorder_cxt);
    } else if (recorder_cxt->vin_source_type == AIC_RECORDER_VIN_FILE) {
        aic_recorder_set_input_file_path(recorder_cxt->recorder,
            "/sdcard/test.yuv", NULL);
    }

    //start recorder
    if (aic_recorder_start(recorder_cxt->recorder)) {
        loge("aic_recorder_start error");
        goto _EXIT;
    }

    //display frame to video layer
    if (recorder_cxt->render_flag) {
        if (recorder_render_init(&recorder_cxt->render_data))
            goto _EXIT;
    }

    //create recorder thread
    pthread_attr_init(&attr);
    attr.stacksize = 8 * 1024;
    attr.schedparam.sched_priority = 0;
    ret = pthread_create(&thread_id, &attr, recorder_thread, recorder_cxt);
    if (ret) {
        loge("create recorder_thread failed\n");
    }

    return ret;

_EXIT:

    if (recorder_cxt) {
        if (recorder_cxt->vin_source_type == AIC_RECORDER_VIN_DVP)
            recorder_cam_deinit(&recorder_cxt->cam_data);
        if (recorder_cxt->render_flag)
            recorder_render_deinit(&recorder_cxt->render_data);

        if (recorder_cxt && recorder_cxt->recorder) {
            aic_recorder_stop(recorder_cxt->recorder);
            aic_recorder_destroy(recorder_cxt->recorder);
            recorder_cxt->recorder = NULL;
        }
        free(recorder_cxt);
        recorder_cxt = NULL;
        g_recorder_cxt = NULL;
    }

    return ret;
}

MSH_CMD_EXPORT_ALIAS(recorder_demo_test, recorder_demo, recorder demo);

static void recorder_snapshot(struct recorder_context *recorder_cxt)
{
    struct aic_record_snapshot_info snap_info;

    if (!recorder_cxt || !recorder_cxt->recorder)
        return;

    snprintf(recorder_cxt->capture_file_path, sizeof(recorder_cxt->capture_file_path),
            "/sdcard/capture-%d.jpg", recorder_cxt->snapshot_count++);
    snap_info.file_path = (s8 *)recorder_cxt->capture_file_path;

    aic_recorder_snapshot(recorder_cxt->recorder, &snap_info);
}

static int recorder_cmd(int argc, char**argv)
{
    struct recorder_context *recorder_cxt = g_recorder_cxt;
    int opt;

    if (argc == 0 || !argv) {
        loge("para error !!!");
        return -1;
    }
    if (!recorder_cxt) {
        loge("recorder is not start running !!!");
        return -1;
    }

    optind = 0;
    while (1) {
        opt = getopt(argc, argv, "esdh");
        if (opt == -1) {
            break;
        }
        switch (opt) {
        case 's':
            recorder_snapshot(recorder_cxt);
            break;

        case 'e':
            recorder_cxt->recorder_stop = true;
            break;

        case 'd':
            aic_recorder_print_debug_info(recorder_cxt->recorder);
            break;

        case 'h':
            print_cmd_help(argv[0]);
            break;

        default:
            break;
        }
    }

    return 0;
}

MSH_CMD_EXPORT_ALIAS(recorder_cmd, recorder_cmd, recorder demo cmd);
