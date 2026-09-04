/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <aic_core.h>
#include <hal_i2c.h>

#define I2C_MASTER_DEV_NAME    "i2c1"
#define I2C_SLAVE_DEV_NAME     "i2c0"
#define I2C_SLAVE_ADDR_7BIT    0x0A
#define I2C_SLAVE_ADDR_10BIT   0x155
#define MAX_TEST_LEN           256

#define DEFAULT_SPEED_HZ       400000
#define DEFAULT_STUCK_CYCLES   400

#define SHORT_DELAY_MS         2
#define MEDIUM_DELAY_MS        5
#define LONG_DELAY_MS          30

#define CUR_SLAVE_ADDR()       (g_slave_10bit ? I2C_SLAVE_ADDR_10BIT : I2C_SLAVE_ADDR_7BIT)
#define STREQ(a, b)            (rt_strncmp((a), (b), rt_strlen(b) + 1) == 0)

static const uint32_t g_speed_table[] = {100000, 200000, 400000};
static const uint32_t g_stuck_table[]  = {50, 100, 200, 400, 800, 1000, 2000};
static const uint32_t g_len_table[]    = {1, 2, 4, 8, 16, 32, 64, 128, 255};

#define SPEED_CNT  (sizeof(g_speed_table) / sizeof(g_speed_table[0]))
#define STUCK_CNT  (sizeof(g_stuck_table)  / sizeof(g_stuck_table[0]))
#define LEN_CNT    (sizeof(g_len_table)    / sizeof(g_len_table[0]))

struct aic_i2c_bus_local {
    struct rt_i2c_bus_device bus;
    aic_i2c_ctrl aic_bus;
    struct rt_completion cmd_complete;
};

#define BUS_TO_CTRL(bus_ptr)  (&((struct aic_i2c_bus_local *)(bus_ptr))->aic_bus)
#define DEV_TO_BUS(dev_ptr)   (rt_container_of((dev_ptr), struct rt_i2c_bus_device, parent))

static struct rt_i2c_bus_device *g_master_bus = RT_NULL;
static rt_device_t g_slave_dev = RT_NULL;
static struct aic_i2c_slave_info g_slave_info = {0};

static volatile uint8_t g_slave_buf[MAX_TEST_LEN] = {0};
static volatile uint32_t g_buf_offset = 0;
static volatile uint32_t g_slave_buf_len = 0;
static volatile uint32_t g_read_active = 0;

static uint32_t g_master_10bit = 0;
static uint32_t g_slave_10bit = 0;
static uint32_t g_cur_speed = 0;
static uint32_t g_cur_stuck = 0;

static uint8_t g_tx_buf[MAX_TEST_LEN] = {0};
static uint8_t g_rx_buf[MAX_TEST_LEN] = {0};

/* ---- helpers ---- */

static aic_i2c_ctrl *master_ctrl(void)
{
    if (!g_master_bus) {
        return RT_NULL;
    }
    return BUS_TO_CTRL(g_master_bus);
}

static aic_i2c_ctrl *slave_ctrl(void)
{
    if (!g_slave_dev) {
        return RT_NULL;
    }
    return BUS_TO_CTRL(DEV_TO_BUS(g_slave_dev));
}

static int i2c_slave_cb(void *arg)
{
    struct slave_param *p;
    uint8_t *data;
    rt_base_t level;

    p = (struct slave_param *)arg;
    if (!p) {
        return 0;
    }

    switch (p->cmd) {
    case I2C_SLAVE_WRITE_REQUESTED:
        level = rt_hw_interrupt_disable();
        g_buf_offset = 0;
        g_read_active = 0;
        rt_hw_interrupt_enable(level);
        break;
    case I2C_SLAVE_WRITE_RECEIVED:
        data = (uint8_t *)p->arg;
        if (data) {
            level = rt_hw_interrupt_disable();
            if (g_buf_offset < g_slave_buf_len) {
                g_slave_buf[g_buf_offset++] = *data;
            }
            rt_hw_interrupt_enable(level);
        }
        break;
    case I2C_SLAVE_READ_REQUESTED:
        level = rt_hw_interrupt_disable();
        if (!g_read_active) {
            g_buf_offset = 0;
            g_read_active = 1;
        }
        rt_hw_interrupt_enable(level);
        data = (uint8_t *)p->arg;
        if (data) {
            level = rt_hw_interrupt_disable();
            if (g_buf_offset < g_slave_buf_len) {
                *data = g_slave_buf[g_buf_offset++];
            } else {
                *data = 0xFF;
            }
            rt_hw_interrupt_enable(level);
        }
        break;
    case I2C_SLAVE_READ_PROCESSED:
        break;
    case I2C_SLAVE_STOP:
        level = rt_hw_interrupt_disable();
        g_read_active = 0;
        rt_hw_interrupt_enable(level);
        break;
    default:
        break;
    }
    return 0;
}

