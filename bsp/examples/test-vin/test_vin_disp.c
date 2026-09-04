/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#include "test_vin.h"

/* ---- FB info / alpha ---- */

int vin_get_fb_info(void)
{
    int ret = 0;

#ifdef AIC_DISPLAY_DRV
    ret = mpp_fb_ioctl(g_fb, AICFB_GET_SCREENINFO, &g_fb_info);
    if (ret < 0)
        pr_err("Failed to get screen info! errno: -%d\n", -ret);
#endif
    return ret;
}

int vin_uilayer_set_alpha(int val)
{
    int ret = 0;
#ifdef AIC_DISPLAY_DRV
    struct aicfb_alpha_config alpha = {0};

    alpha.layer_id = AICFB_LAYER_TYPE_UI;
    alpha.enable = 1;
    alpha.mode = 1;
    alpha.value = val;

    ret = mpp_fb_ioctl(g_fb, AICFB_UPDATE_ALPHA_CONFIG, &alpha);

    if (ret < 0)
        pr_err("Failed to update alpha! errno: -%d\n", -ret);
#endif
    return ret;
}

/* ---- PP (Ping-Pong) buffer management ---- */

void vin_fb_pp_buf_free(struct vin_test_ctx *ctx)
{
    int i;

    if (vin_task_is_running())
        return;

    for (i = 0; i < FB_PP_BUF_NUM; i++) {
        if (ctx->pp_buf[i]) {
            aicos_free(MEM_CMA, ctx->pp_buf[i]);
            ctx->pp_buf[i] = NULL;
        }
    }
}

int vin_fb_pp_buf_init(struct vin_test_ctx *ctx, enum mpp_stitch_mode stitch_mode)
{
    struct vin_video_fmt *dst_fmt = &ctx->ctx.dst_fmt;
    u32 buf_size = dst_fmt->width * dst_fmt->height;
    int i;

    if (dst_fmt->pixelformat == MPP_FMT_NV12)
        buf_size = buf_size * 3 / 2;
    else if (dst_fmt->pixelformat == MPP_FMT_NV16)
        buf_size *= 2;

    if (MPP_IS_STITCH(stitch_mode))
        buf_size *= 2;

    if (aic_dvp_sfield_mode())
        buf_size /= 2;

    for (i = 0; i < FB_PP_BUF_NUM; i++) {
        if (ctx->pp_buf[i])
            continue;

        ctx->pp_buf[i] = aicos_malloc(MEM_CMA, buf_size);
        if (!ctx->pp_buf[i]) {
            pr_err("Failed to allocate %d for PingPang buffer!\n", buf_size);
            vin_fb_pp_buf_free(ctx);
            return -1;
        }
        pr_info("PingPang buffer %d: 0x%08x, size %d\n", i, ctx->pp_buf[i], buf_size);
    }
    return 0;
}

/* ---- Format / size display ---- */

void vin_show_fmt(struct vin_dev_ctx *vin_ctx)
{
    struct mpp_video_fmt *src_fmt = &vin_ctx->src_fmt;
    struct vin_video_fmt *dst_fmt = &vin_ctx->dst_fmt;

    printf("\nThe stream format:\n");
    printf("\t[Camera] %s (0x%x - 0x%x)\n"
           "\t\t└─> [VIN] %s (0x%x)\n"
           "\t\t\t└─>[Panel] (0x%x)\n",
           vin_ctx->camera, src_fmt->code, src_fmt->bus_type,
           VIN_NAME(vin_ctx), dst_fmt->pixelformat, dst_fmt->pixelformat);
}

