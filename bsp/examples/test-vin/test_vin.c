/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#include "test_vin.h"

struct vin_test_ctx g_vin_test_ctx = {0};

const char *g_vin_names[VIN_DEV_MAX] = {"DVP", "CSI"};

#ifdef AIC_DISPLAY_DRV
struct aicfb_screeninfo g_fb_info = {0};
struct mpp_fb *g_fb = NULL;
#endif

static void usage(const char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("\nOptions:\n");
    printf("  -d, --device <dev>     Video in device (dvp/csi), default: dvp\n");
    printf("  -C, --channel <n>      Channel number (0-%d), default: 0\n", VIN_MAX_CHANNELS - 1);
    printf("  -f, --format <fmt>     Pixel format (nv12/nv16/yuv400), default: nv16\n");
    printf("  -c, --count <n>        Number of frames to capture, 0=infinite, default: 10\n");
    printf("  -s, --stitch <mode>    Stitch mode (none/v/h), default: none\n");
    printf("  -S, --show             Show the video of given channel, only valid for none stitch mode\n");
    printf("  -w, --width <n>        Sensor width (pixels), default: camera default\n");
    printf("  -h, --height <n>       Sensor height (pixels), default: camera default\n");
    printf("  -r, --framerate <n>    Sensor framerate (fps), default: camera default\n");
    printf("  -W, --wait             Wait for threads to finish before exiting\n");
#ifdef SUPPORT_ROTATION_BY_VE
    printf("  -a, --angle <deg>      Rotation angle (0/90/180/270) via VE JPEG encode+decode\n");
#endif
    printf("  -u, --help             Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -C 0 -c 10          # Capture 10 frames from channel 0\n", program);
    printf("  %s -C 1 -f nv12 -c 0   # Continuous capture from channel 1\n", program);
    printf("  %s -s v -c 10          # Vertical stitch mode\n", program);
    printf("  %s -s h -c 10          # Horizontal stitch mode\n", program);
    printf("\nRuntime Commands (while streaming):\n");
    printf("  %s pause               # Pause streaming\n", program);
    printf("  %s resume              # Resume streaming\n", program);
    printf("  %s stop                # Stop streaming\n", program);
    printf("  %s chan <0|1|2>        # Switch display: 0=CH0, 1=CH1, 2=stitch\n", program);
}

static enum mpp_stitch_mode parse_stitch_mode(const char *arg)
{
    if (strncasecmp("v", arg, 1) == 0) {
        printf("Stitch mode: Vertical (CH0 top, CH1 bottom)\n");
        return MPP_STITCH_V_MODE;
    }
    if (strncasecmp("h", arg, 1) == 0) {
        printf("Stitch mode: Horizontal (CH0 left, CH1 right)\n");
        return MPP_STITCH_H_MODE;
    }
    if (strncasecmp("none", arg, 4) == 0) {
        printf("Stitch mode: None\n");
        return MPP_STITCH_NONE;
    }
    pr_err("Invalid stitch mode: %s (should use: none/v/h)\n", arg);
    return MPP_STITCH_INVALID;
}

static int parse_format(const char *arg)
{
    if (strncasecmp("nv12", arg, 4) == 0)
        return MPP_FMT_NV12;
    if (strncasecmp("nv16", arg, 4) == 0)
        return MPP_FMT_NV16;
    if (strncasecmp("yuv400", arg, 6) == 0)
        return MPP_FMT_YUV400;

    return -1;
}

bool vin_task_is_running(void)
{
    int i;

    for (i = 0; i < VIN_MAX_CHANNELS; i++) {
        if (g_vin_test_ctx.running[i])
            return true;
    }
    return false;
}

static int camera_get_fmt(struct vin_dev_ctx *vin_ctx)
{
    int ret = 0;
    struct mpp_video_fmt f = {0};

    ret = mpp_vin2_ioctl(VIN_IN_G_FMT, &f, 0, vin_ctx);
    if (ret < 0) {
        pr_err("Failed to get camera format! err -%d\n", -ret);
        return -1;
    }

    vin_ctx->src_fmt = f;
    vin_ctx->src_size.width = f.width;
    vin_ctx->src_size.height = f.height;

    if (f.bus_type == MEDIA_BUS_RAW8_MONO) {
        pr_info("Forbid the output format to YUV400\n");
        vin_ctx->dst_fmt.pixelformat = MPP_FMT_YUV400;
    }

    return 0;
}

/* Apply user-specified sensor resolution / framerate overrides.
 * Must happen before vin_subdev_set_fmt() so the new dimensions
 * are propagated through the MPP layer to the camera driver.
 */

