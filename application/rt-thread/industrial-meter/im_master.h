/*
 * 便携工业调试仪 —— 主机侧 Modbus RTU 主站
 */

#ifndef __IM_MASTER_H__
#define __IM_MASTER_H__

#include <rtthread.h>
#include "im_protocol.h"

/* 从机实时状态镜像：主机轮询后把整块寄存器缓存到这里 */
struct im_status {
    rt_bool_t   online;                     /* 从机是否在线 */
    rt_uint16_t reg[IM_REG_BLOCK_LEN];      /* 寄存器镜像 */
    rt_uint32_t poll_ok;                    /* 成功轮询次数 */
    rt_uint32_t poll_fail;                  /* 失败轮询次数 */
    rt_tick_t   last_ok_tick;               /* 最后一次成功的时间 */
};

/* 启动/停止轮询线程 (重复调用安全) */
int  im_master_start(void);
int  im_master_stop(void);

/* 取状态镜像指针, 未启动时返回 RT_NULL */
struct im_status *im_master_status(void);

/* 取镜像中的寄存器值, 离线或越界返回 0 */
rt_uint16_t im_master_reg(rt_uint16_t addr);

/* 同步读写 (与轮询线程互斥, 供 shell/UI 调用) */
int  im_master_read(rt_uint16_t addr, rt_uint16_t n, rt_uint16_t *buf);
int  im_master_write(rt_uint16_t addr, rt_uint16_t val);

/* 轮询周期 (ms) */
int  im_master_set_poll_ms(rt_uint32_t ms);

/* 请求写 DO 位图 (异步, 由轮询线程下发, UI 里可直接调用) */
void im_master_set_do(rt_uint16_t mask);

#endif /* __IM_MASTER_H__ */
