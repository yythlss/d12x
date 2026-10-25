/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_d12x.h - D12x 本板运行遥测采集（不接任何外设）
 *
 * 采集项（全部来自板载资源，无需外接设备）：
 *   - 结温   ：SoC 内置 TSEN，sensor 框架注册名 "temp_tsen_cpu"（通道 tsen_cpu
 *               + 类型前缀 temp_），单位 0.1 ℃
 *   - 电量   ：复用 dbg_power（片内 GPAI 分压），单位 0.1 %
 *   - 剩余内存：rt_memory_info()，字节 -> KB
 *   - CPU 占用：LPKG cpu_usage 包的 cpu_load_average()，0.0~100.0 -> 0.1 % 定点
 *   - 运行秒数：rt_tick_get() / RT_TICK_PER_SECOND
 *
 * 采集到的快照经 dbg_slave_d12x_set() 交给从机轮询线程，由它 FC16 写到星火一号
 * 0x0400 中转区；最终由星火一号以 device="d12x" 一并上报云端，小程序来源显示
 * 「D12x 主控终端」。这样 D12x 不上独立上行链路也能在系统里成为第二个数据源。
 */
#ifndef __DBG_D12X_H__
#define __DBG_D12X_H__

#include <rtthread.h>

/* 初始化：创建采集线程，周期把本板遥测经星火一号中转上报 */
int dbg_d12x_init(void);

#endif /* __DBG_D12X_H__ */
