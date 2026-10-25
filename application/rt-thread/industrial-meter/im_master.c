/*
 * 便携工业调试仪 —— 主机侧 Modbus RTU 主站
 *
 * 负责: 轮询从机寄存器块 -> 状态镜像; 对外提供同步读写接口给 shell / LVGL UI。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <errno.h>

#include <modbus.h>
#include <modbus-rtu.h>
#include <aic_hal_gpio.h>

#define LOG_TAG         "im.master"
#define LOG_LVL         LOG_LVL_INFO
#include <ulog.h>

#include "im_protocol.h"
#include "im_master.h"

/* ============ 硬件连接: 按实际接线修改 ============ */
#define IM_RS485_UART_NAME      "/dev/uart0"    /* demo68: PA.0 TX / PA.1 RX */
#define IM_RS485_DE_PIN_NAME    "PC.7"          /* RS485 收发方向 (DE/RE) */
#define IM_POLL_MS_DEFAULT      200             /* 默认轮询周期 */
#define IM_OFFLINE_TIMEOUT_MS   3000            /* 超时判定离线 */

#define IM_RS485_DE_PIN         hal_gpio_name2pin(IM_RS485_DE_PIN_NAME)

#define IM_MS2TICK(ms)          ((rt_tick_t)((ms) * RT_TICK_PER_SECOND / 1000))

static modbus_t          *g_ctx     = RT_NULL;
static struct im_status   g_status;
static rt_mutex_t         g_lock    = RT_NULL;
static rt_thread_t        g_tid     = RT_NULL;
static rt_uint32_t        g_poll_ms = IM_POLL_MS_DEFAULT;
static rt_bool_t          g_running = RT_FALSE;
static rt_uint16_t        g_do_req   = 0;        /* UI 侧请求的 DO 值 */
static rt_bool_t          g_do_dirty = RT_FALSE; /* 有待下发的 DO 请求 */

struct im_status *im_master_status(void)
{
    return g_ctx ? &g_status : RT_NULL;
}

rt_uint16_t im_master_reg(rt_uint16_t addr)
{
    if (!g_ctx || g_status.online == RT_FALSE)
        return 0;
    if (addr < IM_REG_BLOCK_START || addr >= IM_REG_BLOCK_START + IM_REG_BLOCK_LEN)
        return 0;

    return g_status.reg[addr - IM_REG_BLOCK_START];
}

int im_master_read(rt_uint16_t addr, rt_uint16_t n, rt_uint16_t *buf)
{
    int ret;

    if (!g_ctx || !buf)
        return -RT_ERROR;

    rt_mutex_take(g_lock, RT_WAITING_FOREVER);
    ret = modbus_read_registers(g_ctx, addr, n, buf);
    rt_mutex_release(g_lock);

    return ret;
}

int im_master_write(rt_uint16_t addr, rt_uint16_t val)
{
    int ret;

    if (!g_ctx)
        return -RT_ERROR;

    rt_mutex_take(g_lock, RT_WAITING_FOREVER);
    ret = modbus_write_register(g_ctx, addr, val);
    rt_mutex_release(g_lock);

    return ret;
}

/* 多寄存器连续写 (FC16, modbus_write_registers)，供 D12x 自身运行态批量上报用。
 * buf 内容按 Modbus 大端按字拷贝，调用方自行保证 addr+n 不越界从站保持区。 */
int im_master_write_regs(rt_uint16_t addr, rt_uint16_t n, const rt_uint16_t *buf)
{
    int ret;

    if (!g_ctx || !buf || n == 0)
        return -RT_ERROR;

    rt_mutex_take(g_lock, RT_WAITING_FOREVER);
    ret = modbus_write_registers(g_ctx, addr, n, (rt_uint16_t *)buf);
    rt_mutex_release(g_lock);

    return ret;
}

int im_master_set_poll_ms(rt_uint32_t ms)
{
    if (ms < 10 || ms > 10000)
        return -RT_ERROR;

    g_poll_ms = ms;
    return RT_EOK;
}

void im_master_set_do(rt_uint16_t mask)
{
    g_do_req   = mask;
    g_do_dirty = RT_TRUE;
}