static int camera_try_set_fmt(struct vin_dev_ctx *vin_ctx, struct mpp_video_fmt *f)
{
    int ret = 0;

    if ((!f->width || !f->height) && !f->fps)
        return 0;

    if (!f->width)
        f->width = vin_ctx->src_fmt.width;
    if (!f->height)
        f->height = vin_ctx->src_fmt.height;
    if (!f->fps)
        f->fps = vin_ctx->src_fmt.fps;

    printf("\nTry to set camera format: %ux%u@%u\n", f->width, f->height, f->fps);
    ret = mpp_vin2_ioctl(VIN_CAM_S_FMT, f, 0, vin_ctx);
    if (ret < 0) {
        pr_err("Failed to set camera format! err -%d\n", -ret);
        return -1;
    }

    return camera_get_fmt(vin_ctx);
}

static int vin_subdev_set_fmt(struct vin_dev_ctx *vin_ctx)
{
    int ret = 0;

    ret = mpp_vin2_ioctl(VIN_IN_S_FMT, &vin_ctx->src_fmt, 0, vin_ctx);
    if (ret < 0) {
        pr_err("Failed to set VIN in-format! err -%d\n", -ret);
        return -1;
    }

    return 0;
}

static int vin_dev_cfg(struct vin_test_ctx *ctx)
{
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    struct mpp_video_fmt *src = &vin_ctx->src_fmt;
    struct vin_video_fmt *dst = &vin_ctx->dst_fmt;
    int ret = 0;

    if (ctx->vin_crop || (dst->stitch_mode == MPP_STITCH_H_MODE)) {
        u32 stitch_w;

        if (dst->stitch_mode == MPP_STITCH_H_MODE)
            stitch_w = src->width * 2;
        else
            stitch_w = src->width;

        /* Crop the camera image in center-aligned way */
        if (stitch_w > DE_MAX_WIDTH) {
            if (dst->stitch_mode == MPP_STITCH_H_MODE) {
                dst->width = ALIGN_DOWN(DE_MAX_WIDTH / 2, 8);
                dst->crop_x = (stitch_w - dst->width * 2) / 4;
            } else {
                dst->width = DE_MAX_WIDTH;
                dst->crop_x = (stitch_w - DE_MAX_WIDTH) / 2;
            }
        } else {
            dst->width = stitch_w;
        }

        dst->height = src->height;
    } else {
        dst->width = src->width;
        dst->height = src->height;
    }

    if (dst->pixelformat == MPP_FMT_NV16)
        dst->framesize = dst->width * dst->height * 2;
    else if (dst->pixelformat == MPP_FMT_NV12)
        dst->framesize = (dst->width * dst->height * 3) >> 1;
    else if (dst->pixelformat == MPP_FMT_YUV400)
        dst->framesize = dst->width * dst->height;

    dst->frame_offset = 0;

    ret = mpp_vin2_ioctl(VIN_OUT_S_FMT, dst, 0, vin_ctx);
    if (ret < 0) {
        pr_err("Failed to set VIN out-format! err -%d\n", -ret);
        return -1;
    }
    return 0;
}

static int vin_request_buf(struct vin_video_buf *binfo, u32 ch, struct vin_dev_ctx *ctx)
{
    int i, min_num = VID_BUF_MIN_NUM;

    if (mpp_vin2_ioctl(VIN_REQ_BUF, binfo, ch, ctx) < 0) {
        pr_err("Failed to request buf for channel %d!\n", ch);
        return -1;
    }

    pr_info("[%s%d] Buf Plane[0]   size   Plane[1]   size\n", VIN_NAME(ctx), ch);
    for (i = 0; i < binfo->num_buffers; i++) {
        pr_info("       %3d 0x%08x %-6d 0x%08x %-6d\n", i,
            binfo->planes[i * binfo->num_planes].buf,
            binfo->planes[i * binfo->num_planes].len,
            binfo->planes[i * binfo->num_planes + 1].buf,
            binfo->planes[i * binfo->num_planes + 1].len);
    }

    if (binfo->num_buffers < min_num) {
        pr_err("[ch%d] The number of video buf must >= %d!\n", ch, min_num);
        return -1;
    }

    return 0;
}

int vin_queue_buf(int index, u32 ch, struct vin_dev_ctx *ctx)
{
    if (mpp_vin2_ioctl(VIN_Q_BUF, (void *)(ptr_t)index, ch, ctx) < 0) {
        pr_err("[%s%d] Q failed! Maybe buf state is invalid.\n", VIN_NAME(ctx), ch);
        return -1;
    }

    return 0;
}

static int vin_dequeue_buf(int *index, u32 ch, struct vin_dev_ctx *ctx)
{
    int ret = 0;

    ret = mpp_vin2_ioctl(VIN_DQ_BUF, (void *)index, ch, ctx);
    if (ret < 0) {
        pr_err("[%s%d] DQ failed! Maybe cannot receive data from Camera. err -%d\n",
               VIN_NAME(ctx), ch, -ret);
        return -1;
    }

    return 0;
}

