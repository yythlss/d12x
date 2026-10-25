/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_main.c - 调试仪主入口
 * 提供 ui_init() 供 aic_ui_init() 调用（需在 menuconfig 关掉 demo_hub）。
 * 职责：
 *   1. 初始化消息总线
 *   2. 初始化串口 / ADC 等采集模块
 *   3. 加载主菜单界面
 *   4. 创建 LVGL 定时器，轮询消息总线并分发到各界面
 */
#include <rtthread.h>
#include <string.h>
#include "lvgl.h"
#include "dbg_msg.h"
#include "dbg_serial.h"
#include "dbg_modbus.h"
#include "dbg_adc.h"
#include "dbg_slave.h"
#include "dbg_log.h"
#include "dbg_power.h"
#include "dbg_d12x.h"
#include "dbg_pwm.h"
#include "dbg_theme.h"
#include "ui_main_screen.h"
#include "ui_serial_screen.h"
#include "ui_scope_screen.h"
#include "ui_slave_screen.h"
#include "ui_env_screen.h"
#include "ui_rs485_screen.h"

static lv_obj_t *g_main_screen;

/* 消息分发：线程 -> UI */
static void dbg_msg_dispatch(dbg_msg_t *msg)
{
    switch (msg->type) {
    case DBG_MSG_SERIAL_RX:
        ui_serial_on_rx(msg->data, msg->len);
        break;
    case DBG_MSG_MODBUS_RESULT:
        ui_serial_on_modbus_result(msg->data, msg->len);
        break;
    case DBG_MSG_ADC_VAL:
        ui_scope_on_adc(msg->data, msg->len);
        break;
    case DBG_MSG_SLAVE_DATA:
        /* 记录模块先入队（内部只是 memcpy，不会拖慢 UI），再刷新仪表盘 */
        dbg_log_on_data(msg->data, msg->len);
        ui_slave_on_data(msg->data, msg->len);
        ui_env_on_data(msg->data, msg->len);
        break;
    case DBG_MSG_RS485_RESULT:
        /* 数据源为 RS485 时，结果先交给示波（自带 pending 守卫，只认自己的请求）；
         * 否则交给扫描页显示。两条链路不会同时激活（一次只在一个页面），
         * 但若示波 RS485 在途而用户切到扫描页，扫描页应暂停接收以免误显，
         * 故 RS485 源下不转发给扫描页。 */
        if (dbg_adc_get_source() == DBG_ADC_SRC_RS485)
            dbg_adc_on_rs485_result();
        else
            ui_rs485_on_result(msg->data, msg->len);
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

    /* 1.5 全局主题（Sleep/Wake 时统一切换全部页面的配色） */
    dbg_theme_init();

    /* 2. 串口模块（uart0） */
    if (dbg_serial_init("uart0") != RT_EOK) {
        rt_kprintf("dbg_tool: serial init failed\n");
    }

    /* 3. Modbus 工作线程（异步）
     *    CAN 调试已移除：D12x 板上没有 CAN 收发器，只有控制器，
     *    接不上真实总线，保留一个死功能反而误导。 */
    if (dbg_modbus_init() != RT_EOK) {
        rt_kprintf("dbg_tool: modbus init failed\n");
    }

    /* 4. 模拟量采集（默认自测信号源，无外设可验证示波） */
    if (dbg_adc_init() != RT_EOK) {
        rt_kprintf("dbg_tool: adc init failed\n");
    }

    /* 5. 从机轮询线程（Modbus-RTU 主站 -> 星火一号） */
    if (dbg_slave_init() != RT_EOK) {
        rt_kprintf("dbg_tool: slave poll init failed\n");
    }

    /* 5.5 D12x 本板遥测采集（经星火一号中转上报，不接外设） */
    if (dbg_d12x_init() != RT_EOK) {
        rt_kprintf("dbg_tool: d12x telemetry init failed\n");
    }

    /* 5.6 板载 PWM 测试信号（第 4 步：PWM 测量） */
    if (dbg_pwm_init() != RT_EOK) {
        rt_kprintf("dbg_tool: pwm init skipped (no pwm device)\n");
    }

    /* 5.7 SD 卡 CSV 记录（第 8 步）：建线程 + 环形缓冲，真正落盘在线程里做 */
    if (dbg_log_init() != RT_EOK) {
        rt_kprintf("dbg_tool: csv log init failed\n");
    }

    /* 5.8 电量监测 + 低功耗（GPAI 未启用时自动 N/A，UI 仍可显示） */
    if (dbg_power_init() != RT_EOK) {
        rt_kprintf("dbg_tool: power init failed\n");
    }

    /* 6. 主菜单界面 */
    g_main_screen = ui_main_screen_create();
    lv_screen_load_anim(g_main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, 0);

    /* 6. UI 定时器 */
    if (lv_timer_create(dbg_ui_timer_cb, 10, NULL) == NULL) {
        rt_kprintf("dbg_tool: create ui timer failed\n");
    }
}
