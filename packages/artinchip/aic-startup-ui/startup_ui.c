/*
 * Copyright (c) 2020-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: ArtInChip
 */
#include <rtthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <inttypes.h>
#include "aic_osal.h"
#include "mpp_fb.h"
#include "mpp_mem.h"
#include "frame_allocator.h"
#include "mpp_decoder.h"
#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_ge.h"
#include "startup_ui.h"
#include "rtconfig.h"
#include "stddef.h"

#define PATH_MAX 1024

#ifdef LPKG_CHERRYUSB_DEVICE_DISPLAY_TEMPLATE
#define AIC_STARTUP_UI_SHOW_LVGL_LOGO

#ifdef AIC_LVGL_USB_OSD_DEMO
#error "Not support turn on LVGL USB OSD DEMO and Startup UI at the same time"
#endif
#endif

#ifdef AIC_STARTUP_UI_SHOW_LVGL_LOGO
#define IMAGE_EXTENSION ".png"
#define IMAGE_CATALOG   "/data/lvgl_data"
#define IMAGE_PATH_ROOT "/data/lvgl_data/logo.png"
#else
#define IMAGE_EXTENSION ".jpg"
#define IMAGE_CATALOG   "/data"
#define IMAGE_PATH_ROOT "/data/image"
#endif

#ifdef AIC_STARTUP_UI_POS_X
#define IMAGE_SCREEN_POS_X AIC_STARTUP_UI_POS_X
#else
#define IMAGE_SCREEN_POS_X 0
#endif

#ifdef AIC_STARTUP_UI_POS_Y
#define IMAGE_SCREEN_POS_Y AIC_STARTUP_UI_POS_Y
#else
#define IMAGE_SCREEN_POS_Y 0
#endif

static struct startup_ui_fb startup_screen_info = {0};
static char image_path[PATH_MAX];

#if defined (AIC_CHIP_D13X) || defined (AIC_CHIP_D12X)
#define USE_VE_FILL_FB
#endif

#ifdef USE_VE_FILL_FB
struct ext_frame_allocator {
    struct frame_allocator allocator;
    dma_addr_t             buf;
    struct mpp_rect        rect;
};

static int start_frame_buf_alloc(struct frame_allocator *p, struct mpp_frame* frame,
                           int stride, int height, enum mpp_pixel_format format)
{
    struct ext_frame_allocator *ext = (struct ext_frame_allocator *)p;
    struct startup_ui_fb *info = &startup_screen_info;
    u32 offset_x, offset_y = ext->rect.y;
    u32 offset = 0;

    if (format > MPP_FMT_BGRA_4444) {
        loge("The decode format 0x%x is not RGB!\n", format);
        return 0;
    }

    frame->buf.format = info->startup_fb_data.format;
    frame->buf.size.width = info->startup_fb_data.width;
    frame->buf.size.height = info->startup_fb_data.height;
    frame->buf.stride[0] = info->startup_fb_data.stride;
    frame->buf.buf_type = MPP_PHY_ADDR;

    if (offset_y + height > info->startup_fb_data.height)
        offset_y = info->startup_fb_data.height - height;

    if (ext->rect.x * (info->startup_fb_data.bits_per_pixel / 8) + stride > info->startup_fb_data.stride)
        offset_x = info->startup_fb_data.stride - stride;
    else
        offset_x = (ext->rect.x - 1) * (info->startup_fb_data.bits_per_pixel / 8);

    offset = info->startup_fb_data.stride * (offset_y - 1) + offset_x;
    if (offset % 8) {
        logi("The offset of (%d, %d) is not 8-byte aligned\n",
             ext->rect.x, ext->rect.y);
        offset = offset - offset % 8;
    }

    frame->buf.phy_addr[0] = (unsigned long)(ext->buf + offset);
    return 0;
}

static int start_frame_buf_free(struct frame_allocator *p, struct mpp_frame *frame)
{
    // Use the UI layer framebuffer directly, do not need to free
    return 0;
}

static int start_allocator_close(struct frame_allocator *p)
{
    struct ext_frame_allocator *ext = (struct ext_frame_allocator*)p;
    mpp_free(ext);
    return 0;
}

static struct alloc_ops def_ops = {
    .alloc_frame_buffer = start_frame_buf_alloc,
    .free_frame_buffer = start_frame_buf_free,
    .close_allocator = start_allocator_close,
};

