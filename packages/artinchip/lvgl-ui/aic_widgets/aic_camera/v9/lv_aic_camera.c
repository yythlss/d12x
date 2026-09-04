/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Zequan Liang <zequan.liang@artinchip.com>
 */
#include "lv_aic_camera.h"

#if defined(AIC_USING_CAMERA) && defined(AIC_DISPLAY_DRV) && defined(AIC_DVP_DRV)
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "mpp_vin.h"
#include "drv_camera.h"
#include "drv_dvp.h"

#include "artinchip_fb.h"
#include "mpp_fb.h"
#include "mpp_mem.h"

#if AIC_CAMERA_USE_BARCODE
#include "include/yydecoder.h"
#define BARCODE_DEMO_SUCCESS    1
#endif

#define MY_CLASS (&lv_aic_camera_class)
#define VID_BUF_NUM             3
#define VID_BUF_PLANE_NUM       2
#define VID_DROP_FRESH_FRAME    3

/**********************
 *      TYPEDEFS
 **********************/
typedef enum {
    LV_AIC_CAMERA_STATUS_INIT,
    LV_AIC_CAMERA_STATUS_READY,
    LV_AIC_CAMERA_STATUS_START,
    LV_AIC_CAMERA_STATUS_RUNNING,
    LV_AIC_CAMERA_STATUS_STOP,
    _LV_AIC_CAMERA_STATUS_LAST
} lv_aic_camera_status;

struct aic_camera_ctx_s {
    int w;
    int h;
    int frame_size;
    int frame_cnt;
    int rotation;
    int dst_fmt;
    struct mpp_video_fmt src_fmt;
    uint32_t num_buffers;
    struct vin_video_buf binfo;
    struct mpp_rect dst_rect;

    struct mpp_fb *fb;
    struct aicfb_screeninfo fb_info;

    uint32_t status;
    lv_mutex_t mutex;

    lv_thread_sync_t video_sync;
    lv_thread_sync_t video_exit_sync;
    lv_thread_t video_thread;

    struct vin_dev_ctx vin_ctx;

    volatile bool thread_exit;
    volatile bool running;
    volatile u32 fresh_frame;
    volatile bool video_ready;
    lv_timer_t *ready_timer;
    volatile bool switching;
    u32 curr_ch;
    u32 ch;

    lv_obj_t *obj;

    struct aicfb_layer_data cached_layer;

#if AIC_CAMERA_USE_BARCODE
    lv_thread_sync_t barcode_sync;
    lv_thread_t barcode_thread;
    void *barcode_buffer;
    bool barcode_create;
    bool barcode_ready;
    lv_thread_sync_t barcode_exit_sync;
#endif
};

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_aic_camera_constructor(const lv_obj_class_t * class_p,
                                         lv_obj_t * obj);
static void lv_aic_camera_destructor(const lv_obj_class_t * class_p,
                                        lv_obj_t * obj);
static void lv_aic_camera_event(const lv_obj_class_t * class_p, lv_event_t * e);

static int mpp_format_to_camera_format(lv_aic_camera_format format);
static int camera_get_format(struct aic_camera_ctx_s *aic_ctx);
static int update_bg_blend_config(struct aic_camera_ctx_s *aic_ctx);
static int lv_camera_video_layer_disable(struct aic_camera_ctx_s *aic_ctx);
static int lv_camera_subdev_set(struct aic_camera_ctx_s *aic_ctx);
static int lv_camera_config(struct aic_camera_ctx_s *aic_ctx, u32 stitch_mode);
static void lv_camera_draw_video_layer_entry(void *ptr);

static int calc_frame_size(int fmt, int w, int h);
static int video_layer_init(struct aic_camera_ctx_s *aic_ctx);
static int video_layer_update_buf(struct aic_camera_ctx_s *aic_ctx, int index);
static void ready_timer_cb(lv_timer_t *t);

#if AIC_CAMERA_USE_BARCODE
static void barcode_decode_entry(void *ptr);
static void barcode_decode_create(struct aic_camera_ctx_s *aic_ctx);
static void barcode_decode_delete(struct aic_camera_ctx_s *aic_ctx);
static void barcode_decode_buffer_sync(struct aic_camera_ctx_s *aic_ctx, int index);
static void barcode_decode(lv_obj_t *obj);
#endif

