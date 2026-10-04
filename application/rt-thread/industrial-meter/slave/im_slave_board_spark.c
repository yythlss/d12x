/*
 * 便携工业调试仪 —— 从机板级后端 (STM32F407 星火一号)
 *
 * 对应关系 (BSP: bsp/stm32/stm32f407-rt-spark):
 *   DO   : LED_R = PF12, LED_B = PF11
 *   DI   : KEY_UP = PC5, KEY_DOWN = PC1, KEY_LEFT = PC0, KEY_RIGHT = PC4
 *   AI   : ADC1 通道 0~3 (PA0/PA1/PA2/PA3), 量程 0 ~ VREF
 *   链路 : 板载 RS485 接口, USART6 + DE = PG8
 *          menuconfig 打开 BSP_USING_RS485 即可 (默认 uart6 / pin 104)
 *
 * 注意: ADC 只能承受 0~3.3V, 测工业信号必须加分压/取样电阻。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drv_gpio.h>

#include "im_protocol.h"
#include "im_slave.h"

/* ---------------- 板级引脚 ---------------- */
#define IM_LED_R_PIN        GET_PIN(F, 12)
#define IM_LED_B_PIN        GET_PIN(F, 11)

#define IM_KEY_UP_PIN       GET_PIN(C, 5)
#define IM_KEY_DOWN_PIN     GET_PIN(C, 1)
#define IM_KEY_LEFT_PIN     GET_PIN(C, 0)
#define IM_KEY_RIGHT_PIN    GET_PIN(C, 4)

#define IM_RS485_UART       "uart6"
#define IM_RS485_DE_PIN     GET_PIN(G, 8)      /* 与 BSP 的 BSP_RS485_RTS_PIN 默认一致 */
#define IM_SLAVE_ADDR       IM_MB_SLAVE_ADDR

/* ---------------- ADC ---------------- */
#define IM_ADC_DEV_NAME     "adc1"
#define IM_ADC_CH0          0
#define IM_ADC_CH1          1
#define IM_ADC_CH2          2
#define IM_ADC_CH3          3
#define IM_ADC_MAX          4095

#define IM_SAMPLE_MS_DEFAULT    100
#define IM_AI_AVG_DEFAULT       4
#define IM_VREF_MV_DEFAULT      3300

static rt_uint16_t  g_reg[IM_REG_BLOCK_LEN];
static rt_adc_device_t g_adc = RT_NULL;
static rt_mutex_t   g_lock = RT_NULL;
static rt_tick_t    g_boot_tick = 0;

#define IM_REG_IDX(addr)    ((addr) - IM_REG_BLOCK_START)
#define IM_LOCK()           rt_mutex_take(g_lock, RT_WAITING_FOREVER)
#define IM_UNLOCK()         rt_mutex_release(g_lock)

/* ---------------- 硬件动作 ---------------- */
static void im_apply_do(rt_uint16_t map)
{
    rt_pin_write(IM_LED_R_PIN, (map & IM_DO_LED_R) ? PIN_HIGH : PIN_LOW);
    rt_pin_write(IM_LED_B_PIN, (map & IM_DO_LED_B) ? PIN_HIGH : PIN_LOW);
}

static rt_uint16_t im_read_di(void)
{
    rt_uint16_t map = 0;

    /* 按键按下为低电平 */
    if (rt_pin_read(IM_KEY_UP_PIN) == PIN_LOW)
        map |= IM_DI_KEY_UP;
    if (rt_pin_read(IM_KEY_DOWN_PIN) == PIN_LOW)
        map |= IM_DI_KEY_DOWN;
    if (rt_pin_read(IM_KEY_LEFT_PIN) == PIN_LOW)
        map |= IM_DI_KEY_LEFT;
    if (rt_pin_read(IM_KEY_RIGHT_PIN) == PIN_LOW)
        map |= IM_DI_KEY_RIGHT;

    return map;
}

static rt_uint16_t im_read_ai_mv(rt_int8_t ch)
{
    rt_uint32_t raw, vref;
    int i, avg;

    if (!g_adc)
        return 0;

    avg = g_reg[IM_REG_IDX(IM_REG_AI_AVG)];
    if (avg == 0)
        avg = 1;

    raw = 0;
    for (i = 0; i < avg; i++)
        raw += (rt_uint32_t)rt_adc_read(g_adc, ch);

    raw /= avg;
    vref = g_reg[IM_REG_IDX(IM_REG_VREF_MV)];

    return (rt_uint16_t)(raw * vref / IM_ADC_MAX);
}

