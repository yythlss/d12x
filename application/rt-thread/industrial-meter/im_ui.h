/*
 * 便携工业调试仪 —— LVGL 仪表盘 (480x272)
 *
 * 由 lv_user_gui_init() 调用, 运行在 LVGL 线程内, 不要在这里做阻塞操作。
 */

#ifndef __IM_UI_H__
#define __IM_UI_H__

#include <rtthread.h>

void im_ui_init(void);

#endif /* __IM_UI_H__ */
