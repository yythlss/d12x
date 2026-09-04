/*
 * Copyright (c) 2023-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Zequan Liang <zequan.liang@artinchip.com>
 */

#ifndef LV_AIC_CAMERA_H
#define LV_AIC_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

struct aic_camera_ctx_s;

extern const lv_obj_class_t lv_aic_camera_class;

typedef void (*lv_aic_camera_barcode_cb_t)(const char *in_data, int in_length, char *out_data, int out_length);
typedef struct {
    lv_obj_t obj;
    uint32_t format;
#if AIC_CAMERA_USE_BARCODE
    bool barcode_only;
    bool barcode_en;
    char *barcode_data;
    int barcode_data_size;
    lv_aic_camera_barcode_cb_t barcode_cb;
#endif
    uint32_t init_ch;
    struct aic_camera_ctx_s * aic_ctx;
} lv_aic_camera_t;

typedef enum {
    LV_AIC_CAMERA_FORMAT_NV16,
    LV_AIC_CAMERA_FORMAT_NV12,
    LV_AIC_CAMERA_FORMAT_YUV400,
    _LV_AIC_CAMERA_FORMAT_LAST
} lv_aic_camera_format;

typedef enum {
    LV_AIC_CAMERA_CH_VIN1 = 0,
    LV_AIC_CAMERA_CH_VIN2 = 2,
} lv_aic_camera_ch_t;

lv_obj_t * lv_aic_camera_create(lv_obj_t * parent);

lv_res_t lv_aic_camera_set_format(lv_obj_t * obj, lv_aic_camera_format format);

lv_res_t lv_aic_camera_open(lv_obj_t * obj);

lv_res_t lv_aic_camera_start(lv_obj_t * obj);

lv_res_t lv_aic_camera_stop(lv_obj_t * obj);

int lv_aic_camera_set_channel(lv_obj_t *obj, uint32_t ch);

uint32_t lv_aic_camera_get_channel(lv_obj_t *obj);

lv_res_t lv_aic_camera_pause(lv_obj_t *obj);

lv_res_t lv_aic_camera_resume(lv_obj_t *obj);

#if AIC_CAMERA_USE_BARCODE
lv_res_t lv_aic_camera_barcode_only(lv_obj_t *obj);

lv_res_t lv_aic_camera_barcode_enable(lv_obj_t *obj);

lv_res_t lv_aic_camera_barcode_disable(lv_obj_t *obj);

lv_res_t lv_aic_camera_barcode_callback(lv_obj_t *obj, lv_aic_camera_barcode_cb_t cb, char *data, int data_size);
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif
#endif