/* ---------------- 采样线程 ---------------- */
static void im_sample_entry(void *param)
{
    rt_uint32_t period;

    while (1) {
        period = g_reg[IM_REG_IDX(IM_REG_SAMPLE_MS)];
        if (period < 10)
            period = IM_SAMPLE_MS_DEFAULT;
        rt_thread_mdelay(period);

        IM_LOCK();
        g_reg[IM_REG_IDX(IM_REG_AI0_MV)] = im_read_ai_mv(IM_ADC_CH0);
        g_reg[IM_REG_IDX(IM_REG_AI1_MV)] = im_read_ai_mv(IM_ADC_CH1);
        g_reg[IM_REG_IDX(IM_REG_AI2_MV)] = im_read_ai_mv(IM_ADC_CH2);
        g_reg[IM_REG_IDX(IM_REG_AI3_MV)] = im_read_ai_mv(IM_ADC_CH3);
        g_reg[IM_REG_IDX(IM_REG_DI_MAP)] = im_read_di();
        g_reg[IM_REG_IDX(IM_REG_HEARTBEAT)]++;

        {
            rt_uint32_t up = (rt_tick_get() - g_boot_tick) / RT_TICK_PER_SECOND;
            g_reg[IM_REG_IDX(IM_REG_UPTIME_LO)] = (rt_uint16_t)(up & 0xFFFF);
            g_reg[IM_REG_IDX(IM_REG_UPTIME_HI)] = (rt_uint16_t)(up >> 16);
        }
        IM_UNLOCK();
    }
}

/* ---------------- 后端接口 ---------------- */
rt_uint16_t im_board_get_reg(rt_uint16_t addr)
{
    rt_uint16_t v;

    if (addr < IM_REG_BLOCK_START || addr >= IM_REG_BLOCK_START + IM_REG_BLOCK_LEN)
        return 0;

    IM_LOCK();
    v = g_reg[IM_REG_IDX(addr)];
    IM_UNLOCK();

    return v;
}

int im_board_set_reg(rt_uint16_t addr, rt_uint16_t val)
{
    int ret = RT_EOK;

    if (addr < IM_REG_BLOCK_START || addr >= IM_REG_BLOCK_START + IM_REG_BLOCK_LEN)
        return -RT_EINVAL;

    IM_LOCK();
    switch (addr) {
    case IM_REG_DO_MAP:
        g_reg[IM_REG_IDX(addr)] = val;
        im_apply_do(val);
        break;

    case IM_REG_SAMPLE_MS:
        if (val < 10 || val > 60000)
            ret = -RT_EINVAL;
        else
            g_reg[IM_REG_IDX(addr)] = val;
        break;

    case IM_REG_AI_AVG:
        if (val != 1 && val != 4 && val != 8 && val != 16)
            ret = -RT_EINVAL;
        else
            g_reg[IM_REG_IDX(addr)] = val;
        break;

    case IM_REG_VREF_MV:
        if (val < 2000 || val > 3600)
            ret = -RT_EINVAL;
        else
            g_reg[IM_REG_IDX(addr)] = val;
        break;

    case IM_REG_CMD:
        /* 命令寄存器不保存, 执行后主机读到 0 */
        if (val == IM_CMD_CLEAR_ERR) {
            g_reg[IM_REG_IDX(IM_REG_ERR_CODE)] = IM_ERR_NONE;
            g_reg[IM_REG_IDX(IM_REG_CRC_ERR_CNT)] = 0;
            g_reg[IM_REG_IDX(IM_REG_FRAME_CNT)] = 0;
        } else if (val == IM_CMD_ADC_CAL) {
            rt_adc_enable(g_adc, IM_ADC_CH0);
            rt_adc_enable(g_adc, IM_ADC_CH1);
            rt_adc_enable(g_adc, IM_ADC_CH2);
            rt_adc_enable(g_adc, IM_ADC_CH3);
        } else if (val == IM_CMD_SOFT_RESET) {
            rt_hw_cpu_reset();
        } else {
            /* IM_CMD_SAVE_CFG 待接入 Flash 后实现 */
            ret = -RT_EINVAL;
        }
        break;

    /* 协议引擎内部维护的计数器 */
    case IM_REG_FRAME_CNT:
    case IM_REG_CRC_ERR_CNT:
        g_reg[IM_REG_IDX(addr)] = val;
        break;

    default:
        ret = -RT_EINVAL;               /* 只读寄存器不可写 */
        break;
    }
    IM_UNLOCK();

    return ret;
}