void vin_show_size(struct vin_test_ctx *ctx)
{
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    struct mpp_video_fmt *src_fmt = &vin_ctx->src_fmt;
    struct vin_video_fmt *dst_fmt = &vin_ctx->dst_fmt;
#ifdef AIC_DISPLAY_DRV
    struct aicfb_layer_data *layer = &ctx->cached_layer;
#endif

    printf("The stream size:\n");
    printf("\t[Camera] %s %d x %d\n"
           "\t\t└─> [VIN] %s %d x %d (Crop: [%d, %d] %d x %d)\n",
           vin_ctx->camera, src_fmt->width, src_fmt->height,
           VIN_NAME(vin_ctx), dst_fmt->width, dst_fmt->height,
           dst_fmt->crop_x, dst_fmt->crop_y, dst_fmt->width, dst_fmt->height);

#ifdef AIC_DISPLAY_DRV
#if DE_SCALE_ENABLE
    printf("\t\t\t└─>[Panel] %d x %d (Pos: [%d, %d] Scale: %d x %d)\n\n",
           g_fb_info.width, g_fb_info.height,
           layer->pos.x, layer->pos.y,
           layer->scale_size.width, layer->scale_size.height);
#elif DE_CROP_ENABLE
    printf("\t\t\t└─>[Panel] %d x %d (Pos: [%d, %d] Crop: %d x %d)\n\n",
           g_fb_info.width, g_fb_info.height,
           layer->buf.crop.x, layer->buf.crop.y,
           layer->buf.crop.width, layer->buf.crop.height);
#else
    printf("\t\t\t└─>[Panel] %d x %d (Pos: [%d, %d] %d x %d)\n\n",
           g_fb_info.width, g_fb_info.height,
           layer->pos.x, layer->pos.y,
           layer->buf.size.width, layer->buf.size.height);
#endif
#endif


}

/* ---- Display layer functions (only with AIC_DISPLAY_DRV) ---- */

#ifdef AIC_DISPLAY_DRV

int vin_vlayer_disable(void)
{
    int ret = 0;
    struct aicfb_layer_data layer = {0};
    layer.enable = 0;
    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;

    ret = mpp_fb_ioctl(g_fb, AICFB_UPDATE_LAYER_CONFIG, &layer);

    if (ret < 0)
        pr_err("Failed to disable video layer!\n");

    return ret;
}

static int vin_set_output_pos(struct vin_test_ctx *ctx,
                              u32 x, u32 y, u32 width, u32 height)
{
    if (!width || !height || x > g_fb_info.width || y > g_fb_info.height) {
        pr_err("[ch%d] Invalid position: [%d, %d] %d x %d\n", ctx->show_channel,
               x, y, width, height);
        return -1;
    }

    ctx->dst_pos.x = x;
    ctx->dst_pos.y = y;
    ctx->dst_pos.width = width;
    ctx->dst_pos.height = height;
    return 0;
}

int vin_output_region_cfg(struct vin_test_ctx *ctx)
{
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;

#if DE_SCALE_ENABLE
    pr_info("[%s] DE scale is enable\n", VIN_NAME(vin_ctx));
    if (vin_set_output_pos(ctx, VID_SCALE_OFFSET, VID_SCALE_OFFSET,
                           g_fb_info.width - VID_SCALE_OFFSET * 2,
                           g_fb_info.height - VID_SCALE_OFFSET * 2))
        return -1;
#elif VIN_CROP_ENABLE
    pr_info("[%s] VIN crop is enable\n", VIN_NAME(vin_ctx));
    if (vin_set_output_pos(ctx, 0, 0,
                           min(g_fb_info.width, vin_ctx->dst_fmt.width),
                           min(g_fb_info.height, vin_ctx->dst_fmt.height)))
        return -1;
#else
    if (vin_set_output_pos(ctx, 0, 0, vin_ctx->dst_fmt.width, vin_ctx->dst_fmt.height))
        return -1;
#endif

#if DE_CROP_ENABLE
    pr_info("[%s] DE crop is enable\n", VIN_NAME(vin_ctx));
#endif
    return 0;
}

/*
 * Pre-compute static layer configuration (position, scale, crop, stride, etc.).
 * Call once before the streaming loop; the result is cached in ctx->cached_layer.
 */
