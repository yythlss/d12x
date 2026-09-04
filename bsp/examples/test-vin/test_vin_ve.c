/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 *
 * VE-based rotation for test_vin:
 *   VIN YUV -> VE JPEG encode -> VE JPEG decode (with rotation) -> DE display
 */

#include "test_vin.h"

#ifdef SUPPORT_ROTATION_BY_VE

#define VE_JPEG_QUALITY   80

/*
 * Custom frame allocator: directs decoder output to the reserved VIN buffer,
 * eliminating the need for memcpy after decoding.
 */
struct vin_ve_frame_alloc {
    struct frame_allocator allocator;
    u32 out_w;
    u32 out_h;
};

static int vin_ve_alloc_frame(struct frame_allocator *p, struct mpp_frame *frame,
                               int width, int height, enum mpp_pixel_format format)
{
    struct vin_ve_frame_alloc *va = (struct vin_ve_frame_alloc *)p;
    u32 plane_size = ALIGN_UP(va->out_w * va->out_h, 1024);

    frame->buf.stride[0]   = va->out_w;
    frame->buf.stride[1]   = va->out_w;
    frame->buf.size.width  = va->out_w;
    frame->buf.size.height = va->out_h;
    frame->buf.format      = format;
    frame->buf.buf_type    = MPP_PHY_ADDR;

    frame->buf.phy_addr[0] = (unsigned int)(ptr_t)mpp_alloc(plane_size);
    if (!frame->buf.phy_addr[0]) {
        pr_err("VE rotation: failed to alloc %d frame buffer\n", plane_size);
        return -1;
    }

    plane_size /= 2;
    frame->buf.phy_addr[1] = (unsigned int)(ptr_t)mpp_alloc(plane_size);
    if (!frame->buf.phy_addr[1]) {
        pr_err("VE rotation: failed to alloc %d frame buffer\n", plane_size);
        mpp_free((void *)(ptr_t)frame->buf.phy_addr[0]);
        return -1;
    }
    pr_debug("VE alloc frame buffer 0x%08x, 0x%08x\n",
             frame->buf.phy_addr[0], frame->buf.phy_addr[1]);
    return 0;
}

static int vin_ve_free_frame(struct frame_allocator *p, struct mpp_frame *frame)
{
    pr_debug("VE free frame buffer 0x%08x, 0x%08x\n",
             frame->buf.phy_addr[0], frame->buf.phy_addr[1]);
    if (frame->buf.phy_addr[0]) {
        mpp_free((void *)(ptr_t)frame->buf.phy_addr[0]);
        frame->buf.phy_addr[0] = 0;
    }

    if (frame->buf.phy_addr[1]) {
        mpp_free((void *)(ptr_t)frame->buf.phy_addr[1]);
        frame->buf.phy_addr[1] = 0;
    }
    return 0;
}

static int vin_ve_alloc_close(struct frame_allocator *p)
{
    mpp_free(p);
    return 0;
}

static struct alloc_ops vin_ve_alloc_ops = {
    .alloc_frame_buffer = vin_ve_alloc_frame,
    .free_frame_buffer  = vin_ve_free_frame,
    .close_allocator    = vin_ve_alloc_close,
};


/*
 * ve_frame_allocator_init - Create custom frame allocator and bind to decoder.
 */
static int ve_frame_allocator_init(struct vin_test_ctx *ctx,
                                    struct mpp_decoder *dec,
                                    u32 width, u32 height, int rotation)
{
    u32 out_w, out_h;
    struct vin_ve_frame_alloc *va;

    if (rotation == MPP_ROTATION_90 || rotation == MPP_ROTATION_270) {
        out_w = height;
        out_h = width;
    } else {
        out_w = width;
        out_h = height;
    }

    va = (struct vin_ve_frame_alloc *)mpp_alloc(sizeof(*va));
    if (!va) {
        pr_err("VE rotation: failed to alloc frame allocator\n");
        return -1;
    }
    memset(va, 0, sizeof(*va));
    va->allocator.ops = &vin_ve_alloc_ops;
    va->out_w = out_w;
    va->out_h = out_h;
    ctx->ve_allocator = &va->allocator;

    mpp_decoder_control(dec, MPP_DEC_INIT_CMD_SET_EXT_FRAME_ALLOCATOR,
                        ctx->ve_allocator);

    pr_info("VE rotation: %ux%u -> %ux%u, rotation %d\n",
            width, height, out_w, out_h, rotation * 90);
    return 0;
}

