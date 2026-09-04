/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: geo <guojun.dong@artinchip.com>
 */

#define LOG_TAG     "BF2072"

#include <drivers/i2c.h>
#include <drivers/pin.h>

#include "aic_core.h"
#include "mpp_types.h"
#include "mpp_img_size.h"
#include "mpp_vin.h"

#include "drv_camera.h"
#include "camera_inner.h"

/* Default format configuration of BF2072 */
#define BF2072_DFT_WIDTH        VGA_WIDTH
#define BF2072_DFT_HEIGHT       VGA_HEIGHT
#define BF2072_DFT_BUS_TYPE     MEDIA_BUS_RAW8_MONO
#define BF2072_DFT_CODE         MEDIA_BUS_FMT_YUYV8_2X8

#define BF2072_I2C_SLAVE_ID     0x3E
#define BF2072_CHIP_ID          0x30

static const struct reg8_info sensor_init_data[] =
{
    {0xF2,0x01},
    {0xF2,0x00},

    // PWD
    //0xf3,0x02},

    {0xF4,0x00},//HVFLIP
    {0xF0,0x01},//BW_EN
    {0xF1,0x03},
    {0xF3,0x00},
    {0x07,0x00},//ADC_MODE_SEL

    {0xFE,0x01},
    {0x0D,0x0F},
    {0xcf,0x84},
    // {0xE0,0x53},//53:120
    {0xE0,0x56},

    {0xE1,0x3C},//{0xE1,0x78},
    {0xEe,0x64},//mclk dly

    {0xe8,0xff},//PCP_Grst_TxR
    {0xe7,0xee},//NCP1_TX1_PCP1_TX1
    {0xe6,0xEF},//NCP0_Grst_NCP2_TxR
    {0x66,0x00},//AEC enable
    {0xe5,0x0a},


    {0xFE,0x00},
    {0x01,0xe0},//0xe2:MN mode
    {0x02,0x02},
    {0x03,0xa2},//a2_Grst plus_b2_Grst always on_82_always off
    {0x04,0x01},
    {0x06,0x10},//qc 2.8 PIXEL
    {0x07,0x00},//adc_sel
    {0xE2,0x09},// raw_en=0
    {0xaa,0x00},//test pattern
    {0x93,0x83},
    {0x44,0xFF},//GTx_rsing
    {0x45,0x01},//GTx_falling
    {0x46,0x00},//GTx_falling_rising
    {0x50,0x45},//Grst_F_R_LPS
    {0x4e,0x00},//Gtx_F_R_LPS

    {0xFE,0x01},
    {0x66,0x00},//AGC AEC//{0x66,0x05},//AGC AEC
    {0x6A,0xff},//Global gain
    {0x6B,0x02},//INTT H
    {0x6C,0x70},//INTT L
    {0x6D,0x10},//DIGITAL GAIN
    {0x74,0x28},//{0x74,0x0a},
    {0x75,0x28},//{0x75,0x0a},
    {0x76,0x01},
    {0xFE,0x00},
    {0x01,0xE0},//PD=E0 MN=E2
    {0x93,0x03},//BLC ON=03 OFF=01
    {0xD0,0x00},//BP OFF
    {0x86,0x00},//TAR
    {0x87,0x20},//TAR
    {0x88,0x20},//TAR
    {0x89,0x20},//TAR
    {0x8A,0x20},//TAR

    {0xfe,0x00},
    {0x23,0x10},//rst_comp1
    {0x24,0x44},
    {0x25,0x10},//rst_comp2
    {0x26,0x44},
    {0x2c,0x46},//set_ramp_2
    {0x2f,0x48},
    {0x16,0x14},//clamp_en
    {0x17,0x72},

    {0xEA,0x00},

    //0xFE,0x00},
    //0x1f,0x3e},
};

struct bf2072_dev {
    struct rt_device dev;
    struct rt_i2c_bus_device *i2c;
    u32 rst_pin;
    u32 pwdn_pin;
    struct clk *clk;

    struct mpp_video_fmt fmt;

    bool on;
    bool streaming;
};

static struct bf2072_dev g_bf2072_dev = {0};