/* ---- Config helpers (disable->modify->enable, always call HAL) ---- */

static int set_master_speed(uint32_t hz)
{
    rt_err_t r;
    if (!g_master_bus) {
        return -RT_ERROR;
    }
    r = rt_i2c_control(g_master_bus, RT_I2C_DEV_CTRL_CLK, hz);
    if (r != RT_EOK) {
        rt_kprintf("[ERR] set speed %u fail %d\n", hz, r);
        return r;
    }
    g_cur_speed = hz;
    return RT_EOK;
}

static int set_master_10bit(int on)
{
    aic_i2c_ctrl *c;
    rt_base_t level;

    c = master_ctrl();
    if (!c) {
        return -RT_ERROR;
    }
    hal_i2c_module_disable(c);
    c->addr_bit = on ? I2C_10BIT_ADDR : I2C_7BIT_ADDR;
    hal_i2c_master_10bit_addr(c);
    hal_i2c_module_enable(c);
    level = rt_hw_interrupt_disable();
    g_master_10bit = on ? 1 : 0;
    rt_hw_interrupt_enable(level);
    rt_thread_mdelay(MEDIUM_DELAY_MS);
    return RT_EOK;
}

static int set_master_stuck(uint32_t cycles)
{
    aic_i2c_ctrl *c;

    c = master_ctrl();
    if (!c) {
        return -RT_ERROR;
    }
    hal_i2c_module_disable(c);
    c->stuck_time = cycles;
    hal_i2c_set_sda_scl_stuck_time(c);
    hal_i2c_module_enable(c);
    g_cur_stuck = cycles;
    return RT_EOK;
}

static int set_slave_addr(uint32_t addr)
{
    aic_i2c_ctrl *c;

    c = slave_ctrl();
    if (!c) {
        return -RT_ERROR;
    }
    hal_i2c_module_disable(c);
    c->slave_addr = addr;
    hal_i2c_slave_own_addr(c, addr);
    hal_i2c_module_enable(c);
    return RT_EOK;
}

static int set_slave_10bit(int on)
{
    aic_i2c_ctrl *c;
    rt_base_t level;

    c = slave_ctrl();
    if (!c) {
        return -RT_ERROR;
    }
    hal_i2c_module_disable(c);
    c->addr_bit = on ? I2C_10BIT_ADDR : I2C_7BIT_ADDR;
    hal_i2c_slave_10bit_addr(c);
    hal_i2c_module_enable(c);
    level = rt_hw_interrupt_disable();
    g_slave_10bit = on ? 1 : 0;
    rt_hw_interrupt_enable(level);
    rt_thread_mdelay(MEDIUM_DELAY_MS);
    return RT_EOK;
}

static int set_slave_stuck(uint32_t cycles)
{
    aic_i2c_ctrl *c;

    c = slave_ctrl();
    if (!c) {
        return -RT_ERROR;
    }
    hal_i2c_module_disable(c);
    c->stuck_time = cycles;
    hal_i2c_set_sda_scl_stuck_time(c);
    hal_i2c_module_enable(c);
    return RT_EOK;
}

static void slave_buf_reset(uint32_t len)
{
    rt_base_t level;
    uint32_t i;
    uint32_t safe_len;

    safe_len = (len > MAX_TEST_LEN) ? MAX_TEST_LEN : len;
    level = rt_hw_interrupt_disable();
    for (i = 0; i < MAX_TEST_LEN; i++) {
        g_slave_buf[i] = 0;
    }
    g_buf_offset = 0;
    g_read_active = 0;
    g_slave_buf_len = safe_len;
    rt_hw_interrupt_enable(level);
}

static void slave_buf_fill(const uint8_t *src, uint32_t len)
{
    rt_base_t level;
    uint32_t i;
    uint32_t safe_len;

    safe_len = (len > MAX_TEST_LEN) ? MAX_TEST_LEN : len;
    level = rt_hw_interrupt_disable();
    for (i = 0; i < safe_len; i++) {
        g_slave_buf[i] = src[i];
    }
    g_buf_offset = 0;
    g_read_active = 0;
    g_slave_buf_len = safe_len;
    rt_hw_interrupt_enable(level);
}

/* ---- Init / Deinit ---- */