static struct frame_allocator *allocator_open(u8 *buf, u32 x, u32 y)
{
    struct ext_frame_allocator *ext = (struct ext_frame_allocator*)mpp_alloc(sizeof(struct ext_frame_allocator));
    if (ext == NULL) {
        return NULL;
    }

    memset(ext, 0, sizeof(struct ext_frame_allocator));
    ext->allocator.ops = &def_ops;
    ext->buf = (dma_addr_t)buf;
    ext->rect.x = min(x, startup_screen_info.startup_fb_data.width);
    ext->rect.y = min(y, startup_screen_info.startup_fb_data.height);
    return &ext->allocator;
}
#endif

static int ge_bitblt(struct ge_bitblt *blt)
{
    int ret = 0;
    struct mpp_ge *ge = mpp_ge_open();

    if (!ge) {
        loge("open ge device error\n");
    }

    ret = mpp_ge_bitblt(ge, blt);
    if (ret < 0) {
        loge("bitblt task failed\n");
        return ret;
    }

    ret = mpp_ge_emit(ge);
    if (ret < 0) {
        loge("emit task failed\n");
        return ret;
    }

    ret = mpp_ge_sync(ge);
    if (ret < 0) {
        loge("ge sync fail\n");
        return ret;
    }

    mpp_ge_close(ge);

    return 0;
}

#ifdef AIC_PAN_DISPLAY
static u32 fb_copy_index = 0;
#ifndef USE_VE_FILL_FB
static u32 startup_fb_buf_index = 0;
#endif
#endif

static struct aicfb_screeninfo *get_screen_info(void)
{
    struct mpp_fb *fb = NULL;
    int ret;

    if (startup_screen_info.startup_fb_data.width)
        return &startup_screen_info.startup_fb_data;

    fb = mpp_fb_open();
    if (!fb)
        return NULL;

    ret = mpp_fb_ioctl(fb, AICFB_GET_SCREENINFO, &startup_screen_info.startup_fb_data);
    if (ret) {
        loge("get screen info failed\n");
        return NULL;
    }

    mpp_fb_close(fb);
    return &startup_screen_info.startup_fb_data;
}

#ifndef USE_VE_FILL_FB
static void render_frame(struct mpp_fb* fb, struct mpp_frame* frame,
                         u32 offset_x, u32 offset_y, u32 width, u32 height,
                         u32 layer_id, bool is_first_img)
{
    struct ge_bitblt blt;
    struct aicfb_screeninfo *info = NULL;
    struct aicfb_layer_data layer = {0};
#ifdef AIC_PAN_DISPLAY
    u32 disp_buf_addr;
    u32 fb0_buf_addr;
#endif
    u32 dst_buf_addr = 0;

    info = get_screen_info();
    if (info == NULL)
        return;

    if (layer_id == AICFB_LAYER_TYPE_UI) {
        layer.layer_id = AICFB_LAYER_TYPE_UI;
        layer.rect_id = 0;
        if (mpp_fb_ioctl(fb, AICFB_GET_LAYER_CONFIG, &layer) < 0) {
            loge("get ui layer config failed\n");
            return;
        }

#ifdef AIC_PAN_DISPLAY
        disp_buf_addr = (u32)layer.buf.phy_addr[0];
        fb0_buf_addr = (u32)(unsigned long)info->framebuffer;
        /* Switch the double-buffer */
        if (disp_buf_addr == fb0_buf_addr) {
            dst_buf_addr = is_first_img ? fb0_buf_addr : fb0_buf_addr + info->smem_len;
            startup_fb_buf_index = 1;
        } else {
            dst_buf_addr = fb0_buf_addr;
            startup_fb_buf_index = 0;
        }
#else
        dst_buf_addr = layer.buf.phy_addr[0];
#endif
    }
    memset(&blt, 0, sizeof(struct ge_bitblt));
    memcpy(&blt.src_buf, &frame->buf, sizeof(struct mpp_buf));

    blt.dst_buf.buf_type = MPP_PHY_ADDR;
    blt.dst_buf.phy_addr[0] = dst_buf_addr;
    blt.dst_buf.format = info->format;
    blt.dst_buf.stride[0] = info->stride;
    blt.dst_buf.size.width = info->width;
    blt.dst_buf.size.height = info->height;
    blt.dst_buf.crop_en = 1;

    blt.dst_buf.crop.x = offset_x;
    blt.dst_buf.crop.y = offset_y;
    blt.dst_buf.crop.width = blt.src_buf.crop.width;
    blt.dst_buf.crop.height = blt.src_buf.crop.height;

    logi("phy_addr: %x, stride: %d", blt.src_buf.phy_addr[0], blt.src_buf.stride[0]);
    logi("width: %d, height: %d, format: %d", blt.src_buf.size.width, blt.src_buf.size.height, blt.src_buf.format);

    ge_bitblt(&blt);

#ifdef AIC_PAN_DISPLAY
    if(!is_first_img) {
        mpp_fb_ioctl(fb, AICFB_PAN_DISPLAY, &startup_fb_buf_index);
        mpp_fb_ioctl(fb, AICFB_WAIT_FOR_VSYNC, NULL);
    }
#endif
}
#endif