const lv_obj_class_t lv_aic_camera_class = {
    .constructor_cb = lv_aic_camera_constructor,
    .destructor_cb = lv_aic_camera_destructor,
    .event_cb = lv_aic_camera_event,
    .width_def = LV_SIZE_CONTENT,
    .height_def = LV_SIZE_CONTENT,
    .instance_size = sizeof(lv_aic_camera_t),
    .base_class = &lv_obj_class,
    .name = "aic_camera",
};

lv_obj_t * lv_aic_camera_create(lv_obj_t * parent)
{
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

lv_res_t lv_aic_camera_set_format(lv_obj_t * obj, lv_aic_camera_format format)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    camera->format = format;
    return LV_RES_OK;
}

lv_res_t lv_aic_camera_open(lv_obj_t * obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s * aic_ctx = camera->aic_ctx;
    bool vin_init_status = false;

    if (aic_ctx == NULL) {
        aic_ctx = lv_mem_alloc(sizeof(struct aic_camera_ctx_s));
        if(aic_ctx == NULL) {
            LV_LOG_ERROR("lv_aic_camera_open failed");
            return LV_RES_INV;
        }
        lv_memset(aic_ctx, 0x0, sizeof(struct aic_camera_ctx_s));
        aic_ctx->status = LV_AIC_CAMERA_STATUS_INIT;
    }

    if (aic_ctx->status != LV_AIC_CAMERA_STATUS_INIT) {
        LV_LOG_WARN("lv_aic_camera_open have opened, status = %d", (int)aic_ctx->status);
        return LV_RES_OK;
    }

    aic_ctx->vin_ctx.type = VIN_DEV_DVP;
    strncpy(aic_ctx->vin_ctx.camera, "camera", sizeof(aic_ctx->vin_ctx.camera) - 1);

    if (mpp_vin2_init(&aic_ctx->vin_ctx)) {
        LV_LOG_ERROR("mpp_vin2_init failed");
        goto CAMERA_ERROR;
    }
    vin_init_status = true;

    if (camera_get_format(aic_ctx) < 0) {
        LV_LOG_ERROR("camera_get_format failed");
        goto CAMERA_ERROR;
    }

    aic_ctx->dst_fmt = mpp_format_to_camera_format(camera->format);
    aic_ctx->frame_size = calc_frame_size(aic_ctx->dst_fmt, aic_ctx->w, aic_ctx->h);
    aic_ctx->num_buffers = VID_BUF_NUM;
    aic_ctx->rotation = MPP_ROTATION_0;

    if (lv_camera_subdev_set(aic_ctx) < 0) {
        LV_LOG_ERROR("lv_camera_subdev_set failed");
        goto CAMERA_ERROR;
    }

    aic_ctx->fb = mpp_fb_open();
    if (!aic_ctx->fb) {
        LV_LOG_ERROR("Failed to open FB");
        goto CAMERA_ERROR;
    }

    if (mpp_fb_ioctl(aic_ctx->fb, AICFB_GET_SCREENINFO, &aic_ctx->fb_info) < 0) {
        LV_LOG_ERROR("Failed to get screen info!");
        goto CAMERA_ERROR;
    }

    if (update_bg_blend_config(aic_ctx) < 0)
        return -1;

    if (lv_camera_config(aic_ctx, MPP_STITCH_INVALID) < 0) {
        LV_LOG_ERROR("lv_camera_config error");
        goto CAMERA_ERROR;
    }

    lv_obj_update_layout(obj);
    lv_area_t fill_area = {0};
    lv_obj_get_coords(obj, &fill_area);

    aic_ctx->dst_rect.x = fill_area.x1;
    aic_ctx->dst_rect.y = fill_area.y1;
    aic_ctx->dst_rect.width = fill_area.x2 - fill_area.x1 + 1;
    aic_ctx->dst_rect.height = fill_area.y2 - fill_area.y1 + 1;

    lv_mutex_init(&aic_ctx->mutex);

    aic_ctx->video_ready = false;
    aic_ctx->ready_timer = lv_timer_create(ready_timer_cb, 20, obj);

    aic_ctx->thread_exit = false;

    aic_ctx->obj = obj;
    lv_thread_sync_init(&aic_ctx->video_sync);
    lv_thread_sync_init(&aic_ctx->video_exit_sync);
    aic_ctx->running = false;
    if (lv_thread_init(&aic_ctx->video_thread, 20,
                       lv_camera_draw_video_layer_entry,
                       8 * 1024, aic_ctx) != LV_RESULT_OK) {
        LV_LOG_ERROR("Failed to create video thread");
        goto CAMERA_ERROR;
    }

#if AIC_CAMERA_USE_BARCODE
    if (camera->barcode_en) {
        barcode_decode_create(aic_ctx);
        lv_thread_sync_init(&aic_ctx->barcode_sync);
        lv_thread_sync_init(&aic_ctx->barcode_exit_sync);
        if (lv_thread_init(&aic_ctx->barcode_thread, 20,
                           barcode_decode_entry,
                           32 * 1024, camera) != LV_RESULT_OK) {
            LV_LOG_ERROR("Failed to create barcode thread");
            barcode_decode_delete(aic_ctx);
            goto CAMERA_ERROR;
        }
        aic_ctx->barcode_create = true;
        aic_ctx->barcode_ready = true;
    }
#endif

    aic_ctx->ch = 0;

    aic_ctx->status = LV_AIC_CAMERA_STATUS_READY;

    lv_obj_refresh_self_size(obj);
    lv_obj_invalidate(obj);

    camera->aic_ctx = aic_ctx;
    return LV_RES_OK;
CAMERA_ERROR:
    if (vin_init_status)
        mpp_vin2_deinit(&aic_ctx->vin_ctx);

    if (aic_ctx && aic_ctx->fb)
        mpp_fb_close(aic_ctx->fb);

    if (aic_ctx) {
        if (aic_ctx->ready_timer)
            lv_timer_del(aic_ctx->ready_timer);
        lv_mutex_delete(&aic_ctx->mutex);
        lv_thread_sync_delete(&aic_ctx->video_sync);
        lv_thread_sync_delete(&aic_ctx->video_exit_sync);
        lv_mem_free(aic_ctx);
    }

    return LV_RES_INV;
}