/*
 * vin_ve_rotation_init - Create encoder, decoder and allocate JPEG buffer.
 * @ctx:      test context
 * @width:    source image width (before rotation)
 * @height:   source image height (before rotation)
 * @rotation: MPP_ROTATION_0 / 90 / 180 / 270
 */
int vin_ve_rotation_init(struct vin_test_ctx *ctx, u32 width, u32 height, int rotation)
{
    struct encode_config enc_cfg = {0};
    struct decode_config dec_cfg = {0};

    if (!rotation)
        return -1;

    /* 1. Create JPEG encoder */
    ctx->ve_encoder = mpp_encoder_create(MPP_CODEC_VIDEO_ENCODER_MJPEG);
    if (!ctx->ve_encoder) {
        pr_err("VE rotation: failed to create JPEG encoder\n");
        goto err;
    }
    enc_cfg.quality = VE_JPEG_QUALITY;
    if (mpp_encoder_init(ctx->ve_encoder, &enc_cfg)) {
        pr_err("VE rotation: failed to init JPEG encoder\n");
        goto err;
    }

    /* 2. Create JPEG decoder with rotation */
    ctx->ve_decoder = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_MJPEG);
    if (!ctx->ve_decoder) {
        pr_err("VE rotation: failed to create JPEG decoder\n");
        goto err;
    }

    dec_cfg.pix_fmt = MPP_FMT_NV12;
    dec_cfg.bitstream_buffer_size = ALIGN_UP((width * height * 3 / 2), 1024);
    dec_cfg.packet_count = 1;
    dec_cfg.extra_frame_num = 1; // MUST use PingPang buf for DE display

    /* Set custom frame allocator */
    if (ve_frame_allocator_init(ctx, ctx->ve_decoder, width, height, rotation) < 0)
        goto err;

    if (mpp_decoder_control(ctx->ve_decoder,
                            MPP_DEC_INIT_CMD_SET_ROT_FLIP_FLAG,
                            &rotation) < 0) {
        pr_err("VE rotation: failed to set rotation %d\n", rotation);
        goto err;
    }

    if (mpp_decoder_init(ctx->ve_decoder, &dec_cfg) < 0) {
        pr_err("VE rotation: failed to init JPEG decoder\n");
        goto err;
    }

    /* 3. Allocate JPEG intermediate buffer */
    ctx->ve_jpeg_buf_size = width * height * 4 / 5 * VE_JPEG_QUALITY / 100;
    ctx->ve_jpeg_phy_addr = mpp_phy_alloc(ctx->ve_jpeg_buf_size);
    if (!ctx->ve_jpeg_phy_addr) {
        pr_err("VE rotation: failed to alloc JPEG buffer (%d bytes)\n",
               ctx->ve_jpeg_buf_size);
        goto err;
    }

    pr_info("VE rotation: jpeg_buf %d bytes\n", ctx->ve_jpeg_buf_size);

    return 0;

err:
    vin_ve_rotation_deinit(ctx);
    return -1;
}

/*
 * vin_ve_rotation_deinit - Destroy encoder/decoder and free buffers.
 */
void vin_ve_rotation_deinit(struct vin_test_ctx *ctx)
{
    if (ctx->ve_encoder) {
        mpp_encoder_destory(ctx->ve_encoder);
        ctx->ve_encoder = NULL;
    }
    if (ctx->ve_decoder) {
        mpp_decoder_destory(ctx->ve_decoder);
        ctx->ve_decoder = NULL;
    }
    if (ctx->ve_allocator) {
        ctx->ve_allocator->ops->close_allocator(ctx->ve_allocator);
        ctx->ve_allocator = NULL;
    }
    if (ctx->ve_jpeg_phy_addr) {
        mpp_phy_free(ctx->ve_jpeg_phy_addr);
        ctx->ve_jpeg_phy_addr = 0;
    }
    ctx->ve_jpeg_buf_size = 0;
}

/*
 * vin_ve_rotation_process - Encode one VIN frame to JPEG, decode with
 *                           rotation, and push to DE video layer.
 *
 * Buffer lifecycle (ping-pong with 2 reserved VIN buffers):
 *   1. Encoder reads from VIN buffer, then VIN buffer is released (Q_BUF)
 *   2. Decoder writes directly to reserved VIN buffer[active] (via custom allocator)
 *   3. Decoded frame is pushed to DE and returned immediately
 *   4. Next frame uses the other reserved buffer (DE still reading previous one)
 */