int vin_vlayer_init(struct vin_test_ctx *ctx)
{
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    struct vin_video_fmt *dst_fmt = &vin_ctx->dst_fmt;
    struct aicfb_layer_data *layer = &ctx->cached_layer;

    memset(layer, 0, sizeof(*layer));
    layer->layer_id = AICFB_LAYER_TYPE_VIDEO;
    layer->enable = 1;

    /* Dst image */
    layer->pos.x = ctx->dst_pos.x;
    layer->pos.y = ctx->dst_pos.y;

#if DE_SCALE_ENABLE
#if SCALE_MODE_FULLSCREEN
    /* Fullscreen scaling: stretch to fill entire dst_pos, ignore aspect ratio */
    layer->scale_size.width = ALIGN_DOWN(ctx->dst_pos.width, 2);
    layer->scale_size.height = ALIGN_DOWN(ctx->dst_pos.height, 2);
#else
    u32 stitch_w, stitch_h;
    float ratio_w, ratio_h, scale;

    if (dst_fmt->stitch_mode == MPP_STITCH_V_MODE) {
        stitch_w = dst_fmt->width;
        stitch_h = dst_fmt->height * 2;
    } else if (dst_fmt->stitch_mode == MPP_STITCH_H_MODE) {
        stitch_w = dst_fmt->width * 2;
        stitch_h = dst_fmt->height;
    } else {
        stitch_w = dst_fmt->width;
        stitch_h = dst_fmt->height;
    }

#ifdef SUPPORT_ROTATION_BY_VE
    /* VE rotation swaps width/height for 90/270 degrees */
    if (ctx->rotation == MPP_ROTATION_90 ||
        ctx->rotation == MPP_ROTATION_270) {
        u32 tmp = stitch_w;
        stitch_w = stitch_h;
        stitch_h = tmp;
    }
#endif

    ratio_w = (float)ctx->dst_pos.width / stitch_w;
    ratio_h = (float)ctx->dst_pos.height / stitch_h;

    /* Proportional scaling: fit source into dst_pos while preserving aspect ratio */
    scale = (ratio_w < ratio_h) ? ratio_w : ratio_h;

    layer->scale_size.width = ALIGN_DOWN((u32)(stitch_w * scale), 2);
    layer->scale_size.height = ALIGN_DOWN((u32)(stitch_h * scale), 2);
    /* Center within dst_pos */
    layer->pos.x = ctx->dst_pos.x + (ctx->dst_pos.width - layer->scale_size.width) / 2;
    layer->pos.y = ctx->dst_pos.y + (ctx->dst_pos.height - layer->scale_size.height) / 2;
#endif
#else
    layer->scale_size.width = ctx->dst_pos.width;
    layer->scale_size.height = ctx->dst_pos.height;
    /* Be center-aligned if screen size is bigger than VIN output */
    if (g_fb_info.width > ctx->dst_pos.width)
        layer->pos.x = (g_fb_info.width - ctx->dst_pos.width) / 2;
    if (g_fb_info.height > ctx->dst_pos.height)
        layer->pos.y = (g_fb_info.height - ctx->dst_pos.height) / 2;
#endif

#if DE_CROP_ENABLE
    layer->buf.crop_en = 1;
    layer->buf.crop.x = 0;
    layer->buf.crop.y = 0;
    layer->buf.crop.width = min(dst_fmt->width, g_fb_info.width);
    layer->buf.crop.height = min(dst_fmt->height, g_fb_info.height);
#endif

    /* Src image */
#ifdef SUPPORT_ROTATION_BY_VE
    if (ctx->rotation) {
        /* VE rotation: output is NV12 with rotated dimensions */
        u32 dst_h = aic_dvp_sfield_mode() ? dst_fmt->height / 2 : dst_fmt->height;

        if (ctx->rotation == MPP_ROTATION_0 ||
            ctx->rotation == MPP_ROTATION_180) {
            layer->buf.size.width = dst_fmt->width;
            layer->buf.size.height = dst_h;
        } else {
            /* 90/270: width and height are swapped */
            layer->buf.size.width = dst_h;
            layer->buf.size.height = dst_fmt->width;
        }
        layer->buf.format = MPP_FMT_NV12;
    } else
#endif
    {
    if (ctx->rotation == MPP_ROTATION_0 || ctx->rotation == MPP_ROTATION_180) {
        layer->buf.size.width = dst_fmt->width;
        if (aic_dvp_sfield_mode())
            layer->buf.size.height = dst_fmt->height / 2;
        else
            layer->buf.size.height = dst_fmt->height;

        if (dst_fmt->stitch_mode == MPP_STITCH_V_MODE)
            layer->buf.size.height *= 2;
        else if (dst_fmt->stitch_mode == MPP_STITCH_H_MODE)
            layer->buf.size.width *= 2;
    } else {
        if (aic_dvp_sfield_mode())
            layer->buf.size.width = dst_fmt->height / 2;
        else
            layer->buf.size.width = dst_fmt->height;
        layer->buf.size.height = dst_fmt->width;
    }

    layer->buf.format = dst_fmt->pixelformat;
    }

    layer->buf.buf_type = MPP_PHY_ADDR;

    for (int i = 0; i < VIN_MAX_PLANE_NUM; i++)
        layer->buf.stride[i] = layer->buf.size.width;

    return 0;
}