lv_res_t lv_aic_camera_start(lv_obj_t * obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s * aic_ctx = camera->aic_ctx;
    if (aic_ctx && (aic_ctx->status == LV_AIC_CAMERA_STATUS_READY ||
                    aic_ctx->status == LV_AIC_CAMERA_STATUS_STOP)) {
        lv_mutex_lock(&aic_ctx->mutex);
        aic_ctx->status = LV_AIC_CAMERA_STATUS_START;
        lv_mutex_unlock(&aic_ctx->mutex);
        lv_thread_sync_signal(&aic_ctx->video_sync);
        return LV_RES_OK;
    } else {
        if (aic_ctx)
            LV_LOG_WARN("the camera status error, status = %d", (int)aic_ctx->status);
        else
            LV_LOG_WARN("the camera ctx is NULL");
    }
    return LV_RES_INV;
}

lv_res_t lv_aic_camera_stop(lv_obj_t * obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s * aic_ctx = camera->aic_ctx;
    if (aic_ctx && (aic_ctx->status == LV_AIC_CAMERA_STATUS_RUNNING)) {
        lv_mutex_lock(&aic_ctx->mutex);
        aic_ctx->status = LV_AIC_CAMERA_STATUS_STOP;
        lv_mutex_unlock(&aic_ctx->mutex);
        return LV_RES_OK;
    } else {
        if (aic_ctx)
            LV_LOG_WARN("the camera status error, status = %d", (int)aic_ctx->status);
        else
            LV_LOG_WARN("the camera ctx is NULL");
    }
    return LV_RES_INV;
}

lv_res_t lv_aic_camera_pause(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (!aic_ctx || aic_ctx->status != LV_AIC_CAMERA_STATUS_RUNNING) {
        LV_LOG_WARN("pause: camera not running");
        return LV_RES_INV;
    }

    mpp_vin2_ioctl(VIN_STREAM_PAUSE, NULL, aic_ctx->curr_ch, &aic_ctx->vin_ctx);

    return LV_RES_OK;
}

lv_res_t lv_aic_camera_resume(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (!aic_ctx || aic_ctx->status != LV_AIC_CAMERA_STATUS_RUNNING) {
        LV_LOG_WARN("resume: camera not running");
        return LV_RES_INV;
    }

    mpp_vin2_ioctl(VIN_STREAM_RESUME, NULL, aic_ctx->curr_ch, &aic_ctx->vin_ctx);

    return LV_RES_OK;
}