static int vin_start(u32 ch, struct vin_dev_ctx *ctx)
{
    int ret = 0;

    ret = mpp_vin2_ioctl(VIN_STREAM_ON, NULL, ch, ctx);
    if (ret < 0) {
        pr_err("[%s%d] Failed to start streaming! err -%d\n",
               VIN_NAME(ctx), ch, -ret);
        return -1;
    }

    return 0;
}

static int vin_stop(u32 ch, struct vin_dev_ctx *ctx)
{
    int ret = 0;

    ret = mpp_vin2_ioctl(VIN_STREAM_OFF, NULL, ch, ctx);
    if (ret < 0) {
        pr_err("[%s%d] Failed to stop streaming! err -%d\n",
               VIN_NAME(ctx), ch, -ret);
        return -1;
    }

    return 0;
}

static int media_dev_init(void)
{
    /* Lazy-create the mutex (used by media_dev_deinit for thread-safe teardown) */
    if (!g_vin_test_ctx.media_dev_mutex) {
        g_vin_test_ctx.media_dev_mutex = aicos_mutex_create();
        if (!g_vin_test_ctx.media_dev_mutex)
            return -1;
    }

#ifdef AIC_DISPLAY_DRV
    if (!g_vin_test_ctx.pp_buf_mutex) {
        g_vin_test_ctx.pp_buf_mutex = aicos_mutex_create();
        if (!g_vin_test_ctx.pp_buf_mutex)
            return -1;
    }
    if (!g_vin_test_ctx.dma_complete) {
        g_vin_test_ctx.dma_complete = aicos_sem_create(0);
        if (!g_vin_test_ctx.dma_complete)
            return -1;
    }

    if (!g_fb) {
        g_fb = mpp_fb_open();
        if (!g_fb) {
            pr_err("Failed to open FB\n");
            return -1;
        }
    }
#endif

#ifdef SUPPORT_ROTATION
    if (!g_ge_dev) {
        g_ge_dev = mpp_ge_open();
        if (!g_ge_dev) {
            pr_err("Failed to open GE\n");
            return -1;
        }
    }
#endif

    return 0;
}

static void media_dev_deinit(void)
{
    if (!g_vin_test_ctx.media_dev_mutex)
        return;

    aicos_mutex_take(g_vin_test_ctx.media_dev_mutex, AICOS_WAIT_FOREVER);

    if (vin_task_is_running()) {
        aicos_mutex_give(g_vin_test_ctx.media_dev_mutex);
        return;
    }

#ifdef SUPPORT_ROTATION
    if (g_ge_dev) {
        mpp_ge_close(g_ge_dev);
        g_ge_dev = NULL;
    }
#endif
#ifdef AIC_DISPLAY_DRV
    if (g_fb) {
        vin_vlayer_disable();
        mpp_fb_close(g_fb);
        g_fb = NULL;
    }

    if (g_vin_test_ctx.pp_buf_mutex) {
        aicos_mutex_delete(g_vin_test_ctx.pp_buf_mutex);
        g_vin_test_ctx.pp_buf_mutex = NULL;
    }
    if (g_vin_test_ctx.dma_complete) {
        aicos_sem_delete(g_vin_test_ctx.dma_complete);
        g_vin_test_ctx.dma_complete = NULL;
    }
#endif

    aicos_mutex_give(g_vin_test_ctx.media_dev_mutex);
    aicos_mutex_delete(g_vin_test_ctx.media_dev_mutex);
    g_vin_test_ctx.media_dev_mutex = NULL;
}

static int vin_dev_ch_init(struct vin_test_ctx *ctx, u32 ch)
{
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    int ret;

    ret = mpp_vin2_vb_init(ch, vin_ctx);
    if (ret < 0) {
        pr_err("[%s%d] Failed to initialize VB for channel %d\n",
               VIN_NAME(vin_ctx), ch);
        return -1;
    }

    ctx->binfo[ch].num_buffers = ctx->num_buffers;
    ret = vin_request_buf(&ctx->binfo[ch], ch, vin_ctx);
    if (ret < 0) {
        pr_err("[%s%d] Failed to request buffers\n", VIN_NAME(vin_ctx), ch);
        return -1;
    }

    ctx->ref_cnt++;
    return 0;
}

