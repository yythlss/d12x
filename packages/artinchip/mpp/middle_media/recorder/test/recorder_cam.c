/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <che.jiang@artinchip.com>
 * Desc:  DVP camera module — sensor init, DVP config, buffer & stream ops
 *        Referenced from luban-1.0 recorder_demo.c DVP code pattern.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG_DEBUG
#include "mpp_log.h"
#include "mpp_vin.h"
#ifdef AIC_USING_CAMERA
#include "drv_camera.h"
#endif
#include "recorder_cam.h"

static struct recorder_cam_data *g_cam = NULL;

int recorder_cam_init(struct recorder_cam_data *cam)
{
    struct mpp_video_fmt *src;
    struct dvp_out_fmt *dst;
    int ret = -1;
    int i;

    if (!cam) {
        loge("cam is null");
        return -1;
    }

    g_cam = cam;
    src = &cam->src_fmt;
    dst = &cam->dst_fmt;

#ifdef AIC_USING_CAMERA
    if (mpp_vin_init(CAMERA_DEV_NAME))
        return -1;
#endif

    /* get sensor format */
    ret = mpp_dvp_ioctl(DVP_IN_G_FMT, src);
    if (ret < 0) {
        loge("ioctl(DVP_IN_G_FMT) failed! err -%d\n", -ret);
        goto _exit;
    }

    ret = mpp_dvp_ioctl(DVP_IN_S_FMT, src);
    if (ret < 0) {
        loge("ioctl(DVP_IN_S_FMT) failed! err -%d\n", -ret);
        goto _exit;
    }

    /* configure DVP output */
    cam->w = src->width;
    cam->h = src->height;
    dst->width  = src->width;
    dst->height = src->height;
    dst->pixelformat = MPP_FMT_NV12;
    dst->num_planes  = RECORDER_CAM_PLANE_NUM;
    dst->framesize   = (dst->width * dst->height * 3) >> 1;
    dst->frame_offset = 0;
    ret = mpp_dvp_ioctl(DVP_OUT_S_FMT, dst);
    if (ret < 0) {
        loge("ioctl(DVP_OUT_S_FMT) failed! err -%d\n", -ret);
        goto _exit;
    }

    /* request buffers */
    cam->binfo.num_buffers = RECORDER_CAM_BUF_NUM;
    cam->binfo.num_planes  = RECORDER_CAM_PLANE_NUM;
    if (mpp_dvp_ioctl(DVP_REQ_BUF, (void *)&cam->binfo) < 0) {
        loge("ioctl(DVP_REQ_BUF) failed!\n");
        goto _exit;
    }

    /* queue all buffers */
    for (i = 0; i < RECORDER_CAM_BUF_NUM; i++) {
        if (recorder_cam_queue_buf(i) < 0) {
            loge("queue_buf %d failed!\n", i);
            goto _exit;
        }
    }

    /* start streaming */
    ret = mpp_dvp_ioctl(DVP_STREAM_ON, NULL);
    if (ret < 0) {
        loge("ioctl(DVP_STREAM_ON) failed! err -%d\n", -ret);
        goto _exit;
    }

    logi("cam: src w(%d) h(%d), dst w(%d) h(%d) fmt(%d)\n",
         src->width, src->height, dst->width, dst->height, dst->pixelformat);

    cam->cam_init = true;
    return 0;

_exit:
    mpp_vin_deinit();
    cam->cam_init = false;
    return -1;
}

void recorder_cam_deinit(struct recorder_cam_data *cam)
{
    int ret;

    if (!cam || !cam->cam_init)
        return;

    ret = mpp_dvp_ioctl(DVP_STREAM_OFF, NULL);
    if (ret < 0) {
        loge("ioctl(DVP_STREAM_OFF) failed! err -%d\n", -ret);
        return;
    }

    mpp_vin_deinit();
    cam->cam_init = false;
}

int recorder_cam_queue_buf(int index)
{
    if (mpp_dvp_ioctl(DVP_Q_BUF, (void *)(ptr_t)index) < 0) {
        loge("Q_BUF %d failed!\n", index);
        return -1;
    }
    return 0;
}

int recorder_cam_dequeue_buf(int *index)
{
    int ret;

    if (!index)
        return -1;

    ret = mpp_dvp_ioctl(DVP_DQ_BUF, (void *)index);
    if (ret < 0) {
        logd("ioctl(DVP_DQ_BUF) failed! err -%d\n", -ret);
        return -1;
    }
    return 0;
}