void vin_vlayer_reconfigure(struct vin_test_ctx *ctx, u32 display_mode)
{
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    struct vin_video_fmt *dst_fmt = &vin_ctx->dst_fmt;
    struct aicfb_layer_data *layer = &ctx->cached_layer;
    u32 src_w, src_h;

    if (display_mode == VIN_DISP_MODE_STITCH && MPP_IS_STITCH(dst_fmt->stitch_mode)) {
        if (dst_fmt->stitch_mode == MPP_STITCH_V_MODE) {
            src_w = dst_fmt->width;
            src_h = dst_fmt->height * 2;
        } else {
            src_w = dst_fmt->width * 2;
            src_h = dst_fmt->height;
        }
    } else {
        src_w = dst_fmt->width;
        src_h = dst_fmt->height;
    }

    layer->buf.size.width = src_w;
    layer->buf.size.height = src_h;
    if (aic_dvp_sfield_mode())
        layer->buf.size.height /= 2;

    for (int i = 0; i < VIN_MAX_PLANE_NUM; i++)
        layer->buf.stride[i] = layer->buf.size.width;

#if DE_SCALE_ENABLE
    float ratio_w = (float)ctx->dst_pos.width / src_w;
    float ratio_h = (float)ctx->dst_pos.height / src_h;
    float scale = (ratio_w < ratio_h) ? ratio_w : ratio_h;

    layer->scale_size.width = ALIGN_DOWN((u32)(src_w * scale), 2);
    layer->scale_size.height = ALIGN_DOWN((u32)(src_h * scale), 2);
    layer->pos.x = ctx->dst_pos.x + (ctx->dst_pos.width - layer->scale_size.width) / 2;
    layer->pos.y = ctx->dst_pos.y + (ctx->dst_pos.height - layer->scale_size.height) / 2;
#endif
}

/* ---- DMA memcpy helpers ---- */

static void vin_dma_memcpy_cb(void *param)
{
    if (g_vin_test_ctx.dma_complete)
	    aicos_sem_give(g_vin_test_ctx.dma_complete);
    else
        pr_err("DMA complete sem is unavailable!\n");
}

static int vin_memcpy(char *dst, char *src, u32 size)
{
    struct dma_chan *chan = NULL;
    int ret = 0;

    chan = dma_request_channel();
    if (chan == NULL) {
        pr_err("Alloc dma chan fail!\n ");
        return -1;
    }

    ret = dmaengine_prep_dma_memcpy(chan, (u32)dst, (u32)src, size);
    if (ret) {
        pr_err("dmaengine_prep_dma_memcpy fail! ret = %d\n ", ret);
        goto free_chan;
    }

    ret = dmaengine_submit(chan, vin_dma_memcpy_cb, chan);
    if (ret) {
        pr_err("dmaengine_submit fail! ret = %d\n ", ret);
        goto free_chan;
    }

    dma_async_issue_pending(chan);
    ret = aicos_sem_take(g_vin_test_ctx.dma_complete, 1000);
    if (ret < 0)
        pr_err("DMA timeout!\n");

free_chan:
    dma_release_channel(chan);
    return ret;
}

