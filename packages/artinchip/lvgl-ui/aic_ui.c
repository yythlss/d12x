/*
 * Copyright (C) 2022-2024 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Ning Fang <ning.fang@artinchip.com>
 */

#include "lvgl.h"
#include "aic_ui.h"
#include "aic_osal.h"
#include "lvgl/demos/lv_demos.h"

#ifdef AIC_USE_TOUCH_MONKEY_TEST
#if LV_USE_MONKEY
static void use_touch_monkey_test(void)
{
    /*Create encoder monkey test*/
    lv_monkey_config_t config;
    lv_monkey_config_init(&config);
    config.type = LV_INDEV_TYPE_POINTER;
    config.period_range.min = AIC_USE_TOUCH_MONKEY_TEST_PERIOD_RANG_MIN;
    config.period_range.max = AIC_USE_TOUCH_MONKEY_TEST_PERIOD_RANG_MAX;
    config.input_range.min = AIC_USE_TOUCH_MONKEY_TEST_INPUT_RANG_MAX;
    config.input_range.max = AIC_USE_TOUCH_MONKEY_TEST_PERIOD_RANG_MAX;
    lv_monkey_t * monkey = lv_monkey_create(&config);

    /*Set the default group*/
    lv_group_t * group = lv_group_create();
    lv_indev_set_group(lv_monkey_get_indev(monkey), group);
    lv_group_set_default(group);

    /*Start monkey test*/
    lv_monkey_set_enable(monkey, true);
}
#endif
#endif

void aic_ui_init()
{
/* qc test demo is only for aic internal qc testing, please ignore it. */

#ifdef AIC_LVGL_VSCODE_DEMO
    extern void vscode_ui_init();
    vscode_ui_init();
    return;
#endif

#ifdef AIC_LVGL_BASE_DEMO
#include "base_ui.h"
    base_ui_init();
#endif

#ifdef AIC_LVGL_METER_DEMO
#include "meter_ui.h"
    meter_ui_init();
#endif

#ifdef AIC_LVGL_LAUNCHER_DEMO
    extern void launcher_ui_init();
    launcher_ui_init();
#endif

#ifdef AIC_LVGL_DASHBOARD_DEMO
    extern void dashboard_ui_init(void);
    dashboard_ui_init();
#endif

#ifdef AIC_LVGL_DEMO_HUB_DEMO
    extern void demo_hub_init(void);
    demo_hub_init();
#endif

#ifdef AIC_LVGL_ELEVATOR_DEMO
#include "elevator_ui.h"
    elevator_ui_init();
#endif

#ifdef AIC_LVGL_USB_OSD_DEMO
#include "usb_osd_ui.h"
    usb_osd_ui_init();
#endif

#ifdef AIC_LVGL_SLIDE_DEMO
    extern void slide_ui_init(void);
    slide_ui_init();
#endif

#ifdef AIC_LVGL_GIF_DEMO
    extern void gif_ui_init(void);
    gif_ui_init();
#endif

#ifdef AIC_LVGL_SIMPLE_PLAYER_DEMO
    extern void simple_player_ui_init(void);
    simple_player_ui_init();
#endif

#ifdef AIC_LVGL_MUSIC_DEMO
    lv_demo_music();
#endif

#ifdef AIC_LVGL_DEMO_BENCHMARK
    lv_demo_benchmark();
#endif

#ifdef AIC_LVGL_DEMO_WIDGETS
    lv_demo_widgets();
#endif

#ifdef AIC_LVGL_IMAGE_DEMO
    extern void image_ui_init(void);
    image_ui_init();
#endif

#ifdef AIC_USE_TOUCH_MONKEY_TEST
    use_touch_monkey_test();
#endif
    return;
}