static int cmd_init(void)
{
    rt_err_t r;
    aic_i2c_ctrl *mc, *sc;

    g_slave_dev = rt_device_find(I2C_SLAVE_DEV_NAME);
    if (!g_slave_dev) {
        rt_kprintf("[ERR] slave %s not found\n", I2C_SLAVE_DEV_NAME);
        return -RT_ERROR;
    }
    r = rt_device_open(g_slave_dev, RT_DEVICE_FLAG_RDWR);
    if (r != RT_EOK) {
        rt_kprintf("[ERR] open slave %d\n", r);
        return r;
    }

    slave_buf_reset(MAX_TEST_LEN);
    g_slave_info.slave_cb = i2c_slave_cb;
    g_slave_info.callback_param = RT_NULL;

    r = rt_device_control(g_slave_dev, RT_I2C_DEV_SLAVE_CONFIG, &g_slave_info);
    if (r != RT_EOK) {
        rt_kprintf("[ERR] slave cfg %d\n", r);
        rt_device_close(g_slave_dev);
        g_slave_dev = RT_NULL;
        return r;
    }

    g_master_bus = rt_i2c_bus_device_find(I2C_MASTER_DEV_NAME);
    if (!g_master_bus) {
        rt_kprintf("[ERR] master %s not found\n", I2C_MASTER_DEV_NAME);
        rt_device_close(g_slave_dev);
        g_slave_dev = RT_NULL;
        return -RT_ERROR;
    }

    mc = master_ctrl();
    sc = slave_ctrl();
    if (!mc || !sc) {
        rt_kprintf("[ERR] get ctrl fail\n");
        return -RT_ERROR;
    }

    {
        rt_base_t level = rt_hw_interrupt_disable();
        g_master_10bit = (mc->addr_bit == I2C_10BIT_ADDR) ? 1 : 0;
        g_slave_10bit  = (sc->addr_bit == I2C_10BIT_ADDR) ? 1 : 0;
        rt_hw_interrupt_enable(level);
    }

    r = set_master_speed(DEFAULT_SPEED_HZ);
    if (r != RT_EOK) {
        rt_kprintf("[ERR] init speed fail\n");
        return r;
    }
    r = set_master_stuck(DEFAULT_STUCK_CYCLES);
    if (r != RT_EOK) {
        rt_kprintf("[ERR] init master stuck fail\n");
        return r;
    }
    r = set_slave_stuck(DEFAULT_STUCK_CYCLES);
    if (r != RT_EOK) {
        rt_kprintf("[ERR] init slave stuck fail\n");
        return r;
    }

    if (g_slave_10bit) {
        set_slave_addr(I2C_SLAVE_ADDR_10BIT);
    } else {
        set_slave_addr(I2C_SLAVE_ADDR_7BIT);
    }

    rt_kprintf("[OK] init: master=%s slave=%s addr=0x%02X speed=%u stuck=%u\n",
               g_master_10bit ? "10bit" : "7bit",
               g_slave_10bit  ? "10bit" : "7bit",
               CUR_SLAVE_ADDR(), DEFAULT_SPEED_HZ, DEFAULT_STUCK_CYCLES);
    return RT_EOK;
}

static int cmd_deinit(void)
{
    if (g_slave_dev) {
        rt_device_close(g_slave_dev);
        g_slave_dev = RT_NULL;
    }
    g_master_bus = RT_NULL;
    rt_kprintf("[OK] resources released\n");
    return RT_EOK;
}

/* ---- Core loopback test (does NOT change addr mode) ---- */