/* ---- Stitch buffer copy (V / H mode) ---- */

static int vin_memcpy_v_mode(struct vin_test_ctx *ctx, u32 ch, int index)
{
    struct vin_video_plane *plane = &ctx->binfo[ch].planes[index * ctx->binfo[ch].num_planes];
    struct aicfb_layer_data *layer = &ctx->cached_layer;
    char *pp_buf = ctx->pp_buf[ctx->pp_buf_index];
    u32 data_len;
    int i;

    for (i = 0; i < ctx->binfo[ch].num_planes; i++, plane++) {
        if (aic_dvp_sfield_mode())
            pp_buf += plane->len * ch / 2;
        else
            pp_buf += plane->len * ch;

        data_len = plane->len;
        if (aic_dvp_sfield_mode())
            data_len /= 2;

        buf_dbg("Copy: [%d-%d]0x%08x -> [%d]0x%08x(%d), s %d\n",
                ch, index, (u32)plane->buf, ctx->pp_buf_index, (u32)pp_buf,
                pp_buf - ctx->pp_buf[ctx->pp_buf_index], data_len);
        if (vin_memcpy(pp_buf, (char *)plane->buf, data_len) < 0)
            return -1;

        if (ch == 0)
            layer->buf.phy_addr[i] = (u32)pp_buf;

        if (aic_dvp_sfield_mode())
            pp_buf += plane->len * (VIN_MAX_CHANNELS - ch) / 2;
        else
            pp_buf += plane->len * (VIN_MAX_CHANNELS - ch);
    }

    return 0;
}

static int vin_memcpy_h_mode(struct vin_test_ctx *ctx, u32 ch, int index)
{
    struct vin_video_plane *plane = &ctx->binfo[ch].planes[index * ctx->binfo[ch].num_planes];
    struct aicfb_layer_data *layer = &ctx->cached_layer;
    char *pp_buf = ctx->pp_buf[ctx->pp_buf_index];
    u32 width = ctx->ctx.dst_fmt.width;
    u32 stitch_stride = width * 2;
    int i, r;

    for (i = 0; i < ctx->binfo[ch].num_planes; i++, plane++) {
        u32 height = plane->len / width;

        if (i == 0)
            pp_buf += width * ch;

        if (aic_dvp_sfield_mode())
            height /= 2;

        buf_dbg("Copy: [%d-%d]0x%08x -> [%d]0x%08x(%d), s %dx%d\n",
                ch, index, (u32)plane->buf, ctx->pp_buf_index, (u32)pp_buf,
                pp_buf - ctx->pp_buf[ctx->pp_buf_index], width, height);
        for (r = 0; r < height; r++)
            memcpy(pp_buf + stitch_stride * r, (char *)(plane->buf + width * r), width);

        if (ch == 0)
            layer->buf.phy_addr[i] = (u32)pp_buf;

        if (aic_dvp_sfield_mode())
            pp_buf += plane->len * VIN_MAX_CHANNELS / 2;
        else
            pp_buf += plane->len * VIN_MAX_CHANNELS;
    }

    return 0;
}

/* ---- Per-frame buffer update ---- */