static int bf2072_write_reg(struct rt_i2c_bus_device *i2c, u8 reg, u8 val)
{
    if (rt_i2c_write_reg(i2c, BF2072_I2C_SLAVE_ID, reg, &val, 1) != 1) {
        LOG_E("%s: error: reg = 0x%x, val = 0x%x", __func__, reg, val);
        return -1;
    }

    return 0;
}

static int bf2072_read_reg(struct rt_i2c_bus_device *i2c, u8 reg, u8 *val)
{
    if (rt_i2c_read_reg(i2c, BF2072_I2C_SLAVE_ID, reg, val, 1) != 1) {
        LOG_E("%s: error: reg = 0x%x, val = 0x%x", __func__, reg, *val);
        return -1;
    }

    return 0;
}

static void bf2072_reset(struct bf2072_dev *sensor)
{
    return;
}

static int bf2072_sensor_init(struct bf2072_dev *sensor)
{
    int i = 0;
    const struct reg8_info *info = sensor_init_data;

    bf2072_reset(sensor);
    aicos_udelay(1000);

    for (i = 0; i < ARRAY_SIZE(sensor_init_data); i++, info++) {
        printf("i:%d,addr:0x%02x,data:0x%02x\n",i,info->reg,info->val);
        if (bf2072_write_reg(sensor->i2c, info->reg, info->val))
            return -1;
    }

    return 0;
}

static int bf2072_probe(struct bf2072_dev *sensor)
{
    u8 id = 0;

    if (bf2072_read_reg(sensor->i2c, 0xfc, &id))
        return -1;


    if (id != BF2072_CHIP_ID) {
        LOG_E("Invalid chip ID: %02x\n", id);
        return -1;
    }
    return bf2072_sensor_init(sensor);
}

static bool bf2072_is_open(struct bf2072_dev *sensor)
{
    return sensor->on;
}

static void bf2072_power_on(struct bf2072_dev *sensor)
{
    if (sensor->on)
        return;

    camera_pin_set_high(sensor->pwdn_pin);
    aicos_mdelay(5);
    camera_pin_set_low(sensor->rst_pin);
    aicos_mdelay(5);
    camera_pin_set_high(sensor->rst_pin);
    aicos_mdelay(5);
    LOG_I("Power on");
    sensor->on = true;
}

static void bf2072_power_off(struct bf2072_dev *sensor)
{
    if (!sensor->on)
        return;

    camera_pin_set_low(sensor->pwdn_pin);

    LOG_I("Power off");
    sensor->on = false;
}

static rt_err_t bf2072_init(rt_device_t dev)
{
    struct bf2072_dev *sensor = &g_bf2072_dev;

    sensor->i2c = camera_i2c_get();
    if (!sensor->i2c)
        return -RT_EINVAL;

    sensor->fmt.code   = BF2072_DFT_CODE;
    sensor->fmt.width  = BF2072_DFT_WIDTH;
    sensor->fmt.height = BF2072_DFT_HEIGHT;
    sensor->fmt.bus_type = BF2072_DFT_BUS_TYPE;
    sensor->fmt.flags = MEDIA_SIGNAL_HSYNC_ACTIVE_HIGH |
                        MEDIA_SIGNAL_VSYNC_ACTIVE_LOW |
                        MEDIA_SIGNAL_PCLK_SAMPLE_FALLING;

    sensor->rst_pin = camera_rst_pin_get();
    sensor->pwdn_pin = camera_pwdn_pin_get();
    if (!sensor->rst_pin || !sensor->pwdn_pin)
        return -RT_EINVAL;

    return RT_EOK;
}

static rt_err_t bf2072_open(rt_device_t dev, rt_uint16_t oflag)
{
    struct bf2072_dev *sensor = (struct bf2072_dev *)dev;

    if (bf2072_is_open(sensor))
        return RT_EOK;

    bf2072_power_on(sensor);

    if (bf2072_probe(sensor)) {
        bf2072_power_off(sensor);
        return -RT_ERROR;
    }

    LOG_I("BF2072 inited");
    return RT_EOK;
}

static rt_err_t bf2072_close(rt_device_t dev)
{
    struct bf2072_dev *sensor = (struct bf2072_dev *)dev;

    if (!bf2072_is_open(sensor))
        return -RT_ERROR;

    bf2072_power_off(sensor);
    return RT_EOK;
}

