/*
 * Modbus RTU 从站协议引擎 (RT-Thread, 与板卡无关)
 *
 * 支持功能码:
 *   0x03 读保持寄存器
 *   0x06 写单个寄存器
 *   0x10 写多个寄存器
 *
 * 帧边界: 用 T3.5 字符静默时间判断 (115200 下约 1.75ms, 取 3 个 tick)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include "im_protocol.h"
#include "im_slave.h"

#define IM_FRAME_MAX        256
#define IM_T35_TICK         3

#define IM_EX_NONE          0x00
#define IM_EX_FUNC          0x01    /* 非法功能码 */
#define IM_EX_ADDR          0x02    /* 非法数据地址 */
#define IM_EX_VALUE         0x03    /* 非法数据值 */

static rt_device_t  g_uart   = RT_NULL;
static rt_base_t    g_de_pin = -1;
static rt_uint8_t   g_addr   = IM_MB_SLAVE_ADDR;
static struct rt_semaphore g_rx_sem;
static rt_uint8_t   g_frame[IM_FRAME_MAX];
static rt_uint8_t   g_txbuf[IM_FRAME_MAX];

/* ---------------- CRC16 (Modbus) ---------------- */
static rt_uint16_t im_crc16(const rt_uint8_t *buf, int len)
{
    rt_uint16_t crc = 0xFFFF;
    int i, j;

    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (rt_uint16_t)((crc >> 1) ^ 0xA001);
            else
                crc = (rt_uint16_t)(crc >> 1);
        }
    }

    return crc;
}

/* ---------------- RS485 发送 ---------------- */
static void im_rs485_send(rt_uint8_t *buf, int len)
{
    rt_uint32_t us;

    rt_pin_write(g_de_pin, PIN_HIGH);
    rt_device_write(g_uart, 0, buf, len);

    /* 等最后一字节 shift out: 10 bit/字节, 再留 1ms 余量 */
    us = (rt_uint32_t)len * 10 * 1000000UL / IM_MB_BAUDRATE;
    rt_thread_mdelay(us / 1000 + 1);

    rt_pin_write(g_de_pin, PIN_LOW);
}

static void im_send_exception(rt_uint8_t fn, rt_uint8_t code)
{
    rt_uint16_t crc;
    int len = 0;

    g_txbuf[len++] = g_addr;
    g_txbuf[len++] = (rt_uint8_t)(fn | 0x80);
    g_txbuf[len++] = code;
    crc = im_crc16(g_txbuf, len);
    g_txbuf[len++] = (rt_uint8_t)(crc & 0xFF);
    g_txbuf[len++] = (rt_uint8_t)(crc >> 8);

    im_rs485_send(g_txbuf, len);
}

/* ---------------- 请求处理 ---------------- */
static void im_handle_read(rt_uint16_t start, rt_uint16_t n)
{
    rt_uint16_t crc;
    int len = 0, i;

    if (n == 0 || n > 0x7D ||
        start < IM_REG_BLOCK_START ||
        (start + n) > (IM_REG_BLOCK_START + IM_REG_BLOCK_LEN)) {
        im_send_exception(0x03, IM_EX_ADDR);
        return;
    }

    g_txbuf[len++] = g_addr;
    g_txbuf[len++] = 0x03;
    g_txbuf[len++] = (rt_uint8_t)(n * 2);
    for (i = 0; i < n; i++) {
        rt_uint16_t v = im_board_get_reg(start + i);
        g_txbuf[len++] = (rt_uint8_t)(v >> 8);
        g_txbuf[len++] = (rt_uint8_t)(v & 0xFF);
    }
    crc = im_crc16(g_txbuf, len);
    g_txbuf[len++] = (rt_uint8_t)(crc & 0xFF);
    g_txbuf[len++] = (rt_uint8_t)(crc >> 8);

    im_rs485_send(g_txbuf, len);
}

static void im_handle_write_one(rt_uint16_t addr, rt_uint16_t val)
{
    rt_uint16_t crc;
    int len = 0;

    if (addr < IM_REG_DO_MAP || addr > IM_REG_CMD) {
        im_send_exception(0x06, IM_EX_ADDR);
        return;
    }
    if (im_board_set_reg(addr, val) != RT_EOK) {
        im_send_exception(0x06, IM_EX_VALUE);
        return;
    }

    g_txbuf[len++] = g_addr;
    g_txbuf[len++] = 0x06;
    g_txbuf[len++] = (rt_uint8_t)(addr >> 8);
    g_txbuf[len++] = (rt_uint8_t)(addr & 0xFF);
    g_txbuf[len++] = (rt_uint8_t)(val >> 8);
    g_txbuf[len++] = (rt_uint8_t)(val & 0xFF);
    crc = im_crc16(g_txbuf, len);
    g_txbuf[len++] = (rt_uint8_t)(crc & 0xFF);
    g_txbuf[len++] = (rt_uint8_t)(crc >> 8);

    im_rs485_send(g_txbuf, len);
}