int lv_aic_camera_set_channel(lv_obj_t *obj, uint32_t ch)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;

    if (ch > 3) {
        LV_LOG_WARN("set_channel: invalid channel %d", (int)ch);
        return -1;
    }

    if (!camera->aic_ctx) {
        LV_LOG_WARN("set_channel: camera not opened");
        return -1;
    }

    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (aic_ctx->switching) {
        LV_LOG_WARN("set_channel: busy");
        return -1;
    }

    aic_ctx->switching = true;

    if (camera_set_channel(aic_ctx->vin_ctx.camera_dev, ch) < 0) {
        LV_LOG_ERROR("set_channel: failed");
        aic_ctx->switching = false;
        return -1;
    }

    aic_ctx->curr_ch = ch;
    aic_ctx->switching = false;

    return 0;
}

uint32_t lv_aic_camera_get_channel(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    if (!camera->aic_ctx)
        return 0;
    return camera->aic_ctx->curr_ch;
}

#if AIC_CAMERA_USE_BARCODE
lv_res_t lv_aic_camera_barcode_enable(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s * aic_ctx = camera->aic_ctx;
    if (aic_ctx) {
        LV_LOG_WARN("Before enable the barcode, please make sure the camera is not opened.");
        return LV_RES_INV;
    }
    camera->barcode_en = true;
    return LV_RES_OK;
}

lv_res_t lv_aic_camera_barcode_disable(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s * aic_ctx = camera->aic_ctx;
    if (aic_ctx) {
        LV_LOG_WARN("Before enable the barcode, please make sure the camera is not opened.");
        return LV_RES_INV;
    }
    camera->barcode_en = false;
    return LV_RES_OK;
}

lv_res_t lv_aic_camera_barcode_callback(lv_obj_t *obj, lv_aic_camera_barcode_cb_t cb, char *data, int data_size)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s * aic_ctx = camera->aic_ctx;

    if (aic_ctx) {
        LV_LOG_WARN("Before enable the barcode, please make sure the camera is not opened.");
        return LV_RES_INV;
    }

    camera->barcode_cb = cb;
    camera->barcode_data = data;
    camera->barcode_data_size = data_size;
    return LV_RES_OK;
}

lv_res_t lv_aic_camera_barcode_only(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    camera->barcode_only = true;
    return LV_RES_OK;
}

static void barcode_decode_create(struct aic_camera_ctx_s *aic_ctx)
{
    int ret;
    static int barcode_init = 0;

    if (barcode_init == 0) {
        ret = Initial_Decoder();
        if (ret != BARCODE_DEMO_SUCCESS) {
            LV_LOG_WARN("Initial_Decoder failed with %d \n", ret);
        }
    }

    for (int i = 0; i <= 13; i++) {
        if (barcode_init == 0)
            Set_Donfig_Decoder(i, 1);
    }
    barcode_init = 1;

    /* only malloc the Y plane to barcode buffer */
    aic_ctx->barcode_buffer = aicos_malloc_try_cma(aic_ctx->w * aic_ctx->h);
    if (aic_ctx->barcode_buffer == NULL)
        LV_LOG_WARN("malloc barcode buffer failed, size: %d\n", (int)aic_ctx->w * aic_ctx->h);
}

static void barcode_decode_delete(struct aic_camera_ctx_s *aic_ctx)
{
    if (aic_ctx->barcode_buffer)
        aicos_free(MEM_CMA, aic_ctx->barcode_buffer);
    aic_ctx->barcode_buffer = NULL;
}

static void barcode_decode_buffer_sync(struct aic_camera_ctx_s *aic_ctx, int index)
{
    struct vin_video_buf *binfo = &aic_ctx->binfo;
    unsigned long buf = binfo->planes[index * VID_BUF_PLANE_NUM].buf;
    int len = binfo->planes[index * VID_BUF_PLANE_NUM].len;

    /* only copy the Y plane to barcode buffer */
    aicos_dcache_invalid_range((void*)buf, len);
    aicos_memcpy(aic_ctx->barcode_buffer, (void *)buf, len);
}

static void barcode_decode(lv_obj_t *obj)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;
    unsigned char result[256] = {0};
    int len = 0;

    if (aic_ctx == NULL || aic_ctx->barcode_buffer == NULL)
        return;

    Decoding_Image(aic_ctx->barcode_buffer, aic_ctx->w, aic_ctx->h);
    len =  GetResultLength();
    if (len > 0) {
        if (len >= 255)
            LV_LOG_ERROR("barcode len too long, max len is 255");
        GetDecoderResult(result);
        if (camera->barcode_cb)
            camera->barcode_cb((const char *)result, len, camera->barcode_data, camera->barcode_data_size);
    }
}
#endif

