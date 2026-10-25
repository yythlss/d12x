/*
 * Copyright (c) 2026, Portable Industrial Debug Tool
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * dbg_log.h - 第 8 步：采样数据写 SD 卡，导出 CSV
 *
 * 设计要点（踩坑前先看这里）：
 *   1. 写文件**绝不在 LVGL 线程里做**。
 *      dbg_log_on_data() 是在 dbg_main.c 的消息分发里被调用的，跑在 LVGL timer
 *      上下文；在里面 open/write 一张慢卡会直接把界面卡住。
 *      所以这里只做「拷进环形缓冲 + 推进写指针」，真正的格式化与落盘交给
 *      独立的 dbglog 线程。
 *   2. 环形缓冲是单生产者（LVGL）单消费者（dbglog）模型，用 volatile 索引即可，
 *      不需要锁。满了就丢最旧的一条 —— 记录宁可丢采样，也不能阻塞 UI。
 *   3. 挂载点 /sdcard 由 board.c 的 mount_table 提供（sd1 -> /sdcard, elm）。
 *      但开机若没插卡，挂载会失败且本工程没开热插拔检测（drv_sdcard.c 整段被
 *      #ifdef 掉），所以 start 时会再试一次挂载，让用户插卡后点一下就能用。
 */

#ifndef __DBG_LOG_H__
#define __DBG_LOG_H__

#include <rtthread.h>

/* 挂载点与设备（与 target/d12x/demo68-nor/board.c 的 mount_table 保持一致） */
#define DBG_LOG_MOUNT_POINT "/sdcard"
#define DBG_LOG_BLK_DEVICE  "sd1"

/* 记录状态 */
#define DBG_LOG_ST_NOCARD   0   /* 挂载失败：没插卡 / 卡坏了 / 文件系统不认 */
#define DBG_LOG_ST_IDLE     1   /* 待机，可以开始 */
#define DBG_LOG_ST_RUNNING  2   /* 正在记录 */

/* 创建记录线程 + 环形缓冲。ui_init() 里调一次即可。 */
int dbg_log_init(void);

/*
 * 开始记录：
 *   - 确保 /sdcard 已挂载（失败返回 -RT_ERROR，状态置 NOCARD）
 *   - 建一个不重名的新文件 /sdcard/dbg_001.csv、dbg_002.csv ...（O_EXCL，绝不覆盖旧数据）
 *   - 写 CSV 表头
 * 返回 RT_EOK 表示已开始。
 */
int dbg_log_start(void);

/* 停止记录：close 文件（close 会把缓存冲进卡里）。 */
int dbg_log_stop(void);

/* 一键切换（UI 按钮用）：返回切换后的状态 DBG_LOG_ST_* */
int dbg_log_toggle(void);

/* 当前状态 DBG_LOG_ST_* */
int dbg_log_state(void);

/* 已写入的数据行数（不含表头） */
rt_uint32_t dbg_log_rows(void);

/* 当前文件名（含挂载点，UI 显示用；未记录时返回 "-"） */
const char *dbg_log_path(void);

/* ---------------------------------------------------------------------------
 * 数据入口：**只能**由 LVGL 线程的消息分发调用（dbg_main.c 的 dbg_msg_dispatch）。
 * 内部只是 memcpy，不碰文件系统，可以放心高频调用。
 * data/len 为 dbg_slave 的 27 字节报文，见 dbg_slave.h 的 SLV_OFF_* 偏移。
 * ------------------------------------------------------------------------- */
void dbg_log_on_data(const rt_uint8_t *data, rt_uint16_t len);

/* ---------------------------------------------------------------------------
 * 回放（第 4 步）：列出 /sdcard 下已录 CSV，并解析指定文件为温度/湿度/光照波形点。
 * 所有文件 I/O 仍在此模块内完成，调用方（UI 线程）只是拿点数组去画，不碰卡。
 * CSV 列（见 dbg_log.c 表头）：t_s,temp_C,humi_pct,lux,ps,acc_x..gyro_z
 * ------------------------------------------------------------------------- */
typedef struct {
    rt_uint32_t t_s;    /* 相对秒 */
    float       temp;   /* ℃   */
    float       humi;   /* %RH */
    float       lux;    /* lx  */
} dbg_replay_point_t;

/* 列出 CSV 文件名（仅文件名，不含路径），names 为 max 个长度 24 的缓冲；返回文件数 */
int dbg_log_list_csv(char (*names)[24], int max);

/* 解析指定文件名（仅文件名，自动拼 /sdcard/），填充 pts（最多 max_pts），返回点数；失败 -1 */
int dbg_log_replay(const char *name, dbg_replay_point_t *pts, int max_pts);

#endif /* __DBG_LOG_H__ */