static int vin_dev_init(struct vin_test_ctx *ctx)
{
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    u32 i, ch = ctx->channel;
    int ret = 0;

    if (vin_ctx->state == VIN_STATE_INIT) {
        ret = mpp_vin2_init(vin_ctx);
        if (ret < 0) {
            pr_err("[%s%d] Failed to initialize VIN device\n",
                   VIN_NAME(vin_ctx), ch);
            return -1;
        }

        if (camera_get_fmt(vin_ctx) < 0)
            goto error;

        camera_try_set_fmt(vin_ctx, &ctx->sensor_fmt);

        if (vin_subdev_set_fmt(vin_ctx) < 0)
            goto error;

        if (vin_dev_cfg(ctx) < 0)
            goto error;
    }

    ctx->num_buffers = VID_BUF_NUM;
    if (vin_dev_ch_init(ctx, ch) < 0)
        goto error;

    if (vin_ctx->dst_fmt.stitch_mode != MPP_STITCH_INVALID) {
        for (i = 0; i < VIN_MAX_CHANNELS; i++) {
            if (i == ch)
                continue;
            if (vin_dev_ch_init(ctx, i) < 0)
                goto error;
        }
    }
    vin_ctx->state = VIN_STATE_READY;
    return 0;

error:
    mpp_vin2_deinit(vin_ctx);
    return -1;
}

static void vin_dev_deinit(u32 ch, struct vin_test_ctx *ctx)
{
    mpp_vin2_vb_deinit(ch, &ctx->ctx);

    if (ctx->ref_cnt > 0)
        ctx->ref_cnt--;
    if (ctx->ref_cnt == 0)
        mpp_vin2_deinit(&ctx->ctx);
}

static void vin_show_frame_fps(struct vin_dev_ctx *vin_ctx, u32 ch, u32 total,
                               u32 interval, struct timespec *begin)
{
    struct timespec now;
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "[%s%d] %6d", VIN_NAME(vin_ctx), ch, total);
    gettimespec(&now);
    show_fps(tmp, begin, &now, interval);
    gettimespec(begin);
}

static void vin_test_thread(void *arg)
{
    struct vin_test_ctx *ctx = &g_vin_test_ctx;
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    struct timespec begin;
    u32 buf_index, frame = 0;
    u32 ch = (u32)arg;
    int ret;

#ifdef RT_USING_PM
    rt_pm_module_request(PM_VIN_ID, PM_SLEEP_MODE_NONE);
#endif

    for (u32 i = 0; i < ctx->num_buffers; i++) {
        ret = vin_queue_buf(i, ch, vin_ctx);
        if (ret < 0) {
            pr_err("[%s%d] Failed to queue buffer %d\n",
                   VIN_NAME(vin_ctx), ch, i);
            goto exit;
        }
    }

    ret = vin_start(ch, vin_ctx);
    if (ret < 0) {
        pr_err("[%s%d] Failed to start streaming\n", VIN_NAME(vin_ctx), ch);
        goto exit;
    }

#ifdef AIC_DISPLAY_DRV
    if (ch == ctx->show_channel) {
        if (vin_output_region_cfg(ctx) < 0)
            goto exit;
    }

    if (ch == ctx->show_channel) {
        if (vin_vlayer_init(ctx) < 0)
            goto exit;
    }
#endif

    vin_show_fmt(&ctx->ctx);
    vin_show_size(ctx);

    vin_ctx->state = VIN_STATE_STREAMING;
    ctx->running[ch] = true;

#ifdef AIC_DVP_NO_SIGNAL_PATTERN
    vin_vlayer_update_buf(ctx, ch, ctx->num_buffers - 1);
#endif

    pr_info("[%s%d] Start streaming\n", VIN_NAME(vin_ctx), ch);
    gettimespec(&begin);
    while (ctx->running[ch] && (ctx->frame_count == 0 || frame < ctx->frame_count)) {
        if (vin_ctx->state == VIN_STATE_PAUSED) {
            aicos_msleep(100);
            continue;
        }

        /* Check pending display mode switch */
        if (ctx->pending_display_mode != VIN_DISP_MODE_NONE) {
            u32 new_mode = ctx->pending_display_mode;

            if (ctx->display_mode != new_mode) {
                printf("Switch display: mode %d -> mode %d\n",
                        ctx->display_mode, new_mode);
                ctx->display_mode = new_mode;
#ifdef AIC_DISPLAY_DRV
                vin_vlayer_reconfigure(ctx, new_mode);
#endif
            }
            ctx->pending_display_mode = VIN_DISP_MODE_NONE;
        }

        ret = vin_dequeue_buf((int *)&buf_index, ch, vin_ctx);
        if (ret < 0) {
            pr_err("[%s%d] Failed to dequeue buffer\n", VIN_NAME(vin_ctx), ch);
            break;
        }

        frame++;
        pr_debug("[%s%d] Frame %d/%d, buffer %d\n", VIN_NAME(vin_ctx), ch,
                 frame, ctx->frame_count, buf_index);

#ifdef AIC_DISPLAY_DRV
#ifdef SUPPORT_ROTATION_BY_VE
        if (ctx->rotation && ch == ctx->show_channel) {
            if (vin_ve_rotation_process(ctx, ch, buf_index) < 0) {
                pr_err("[%s%d] VE rotation failed for buf %d\n",
                       VIN_NAME(vin_ctx), ch, buf_index);
                break;
            }
            /* buf_index already queued inside vin_ve_rotation_process */
        } else
#endif
        if (MPP_IS_STITCH(vin_ctx->dst_fmt.stitch_mode) || (ch == ctx->show_channel)) {
            if (vin_vlayer_update_buf(ctx, ch, buf_index) < 0) {
                pr_err("[%s%d] Failed to set video layer for buf %d\n",
                       VIN_NAME(vin_ctx), ch, buf_index);
                break;
            }
        }

        if (!(ctx->rotation && ch == ctx->show_channel))
#endif
        {
            ret = vin_queue_buf(buf_index, ch, vin_ctx);
            if (ret < 0) {
                pr_err("[%s%d] Failed to queue buf %d\n",
                       VIN_NAME(vin_ctx), ch, buf_index);
                break;
            }
        }

        if (frame && (frame % 1000 == 0))
            vin_show_frame_fps(vin_ctx, ch, frame, 1000, &begin);
    }

    if (frame % 1000)
        vin_show_frame_fps(vin_ctx, ch, frame, frame % 1000, &begin);

exit:
    /* Set exit code: 0 if frame_count reached (or infinite mode), -1 on error */
    ctx->exit_code[ch] = (ctx->frame_count == 0 || frame >= ctx->frame_count) ? 0 : -1;

    vin_ctx->state = VIN_STATE_INIT;
    ctx->running[ch] = false;

    vin_stop(ch, vin_ctx);
    vin_dev_deinit(ch, ctx);
#ifdef SUPPORT_ROTATION_BY_VE
    if (ctx->rotation)
        vin_ve_rotation_deinit(ctx);
#endif
    media_dev_deinit();
    vin_fb_pp_buf_free(ctx);

#ifdef RT_USING_PM
    rt_pm_module_release(PM_VIN_ID, PM_SLEEP_MODE_NONE);
#endif

    pr_info("[%s%d] Stopped streaming, captured %d frames\n",
            VIN_NAME(vin_ctx), ch, frame);

    /* Must be last: signals vin_wait_threads() that all cleanup is done */
    ctx->finished[ch] = true;
}