static void im_handle_write_multi(rt_uint16_t start, rt_uint16_t n,
                                  const rt_uint8_t *data)
{
    rt_uint16_t crc, v;
    int len = 0, i;

    if (n == 0 || n > 0x7B ||
        start < IM_REG_DO_MAP ||
        (start + n) > (IM_REG_CMD + 1)) {
        im_send_exception(0x10, IM_EX_ADDR);
        return;
    }

    for (i = 0; i < n; i++) {
        v = (rt_uint16_t)((data[i * 2] << 8) | data[i * 2 + 1]);
        if (im_board_set_reg(start + i, v) != RT_EOK) {
            im_send_exception(0x10, IM_EX_VALUE);
            return;
        }
    }

    g_txbuf[len++] = g_addr;
    g_txbuf[len++] = 0x10;
    g_txbuf[len++] = (rt_uint8_t)(start >> 8);
    g_txbuf[len++] = (rt_uint8_t)(start & 0xFF);
    g_txbuf[len++] = (rt_uint8_t)(n >> 8);
    g_txbuf[len++] = (rt_uint8_t)(n & 0xFF);
    crc = im_crc16(g_txbuf, len);
    g_txbuf[len++] = (rt_uint8_t)(crc & 0xFF);
    g_txbuf[len++] = (rt_uint8_t)(crc >> 8);

    im_rs485_send(g_txbuf, len);
}

static void im_handle_frame(rt_uint8_t *f, int len)
{
    rt_uint16_t crc_recv, crc_calc;
    rt_uint16_t start, n;
    rt_uint8_t  fn;

    if (f[0] != g_addr)
        return;                             /* 不是本机的帧, 忽略 */

    crc_recv = (rt_uint16_t)(f[len - 2] | (f[len - 1] << 8));
    crc_calc = im_crc16(f, len - 2);
    if (crc_recv != crc_calc) {
        im_board_set_reg(IM_REG_CRC_ERR_CNT,
                         im_board_get_reg(IM_REG_CRC_ERR_CNT) + 1);
        return;
    }

    fn = f[1];
    if (fn == 0x03) {
        start = (rt_uint16_t)((f[2] << 8) | f[3]);
        n     = (rt_uint16_t)((f[4] << 8) | f[5]);
        if (len != 8)
            return;
        im_handle_read(start, n);
    } else if (fn == 0x06) {
        start = (rt_uint16_t)((f[2] << 8) | f[3]);
        if (len != 8)
            return;
        im_handle_write_one(start, (rt_uint16_t)((f[4] << 8) | f[5]));
    } else if (fn == 0x10) {
        start = (rt_uint16_t)((f[2] << 8) | f[3]);
        n     = (rt_uint16_t)((f[4] << 8) | f[5]);
        if (len != (int)(9 + n * 2) || f[6] != n * 2)
            return;
        im_handle_write_multi(start, n, &f[7]);
    } else {
        im_send_exception(fn, IM_EX_FUNC);
    }

    /* 有效帧计数 */
    im_board_set_reg(IM_REG_FRAME_CNT, im_board_get_reg(IM_REG_FRAME_CNT) + 1);
}

/* ---------------- 接收线程 ---------------- */
static rt_err_t im_rx_ind(rt_device_t dev, rt_size_t size)
{
    rt_sem_release(&g_rx_sem);
    return RT_EOK;
}

static void im_slave_entry(void *param)
{
    rt_uint8_t ch;
    rt_tick_t  last;
    int len;

    while (1) {
        /* 等一帧的首字节 */
        while (rt_device_read(g_uart, -1, &ch, 1) != 1)
            rt_sem_take(&g_rx_sem, RT_WAITING_FOREVER);

        len    = 0;
        g_frame[len++] = ch;
        last   = rt_tick_get();

        /* 收完剩余字节, T3.5 静默认为帧结束 */
        while (len < IM_FRAME_MAX) {
            if (rt_device_read(g_uart, -1, &ch, 1) == 1) {
                g_frame[len++] = ch;
                last = rt_tick_get();
                continue;
            }
            if ((rt_tick_get() - last) >= IM_T35_TICK)
                break;
            rt_sem_take(&g_rx_sem, IM_T35_TICK);
        }

        if (len >= 4 && len <= IM_FRAME_MAX)
            im_handle_frame(g_frame, len);
    }
}

int im_slave_init(const char *uart_name, rt_base_t de_pin, rt_uint8_t addr)
{
    rt_thread_t tid;

    g_addr   = addr;
    g_de_pin = de_pin;

    rt_pin_mode(g_de_pin, PIN_MODE_OUTPUT);
    rt_pin_write(g_de_pin, PIN_LOW);

    g_uart = rt_device_find(uart_name);
    if (!g_uart) {
        rt_kprintf("im_slave: find %s failed\n", uart_name);
        return -RT_ERROR;
    }

    rt_device_open(g_uart, RT_DEVICE_FLAG_INT_RX);
    rt_device_set_rx_indicate(g_uart, im_rx_ind);
    rt_sem_init(&g_rx_sem, "im_rx", 0, RT_IPC_FLAG_FIFO);

    im_board_init();

    tid = rt_thread_create("im_slave", im_slave_entry, RT_NULL,
                           2048, 10, 10);
    if (!tid)
        return -RT_ERROR;

    rt_thread_startup(tid);
    rt_kprintf("im_slave: %s, %d bps, addr %d\n",
               uart_name, IM_MB_BAUDRATE, addr);

    return RT_EOK;
}
