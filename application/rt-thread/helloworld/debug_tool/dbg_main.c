/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_main.c - 调试仪主入口
 * 提供 ui_init() 供 aic_ui_init() 调用（需在 menuconfig 关掉 demo_hub）。
 * 职责：
 *   1. 初始化消息总线
 *   2. 初始化串口 / CAN / ADC 等采集模块
 *   3. 加载主菜单界面
 *   4. 创建 LVGL 定时器，轮询消息总线并分发到各界面
 */
#include <rtthread.h>
#include <string.h>
#include "lvgl.h"
#include "dbg_msg.h"
#include "dbg_serial.h"
#include "dbg_can.h"
#include "dbg_modbus.h"
#include "dbg_adc.h"
#include "dbg_slave.h"
#include "ui_main_screen.h"
#include "ui_serial_screen.h"
#include "ui_can_screen.h"
#include "ui_scope_screen.h"
#include "ui_slave_screen.h"

static lv_obj_t *g_main_screen;

/* 消息分发：线程 -> UI */
static void dbg_msg_dispatch(dbg_msg_t *msg)
{
    switch (msg->type) {
    case DBG_MSG_SERIAL_RX:
        ui_serial_on_rx(msg->data, msg->len);
        break;
    case DBG_MSG_CAN_RX:
        ui_can_on_rx(msg->data, msg->len);
        break;
    case DBG_MSG_MODBUS_RESULT:
        ui_serial_on_modbus_result(msg->data, msg->len);
        break;
    case DBG_MSG_ADC_VAL:
        ui_scope_on_adc(msg->data, msg->len);
        break;
    case DBG_MSG_SLAVE_DATA:
        ui_slave_on_data(msg->data, msg->len);
        break;
    default:
        break;
    }
}

/* LVGL 定时器：轮询消息队列 */
static void dbg_ui_timer_cb(lv_timer_t *timer)
{
    dbg_msg_t msg;

    while (dbg_msg_recv(&msg) == RT_EOK) {
        dbg_msg_dispatch(&msg);
    }
}

void ui_init(void)
{
    /* 1. 消息总线 */
    if (dbg_msg_init() != RT_EOK) {
        rt_kprintf("dbg_tool: msg init failed\n");
        return;
    }

    /* 2. 串口模块（uart0） */
    if (dbg_serial_init("uart0") != RT_EOK) {
        rt_kprintf("dbg_tool: serial init failed\n");
    }

    /* 3. CAN 模块 */
    if (dbg_can_init() != RT_EOK) {
        rt_kprintf("dbg_tool: can init failed\n");
    }

    /* 4. Modbus 工作线程（异步） */
    if (dbg_modbus_init() != RT_EOK) {
        rt_kprintf("dbg_tool: modbus init failed\n");
    }

    /* 5. 模拟量采集（默认自测信号源，无外设可验证示波） */
    if (dbg_adc_init() != RT_EOK) {
        rt_kprintf("dbg_tool: adc init failed\n");
    }

    /* 5.5 从机轮询线程（Modbus-RTU 主站 -> 星火一号） */
    if (dbg_slave_init() != RT_EOK) {
        rt_kprintf("dbg_tool: slave poll init failed\n");
    }

    /* 6. 主菜单界面 */
    g_main_screen = ui_main_screen_create();
    lv_screen_load_anim(g_main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, 0);

    /* 6. UI 定时器 */
    if (lv_timer_create(dbg_ui_timer_cb, 10, NULL) == NULL) {
        rt_kprintf("dbg_tool: create ui timer failed\n");
    }
}
