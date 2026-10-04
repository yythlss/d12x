/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 便携工业调试仪 —— 主机/从机共用协议定义
 *
 * 链路 : RS485 / Modbus RTU / 115200 8N1
 * 主机 : d12x demo68-nand (RT-Thread + LVGL)
 * 从机 : STM32F407 星火一号 (RT-Thread)
 *
 * 本文件在主机工程和从机工程各存一份，修改时必须同步。
 */

#ifndef __IM_PROTOCOL_H__
#define __IM_PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 链路参数 ---------------- */
#define IM_MB_SLAVE_ADDR        1           /* 从机地址 */
#define IM_MB_BAUDRATE          115200

/* ---------------- 保持寄存器地址 (协议地址, 即 4xxxx 区) ---------------- */

/* 只读：设备信息 */
#define IM_REG_DEV_MODEL        0x0000      /* 型号码, 固定 0xA101 */
#define IM_REG_FW_VER           0x0001      /* 固件版本, BCD: 0x0100 = v1.00 */
#define IM_REG_UPTIME_LO        0x0002      /* 上电时长低 16 位 (s) */
#define IM_REG_UPTIME_HI        0x0003      /* 上电时长高 16 位 (s) */
#define IM_REG_HEARTBEAT        0x0004      /* 心跳, 每轮采样 +1, 用于判在线 */
#define IM_REG_ERR_CODE         0x0005      /* 错误码, 0 = 正常 */
#define IM_REG_FRAME_CNT        0x0006      /* 收到的有效帧计数 */
#define IM_REG_CRC_ERR_CNT      0x0007      /* CRC 错误帧计数 */

/* 只读：采集数据 */
#define IM_REG_AI0_MV           0x0010      /* 模拟量输入 0, 单位 mV */
#define IM_REG_AI1_MV           0x0011
#define IM_REG_AI2_MV           0x0012
#define IM_REG_AI3_MV           0x0013
#define IM_REG_DI_MAP           0x0014      /* 数字量输入位图 */

/* 读写：控制 */
#define IM_REG_DO_MAP           0x0020      /* 数字量输出位图 */
#define IM_REG_SAMPLE_MS        0x0021      /* 采样周期 (ms), 默认 100 */
#define IM_REG_AI_AVG           0x0022      /* AI 平均次数: 1/4/8/16 */
#define IM_REG_VREF_MV          0x0023      /* ADC 参考电压 (mV), 校准用 */
#define IM_REG_CMD              0x0024      /* 命令寄存器, 写后自动清零 */

/* 一次性轮询的寄存器块 */
#define IM_REG_BLOCK_START      IM_REG_DEV_MODEL
#define IM_REG_BLOCK_LEN        (IM_REG_CMD - IM_REG_BLOCK_START + 1)

/* ---------------- 位定义 ---------------- */

/* DI_MAP: 星火一号板载 4 个按键 (按下为 1) */
#define IM_DI_KEY_UP            (1U << 0)   /* KEY_UP   PC5 */
#define IM_DI_KEY_DOWN          (1U << 1)   /* KEY_DOWN PC1 */
#define IM_DI_KEY_LEFT          (1U << 2)   /* KEY_LEFT PC0 */
#define IM_DI_KEY_RIGHT         (1U << 3)   /* KEY_RIGHT PC4 */

/* DO_MAP: 星火一号板载 2 个 LED (写 1 点亮) */
#define IM_DO_LED_R             (1U << 0)   /* LED_R PF12 */
#define IM_DO_LED_B             (1U << 1)   /* LED_B PF11 */
/* bit2 ~ bit7 预留给扩展继电器模块 */

/* ---------------- 命令字 (IM_REG_CMD) ---------------- */
#define IM_CMD_NONE             0
#define IM_CMD_CLEAR_ERR        1           /* 清错误计数 */
#define IM_CMD_SAVE_CFG         2           /* 保存参数到 Flash */
#define IM_CMD_SOFT_RESET       3           /* 软复位从机 */
#define IM_CMD_ADC_CAL          4           /* ADC 自校准 */

/* ---------------- 错误码 (IM_REG_ERR_CODE) ---------------- */
#define IM_ERR_NONE             0
#define IM_ERR_ADC              1           /* ADC 采集失败 */
#define IM_ERR_PARAM            2           /* 参数越界 */
#define IM_ERR_STORE            3           /* 参数存储失败 */

#define IM_DEV_MODEL            0xA101
#define IM_FW_VER               0x0100

#ifdef __cplusplus
}
#endif

#endif /* __IM_PROTOCOL_H__ */