static int video_layer_update_pp_buf(struct vin_test_ctx *ctx, u32 ch, int index)
{
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    struct vin_video_fmt *dst_fmt = &vin_ctx->dst_fmt;
    enum mpp_stitch_mode stitch_mode = dst_fmt->stitch_mode;
    int ret = 0;

    /**
    * Buffer layout for stitch mode:
    *
    *    MPP_STITCH_V_MODE              MPP_STITCH_H_MODE
    *    +-----------+                  +-----------+-----------+
    *    | CH0       |                  | CH0       | CH1       | Y
    *    +-----------+ Y                +-----------+-----------+
    *    | CH1       |                  | CH0       | CH1       | UV
    *    +-----------+                  +-----------+-----------+
    *    | CH0       |
    *    +-----------+ UV
    *    | CH1       |
    *    +-----------+
    */

    if (stitch_mode == MPP_STITCH_V_MODE) {
        ret = vin_memcpy_v_mode(ctx, ch, index);
    } else if (stitch_mode == MPP_STITCH_H_MODE) {
        ret = vin_memcpy_h_mode(ctx, ch, index);
    } else {
        pr_err("Stitch mode %d is not supported!\n", stitch_mode);
        return -1;
    }

    if (ret < 0) {
        pr_err("Failed to copy buffer %d-%d\n", ch, index);
        return -1;
    }

    ctx->pp_buf_state |= BIT(ch);
    ret = ctx->pp_buf_state;
    if (ctx->pp_buf_state == FB_PP_BUF_READY) {
#if DEBUG_BUF_ENABLE
        pr_info("[DVP%d]: PP %d is ready\n", ch, ctx->pp_buf_index);
#endif
        ctx->pp_buf_index = (ctx->pp_buf_index + 1) % FB_PP_BUF_NUM;
        ctx->pp_buf_state = 0;
    }

    return ret;
}

/*
 * Per-frame update: only refresh buffer physical addresses and push to FB.
 * Must be called after video_layer_init().
 */
int vin_vlayer_update_buf(struct vin_test_ctx *ctx, u32 ch, int index)
{
    struct vin_video_buf *binfo = &ctx->binfo[ch];
    struct vin_dev_ctx *vin_ctx = &ctx->ctx;
    struct vin_video_fmt *dst_fmt = &vin_ctx->dst_fmt;
    enum mpp_stitch_mode stitch_mode = dst_fmt->stitch_mode;
    struct aicfb_layer_data *layer = &ctx->cached_layer;
    int ret = 0;

    bool use_stitch = MPP_IS_STITCH(stitch_mode)
                      && ctx->display_mode == VIN_DISP_MODE_STITCH;
    if (use_stitch) {
        if (!ctx->pp_buf_mutex) {
            pr_err("PP buf mutex is unavailable!\n");
            return -1;
        }
        aicos_mutex_take(ctx->pp_buf_mutex, AICOS_WAIT_FOREVER);

        /* Need process the stitch buffer */
        ret = video_layer_update_pp_buf(ctx, ch, index);
        if (ret != FB_PP_BUF_READY)
            goto release_mutex;
    } else if (MPP_IS_STITCH(stitch_mode)) {
        /* Stitch running but single-channel display: use VIN buf directly */
        if (ch != ctx->display_mode)
            return 0;
        for (int i = 0; i < VIN_MAX_PLANE_NUM; i++)
            layer->buf.phy_addr[i] = binfo->planes[index * binfo->num_planes + i].buf;
    } else {
        /* Use the vin buffer addresses directly */
        for (int i = 0; i < VIN_MAX_PLANE_NUM; i++) {
            if (MPP_IS_STITCH(dst_fmt->stitch_mode) && aic_dvp_sfield_mode())
                layer->buf.phy_addr[i] = binfo->planes[index * binfo->num_planes + i].buf
                                        + binfo->planes[index * binfo->num_planes + i].len / 2;
            else
                layer->buf.phy_addr[i] = binfo->planes[index * binfo->num_planes + i].buf;
        }
    }

    ret = mpp_fb_ioctl(g_fb, AICFB_UPDATE_LAYER_CONFIG, layer);
    if (ret < 0)
        pr_err("[ch%d] Failed to update layer config!\n", ch);

    if (!use_stitch)
        return ret;

    ret = mpp_fb_ioctl(g_fb, AICFB_WAIT_FOR_VSYNC, NULL);
    if (ret < 0)
        pr_err("[ch%d] Wait for DE sync timeout\n", ch);

release_mutex:
    aicos_mutex_give(ctx->pp_buf_mutex);
    return ret;
}
#endif /* AIC_DISPLAY_DRV */
