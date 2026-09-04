/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#include "lvgl.h"
#include "aic_ui.h"
#include "lv_port_disp.h"
#include "mpp_fb.h"
#include "meter_ui.h"
#include "aic_screen_ctl.h"

void meter_ui_init()
{
    lv_obj_t * label1 = lv_label_create(lv_screen_active());
    lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);     /*Break the long lines*/
    lv_label_set_text(label1, "Recolor is not supported for v9 now.");
    lv_obj_set_width(label1, 150);  /*Set smaller width to make the lines wrap*/
    lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t * label2 = lv_label_create(lv_screen_active());
    lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);     /*Circular scroll*/
    lv_obj_set_width(label2, 150);
    lv_label_set_text(label2, "It is a circularly scrolling text. ");
    lv_obj_align(label2, LV_ALIGN_CENTER, 0, 40);

#ifdef LV_AIC_SCREEN_CTL
    screen_ctl_init();
#endif
}

void ui_init(void)
{
    meter_ui_init();
}