static int calc_frame_size(int fmt, int w, int h)
{
    if (fmt == MPP_FMT_NV16)
        return w * h * 2;
    if (fmt == MPP_FMT_NV12)
        return (w * h * 3) >> 1;
    return w * h;
}

static int mpp_format_to_camera_format(lv_aic_camera_format format)
{
    switch (format)
    {
    case LV_AIC_CAMERA_FORMAT_NV16:
        return MPP_FMT_NV16;
    case LV_AIC_CAMERA_FORMAT_NV12:
        return MPP_FMT_NV12;
    case LV_AIC_CAMERA_FORMAT_YUV400:
        return MPP_FMT_YUV400;
    default:
        return MPP_FMT_NV16;
    }
}

static int update_bg_blend_config(struct aic_camera_ctx_s *aic_ctx)
{
    unsigned int bg_update = 1;

    if (mpp_fb_ioctl(aic_ctx->fb, AICFB_UPDATE_BG_BLEND_UI_CONFIG, &bg_update) < 0) {
        LV_LOG_ERROR("Failed to update bg ui blend config!");
        return -1;
    }
    return 0;
}

static int lv_camera_video_layer_disable(struct aic_camera_ctx_s *aic_ctx)
{
    struct aicfb_layer_data layer = {0};

    layer.enable = 0;
    int ret = mpp_fb_ioctl(aic_ctx->fb, AICFB_UPDATE_LAYER_CONFIG, &layer);
    if (ret < 0) {
        LV_LOG_ERROR("Failed to disable video layer!");
        return -1;
    }

    return update_bg_blend_config(aic_ctx);
}

static int video_layer_init(struct aic_camera_ctx_s *aic_ctx)
{
    struct aicfb_layer_data *layer = &aic_ctx->cached_layer;
    struct mpp_rect dst_rect = {0};

    memset(layer, 0, sizeof(*layer));
    layer->layer_id = AICFB_LAYER_TYPE_VIDEO;
    layer->enable = 1;

    lv_mutex_lock(&aic_ctx->mutex);
    memcpy(&dst_rect, &aic_ctx->dst_rect, sizeof(struct mpp_rect));
    lv_mutex_unlock(&aic_ctx->mutex);

    layer->pos.x = dst_rect.x;
    layer->pos.y = dst_rect.y;
    layer->scale_size.width = dst_rect.width;
    layer->scale_size.height = dst_rect.height;

    layer->buf.size.width = aic_ctx->w;
    if (aic_dvp_sfield_mode())
        layer->buf.size.height = aic_ctx->h / 2;
    else
        layer->buf.size.height = aic_ctx->h;

    layer->buf.format = aic_ctx->dst_fmt;
    layer->buf.buf_type = MPP_PHY_ADDR;

    for (int i = 0; i < VID_BUF_PLANE_NUM; i++)
        layer->buf.stride[i] = layer->buf.size.width;

    return 0;
}

static int video_layer_update_buf(struct aic_camera_ctx_s *aic_ctx, int index)
{
    struct vin_video_buf *binfo = &aic_ctx->binfo;
    struct aicfb_layer_data *layer = &aic_ctx->cached_layer;
    int ret;

    lv_mutex_lock(&aic_ctx->mutex);
    layer->pos.x = aic_ctx->dst_rect.x;
    layer->pos.y = aic_ctx->dst_rect.y;
    layer->scale_size.width = aic_ctx->dst_rect.width;
    layer->scale_size.height = aic_ctx->dst_rect.height;
    lv_mutex_unlock(&aic_ctx->mutex);

    for (int i = 0; i < VID_BUF_PLANE_NUM; i++)
        layer->buf.phy_addr[i] = binfo->planes[index * VID_BUF_PLANE_NUM + i].buf;

    ret = mpp_fb_ioctl(aic_ctx->fb, AICFB_UPDATE_LAYER_CONFIG, layer);
    if (ret < 0)
        LV_LOG_ERROR("Failed to update layer config for ch %d", (int)aic_ctx->curr_ch);

    return ret;
}

