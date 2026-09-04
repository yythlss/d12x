/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <che.jiang@artinchip.com>
 * Desc:  DVP camera module — VIN init, buffer ops, stream control
 */

#ifndef __RECORDER_CAM_H__
#define __RECORDER_CAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "mpp_types.h"
#include "mpp_vin.h"

#define RECORDER_CAM_BUF_NUM        3
#define RECORDER_CAM_PLANE_NUM      2

struct recorder_cam_data {
    int w;
    int h;
    struct mpp_video_fmt src_fmt;   /* input format from camera sensor */
    struct dvp_out_fmt   dst_fmt;   /* output format to DVP */
    struct vin_video_buf binfo;
    bool cam_init;
};

/**
 * recorder_cam_init - initialize camera (sensor + DVP) and start streaming
 * @cam: pointer to cam data structure
 * Return: 0 on success, -1 on failure
 */
int recorder_cam_init(struct recorder_cam_data *cam);

/**
 * recorder_cam_deinit - stop streaming and release camera resources
 * @cam: pointer to cam data structure
 */
void recorder_cam_deinit(struct recorder_cam_data *cam);

/**
 * recorder_cam_queue_buf - queue a buffer back to DVP for refill
 * @index: buffer index
 * Return: 0 on success, -1 on failure
 */
int recorder_cam_queue_buf(int index);

/**
 * recorder_cam_dequeue_buf - dequeue a filled buffer from DVP
 * @index: output, buffer index
 * Return: 0 on success, -1 on failure
 */
int recorder_cam_dequeue_buf(int *index);

#ifdef __cplusplus
}
#endif

#endif /* __RECORDER_CAM_H__ */