static void vin_ctx_init(struct vin_test_ctx *ctx,
                         enum vin_dev_type type,
                         u32 ch, u32 frame_count,
                         enum mpp_stitch_mode stitch_mode, u32 dst_fmt)
{
    ctx->ctx.type = type;
    strncpy(ctx->ctx.camera, "camera", sizeof(ctx->ctx.camera) - 1);
    ctx->channel = ch;
    ctx->frame_count = frame_count;
    ctx->num_buffers = VID_BUF_NUM;
    ctx->ctx.dst_fmt.stitch_mode = stitch_mode;
    ctx->ctx.dst_fmt.pixelformat = dst_fmt;
}

static int vin_test_single_channel(struct vin_test_ctx *ctx, enum vin_dev_type type,
                                   u32 ch, u32 frame_count, u32 dst_fmt)
{
    vin_ctx_init(ctx, type, ch, frame_count, MPP_STITCH_INVALID, dst_fmt);
    ctx->display_mode = ch;
    ctx->pending_display_mode = VIN_DISP_MODE_NONE;
    if (vin_dev_init(ctx) < 0)
        return -1;

#ifdef SUPPORT_ROTATION_BY_VE
    if (ctx->rotation) {
        u32 src_h = ctx->ctx.src_fmt.height;

        if (aic_dvp_sfield_mode())
            src_h /= 2;  /* use top field only */

        if (vin_ve_rotation_init(ctx, ctx->ctx.src_fmt.width, src_h,
                                 ctx->rotation) < 0) {
            pr_err("Failed to init VE rotation\n");
            vin_dev_deinit(ch, ctx);
            media_dev_deinit();
            return -1;
        }
    }
#endif

    ctx->thread[ch] = aicos_thread_create("t_vin", 4096, 0, vin_test_thread, (void *)ch);
    if (!ctx->thread[ch]) {
        pr_err("[%s%d] Failed to create thread\n", VIN_NAME(&ctx->ctx), ch);
        vin_dev_deinit(ch, ctx);
        media_dev_deinit();
        return -1;
    }

    return 0;
}

static int vin_test_multi_channel(struct vin_test_ctx *ctx, enum vin_dev_type type,
                                  u32 frame_count, u32 dst_fmt,
                                  enum mpp_stitch_mode stitch_mode)
{
    char thread_name[16] = "";
    int i;

    /* Clear the channel NO. in stitch mode */
    vin_ctx_init(ctx, type, 0, frame_count, stitch_mode, dst_fmt);
    ctx->display_mode = VIN_DISP_MODE_STITCH;
    ctx->pending_display_mode = VIN_DISP_MODE_NONE;
    if (vin_dev_init(ctx) < 0)
        return -1;