static void lv_camera_draw_video_layer_entry(void *ptr)
{
    struct aic_camera_ctx_s *aic_ctx = (struct aic_camera_ctx_s *)ptr;
    struct vin_dev_ctx *vin_ctx = &aic_ctx->vin_ctx;
#if AIC_CAMERA_USE_BARCODE
    lv_aic_camera_t *camera = (lv_aic_camera_t *)aic_ctx->obj;
#endif
    int i, index;
    bool layer_inited = false;

    while (!aic_ctx->thread_exit) {
        lv_thread_sync_wait(&aic_ctx->video_sync);
        if (aic_ctx->thread_exit)
            break;

        aic_ctx->binfo.num_buffers = aic_ctx->num_buffers;
        if (mpp_vin2_vb_init(aic_ctx->ch, vin_ctx) < 0) {
            LV_LOG_ERROR("mpp_vin2_vb_init failed for ch %d", (int)aic_ctx->curr_ch);
            break;
        }

        if (mpp_vin2_ioctl(VIN_REQ_BUF, &aic_ctx->binfo, aic_ctx->ch, vin_ctx) < 0) {
            LV_LOG_ERROR("mpp_vin2_ioctl(VIN_REQ_BUF) failed for ch %d", (int)aic_ctx->curr_ch);
            goto stop;
        }

        for (i = 0; i < VID_BUF_NUM; i++) {
            if (mpp_vin2_ioctl(VIN_Q_BUF, (void *)(ptr_t)i, aic_ctx->ch, vin_ctx) < 0) {
                LV_LOG_ERROR("mpp_vin2_ioctl(VIN_Q_BUF) failed for index %d", i);
                goto stop;
            }
        }

        if (mpp_vin2_ioctl(VIN_STREAM_ON, NULL, aic_ctx->ch, vin_ctx) < 0) {
            LV_LOG_ERROR("mpp_vin2_ioctl(VIN_STREAM_ON) failed for ch %d", (int)aic_ctx->curr_ch);
            goto stop;
        }

        aic_ctx->fresh_frame = VID_DROP_FRESH_FRAME;
        lv_mutex_lock(&aic_ctx->mutex);
        aic_ctx->running = true;
        if (aic_ctx->status != LV_AIC_CAMERA_STATUS_RUNNING)
            aic_ctx->status = LV_AIC_CAMERA_STATUS_RUNNING;
        lv_mutex_unlock(&aic_ctx->mutex);

        if (!layer_inited) {
            video_layer_init(aic_ctx);
            layer_inited = true;
        }

        int ret = 0;

        while (aic_ctx->status == LV_AIC_CAMERA_STATUS_RUNNING && !aic_ctx->thread_exit) {
            ret = mpp_vin2_ioctl(VIN_DQ_BUF, (void *)&index, aic_ctx->ch, vin_ctx);
            if (ret < 0) {
                if (ret == -EBUSY) {
                    aicos_msleep(10);
                    continue;
                }
                LV_LOG_WARN("mpp_vin2_ioctl(VIN_DQ_BUF) failed for ch %d, ret = %d", (int)aic_ctx->curr_ch, ret);
                break;
            }

            if (aic_ctx->fresh_frame) {
                if (mpp_vin2_ioctl(VIN_Q_BUF, (void *)(ptr_t)index, aic_ctx->ch, vin_ctx) < 0) {
                    LV_LOG_WARN("mpp_vin2_ioctl(VIN_Q_BUF) failed for ch %d", (int)aic_ctx->curr_ch);
                    break;
                }
                aic_ctx->fresh_frame--;
                continue;
            }

#if AIC_CAMERA_USE_BARCODE
            if (camera->barcode_en) {
                if (aic_ctx->barcode_ready == true && aic_ctx->barcode_buffer) {
                    lv_mutex_lock(&aic_ctx->mutex);
                    barcode_decode_buffer_sync(aic_ctx, index);
                    lv_mutex_unlock(&aic_ctx->mutex);
                    lv_thread_sync_signal(&aic_ctx->barcode_sync);
                } else {
                    lv_mutex_lock(&aic_ctx->mutex);
                    aic_ctx->barcode_ready = false;
                    lv_mutex_unlock(&aic_ctx->mutex);
                }
            }
            if (camera->barcode_only == false
                && video_layer_update_buf(aic_ctx, index) < 0) {
                LV_LOG_WARN("video_layer_update_buf error");
                break;
            }
#else
            if (video_layer_update_buf(aic_ctx, index) < 0) {
                LV_LOG_WARN("video_layer_update_buf error");
                break;
            }
#endif

            if (!aic_ctx->video_ready) {
                aic_ctx->video_ready = true;
            }

            if (mpp_vin2_ioctl(VIN_Q_BUF, (void *)(ptr_t)index, aic_ctx->ch, vin_ctx) < 0) {
                LV_LOG_WARN("mpp_vin2_ioctl(VIN_Q_BUF) failed for ch %d", (int)aic_ctx->curr_ch);
                break;
            }
        }

stop:
        aic_ctx->running = false;
        mpp_vin2_ioctl(VIN_STREAM_OFF, NULL, aic_ctx->ch, vin_ctx);
        mpp_vin2_vb_deinit(aic_ctx->ch, vin_ctx);
    }

    lv_thread_sync_signal(&aic_ctx->video_exit_sync);
}