static int run_core_len(uint16_t addr, uint32_t len, const char *tag, int verbose)
{
    struct rt_i2c_msg wmsg, rmsg;
    rt_err_t r;
    uint32_t i;
    uint16_t flags_10;
    rt_base_t level;

    if (!g_master_bus || !g_slave_dev) {
        rt_kprintf("[ERR] run init first\n");
        return -RT_ERROR;
    }
    if (len == 0 || len > MAX_TEST_LEN) {
        rt_kprintf("[ERR] len %u out of range [1,%d]\n", len, MAX_TEST_LEN);
        return -RT_ERROR;
    }

    for (i = 0; i < len; i++) {
        g_tx_buf[i] = (uint8_t)(i & 0xFF);
    }
    rt_memset(g_rx_buf, 0, len);

    level = rt_hw_interrupt_disable();
    flags_10 = g_master_10bit ? RT_I2C_ADDR_10BIT : 0;
    rt_hw_interrupt_enable(level);

    slave_buf_reset(len);

    if (verbose) {
        rt_kprintf("--- %s | speed=%u stuck=%u addr=0x%X(m=%s s=%s) len=%u ---\n",
                   tag, g_cur_speed, g_cur_stuck, addr,
                   (flags_10 & RT_I2C_ADDR_10BIT) ? "10b" : "7b",
                   g_slave_10bit ? "10b" : "7b", len);
    }

    wmsg.addr  = addr;
    wmsg.flags = flags_10 | RT_I2C_WR;
    wmsg.buf   = g_tx_buf;
    wmsg.len   = (rt_uint16_t)len;
    r = rt_i2c_transfer(g_master_bus, &wmsg, 1);
    if (r != 1) {
        if (verbose) {
            rt_kprintf("[FAIL] write no ACK\n");
        }
        return -RT_ERROR;
    }

    rt_thread_mdelay(MEDIUM_DELAY_MS);

    slave_buf_fill(g_tx_buf, len);

    rmsg.addr  = addr;
    rmsg.flags = flags_10 | RT_I2C_RD;
    rmsg.buf   = g_rx_buf;
    rmsg.len   = (rt_uint16_t)len;
    r = rt_i2c_transfer(g_master_bus, &rmsg, 1);
    if (r != 1) {
        if (verbose) {
            rt_kprintf("[FAIL] read no ACK\n");
        }
        return -RT_ERROR;
    }

    if (rt_memcmp(g_tx_buf, g_rx_buf, len) != 0) {
        if (verbose) {
            for (i = 0; i < len; i++) {
                if (g_tx_buf[i] != g_rx_buf[i]) {
                    rt_kprintf("[FAIL] mismatch @%u: tx=%02X rx=%02X\n",
                               i, g_tx_buf[i], g_rx_buf[i]);
                    break;
                }
            }
        }
        return -RT_ERROR;
    }

    if (verbose) {
        rt_kprintf("[PASS]\n");
    }
    return RT_EOK;
}

static int run_core(uint16_t addr, const char *tag)
{
    return run_core_len(addr, 16, tag, 1);
}

/* ---- Single tests ---- */

static int cmd_test_7bit(void)
{
    set_slave_addr(I2C_SLAVE_ADDR_7BIT);
    return run_core(I2C_SLAVE_ADDR_7BIT, "7bit default");
}

static int cmd_test_10bit(void)
{
    int ret;
    uint32_t save_m = g_master_10bit;
    uint32_t save_s = g_slave_10bit;

    set_slave_10bit(1);
    set_master_10bit(1);
    set_slave_addr(I2C_SLAVE_ADDR_10BIT);
    rt_thread_mdelay(MEDIUM_DELAY_MS);

    ret = run_core(I2C_SLAVE_ADDR_10BIT, "10bit addr");

    set_master_10bit(save_m);
    set_slave_10bit(save_s);
    set_slave_addr(CUR_SLAVE_ADDR());
    return ret;
}

static int cmd_test_speed(uint32_t hz)
{
    int ret;
    uint32_t save = g_cur_speed;
    uint16_t addr;

    if (hz != 100000 && hz != 200000 && hz != 400000) {
        rt_kprintf("[WARN] hardware only supports 100000/200000/400000 Hz\n");
    }

    addr = CUR_SLAVE_ADDR();
    ret = set_master_speed(hz);
    if (ret != RT_EOK) {
        return ret;
    }
    ret = run_core(addr, "single speed");
    set_master_speed(save);
    return ret;
}

static int cmd_test_stuck(uint32_t cycles)
{
    int ret;
    uint32_t save = g_cur_stuck;
    uint16_t addr;

    if (cycles < 50 || cycles > 2000) {
        rt_kprintf("[WARN] recommended stuck range: 50~2000 cycles\n");
    }

    addr = CUR_SLAVE_ADDR();
    set_master_stuck(cycles);
    set_slave_stuck(cycles);
    rt_thread_mdelay(MEDIUM_DELAY_MS);
    ret = run_core(addr, "single stuck");
    set_master_stuck(save);
    set_slave_stuck(save);
    return ret;
}

static int cmd_test_len(uint32_t len)
{
    uint16_t addr;

    if (len < 1 || len > MAX_TEST_LEN) {
        rt_kprintf("[ERR] len must be 1~%d\n", MAX_TEST_LEN);
        return -RT_ERROR;
    }

    addr = CUR_SLAVE_ADDR();
    return run_core_len(addr, len, "len test", 1);
}

