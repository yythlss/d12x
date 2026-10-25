/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_pwm.c - D12x 板载 PWM 测试信号（第 4 步：PWM 测量）
 *
 * 用法（UI 线程调用，非阻塞）：
 *   dbg_pwm_init();
 *   dbg_pwm_set(1000, 500);          // 1kHz, 50%
 *   然后 dbg_pwm_get_freq()/get_duty() 拿最近设定去显示。
 *
 * 单位：period/pulse 按 RT-Thread PWM 框架要求用 ns。
 * 用 rt_pwm_set_period / rt_pwm_set_pulse（参数固定，不受 AIC_XPWM_DRV 重载
 * rt_pwm_set 签名的影响），避免不同 BSP 配置下的编译歧义。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/rt_drv_pwm.h>
#include "dbg_pwm.h"

#ifndef DBG_PWM_DEV_NAME
#define DBG_PWM_DEV_NAME  "pwm0"
#endif
#ifndef DBG_PWM_CH
#define DBG_PWM_CH        0
#endif
#ifndef DBG_PWM_ENABLED
#define DBG_PWM_ENABLED   1
#endif

static struct rt_device_pwm *g_pwm  = RT_NULL;
static rt_uint32_t          g_freq = 1000;   /* Hz，当前设定 */
static rt_uint32_t          g_duty = 500;     /* 千分比，500 = 50% */
static int                  g_enabled = 0;   /* 输出开关（用于 UI 回显） */

int dbg_pwm_init(void)
{
#if DBG_PWM_ENABLED
    g_pwm = (struct rt_device_pwm *)rt_device_find(DBG_PWM_DEV_NAME);
    if (g_pwm == RT_NULL) {
        rt_kprintf("dbg_pwm: %s not found (PWM output disabled)\n", DBG_PWM_DEV_NAME);
        return -RT_ERROR;
    }
    rt_kprintf("dbg_pwm: %s ready (ch%d)\n", DBG_PWM_DEV_NAME, DBG_PWM_CH);
#endif
    return RT_EOK;
}

/* freq_hz: 1~1MHz；duty_permil: 0~1000（千分比） */
int dbg_pwm_set(rt_uint32_t freq_hz, rt_uint32_t duty_permil)
{
    if (freq_hz == 0 || freq_hz > 1000000)
        return -RT_EINVAL;
    if (duty_permil > 1000)
        duty_permil = 1000;

#if DBG_PWM_ENABLED
    if (g_pwm != RT_NULL) {
        rt_uint32_t period = 1000000000u / freq_hz;          /* ns */
        rt_uint32_t pulse  = period * duty_permil / 1000u;   /* ns */

        rt_pwm_set_period(g_pwm, DBG_PWM_CH, period);
        rt_pwm_set_pulse(g_pwm, DBG_PWM_CH, pulse);
        rt_pwm_enable(g_pwm, DBG_PWM_CH);
        g_enabled = 1;
    }
#endif

    g_freq = freq_hz;
    g_duty = duty_permil;
    return RT_EOK;
}

int dbg_pwm_disable(void)
{
#if DBG_PWM_ENABLED
    if (g_pwm != RT_NULL)
        rt_pwm_disable(g_pwm, DBG_PWM_CH);
#endif
    g_enabled = 0;
    return RT_EOK;
}

int dbg_pwm_is_enabled(void) { return g_enabled; }

rt_uint32_t dbg_pwm_get_freq(void) { return g_freq; }
rt_uint32_t dbg_pwm_get_duty(void) { return g_duty; }