#if AIC_CAMERA_USE_BARCODE
static void barcode_decode_entry(void *ptr)
{
    lv_aic_camera_t *camera = (lv_aic_camera_t *)ptr;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    while (!aic_ctx->thread_exit) {
        lv_thread_sync_wait(&aic_ctx->barcode_sync);
        if (aic_ctx->thread_exit)
            break;

        barcode_decode((lv_obj_t *)camera);
        lv_mutex_lock(&aic_ctx->mutex);
        aic_ctx->barcode_ready = true;
        lv_mutex_unlock(&aic_ctx->mutex);
    }
    lv_thread_sync_signal(&aic_ctx->barcode_exit_sync);
}
#endif

static int camera_get_format(struct aic_camera_ctx_s *aic_ctx)
{
    int ret = 0;
    struct mpp_video_fmt fmt = {0};

    ret = mpp_vin2_ioctl(VIN_IN_G_FMT, &fmt, 0, &aic_ctx->vin_ctx);
    if (ret < 0) {
        LV_LOG_ERROR("Failed to get sensor format!");
        return -1;
    }
    aic_ctx->src_fmt = fmt;
    aic_ctx->w = aic_ctx->src_fmt.width;
    aic_ctx->h = aic_ctx->src_fmt.height;
    LV_LOG_INFO("Sensor format: w %d h %d, code 0x%x, bus 0x%x, colorspace 0x%x", fmt.width, fmt.height, fmt.code, fmt.bus_type, fmt.colorspace);

    if (fmt.bus_type == MEDIA_BUS_RAW8_MONO) {
        LV_LOG_INFO("Forbid the output format to YUV400");
        aic_ctx->dst_fmt = MPP_FMT_YUV400;
    }

    return 0;
}

static int lv_camera_subdev_set(struct aic_camera_ctx_s *aic_ctx)
{
    int ret = 0;

    ret = mpp_vin2_ioctl(VIN_IN_S_FMT, &aic_ctx->src_fmt, 0, &aic_ctx->vin_ctx);
    if (ret < 0) {
        LV_LOG_ERROR("Failed to set VIN in-format!\n");
        return -1;
    }

    return 0;
}

static int lv_camera_config(struct aic_camera_ctx_s *aic_ctx, u32 stitch_mode)
{
    int ret = 0;
    struct dvp_out_fmt fmt = {0};

    fmt.width = aic_ctx->src_fmt.width;
    fmt.height = aic_ctx->src_fmt.height;
    fmt.pixelformat = aic_ctx->dst_fmt;
    fmt.num_planes = VID_BUF_PLANE_NUM;
    fmt.stitch_mode = stitch_mode;

    fmt.framesize = calc_frame_size(fmt.pixelformat, fmt.width, fmt.height);

    ret = mpp_vin2_ioctl(VIN_OUT_S_FMT, &fmt, 0, &aic_ctx->vin_ctx);
    if (ret < 0) {
        LV_LOG_ERROR("Failed to set VIN out-format! err -%d", -ret);
        return -1;
    }

    aic_ctx->vin_ctx.dst_fmt = fmt;

    return 0;
}

static void lv_aic_camera_constructor(const lv_obj_class_t * class_p,
                                        lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    lv_aic_camera_t * camera = (lv_aic_camera_t *)obj;
    camera->format  = LV_AIC_CAMERA_FORMAT_NV16;
    camera->aic_ctx = NULL;
}

static void ready_timer_cb(lv_timer_t *t)
{
    lv_obj_t *obj = t->user_data;
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    if (!camera->aic_ctx || !camera->aic_ctx->video_ready)
        return;
    lv_obj_invalidate(obj);
    lv_timer_del(t);
    camera->aic_ctx->ready_timer = NULL;
}