static int cmd_writeread(void)
{
    struct rt_i2c_msg msgs[2];
    uint8_t regaddr;
    uint8_t rx[16];
    rt_err_t r;
    int i, ok;
    int ret = RT_EOK;
    uint32_t save_m = g_master_10bit;
    uint32_t save_s = g_slave_10bit;
    uint8_t expected[16];

    if (!g_master_bus || !g_slave_dev) {
        rt_kprintf("[ERR] run init first\n");
        return -RT_ERROR;
    }

    set_master_10bit(0);
    set_slave_10bit(0);
    set_slave_addr(I2C_SLAVE_ADDR_7BIT);
    rt_thread_mdelay(MEDIUM_DELAY_MS);

    expected[0] = 0x00;
    for (i = 1; i < 16; i++) {
        expected[i] = (uint8_t)(0xAA + i);
    }
    slave_buf_fill(expected, 16);

    rt_kprintf("--- Write-then-Read (combined restart) ---\n");

    regaddr = 0x00;
    msgs[0].addr  = I2C_SLAVE_ADDR_7BIT;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf   = &regaddr;
    msgs[0].len   = 1;

    msgs[1].addr  = I2C_SLAVE_ADDR_7BIT;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf   = rx;
    msgs[1].len   = 16;

    r = rt_i2c_transfer(g_master_bus, msgs, 2);
    if (r != 2) {
        rt_kprintf("[FAIL] combined transfer ret=%d\n", r);
        ret = -RT_ERROR;
    } else {
        rt_kprintf("Read: ");
        for (i = 0; i < 16; i++) {
            rt_kprintf("%02X ", rx[i]);
        }
        rt_kprintf("\n");

        ok = 1;
        for (i = 0; i < 16; i++) {
            if (i == 0) {
                if (rx[i] != regaddr) {
                    rt_kprintf("[FAIL] byte[0] expected %02X got %02X\n", regaddr, rx[i]);
                    ok = 0;
                    break;
                }
            } else {
                if (rx[i] != (uint8_t)(0xAA + i)) {
                    rt_kprintf("[FAIL] byte[%d] expected %02X got %02X\n",
                               i, 0xAA + i, rx[i]);
                    ok = 0;
                    break;
                }
            }
        }
        if (ok) {
            rt_kprintf("[PASS] combined write-read (restart) OK\n");
        }
        ret = ok ? RT_EOK : -RT_ERROR;
    }

    set_master_10bit(save_m);
    set_slave_10bit(save_s);
    set_slave_addr(CUR_SLAVE_ADDR());
    return ret;
}

/* ---- Address scans ---- */

static int cmd_scan(void)
{
    uint16_t addr;
    uint8_t dummy = 0;
    struct rt_i2c_msg msg;
    int found = 0;

    if (!g_master_bus) {
        rt_kprintf("[ERR] run init first\n");
        return -RT_ERROR;
    }

    rt_kprintf("--- 7bit Address Scan (0x08~0x77) ---\n");
    rt_kprintf("Found: ");

    for (addr = 0x08; addr <= 0x77; addr++) {
        msg.addr  = addr;
        msg.flags = RT_I2C_RD;
        msg.buf   = &dummy;
        msg.len   = 1;
        if (rt_i2c_transfer(g_master_bus, &msg, 1) == 1) {
            rt_kprintf("0x%02X ", addr);
            found++;
        }
        rt_thread_mdelay(SHORT_DELAY_MS);
    }

    if (found == 0) {
        rt_kprintf("(none)\n");
    } else {
        rt_kprintf("\nTotal %d device(s)\n", found);
    }
    return RT_EOK;
}

static int cmd_scan10(void)
{
    uint32_t addr;
    uint8_t dummy = 0;
    struct rt_i2c_msg msg;
    int found = 0;
    uint32_t save_m = g_master_10bit;

    if (!g_master_bus) {
        rt_kprintf("[ERR] run init first\n");
        return -RT_ERROR;
    }

    set_master_10bit(1);
    rt_thread_mdelay(MEDIUM_DELAY_MS);

    rt_kprintf("--- 10bit Address Scan (0x000~0x3FF) ---\n");
    rt_kprintf("Found: ");

    for (addr = 0x000; addr <= 0x3FF; addr++) {
        if (addr <= 0x007) {
            continue;
        }
        if (addr >= 0x078 && addr <= 0x07F) {
            continue;
        }
        msg.addr  = (rt_uint16_t)addr;
        msg.flags = RT_I2C_RD | RT_I2C_ADDR_10BIT;
        msg.buf   = &dummy;
        msg.len   = 1;
        if (rt_i2c_transfer(g_master_bus, &msg, 1) == 1) {
            rt_kprintf("0x%03X ", addr);
            found++;
        }
        rt_thread_mdelay(1);
    }

    if (found == 0) {
        rt_kprintf("(none)\n");
    } else {
        rt_kprintf("\nTotal %d device(s)\n", found);
    }

    set_master_10bit(save_m);
    set_slave_addr(CUR_SLAVE_ADDR());
    return RT_EOK;
}

/* ---- Batch sweeps ---- */

