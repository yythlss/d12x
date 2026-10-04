/*
 * 便携工业调试仪 —— 从机侧 (STM32F407 星火一号, RT-Thread)
 *
 * im_slave.c        : Modbus RTU 从站协议引擎, 与板卡无关
 * im_slave_board.c  : 板级后端 (LED/KEY/ADC), 提供寄存器读写实现
 */

#ifndef __IM_SLAVE_H__
#define __IM_SLAVE_H__

#include <rtthread.h>
#include "im_protocol.h"

/* ---- 从站引擎 ---- */
int im_slave_init(const char *uart_name, rt_base_t de_pin, rt_uint8_t addr);

/* ---- 板级后端 (由 im_slave_board_xxx.c 实现) ---- */
void        im_board_init(void);
rt_uint16_t im_board_get_reg(rt_uint16_t addr);
int         im_board_set_reg(rt_uint16_t addr, rt_uint16_t val);

#endif /* __IM_SLAVE_H__ */