    if (MPP_IS_STITCH(stitch_mode) && vin_fb_pp_buf_init(ctx, stitch_mode) < 0) {
        vin_dev_deinit(0, ctx);
        return -1;
    }

    for (i = 0; i < VIN_MAX_CHANNELS; i++) {
        snprintf(thread_name, sizeof(thread_name), "t_vin%d", i);
        ctx->thread[i] = aicos_thread_create(thread_name, 4096, 0, vin_test_thread, (void *)i);
        if (!ctx->thread[i]) {
            pr_err("Failed to create thread for channel %d\n", i);
            vin_dev_deinit(i, ctx);
            media_dev_deinit();
            vin_fb_pp_buf_free(ctx);
            return -1;
        }
    }

    return 0;
}

/*
 * Wait for all active VIN threads to finish, then return aggregate exit code.
 * Returns 0 if all channels succeeded, -1 if any channel failed.
 */
static int vin_wait_threads(void)
{
    struct vin_test_ctx *ctx = &g_vin_test_ctx;
    int i, ret = 0;

    /* Phase 1: wait for all created threads to actually start running */
    while (1) {
        bool all_started = true;

        for (i = 0; i < VIN_MAX_CHANNELS; i++) {
            if (ctx->thread[i] && !ctx->running[i]) {
                all_started = false;
                break;
            }
        }
        if (all_started)
            break;
        aicos_msleep(50);
    }

    /* Phase 2: poll until all threads have fully exited (cleanup complete) */
    while (1) {
        bool any_active = false;

        for (i = 0; i < VIN_MAX_CHANNELS; i++) {
            if (ctx->thread[i] && !ctx->finished[i]) {
                any_active = true;
                break;
            }
        }
        if (!any_active)
            break;
        aicos_msleep(100);
    }

    /* Phase 3: check per-channel exit codes */
    for (i = 0; i < VIN_MAX_CHANNELS; i++) {
        if (ctx->thread[i] && ctx->exit_code[i] != 0) {
            pr_err("[ch%d] Failed with exit code %d\n", i, ctx->exit_code[i]);
            ret = -1;
        }
        ctx->thread[i] = NULL;
    }

    return ret;
}

static int vin_play_ctrl(int argc, char **argv)
{
    struct vin_test_ctx *ctx = &g_vin_test_ctx;
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    char *action = argv[1];
    int i;

    if (strncasecmp(action, "r", 1) == 0) {
        if (vin_ctx->state == VIN_STATE_STREAMING) {
            pr_info("VIN is already playing\n");
            return 0;
        }
        if (vin_ctx->state != VIN_STATE_PAUSED) {
            pr_err("Invalid state: %d\n", vin_ctx->state);
            return -1;
        }
        for (i = 0; i < VIN_MAX_CHANNELS; i++) {
            if (ctx->running[i])
                mpp_vin2_ioctl(VIN_STREAM_RESUME, NULL, i, vin_ctx);
        }
        vin_ctx->state = VIN_STATE_STREAMING;
        pr_info("VIN resumed\n");
        return 0;
    }

    if (strncasecmp(action, "p", 1) == 0) {
        if (vin_ctx->state == VIN_STATE_PAUSED) {
            pr_info("VIN is already paused\n");
            return 0;
        }
        if (vin_ctx->state != VIN_STATE_STREAMING) {
            pr_err("Invalid state: %d\n", vin_ctx->state);
            return -1;
        }
        for (i = 0; i < VIN_MAX_CHANNELS; i++) {
            if (ctx->running[i])
                mpp_vin2_ioctl(VIN_STREAM_PAUSE, NULL, i, vin_ctx);
        }
        vin_ctx->state = VIN_STATE_PAUSED;
        pr_info("VIN paused\n");
        return 0;
    }

    if (strncasecmp(action, "s", 1) == 0) {
        if (vin_ctx->state != VIN_STATE_STREAMING &&
            vin_ctx->state != VIN_STATE_PAUSED) {
            pr_err("Invalid state: %d\n", vin_ctx->state);
            return -1;
        }
        for (i = 0; i < VIN_MAX_CHANNELS; i++)
            ctx->running[i] = false;
        pr_info("VIN stopping\n");
        return 0;
    }

    if (strncasecmp(action, "c", 1) == 0) {
        u32 mode;

        if (!MPP_IS_STITCH(vin_ctx->dst_fmt.stitch_mode)) {
            pr_err("Only can switch channel in stitch mode\n");
            return -1;
        }
        if (argc < 3) {
            printf("Usage: test_vin chan <0|1|2>\n");
            printf("  0 = CH0 only\n");
            printf("  1 = CH1 only\n");
            printf("  2 = stitch (both channels)\n");
            return -1;
        }
        mode = atoi(argv[2]);
        if (mode > VIN_DISP_MODE_STITCH) {
            pr_err("Invalid channel mode: %d (0-2)\n", mode);
            return -1;
        }
        ctx->pending_display_mode = mode;
        pr_debug("Pending display mode switch to %d\n", mode);
        return 0;
    }

    printf("Invalid action: %s\n", action);
    return -1;
}