static int cmd_sweep_speed(void)
{
    uint32_t i;
    int pass = 0;
    uint32_t save = g_cur_speed;
    uint16_t addr;
    int ret;

    addr = CUR_SLAVE_ADDR();
    rt_kprintf("\n###### SPEED SWEEP (%u levels: 100k/200k/400k) ######\n", SPEED_CNT);
    for (i = 0; i < SPEED_CNT; i++) {
        rt_kprintf("[%u/%u] %u Hz: ", i + 1, SPEED_CNT, g_speed_table[i]);
        ret = set_master_speed(g_speed_table[i]);
        if (ret == RT_EOK && run_core_len(addr, 16, "", 0) == RT_EOK) {
            rt_kprintf("PASS\n");
            pass++;
        } else {
            rt_kprintf("FAIL\n");
        }
        rt_thread_mdelay(LONG_DELAY_MS);
    }
    set_master_speed(save);
    rt_kprintf("###### SPEED RESULT: %d/%u ######\n\n", pass, SPEED_CNT);
    return RT_EOK;
}

static int cmd_sweep_stuck(void)
{
    uint32_t i;
    int pass = 0;
    uint32_t save = g_cur_stuck;
    uint16_t addr;

    addr = CUR_SLAVE_ADDR();
    rt_kprintf("\n###### STUCK SWEEP (%u levels: 50~2000) ######\n", STUCK_CNT);
    for (i = 0; i < STUCK_CNT; i++) {
        rt_kprintf("[%u/%u] %4u cycles: ", i + 1, STUCK_CNT, g_stuck_table[i]);
        set_master_stuck(g_stuck_table[i]);
        set_slave_stuck(g_stuck_table[i]);
        rt_thread_mdelay(10);
        if (run_core_len(addr, 16, "", 0) == RT_EOK) {
            rt_kprintf("PASS\n");
            pass++;
        } else {
            rt_kprintf("FAIL\n");
        }
        rt_thread_mdelay(LONG_DELAY_MS);
    }
    set_master_stuck(save);
    set_slave_stuck(save);
    rt_kprintf("###### STUCK RESULT: %d/%u ######\n\n", pass, STUCK_CNT);
    return RT_EOK;
}

static int cmd_sweep_len(void)
{
    uint32_t i;
    int pass = 0;
    uint16_t addr;

    addr = CUR_SLAVE_ADDR();
    rt_kprintf("\n###### LENGTH SWEEP (%u levels: 1~255) ######\n", LEN_CNT);
    for (i = 0; i < LEN_CNT; i++) {
        rt_kprintf("[%u/%u] %3u bytes: ", i + 1, LEN_CNT, g_len_table[i]);
        if (run_core_len(addr, g_len_table[i], "", 0) == RT_EOK) {
            rt_kprintf("PASS\n");
            pass++;
        } else {
            rt_kprintf("FAIL\n");
        }
        rt_thread_mdelay(10);
    }
    rt_kprintf("###### LENGTH RESULT: %d/%u ######\n\n", pass, LEN_CNT);
    return RT_EOK;
}

static int cmd_stress(uint32_t count)
{
    uint32_t i, pass = 0, fail = 0;
    rt_tick_t start, elapsed_ms;
    uint32_t loops_per_sec;
    uint16_t addr;

    if (count == 0) {
        count = 100;
    }
    addr = CUR_SLAVE_ADDR();

    rt_kprintf("--- Stress Test: %u loops @ speed=%u stuck=%u ---\n",
               count, g_cur_speed, g_cur_stuck);
    start = rt_tick_get();

    for (i = 0; i < count; i++) {
        if (run_core_len(addr, 16, "", 0) == RT_EOK) {
            pass++;
        } else {
            fail++;
            if (fail <= 3) {
                rt_kprintf("  fail @ loop %u\n", i);
            }
        }
    }

    elapsed_ms = (rt_tick_get() - start) * 1000 / RT_TICK_PER_SECOND;
    loops_per_sec = elapsed_ms > 0 ? (count * 1000 / elapsed_ms) : 0;
    rt_kprintf("[RESULT] pass=%u fail=%u total=%u time=%ums (%u loops/s)\n",
               pass, fail, count, elapsed_ms, loops_per_sec);
    return (fail == 0) ? RT_EOK : -RT_ERROR;
}