static int bf2072_get_fmt(struct bf2072_dev *sensor, struct mpp_video_fmt *cfg)
{
    cfg->code   = sensor->fmt.code;
    cfg->width  = sensor->fmt.width;
    cfg->height = sensor->fmt.height;
    cfg->flags  = sensor->fmt.flags;
    cfg->bus_type = sensor->fmt.bus_type;
    return RT_EOK;
}

static int bf2072_start(struct bf2072_dev *sensor)
{
    return 0;
}

static int bf2072_stop(struct bf2072_dev *sensor)
{
    return 0;
}

static int bf2072_pause(rt_device_t dev)
{
    return bf2072_close(dev);
}

static int bf2072_resume(rt_device_t dev)
{
    return bf2072_open(dev, 0);
}

static int bf2072_set_ee(struct bf2072_dev *sensor, u32 percent)
{
    u8 cur = 0, val = PERCENT_TO_INT(0, 0xFF, percent);

    if (bf2072_read_reg(sensor->i2c, 0x77, &cur)) {
        LOG_E("Failed to get current EE\n");
        return -1;
    }

    LOG_I("Set Edge Enhancement 0x%02x -> 0x%02x\n", cur, val);
    if (cur == val)
        return 0;

    return bf2072_write_reg(sensor->i2c, 0x77, val);
}

static int bf2072_enable_flip(struct bf2072_dev *sensor, bool enable,
                            u8 mask, u8 shift, char *name)
{
    u8 cur = 0;

    if (bf2072_read_reg(sensor->i2c, 0x14, &cur)) {
        LOG_E("Failed to get current flip\n");
        return -1;
    }

    LOG_I("Set %s flip %d -> %d\n", name, (cur & mask) >> shift, enable);
    if ((cur & mask) >> shift == (u8)enable)
        return 0;

    if (enable)
        return bf2072_write_reg(sensor->i2c, 0x14, cur | mask);
    else
        return bf2072_write_reg(sensor->i2c, 0x14, cur & ~mask);
}

static int bf2072_enable_h_flip(struct bf2072_dev *sensor, bool enable)
{
    return bf2072_enable_flip(sensor, enable, 1, 0, "H");
}

static int bf2072_enable_v_flip(struct bf2072_dev *sensor, bool enable)
{
    return bf2072_enable_flip(sensor, enable, 2, 1, "V");
}

static rt_err_t bf2072_control(rt_device_t dev, int cmd, void *args)
{
    struct bf2072_dev *sensor = (struct bf2072_dev *)dev;

    switch (cmd) {
    case CAMERA_CMD_START:
        return bf2072_start(sensor);
    case CAMERA_CMD_STOP:
        return bf2072_stop(sensor);
    case CAMERA_CMD_PAUSE:
        return bf2072_pause(dev);
    case CAMERA_CMD_RESUME:
        return bf2072_resume(dev);
    case CAMERA_CMD_GET_FMT:
        return bf2072_get_fmt(sensor, (struct mpp_video_fmt *)args);
    case CAMERA_CMD_SET_SHARPNESS:
        return bf2072_set_ee(sensor, *(u32 *)args);
    case CAMERA_CMD_SET_H_FLIP:
        return bf2072_enable_h_flip(sensor, *(bool *)args);
    case CAMERA_CMD_SET_V_FLIP:
        return bf2072_enable_v_flip(sensor, *(bool *)args);
    default:
        LOG_I("Unsupported cmd: 0x%x", cmd);
        return -RT_EINVAL;
    }
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops bf2072_ops =
{
    .init = bf2072_init,
    .open = bf2072_open,
    .close = bf2072_close,
    .control = bf2072_control,
};
#endif

int rt_hw_bf2072_init(void)
{
#ifdef RT_USING_DEVICE_OPS
    g_bf2072_dev.dev.ops = &bf2072_ops;
#else
    g_bf2072_dev.dev.init = bf2072_init;
    g_bf2072_dev.dev.open = bf2072_open;
    g_bf2072_dev.dev.close = bf2072_close;
    g_bf2072_dev.dev.control = bf2072_control;
#endif
    g_bf2072_dev.dev.type = RT_Device_Class_CAMERA;

    rt_device_register(&g_bf2072_dev.dev, CAMERA_DEV_NAME, 0);

    return 0;
}
INIT_DEVICE_EXPORT(rt_hw_bf2072_init);