void im_board_init(void)
{
    rt_thread_t tid;

    g_lock = rt_mutex_create("im_reg", RT_IPC_FLAG_FIFO);
    RT_ASSERT(g_lock);

    g_boot_tick = rt_tick_get();

    /* 固定信息 */
    g_reg[IM_REG_IDX(IM_REG_DEV_MODEL)]  = IM_DEV_MODEL;
    g_reg[IM_REG_IDX(IM_REG_FW_VER)]     = IM_FW_VER;
    g_reg[IM_REG_IDX(IM_REG_SAMPLE_MS)]  = IM_SAMPLE_MS_DEFAULT;
    g_reg[IM_REG_IDX(IM_REG_AI_AVG)]     = IM_AI_AVG_DEFAULT;
    g_reg[IM_REG_IDX(IM_REG_VREF_MV)]    = IM_VREF_MV_DEFAULT;
    g_reg[IM_REG_IDX(IM_REG_ERR_CODE)]   = IM_ERR_NONE;

    /* DO */
    rt_pin_mode(IM_LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(IM_LED_B_PIN, PIN_MODE_OUTPUT);
    im_apply_do(0);

    /* DI: 按键上拉, 按下接地 */
    rt_pin_mode(IM_KEY_UP_PIN,    PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(IM_KEY_DOWN_PIN,  PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(IM_KEY_LEFT_PIN,  PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(IM_KEY_RIGHT_PIN, PIN_MODE_INPUT_PULLUP);

    /* AI: ADC1/ADC3 的通道 0~3 都对应 PA0~PA3, 哪个使能了就用哪个 */
    {
        static const char *adc_name[] = {IM_ADC_DEV_NAME, "adc3"};
        int i;

        for (i = 0; i < 2 && !g_adc; i++)
            g_adc = (rt_adc_device_t)rt_device_find(adc_name[i]);
    }

    if (g_adc) {
        rt_adc_enable(g_adc, IM_ADC_CH0);
        rt_adc_enable(g_adc, IM_ADC_CH1);
        rt_adc_enable(g_adc, IM_ADC_CH2);
        rt_adc_enable(g_adc, IM_ADC_CH3);
    } else {
        g_reg[IM_REG_IDX(IM_REG_ERR_CODE)] = IM_ERR_ADC;
        rt_kprintf("im_board: adc %s not found\n", IM_ADC_DEV_NAME);
    }

    tid = rt_thread_create("im_samp", im_sample_entry, RT_NULL,
                           1024, 15, 10);
    if (tid)
        rt_thread_startup(tid);
}

/* 本地自检: 不接主机也能看采集是否正常 */
static void im_slave_dump(void)
{
    rt_kprintf("model 0x%04X  fw 0x%04X  uptime %us\n",
               g_reg[IM_REG_IDX(IM_REG_DEV_MODEL)],
               g_reg[IM_REG_IDX(IM_REG_FW_VER)],
               (rt_uint32_t)((g_reg[IM_REG_IDX(IM_REG_UPTIME_HI)] << 16) |
                             g_reg[IM_REG_IDX(IM_REG_UPTIME_LO)]));
    rt_kprintf("AI %4u %4u %4u %4u mV   DI 0x%02X   DO 0x%02X\n",
               g_reg[IM_REG_IDX(IM_REG_AI0_MV)], g_reg[IM_REG_IDX(IM_REG_AI1_MV)],
               g_reg[IM_REG_IDX(IM_REG_AI2_MV)], g_reg[IM_REG_IDX(IM_REG_AI3_MV)],
               g_reg[IM_REG_IDX(IM_REG_DI_MAP)], g_reg[IM_REG_IDX(IM_REG_DO_MAP)]);
    rt_kprintf("hb %u  frame %u  crc_err %u  err 0x%X  %s\n",
               g_reg[IM_REG_IDX(IM_REG_HEARTBEAT)],
               g_reg[IM_REG_IDX(IM_REG_FRAME_CNT)],
               g_reg[IM_REG_IDX(IM_REG_CRC_ERR_CNT)],
               g_reg[IM_REG_IDX(IM_REG_ERR_CODE)],
               g_adc ? "" : "(adc missing)");
}
MSH_CMD_EXPORT(im_slave_dump, dump industrial meter slave registers);

/* 从站启动入口: 在 RT-Thread 上电后调用一次 */
int im_slave_startup(void)
{
    return im_slave_init(IM_RS485_UART, IM_RS485_DE_PIN, IM_SLAVE_ADDR);
}
INIT_APP_EXPORT(im_slave_startup);