int vin_ve_rotation_process(struct vin_test_ctx *ctx, u32 ch, int buf_index)
{
    struct vin_video_buf *binfo = &ctx->binfo[ch];
    struct vin_video_fmt *dst_fmt = &ctx->ctx.dst_fmt;
    struct aicfb_layer_data *layer = &ctx->cached_layer;
    struct mpp_frame frame;
    struct mpp_packet packet;
    int ret;

    /* ---- Step 1: JPEG encode the VIN YUV frame ---- */
    memset(&frame, 0, sizeof(frame));
    frame.buf.phy_addr[0] = binfo->planes[buf_index * binfo->num_planes].buf;
    frame.buf.phy_addr[1] = binfo->planes[buf_index * binfo->num_planes + 1].buf;
    frame.buf.size.width  = dst_fmt->width;
    /* SFIELD interlaced: buffer contains only top field (height/2 lines) */
    frame.buf.size.height = aic_dvp_sfield_mode() ?
                            dst_fmt->height / 2 : dst_fmt->height;
    frame.buf.stride[0]   = dst_fmt->width;
    frame.buf.stride[1]   = dst_fmt->width;
    frame.buf.format      = dst_fmt->pixelformat;

    memset(&packet, 0, sizeof(packet));
    packet.phy_addr = ctx->ve_jpeg_phy_addr;
    packet.size     = ctx->ve_jpeg_buf_size;

    ret = mpp_encoder_encode(ctx->ve_encoder, &frame, &packet);
    if (ret < 0) {
        pr_err("VE rotation: JPEG encode failed, ret %d\n", ret);
        return -1;
    }

    /* VIN buffer no longer needed after encoding — release it early */
    vin_queue_buf(buf_index, ch, &ctx->ctx);

    /* ---- Step 2: JPEG decode with rotation ---- */
    struct mpp_packet dec_pkt;
    memset(&dec_pkt, 0, sizeof(dec_pkt));
    ret = mpp_decoder_get_packet(ctx->ve_decoder, &dec_pkt, packet.len);
    if (ret < 0) {
        pr_err("VE rotation: get_packet failed, ret %d\n", ret);
        return -1;
    }

    /* Copy JPEG data from encode output to decoder input */
    memcpy((void *)(unsigned long)dec_pkt.phy_addr,
           (void *)(unsigned long)ctx->ve_jpeg_phy_addr,
           packet.len);
    dec_pkt.size = packet.len;
    dec_pkt.flag = PACKET_FLAG_EOS;

    ret = mpp_decoder_put_packet(ctx->ve_decoder, &dec_pkt);
    if (ret < 0) {
        pr_err("VE rotation: put_packet failed, ret %d\n", ret);
        return -1;
    }

    ret = mpp_decoder_decode(ctx->ve_decoder);
    if (ret < 0) {
        pr_err("VE rotation: decode failed, ret %d\n", ret);
        return -1;
    }

    /* ---- Step 3: Get decoded (rotated) frame ---- */
    struct mpp_frame dec_frame;
    memset(&dec_frame, 0, sizeof(dec_frame));
    ret = mpp_decoder_get_frame(ctx->ve_decoder, &dec_frame);
    if (ret < 0) {
        pr_err("VE rotation: get_frame failed, ret %d\n", ret);
        return -1;
    }

    /*
    * Decoder wrote directly to reserved VIN buffer[active] via custom allocator.
    * dec_frame.buf.phy_addr already points to the correct ping-pong buffer.
    */
    layer->buf.phy_addr[0] = dec_frame.buf.phy_addr[0];
    layer->buf.phy_addr[1] = dec_frame.buf.phy_addr[1];

    /* Return decoder frame immediately (other ping-pong buffer is safe) */
    mpp_decoder_put_frame(ctx->ve_decoder, &dec_frame);

    ret = mpp_fb_ioctl(g_fb, AICFB_UPDATE_LAYER_CONFIG, layer);
    if (ret < 0) {
        pr_err("VE rotation: UPDATE_LAYER_CONFIG failed\n");
        return -1;
    }

    return 0;
}

#endif /* SUPPORT_ROTATION_BY_VE */