#ifdef AIC_PAN_DISPLAY
static void framebuffer_copy(int buffer_index)
{
    struct ge_bitblt blt;
    struct aicfb_screeninfo *info = NULL;;

    info = get_screen_info();
    if (info == NULL)
        return;

    u32 fb0_buf_addr0 = (u32)(unsigned long)info->framebuffer;;
    u32 fb0_buf_addr1 = fb0_buf_addr0 + info->smem_len;;

    memset(&blt, 0, sizeof(struct ge_bitblt));

    if (buffer_index == 0) {
        blt.src_buf.phy_addr[0] = fb0_buf_addr0;
    } else {
        blt.src_buf.phy_addr[0] = fb0_buf_addr1;
    }
    blt.src_buf.buf_type = MPP_PHY_ADDR;
    blt.src_buf.format = info->format;
    blt.src_buf.stride[0] = info->stride;
    blt.src_buf.size.width = info->width;
    blt.src_buf.size.height = info->height;
    blt.src_buf.crop_en = 0;

    if (buffer_index == 0) {
        blt.dst_buf.phy_addr[0] = fb0_buf_addr1;
    } else {
        blt.dst_buf.phy_addr[0] = fb0_buf_addr0;
    }
    blt.dst_buf.buf_type = MPP_PHY_ADDR;
    blt.dst_buf.format = info->format;
    blt.dst_buf.stride[0] = info->stride;
    blt.dst_buf.size.width = info->width;
    blt.dst_buf.size.height = info->height;
    blt.dst_buf.crop_en = 0;

    logi("phy_addr: %x, stride: %d", blt.src_buf.phy_addr[0], blt.src_buf.stride[0]);
    logi("width: %d, height: %d, format: %d", blt.src_buf.size.width, blt.src_buf.size.height, blt.src_buf.format);

    ge_bitblt(&blt);
}
#endif

static int decode_pic_from_path(const char *path, u32 offset_x, u32 offset_y,
                         u32 width, u32 height, u32 layer_id, bool is_first_img)
{
    struct mpp_fb *fb = mpp_fb_open();
    int ret = 0;

#ifdef USE_VE_FILL_FB
    struct frame_allocator *allocator = NULL;
    struct aicfb_screeninfo *info = get_screen_info();
#endif

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        loge("Failed to open file '%s'\n", path);
        return 0;
    }

    fseek(fp, 0L, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    uint8_t *pic = malloc(fsize);
    if (!pic) {
        loge("Failed to allocate memory for picture data,size is :%ld\n",fsize);
        fclose(fp);
        return 0;
    }

    size_t read_len = fread(pic, 1, fsize, fp);
    fclose(fp);
    if (read_len != fsize) {
        loge("Failed to read entire file\n");
        free(pic);
        return 0;
    }

    struct mpp_decoder* dec = NULL;
    struct decode_config config;
    config.bitstream_buffer_size = (fsize + 1023) & (~1023);
    config.extra_frame_num = 0;
    config.packet_count = 1;

#ifdef USE_VE_FILL_FB
    config.pix_fmt = info->format;
#else
    config.pix_fmt = MPP_FMT_NV12;
#endif

    if (pic[0] == 0xff && pic[1] == 0xd8) {
        dec = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_MJPEG);
    } else if (pic[1] == 'P' || pic[2] == 'N' || pic[3] == 'G') {
        if (config.pix_fmt == MPP_FMT_RGB_565 || config.pix_fmt == MPP_FMT_BGR_565) {
            loge("PNG decode does nor support RGB565\n");
            free(pic);
            return -1;
        }
        dec = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_PNG);
    } else {
        loge("Unsupported or invalid image format\n");
        free(pic);
        return 0;
    }

    if (!dec)
        goto out;


#ifdef USE_VE_FILL_FB
    allocator = allocator_open(info->framebuffer, offset_x, offset_y);
    mpp_decoder_control(dec, MPP_DEC_INIT_CMD_SET_EXT_FRAME_ALLOCATOR, (void*)allocator);