struct vin_cmd_args {
    enum mpp_stitch_mode stitch_mode;
    enum vin_dev_type dev_type;
    u32 dst_fmt;
    u32 frame_cnt;
    u32 ch;
    bool wait;
};

static int test_vin_parse_args(int argc, char **argv, struct vin_cmd_args *args)
{
    static const char sopts[] = "d:C:f:c:s:S:w:h:r:Wa:u";
    static const struct option lopts[] = {
        {"device",    required_argument, NULL, 'd'},
        {"channel",   required_argument, NULL, 'C'},
        {"format",    required_argument, NULL, 'f'},
        {"count",     required_argument, NULL, 'c'},
        {"stitch",    required_argument, NULL, 's'},
        {"show",      required_argument, NULL, 'S'},
        {"width",     required_argument, NULL, 'w'},
        {"height",    required_argument, NULL, 'h'},
        {"framerate", required_argument, NULL, 'r'},
        {"wait",      no_argument,       NULL, 'W'},
        {"angle",     required_argument, NULL, 'a'},
        {"help",      no_argument,       NULL, 'u'},
        {0, 0, 0, 0}
    };
    int ret, c;

    optind = 0;
    memset(&g_vin_test_ctx, 0, sizeof(g_vin_test_ctx));

    while ((c = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
        switch (c) {
        case 'd':
            if (strncasecmp("dvp", optarg, 3) == 0) {
                args->dev_type = VIN_DEV_DVP;
            } else if (strncasecmp("csi", optarg, 3) == 0) {
                args->dev_type = VIN_DEV_CSI;
            } else {
                pr_err("Invalid device: %s\n", optarg);
                return -1;
            }
            break;

        case 'c':
            args->frame_cnt = atoi(optarg);
            break;

        case 'C':
            args->ch = atoi(optarg);
            if (args->ch >= VIN_MAX_CHANNELS) {
                pr_err("Invalid channel: %d\n", args->ch);
                return -1;
            }
            g_vin_test_ctx.show_channel = args->ch;
            break;

        case 'f':
            ret = parse_format(optarg);
            if (ret < 0) {
                pr_err("Invalid format: %s\n", optarg);
                return -1;
            }
            args->dst_fmt = ret;
            break;

        case 's':
            args->stitch_mode = parse_stitch_mode(optarg);
            if (args->stitch_mode < 0) {
                pr_err("Invalid stitch mode: %s\n", optarg);
                return -1;
            }
            if (MPP_IS_STITCH(args->stitch_mode)) {
                aic_dvp_ignore_loss();
#if !DE_SCALE_ENABLE
                pr_err("Stitch mode requires DE_SCALE_ENABLE\n");
                return -1;
#endif
            }
            break;

        case 'S':
            g_vin_test_ctx.show_channel = atoi(optarg);
            if (g_vin_test_ctx.show_channel >= VIN_MAX_CHANNELS) {
                pr_err("Invalid show channel: %d\n", g_vin_test_ctx.show_channel);
                return -1;
            }
            break;

        case 'w':
            g_vin_test_ctx.sensor_fmt.width = atoi(optarg);
            if (!g_vin_test_ctx.sensor_fmt.width) {
                pr_err("Invalid width: %s\n", optarg);
                return -1;
            }
            break;

        case 'h':
            g_vin_test_ctx.sensor_fmt.height = atoi(optarg);
            if (!g_vin_test_ctx.sensor_fmt.height) {
                pr_err("Invalid height: %s\n", optarg);
                return -1;
            }
            break;

        case 'r':
            g_vin_test_ctx.sensor_fmt.fps = atoi(optarg);
            if (!g_vin_test_ctx.sensor_fmt.fps) {
                pr_err("Invalid framerate: %s\n", optarg);
                return -1;
            }
            break;

        case 'W':
            args->wait = true;
            break;

#ifdef SUPPORT_ROTATION_BY_VE
        case 'a':
            g_vin_test_ctx.rotation = (atoi(optarg) % 360) / 90;
            aic_dvp_ignore_loss();
            break;
#endif

        case 'u':
        default:
            usage(argv[0]);
            return 1; /* positive = help shown, not an error */
        }
    }
    return 0;
}

static int test_vin_execute(struct vin_cmd_args *args)
{
    int ret;

#if VIN_CROP_ENABLE
    g_vin_test_ctx.vin_crop = true;
#endif

    ret = media_dev_init();
    if (ret < 0)
        return -1;

    if (vin_get_fb_info() < 0)
        goto error_out;

    if (vin_uilayer_set_alpha(0) < 0)
        goto error_out;

#ifdef SUPPORT_ROTATION_BY_VE
    if (g_vin_test_ctx.rotation && MPP_IS_STITCH(args->stitch_mode)) {
        pr_err("VE rotation is not supported in stitch mode\n");
        goto error_out;
    }
#endif

    if (args->stitch_mode == MPP_STITCH_INVALID)
        ret = vin_test_single_channel(&g_vin_test_ctx, args->dev_type,
                                       args->ch, args->frame_cnt, args->dst_fmt);
    else
        ret = vin_test_multi_channel(&g_vin_test_ctx, args->dev_type,
                                      args->frame_cnt, args->dst_fmt,
                                      args->stitch_mode);

    if (ret == 0 && args->wait)
        ret = vin_wait_threads();

    return ret;

error_out:
    media_dev_deinit();
    return ret;
}

static int cmd_test_vin(int argc, char **argv)
{
    struct vin_cmd_args args = {
        .stitch_mode = MPP_STITCH_INVALID,
        .dev_type    = VIN_DEV_DVP,
        .dst_fmt     = MPP_FMT_NV16,
        .frame_cnt   = 10,
        .ch          = 0,
        .wait        = false,
    };
    int ret;

    if (g_vin_test_ctx.ctx.state == VIN_STATE_STREAMING ||
        g_vin_test_ctx.ctx.state == VIN_STATE_PAUSED) {
        if (argc < 2) {
            printf("Usage:\n\t%s [pause/resume/stop/chan]: \n\n", argv[0]);
            return -1;
        }
        return vin_play_ctrl(argc, argv);
    }

    ret = test_vin_parse_args(argc, argv, &args);
    if (ret != 0)
        return ret > 0 ? 0 : -1; /* positive = help shown */

    return test_vin_execute(&args);
}

MSH_CMD_EXPORT_ALIAS(cmd_test_vin, test_vin, Test Video In);

struct vin_stress_param {
    u32 count;
    u32 min_sec;
    u32 max_sec;
};

static void vin_stress_thread(void *arg)
{
    struct vin_stress_param *p = (struct vin_stress_param *)arg;
    static const u32 modes[] = {0, 1, VIN_DISP_MODE_STITCH};
    u32 i;

    srand((u32)gettimespec);
    printf("Stress test: %d switches, interval %d-%ds\n",
           p->count, p->min_sec, p->max_sec);

    for (i = 0; i < p->count; i++) {
        u32 mode = modes[rand() % 3];
        u32 interval = p->min_sec + rand() % (p->max_sec - p->min_sec + 1);

        printf("[%3d/%d] -> mode %d, wait %ds\n", i + 1, p->count, mode, interval);
        g_vin_test_ctx.pending_display_mode = mode;
        aicos_msleep(interval * 1000);
    }

    /* Restore stitch mode */
    g_vin_test_ctx.pending_display_mode = VIN_DISP_MODE_STITCH;
    printf("Stress test done, restored to stitch mode\n");
    aicos_free(MEM_DEFAULT, p);
}

static int cmd_test_vin_stress(int argc, char **argv)
{
    struct vin_stress_param *p;
    aicos_thread_t thid;

    if (!MPP_IS_STITCH(g_vin_test_ctx.ctx.dst_fmt.stitch_mode)) {
        pr_err("Stress test only available in stitch mode\n");
        return -1;
    }
    if (g_vin_test_ctx.ctx.state != VIN_STATE_STREAMING) {
        pr_err("VIN is not streaming\n");
        return -1;
    }

    p = aicos_malloc(MEM_DEFAULT, sizeof(*p));
    if (!p) {
        pr_err("Failed to alloc stress param\n");
        return -1;
    }
    p->count = 20;
    p->min_sec = 2;
    p->max_sec = 10;
    if (argc >= 2)
        p->count = atoi(argv[1]);
    if (argc >= 4) {
        p->min_sec = atoi(argv[2]);
        p->max_sec = atoi(argv[3]);
    }
    if (p->min_sec >= p->max_sec || !p->count) {
        printf("Usage: test_vin_stress [count [min_sec max_sec]]\n");
        printf("  Default: 20 switches, 2-10s random interval\n");
        aicos_free(MEM_DEFAULT, p);
        return -1;
    }

    thid = aicos_thread_create("vin_stress", 2048, 20,
                                vin_stress_thread, p);
    if (!thid) {
        pr_err("Failed to create stress thread\n");
        aicos_free(MEM_DEFAULT, p);
        return -1;
    }

    printf("Stress test started in background\n");
    return 0;
}

MSH_CMD_EXPORT_ALIAS(cmd_test_vin_stress, test_vin_stress, VIN stitch display stress test);