static int cmd_random(uint32_t count)
{
    uint32_t i, j, pass = 0, fail = 0;
    uint16_t addr;
    uint16_t addr_flags;
    rt_base_t level;
    struct rt_i2c_msg m;

    if (count == 0) {
        count = 50;
    }
    level = rt_hw_interrupt_disable();
    addr = CUR_SLAVE_ADDR();
    addr_flags = g_master_10bit ? RT_I2C_ADDR_10BIT : 0;
    rt_hw_interrupt_enable(level);

    rt_kprintf("--- Random Data Test: %u loops ---\n", count);
    srand(rt_tick_get());

    for (i = 0; i < count; i++) {
        uint32_t len = (rand() % 64) + 1;
        for (j = 0; j < len; j++) {
            g_tx_buf[j] = (uint8_t)rand();
        }

        slave_buf_reset(len);
        m.addr = addr;
        m.flags = addr_flags | RT_I2C_WR;
        m.buf = g_tx_buf;
        m.len = (rt_uint16_t)len;
        if (rt_i2c_transfer(g_master_bus, &m, 1) != 1) {
            fail++;
            continue;
        }
        rt_thread_mdelay(SHORT_DELAY_MS);

        rt_memset(g_rx_buf, 0, len);
        slave_buf_fill(g_tx_buf, len);
        m.addr = addr;
        m.flags = addr_flags | RT_I2C_RD;
        m.buf = g_rx_buf;
        m.len = (rt_uint16_t)len;
        if (rt_i2c_transfer(g_master_bus, &m, 1) != 1) {
            fail++;
            continue;
        }

        if (rt_memcmp(g_tx_buf, g_rx_buf, len) == 0) {
            pass++;
        } else {
            fail++;
            if (fail <= 3) {
                rt_kprintf("  mismatch len=%u @ loop %u\n", len, i);
            }
        }
    }
    rt_kprintf("[RESULT] pass=%u fail=%u total=%u\n", pass, fail, count);
    return (fail == 0) ? RT_EOK : -RT_ERROR;
}

/* ---- Full suite stages ---- */

static int stage1_current_config(void)
{
    uint16_t cur_addr;

    rt_kprintf("\n--- Stage 1: Current Config Validation (no mode change) ---\n");
    cur_addr = CUR_SLAVE_ADDR();
    set_slave_addr(cur_addr);
    rt_thread_mdelay(MEDIUM_DELAY_MS);
    return run_core(cur_addr, "current config");
}

static int stage2_mode_test(void)
{
    int ret7, ret10;

    rt_kprintf("\n--- Stage 2: Mode Basic Function Tests ---\n");

    set_master_10bit(0);
    set_slave_10bit(0);
    set_slave_addr(I2C_SLAVE_ADDR_7BIT);
    rt_thread_mdelay(MEDIUM_DELAY_MS);
    ret7 = run_core(I2C_SLAVE_ADDR_7BIT, "7bit mode");
    rt_thread_mdelay(LONG_DELAY_MS);

    set_master_10bit(1);
    set_slave_10bit(1);
    set_slave_addr(I2C_SLAVE_ADDR_10BIT);
    rt_thread_mdelay(MEDIUM_DELAY_MS);
    ret10 = run_core(I2C_SLAVE_ADDR_10BIT, "10bit mode");
    rt_thread_mdelay(LONG_DELAY_MS);

    return (ret7 == RT_EOK && ret10 == RT_EOK) ? RT_EOK : -RT_ERROR;
}

static int stage3_full_verify(void)
{
    int ret;

    rt_kprintf("\n--- Stage 3: Full Verification (7bit matched mode) ---\n");
    set_master_10bit(0);
    set_slave_10bit(0);
    set_slave_addr(I2C_SLAVE_ADDR_7BIT);
    set_master_speed(DEFAULT_SPEED_HZ);
    set_master_stuck(DEFAULT_STUCK_CYCLES);
    set_slave_stuck(DEFAULT_STUCK_CYCLES);
    rt_thread_mdelay(MEDIUM_DELAY_MS);

    ret = cmd_writeread();
    if (ret != RT_EOK) {
        return ret;
    }
    rt_thread_mdelay(LONG_DELAY_MS);

    ret = cmd_sweep_speed();
    if (ret != RT_EOK) {
        return ret;
    }
    rt_thread_mdelay(LONG_DELAY_MS);

    ret = cmd_sweep_stuck();
    if (ret != RT_EOK) {
        return ret;
    }
    rt_thread_mdelay(LONG_DELAY_MS);

    ret = cmd_sweep_len();
    rt_thread_mdelay(LONG_DELAY_MS);

    ret = cmd_stress(100);
    return ret;
}