#endif

    mpp_decoder_init(dec, &config);

    struct mpp_packet packet;
    memset(&packet, 0, sizeof(struct mpp_packet));
    mpp_decoder_get_packet(dec, &packet, fsize);

    memcpy(packet.data, pic, fsize);
    packet.size = fsize;
    packet.flag = PACKET_FLAG_EOS;

    mpp_decoder_put_packet(dec, &packet);

    ret = mpp_decoder_decode(dec);
    if(ret < 0) {
        loge("decode error");
        ret = 0;
        goto out;
    }

    struct mpp_frame frame;
    memset(&frame, 0, sizeof(struct mpp_frame));
    mpp_decoder_get_frame(dec, &frame);

#ifndef USE_VE_FILL_FB
    render_frame(fb, &frame, offset_x, offset_y, width, height, layer_id, is_first_img);
#endif
    mpp_decoder_put_frame(dec, &frame);

out:
    if (dec)
        mpp_decoder_destory(dec);

    free(pic);

    if (fb)
        mpp_fb_close(fb);

    return ret;
}

static void fb_clean(void* ptr, int value, size_t num)
{
    memset(ptr, value, num);
    aicos_dcache_clean_invalid_range(ptr, num);
}

static int count_images(const char *directoryPath)
{
    DIR *dir;
    struct dirent *ent;
    int imageCount = 0;

    dir = opendir(directoryPath);
    if (!dir) {
        printf("Error opening directory");
        return 0;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, IMAGE_EXTENSION)) {
            imageCount++;
        }
    }

    closedir(dir);
    return imageCount;
}

static void construct_imagepath(char *outputBuffer, int image_number)
{
#ifdef AIC_STARTUP_UI_SHOW_LVGL_LOGO
    (void)image_number;
    snprintf(outputBuffer, PATH_MAX, "%s", IMAGE_PATH_ROOT);
#else
    snprintf(outputBuffer, PATH_MAX, "%s%d%s", IMAGE_PATH_ROOT, image_number, IMAGE_EXTENSION);
#endif
}

int startup_ui_show(void)
{
    int i, ret = 0;
    int image_count = count_images(IMAGE_CATALOG);
    struct startup_ui_fb *startup_ui = malloc(sizeof(struct startup_ui_fb));
    startup_ui->startup_fb = mpp_fb_open();

    if (!startup_ui->startup_fb) {
        printf("fb open failed\n");
        free(startup_ui);
    }

    ret = mpp_fb_ioctl(startup_ui->startup_fb, AICFB_GET_SCREENINFO , &startup_ui->startup_fb_data);
    if (ret) {
        printf("mpp_fb_ioctl ops failed\n");
        free(startup_ui);
    }

    fb_clean(startup_ui->startup_fb_data.framebuffer, 0, startup_ui->startup_fb_data.height * startup_ui->startup_fb_data.stride);

#ifdef AIC_PAN_DISPLAY
    fb_clean(startup_ui->startup_fb_data.framebuffer + startup_ui->startup_fb_data.smem_len, 0, \
    startup_ui->startup_fb_data.height * startup_ui->startup_fb_data.stride);
#endif

    for(i = 0; i < image_count; i++) {
        construct_imagepath(image_path, i);

        if(i == 0) {
            decode_pic_from_path(image_path, IMAGE_SCREEN_POS_X, IMAGE_SCREEN_POS_Y, 0, 0, AICFB_LAYER_TYPE_UI, true);
            mpp_fb_ioctl(startup_ui->startup_fb, AICFB_POWERON, 0);
        } else {
            aic_mdelay(80);
            decode_pic_from_path(image_path, IMAGE_SCREEN_POS_X, IMAGE_SCREEN_POS_Y, 0, 0, AICFB_LAYER_TYPE_UI, false);
        }
    }
#ifdef AIC_PAN_DISPLAY
    if ((image_count % 2) == 0) {
        /* frame buffer copy */
        framebuffer_copy(!fb_copy_index);
        /* switch to frame buffer 0 */
        mpp_fb_ioctl(startup_ui->startup_fb, AICFB_PAN_DISPLAY, &fb_copy_index);
        mpp_fb_ioctl(startup_ui->startup_fb, AICFB_WAIT_FOR_VSYNC, NULL);
    }
#endif
    free(startup_ui);

    return 0;
}

INIT_LATE_APP_EXPORT(startup_ui_show);