static void lv_aic_camera_destructor(const lv_obj_class_t * class_p,
                                       lv_obj_t * obj)
{
    LV_TRACE_OBJ_CREATE("begin");

    lv_aic_camera_t * camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s * aic_ctx = camera->aic_ctx;

    if (aic_ctx) {
        lv_mutex_lock(&aic_ctx->mutex);
        aic_ctx->thread_exit = true;
        aic_ctx->status = LV_AIC_CAMERA_STATUS_STOP;
        lv_mutex_unlock(&aic_ctx->mutex);

        lv_thread_sync_signal(&aic_ctx->video_sync);

        if (aic_ctx->ready_timer)
            lv_timer_del(aic_ctx->ready_timer);

        if (aic_ctx->running) {
            mpp_vin2_ioctl(VIN_STREAM_OFF, NULL, aic_ctx->ch, &aic_ctx->vin_ctx);
        }

        lv_thread_sync_wait(&aic_ctx->video_exit_sync);

        lv_thread_delete(&aic_ctx->video_thread);
#if AIC_CAMERA_USE_BARCODE
        if (aic_ctx->barcode_create) {
            lv_thread_sync_signal(&aic_ctx->barcode_sync);
            lv_thread_sync_wait(&aic_ctx->barcode_exit_sync);
            lv_thread_delete(&aic_ctx->barcode_thread);
            lv_thread_sync_delete(&aic_ctx->barcode_sync);
            lv_thread_sync_delete(&aic_ctx->barcode_exit_sync);
            barcode_decode_delete(aic_ctx);
        }
#endif

        mpp_vin2_deinit(&aic_ctx->vin_ctx);

        lv_mutex_delete(&aic_ctx->mutex);
        lv_thread_sync_delete(&aic_ctx->video_sync);
        lv_thread_sync_delete(&aic_ctx->video_exit_sync);
        lv_mem_free(aic_ctx);

        lv_camera_video_layer_disable(aic_ctx);
        mpp_fb_close(aic_ctx->fb);
    }
    LV_TRACE_OBJ_CREATE("finished");
}

static void lv_aic_camera_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);
    static char fill_src[128] = {0};

    lv_event_code_t code = lv_event_get_code(e);
    lv_result_t res = lv_obj_event_base(MY_CLASS, e);
    if(res != LV_RESULT_OK) return;
    lv_obj_t * obj = lv_event_get_current_target(e);
    lv_aic_camera_t *camera = (lv_aic_camera_t *)obj;
    struct aic_camera_ctx_s *aic_ctx = camera->aic_ctx;

    if (!aic_ctx)
        return;
    if(code == LV_EVENT_GET_SELF_SIZE) {
        lv_point_t * p = lv_event_get_param(e);
        p->x = aic_ctx->w;
        p->y = aic_ctx->h;
    } else if(code == LV_EVENT_DRAW_MAIN) {
#if AIC_CAMERA_USE_BARCODE
        if (camera->barcode_only)
            return;
#endif
        lv_area_t fill_area = {0};
        lv_obj_get_coords(obj, &fill_area);

        if (aic_ctx->video_ready) {
            lv_mutex_lock(&aic_ctx->mutex);
            aic_ctx->dst_rect.x = fill_area.x1;
            aic_ctx->dst_rect.y = fill_area.y1;
            aic_ctx->dst_rect.width = fill_area.x2 - fill_area.x1 + 1;
            aic_ctx->dst_rect.height = fill_area.y2 - fill_area.y1 + 1;
            lv_mutex_unlock(&aic_ctx->mutex);
        }

        int alpha_en = 0;
        int width = fill_area.x2 - fill_area.x1 + 1;
        int height = fill_area.y2 - fill_area.y1 + 1;
        uint8_t bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
        lv_color_t bg_color = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
        uint32_t color = (bg_opa << 24) | (bg_color.red << 16) |
                        (bg_color.green << 8) | (bg_color.blue);
        snprintf(fill_src, sizeof(fill_src), "L:/%dx%d_%d_%08lx.fake",
                (int)width, (int)height, alpha_en, (unsigned long)color);

        lv_draw_image_dsc_t draw_dsc;
        lv_draw_image_dsc_init(&draw_dsc);
        lv_obj_init_draw_image_dsc(obj, LV_PART_MAIN, &draw_dsc);
        draw_dsc.src = fill_src;
        lv_layer_t * layer = lv_event_get_layer(e);
        lv_draw_image(layer, &draw_dsc, &fill_area);
    }
}
#endif