static void im_poll_entry(void *param)
{
    int n;

    while (g_running) {
        /* 先处理 UI 下发的 DO 请求 */
        if (g_do_dirty) {
            g_do_dirty = RT_FALSE;
            if (rt_mutex_take(g_lock, IM_MS2TICK(500)) == RT_EOK) {
                modbus_write_register(g_ctx, IM_REG_DO_MAP, g_do_req);
                rt_mutex_release(g_lock);
            }
        }

        n = -1;
        if (rt_mutex_take(g_lock, IM_MS2TICK(1000)) == RT_EOK) {
            n = modbus_read_registers(g_ctx,
                                      IM_REG_BLOCK_START,
                                      IM_REG_BLOCK_LEN,
                                      g_status.reg);
            rt_mutex_release(g_lock);
        }

        if (n == IM_REG_BLOCK_LEN) {
            g_status.poll_ok++;
            g_status.last_ok_tick = rt_tick_get();
            if (!g_status.online) {
                g_status.online = RT_TRUE;
                LOG_I("slave online: model 0x%04X, fw 0x%04X",
                      g_status.reg[IM_REG_DEV_MODEL],
                      g_status.reg[IM_REG_FW_VER]);
            }
        } else {
            g_status.poll_fail++;
            if (g_status.online &&
                (rt_tick_get() - g_status.last_ok_tick) > IM_MS2TICK(IM_OFFLINE_TIMEOUT_MS)) {
                g_status.online = RT_FALSE;
                LOG_E("slave offline: %s", modbus_strerror(errno));
            }
        }

        rt_thread_mdelay(g_poll_ms);
    }
}

int im_master_start(void)
{
    if (g_ctx) {
        LOG_I("already started");
        return RT_EOK;
    }

    g_lock = rt_mutex_create("im_mb", RT_IPC_FLAG_FIFO);
    if (!g_lock)
        return -RT_ENOMEM;

    /* 注意顺序: 先设引脚模式, 再设 RS485 模式, 最后设方向控制引脚 */
    rt_pin_mode(IM_RS485_DE_PIN, PIN_MODE_OUTPUT);

    g_ctx = modbus_new_rtu(IM_RS485_UART_NAME, IM_MB_BAUDRATE, 'N', 8, 1);
    if (!g_ctx) {
        LOG_E("new rtu failed");
        goto _fail_ctx;
    }

    modbus_rtu_set_serial_mode(g_ctx, MODBUS_RTU_RS485);
    modbus_rtu_set_rts(g_ctx, IM_RS485_DE_PIN, MODBUS_RTU_RTS_UP);
    modbus_set_slave(g_ctx, IM_MB_SLAVE_ADDR);
    modbus_set_response_timeout(g_ctx, 0, 300000);

    if (modbus_connect(g_ctx) == -1) {
        LOG_E("connect %s failed: %s", IM_RS485_UART_NAME, modbus_strerror(errno));
        goto _fail_connect;
    }

    rt_memset(&g_status, 0, sizeof(g_status));
    g_status.last_ok_tick = rt_tick_get();
    g_running = RT_TRUE;

    g_tid = rt_thread_create("im_poll",
                             im_poll_entry, RT_NULL,
                             2048, 12, 10);
    if (!g_tid) {
        g_running = RT_FALSE;
        goto _fail_thread;
    }

    rt_thread_startup(g_tid);
    LOG_I("modbus master on %s, %d bps, slave %d",
          IM_RS485_UART_NAME, IM_MB_BAUDRATE, IM_MB_SLAVE_ADDR);

    return RT_EOK;

_fail_thread:
    modbus_close(g_ctx);
_fail_connect:
    modbus_free(g_ctx);
    g_ctx = RT_NULL;
_fail_ctx:
    rt_mutex_delete(g_lock);
    g_lock = RT_NULL;
    return -RT_ERROR;
}

int im_master_stop(void)
{
    if (!g_ctx)
        return RT_EOK;

    g_running = RT_FALSE;
    if (g_tid) {
        rt_thread_mdelay(g_poll_ms + 50);
        g_tid = RT_NULL;
    }

    modbus_close(g_ctx);
    modbus_free(g_ctx);
    g_ctx = RT_NULL;
    g_status.online = RT_FALSE;

    rt_mutex_delete(g_lock);
    g_lock = RT_NULL;
    LOG_I("master stopped");

    return RT_EOK;
}

/* ==================== shell 调试命令 ==================== */

