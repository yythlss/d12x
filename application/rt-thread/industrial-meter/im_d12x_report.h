/*
 * 便携工业调试仪 —— D12x 主机自身运行态采集上报
 *
 * D12x 作为 Modbus 主机，除了轮询星火一号从站，还要把自己的「结温 / 电量 /
 * CPU 占用 / 剩余内存 / 运行秒数」经 RS485 写回从站 0x0400~0x0404，由从站
 * 原样中转上云 (device = "d12x")。这样小程序 overview 里「D12x 主控终端」
 * 节点才会有真实数据，而不是永远红块 (nodata)。
 *
 * 单位约定 (与云端 small_report / SparkNo1 mb_slave.c 对齐)：
 *   REG_D12X_TEMP   int16  0.1 ℃  (可为负)
 *   REG_D12X_BAT    uint16 0.1 %  ; 未接电池/电源供电时默认 100% (GPAI 未开/无读数)
 *   REG_D12X_CPU    uint16 0.1 %
 *   REG_D12X_MEM    uint16 剩余内存 KB
 *   REG_D12X_UPTIME uint16 运行秒数
 */

#ifndef __IM_D12X_REPORT_H__
#define __IM_D12X_REPORT_H__

#include <rtthread.h>
#include "im_protocol.h"

/* 上报周期 (ms)：1s 一刷，仪表盘足够实时，又不压轮询线程 */
#define IM_D12X_REPORT_MS      1000

/* 电量分压网络参数（按实际硬件改）：pin 脚电压 -> 电池组 -> 单节 -> 百分比。
 * 与 dbg_power.h 的 DBG_BAT_* 同一套公式，这里自包含、不依赖 debug_tool 模块。 */
#define IM_D12X_BAT_ADC_CH       0      /* GPAI 通道：电池分压网络所接通道 */
#define IM_D12X_BAT_VDIV_NUM     255    /* 分压比 (R_up+R_down)/R_down, 0.01 单位 */
#define IM_D12X_BAT_VDIV_DEN     100
#define IM_D12X_BAT_CELLS        2      /* 串联电芯数 */
#define IM_D12X_BAT_CELL_FULL_MV 4200   /* 单节满电 mV */
#define IM_D12X_BAT_CELL_EMPTY_MV 3000  /* 单节空电 mV */

/* 启动/停止 D12x 运行态上报线程 (重复调用安全) */
int  im_d12x_report_start(void);
int  im_d12x_report_stop(void);

#endif /* __IM_D12X_REPORT_H__ */