static int cmd_all(void)
{
    uint32_t save_m = g_master_10bit;
    uint32_t save_s = g_slave_10bit;
    uint32_t save_speed = g_cur_speed;
    uint32_t save_stuck = g_cur_stuck;
    int ret = 0;

    rt_kprintf("\n========================================\n");
    rt_kprintf("  I2C COMPREHENSIVE TEST SUITE\n");
    rt_kprintf("========================================\n");
    rt_kprintf("Current config: master=%s slave=%s\n",
               save_m ? "10bit" : "7bit",
               save_s ? "10bit" : "7bit");

    ret |= stage1_current_config();
    rt_thread_mdelay(LONG_DELAY_MS);

    ret |= stage2_mode_test();

    set_master_10bit(save_m);
    set_slave_10bit(save_s);
    set_slave_addr(CUR_SLAVE_ADDR());
    rt_thread_mdelay(MEDIUM_DELAY_MS);

    ret |= stage3_full_verify();

    set_master_10bit(save_m);
    set_slave_10bit(save_s);
    set_slave_addr(CUR_SLAVE_ADDR());
    set_master_speed(save_speed);
    set_master_stuck(save_stuck);
    set_slave_stuck(save_stuck);

    rt_kprintf("\n========================================\n");
    if (ret == RT_EOK) {
        rt_kprintf("  ALL TESTS PASSED (config restored)\n");
    } else {
        rt_kprintf("  SOME TESTS FAILED (config restored)\n");
    }
    rt_kprintf("========================================\n");
    return ret;
}

/* ---- MSH command entry ---- */

static int cmd_i2c_loop(int argc, char **argv)
{
    if (argc < 2) {
        rt_kprintf("I2C Loopback Test Commands:\n");
        rt_kprintf("  init              init (respect menuconfig mode)\n");
        rt_kprintf("  test              7bit test at CURRENT config\n");
        rt_kprintf("  test10            10bit test (temp switch, restore)\n");
        rt_kprintf("  speed <hz>        set speed & test\n");
        rt_kprintf("  stuck <cycles>    set stuck & test\n");
        rt_kprintf("  len <n>           packet length test 1~255\n");
        rt_kprintf("  writeread         combined write-then-read (restart)\n");
        rt_kprintf("  scan              scan 7bit address bus\n");
        rt_kprintf("  scan10            scan 10bit address bus\n");
        rt_kprintf("  speedsweep        sweep 100k/200k/400k (current mode)\n");
        rt_kprintf("  stucksweep        sweep stuck values (current mode)\n");
        rt_kprintf("  lensweep          sweep packet lengths (current mode)\n");
        rt_kprintf("  stress [n]        stress test N loops (def 100)\n");
        rt_kprintf("  random [n]        random data N loops (def 50)\n");
        rt_kprintf("  all               full 3-stage suite\n");
        rt_kprintf("  deinit            release resources\n");
        return 0;
    }

    if (STREQ(argv[1], "init")) {
        return cmd_init();
    }
    if (STREQ(argv[1], "deinit")) {
        return cmd_deinit();
    }
    if (STREQ(argv[1], "test")) {
        return cmd_test_7bit();
    }
    if (STREQ(argv[1], "test10")) {
        return cmd_test_10bit();
    }
    if (STREQ(argv[1], "writeread")) {
        return cmd_writeread();
    }
    if (STREQ(argv[1], "scan")) {
        return cmd_scan();
    }
    if (STREQ(argv[1], "scan10")) {
        return cmd_scan10();
    }
    if (STREQ(argv[1], "speedsweep")) {
        return cmd_sweep_speed();
    }
    if (STREQ(argv[1], "stucksweep")) {
        return cmd_sweep_stuck();
    }
    if (STREQ(argv[1], "lensweep")) {
        return cmd_sweep_len();
    }
    if (STREQ(argv[1], "all")) {
        return cmd_all();
    }

    if (STREQ(argv[1], "speed")) {
        if (argc < 3) {
            rt_kprintf("Usage: i2c_loop speed <hz>\n");
            return -RT_ERROR;
        }
        return cmd_test_speed((uint32_t)atoi(argv[2]));
    }
    if (STREQ(argv[1], "stuck")) {
        if (argc < 3) {
            rt_kprintf("Usage: i2c_loop stuck <cycles>\n");
            return -RT_ERROR;
        }
        return cmd_test_stuck((uint32_t)atoi(argv[2]));
    }
    if (STREQ(argv[1], "len")) {
        if (argc < 3) {
            rt_kprintf("Usage: i2c_loop len <bytes>\n");
            return -RT_ERROR;
        }
        return cmd_test_len((uint32_t)atoi(argv[2]));
    }
    if (STREQ(argv[1], "stress")) {
        uint32_t n = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 100;
        return cmd_stress(n);
    }
    if (STREQ(argv[1], "random")) {
        uint32_t n = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 50;
        return cmd_random(n);
    }

    rt_kprintf("unknown cmd, type 'i2c_loop' for help\n");
    return -RT_ERROR;
}

MSH_CMD_EXPORT_ALIAS(cmd_i2c_loop, i2c_loop, I2C comprehensive loopback test);
