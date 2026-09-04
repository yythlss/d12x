/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#ifndef __TEST_VIN_H__
#define __TEST_VIN_H__

#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#include <posix/string.h>

#include "aic_core.h"
#include "aic_log.h"
#include "aic_osal.h"

#include "drv_dvp.h"
#include "mpp_vin.h"

#ifdef AIC_DISPLAY_DRV
#include "mpp_fb.h"
#include "artinchip_fb.h"
#include "drv_dma.h"
#endif

/* MUST and ONLY enable one of the follow mode: */
#define DE_SCALE_ENABLE         1
#define DE_CROP_ENABLE          0
#define VIN_CROP_ENABLE         0

#if (DE_SCALE_ENABLE + DE_CROP_ENABLE + VIN_CROP_ENABLE) != 1
#error "MUST and ONLY enable one: DE_SCALE_ENABLE, DE_CROP_ENABLE, VIN_CROP_ENABLE"
#endif

#define VID_BUF_NUM             3
#define VID_BUF_MIN_NUM         3
#define VID_SCALE_OFFSET        0
#define DE_MAX_WIDTH            1280

#define VIN_DISP_MODE_STITCH    2       /* dual-channel stitch */
#define VIN_DISP_MODE_NONE      UINT32_MAX

#ifdef AIC_USING_VE
#define SUPPORT_ROTATION_BY_VE
#endif

#ifdef SUPPORT_ROTATION_BY_VE
#include "mpp_encoder.h"
#include "mpp_decoder.h"
#include "mpp_mem.h"
#include "frame_allocator.h"
#endif

/* 0=proportional (preserve aspect ratio), 1=fullscreen (stretch to fill) */
#define SCALE_MODE_FULLSCREEN   0

#define FB_PP_BUF_NUM           2
#define FB_PP_BUF_FREE          0
#define FB_PP_BUF_READY         (BIT(0) | BIT(1))

#define DEBUG_BUF_ENABLE        0
#if DEBUG_BUF_ENABLE
#define buf_dbg(fmt, args...)   printf(fmt, ##args)
#else
#define buf_dbg(fmt, args...)
#endif

struct vin_test_ctx {
    struct vin_dev_ctx ctx;
    u32 channel;
    u32 show_channel;
    u32 frame_count;
    u32 ref_cnt;
    int rotation;
    struct mpp_rect dst_pos;
#ifdef SUPPORT_ROTATION_BY_VE
    struct mpp_encoder *ve_encoder;      /* JPEG encoder instance */
    struct mpp_decoder *ve_decoder;      /* JPEG decoder instance */
    int ve_jpeg_phy_addr;                /* JPEG intermediate buffer physical address */
    int ve_jpeg_buf_size;                /* JPEG buffer size */
    struct frame_allocator *ve_allocator; /* custom decoder frame allocator */
#endif
    u32 display_mode;   /* current display mode: 0=CH0, 1=CH1, 2=stitch */
    volatile u32 pending_display_mode; /* pending display mode switch */
    bool vin_crop;
    struct mpp_video_fmt sensor_fmt; /* user override; 0 = use camera default */
    bool running[VIN_MAX_CHANNELS];
    volatile bool finished[VIN_MAX_CHANNELS];
    int exit_code[VIN_MAX_CHANNELS];
    aicos_thread_t thread[VIN_MAX_CHANNELS];

    u32 num_buffers;
    struct vin_video_buf binfo[VIN_MAX_CHANNELS];
#ifdef AIC_DISPLAY_DRV
    struct aicfb_layer_data cached_layer;
    char *pp_buf[FB_PP_BUF_NUM];
    u32   pp_buf_index;
    u32   pp_buf_state;
    aicos_mutex_t pp_buf_mutex;
    aicos_sem_t dma_complete;
#endif
    aicos_mutex_t media_dev_mutex;
};

/* Global variables */
extern struct vin_test_ctx g_vin_test_ctx;
extern const char *g_vin_names[];
#define VIN_NAME(ctx)       g_vin_names[(ctx)->type]

#ifdef AIC_DISPLAY_DRV
extern struct aicfb_screeninfo g_fb_info;
extern struct mpp_fb *g_fb;
#endif

/* Functions exported from test_vin.c */
bool vin_task_is_running(void);
int vin_queue_buf(int index, u32 ch, struct vin_dev_ctx *ctx);

/* Functions exported from test_vin_disp.c */
int vin_get_fb_info(void);
int vin_uilayer_set_alpha(int val);
void vin_show_fmt(struct vin_dev_ctx *vin_ctx);
void vin_show_size(struct vin_test_ctx *ctx);
void vin_fb_pp_buf_free(struct vin_test_ctx *ctx);
int vin_fb_pp_buf_init(struct vin_test_ctx *ctx, enum mpp_stitch_mode stitch_mode);

/* Functions exported from test_vin_ve.c */
#ifdef SUPPORT_ROTATION_BY_VE
int vin_ve_rotation_init(struct vin_test_ctx *ctx, u32 width, u32 height, int rotation);
int vin_ve_rotation_process(struct vin_test_ctx *ctx, u32 ch, int buf_index);
void vin_ve_rotation_deinit(struct vin_test_ctx *ctx);
#endif

#ifdef AIC_DISPLAY_DRV
int vin_vlayer_disable(void);
int vin_output_region_cfg(struct vin_test_ctx *ctx);
int vin_vlayer_init(struct vin_test_ctx *ctx);
void vin_vlayer_reconfigure(struct vin_test_ctx *ctx, u32 display_mode);
int vin_vlayer_update_buf(struct vin_test_ctx *ctx, u32 ch, int index);
#endif

#endif /* __TEST_VIN_H__ */