static void im_dump(void)
{
    struct im_status *s = &g_status;

    if (!g_ctx) {
        rt_kprintf("master not started\n");
        return;
    }

    rt_kprintf("slave      : %s (%s)\n",
               s->online ? "online" : "offline", IM_RS485_UART_NAME);
    rt_kprintf("model/fw   : 0x%04X / 0x%04X\n",
               im_master_reg(IM_REG_DEV_MODEL), im_master_reg(IM_REG_FW_VER));
    rt_kprintf("uptime     : %u s\n",
               (rt_uint32_t)(im_master_reg(IM_REG_UPTIME_HI) << 16) |
               im_master_reg(IM_REG_UPTIME_LO));
    rt_kprintf("heartbeat  : %u   err: 0x%04X\n",
               im_master_reg(IM_REG_HEARTBEAT), im_master_reg(IM_REG_ERR_CODE));
    rt_kprintf("AI (mV)    : %u %u %u %u\n",
               im_master_reg(IM_REG_AI0_MV), im_master_reg(IM_REG_AI1_MV),
               im_master_reg(IM_REG_AI2_MV), im_master_reg(IM_REG_AI3_MV));
    rt_kprintf("DI         : 0x%02X  (UP:%d DOWN:%d LEFT:%d RIGHT:%d)\n",
               im_master_reg(IM_REG_DI_MAP),
               !!(im_master_reg(IM_REG_DI_MAP) & IM_DI_KEY_UP),
               !!(im_master_reg(IM_REG_DI_MAP) & IM_DI_KEY_DOWN),
               !!(im_master_reg(IM_REG_DI_MAP) & IM_DI_KEY_LEFT),
               !!(im_master_reg(IM_REG_DI_MAP) & IM_DI_KEY_RIGHT));
    rt_kprintf("DO         : 0x%02X\n", im_master_reg(IM_REG_DO_MAP));
    rt_kprintf("sample/avg : %u ms / %u\n",
               im_master_reg(IM_REG_SAMPLE_MS), im_master_reg(IM_REG_AI_AVG));
    rt_kprintf("poll       : ok %u, fail %u, %u ms\n",
               s->poll_ok, s->poll_fail, g_poll_ms);
}
MSH_CMD_EXPORT(im_dump, dump industrial meter slave status);

static int im_rd(int argc, char **argv)
{
    rt_uint16_t addr, n, buf[64];
    int i, ret;

    if (argc != 3) {
        rt_kprintf("usage: im_rd <addr(hex)> <n>\n");
        return -1;
    }

    addr = (rt_uint16_t)strtoul(argv[1], RT_NULL, 16);
    n    = (rt_uint16_t)strtoul(argv[2], RT_NULL, 10);
    if (n == 0 || n > 64)
        return -1;

    ret = im_master_read(addr, n, buf);
    if (ret <= 0) {
        rt_kprintf("read failed: %s\n", modbus_strerror(errno));
        return -1;
    }

    for (i = 0; i < ret; i++)
        rt_kprintf("[0x%04X] = 0x%04X (%u)\n", addr + i, buf[i], buf[i]);

    return 0;
}
MSH_CMD_EXPORT(im_rd, im_rd <addr> <n> - read holding registers);

static int im_wr(int argc, char **argv)
{
    rt_uint16_t addr, val;

    if (argc != 3) {
        rt_kprintf("usage: im_wr <addr(hex)> <val(dec|hex)>\n");
        return -1;
    }

    addr = (rt_uint16_t)strtoul(argv[1], RT_NULL, 16);
    val  = (rt_uint16_t)strtoul(argv[2], RT_NULL, 0);

    if (im_master_write(addr, val) != 1) {
        rt_kprintf("write failed: %s\n", modbus_strerror(errno));
        return -1;
    }

    rt_kprintf("write [0x%04X] = 0x%04X ok\n", addr, val);
    return 0;
}
MSH_CMD_EXPORT(im_wr, im_wr <addr> <val> - write one holding register);

static int im_do(int argc, char **argv)
{
    rt_uint16_t map;

    if (argc != 2) {
        rt_kprintf("usage: im_do <mask>  (bit0=LED_R bit1=LED_B)\n");
        return -1;
    }

    map = (rt_uint16_t)strtoul(argv[1], RT_NULL, 0);
    if (im_master_write(IM_REG_DO_MAP, map) != 1) {
        rt_kprintf("write DO failed\n");
        return -1;
    }

    rt_kprintf("DO = 0x%02X\n", map);
    return 0;
}
MSH_CMD_EXPORT(im_do, im_do <mask> - set slave digital output);

static int im_poll(int argc, char **argv)
{
    rt_uint32_t ms;

    if (argc != 2) {
        rt_kprintf("usage: im_poll <ms>\n");
        return -1;
    }

    ms = strtoul(argv[1], RT_NULL, 10);
    if (im_master_set_poll_ms(ms) != RT_EOK)
        return -1;

    rt_kprintf("poll period = %u ms\n", g_poll_ms);
    return 0;
}
MSH_CMD_EXPORT(im_poll, im_poll <ms> - set poll period);
