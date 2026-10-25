/*
 * 便携工业调试仪 —— D12x 主机自身运行态采集上报
 *
 * 周期性采集 D12x 本机的结温 / 电量 / CPU 占用 / 剩余内存 / 运行秒数，
 * 经 RS485 以 FC16 多寄存器写回星火一号从站 0x0400~0x0404，
 * 由从站原样中转上云 (device="d12x")。
 *
 * 见 im_d12x_report.h 的单位约定。电量依赖 GPAI(AIC_GPAI_DRV)：已接电池时
 * 走真实分压换算；未接电池 / 仅外接电源供电时默认报 100%(0.1% 单位=1000)，
 * 不再用 0xFFFF 哨兵，避免云平台把电量误判为「无数据」。
 */

#include <rtthread.h>
#include <rtdevice.h>       /* rt_adc_device_t / rt_adc_control / RT_ADC_CMD_GET_CH_INFO */
#include <errno.h>

#include "im_protocol.h"
#include "im_master.h"
#include "im_d12x_report.h"

#include "hal_tsen.h"       /* 结温: hal_tsen_get_valid_ch / hal_tsen_get_temp */
#ifdef AIC_GPAI_DRV
#include "hal_gpai.h"       /* struct aic_gpai_ch_info */
#include "hal_adcim.h"      /* hal_adcim_auto_calibration / hal_adcim_adc2voltage */
#endif

#define LOG_TAG            "im.d12x"
#define LOG_LVL            LOG_LVL_INFO
#include <ulog.h>

/* ---------------- 采集句柄 ---------------- */
static rt_thread_t        g_tid      = RT_NULL;
static rt_bool_t          g_running  = RT_FALSE;

/* 结温通道（init 时取一次，周期采样直接复用） */
static struct aic_tsen_ch *g_tsen    = RT_NULL;

/* GPAI 电量设备（未启用/找不到则为 RT_NULL，电量默认 100% = 电源供电） */
#ifdef AIC_GPAI_DRV
static rt_adc_device_t    g_gpai     = RT_NULL;
#endif

/* 堆统计诊断：首帧打印一次 rt_memory_info 原始值，便于确认剩余内存为何为 0 */
static rt_bool_t          g_mem_logged = RT_FALSE;

/* CPU 占用：idle 钩子计次 + 1s 窗口自校准 */
#ifdef RT_USING_IDLE_HOOK
static volatile rt_uint64_t g_idle_cnt = 0;
static rt_uint64_t          g_idle_last = 0;
static rt_uint64_t          g_idle_max  = 0;   /* 同窗口纯空闲基线 */

static void im_idle_hook(void)
{
    g_idle_cnt++;
}
#endif

/* ---------------- 电量：GPAI 分压 -> 百分比 ---------------- */
#ifdef AIC_GPAI_DRV
/* 返回 0.1% 单位；传感器不可用时返回默认 100%(=1000)，因当前为电源供电 */
static rt_uint16_t sample_bat_pct_x10(void)
{
    struct aic_gpai_ch_info info;
    rt_uint16_t adc_val;
    rt_uint32_t cal, v, pin_mv, pack_mv, cell_mv, pct;

    if (g_gpai == RT_NULL)
        return 1000;

    memset(&info, 0, sizeof(info));
    info.chan_id = IM_D12X_BAT_ADC_CH;
    if (rt_adc_control(g_gpai, RT_ADC_CMD_GET_CH_INFO, (void *)&info) != RT_EOK
        || info.fifo_valid_cnt == 0)
        return 1000;

    adc_val = info.adc_values[info.fifo_valid_cnt - 1];
    cal     = hal_adcim_auto_calibration();
    v       = (rt_uint32_t)hal_adcim_adc2voltage(&adc_val, cal, 10000, 3);
    if (v <= 0)
        return 1000;

    pin_mv  = v / 10;                                       /* 0.1mV -> mV */
    pack_mv = pin_mv * IM_D12X_BAT_VDIV_NUM / IM_D12X_BAT_VDIV_DEN;
    cell_mv = pack_mv / IM_D12X_BAT_CELLS;

    if (cell_mv <= IM_D12X_BAT_CELL_EMPTY_MV)
        pct = 0;
    else if (cell_mv >= IM_D12X_BAT_CELL_FULL_MV)
        pct = 100;
    else
        pct = (cell_mv - IM_D12X_BAT_CELL_EMPTY_MV) * 100
              / (IM_D12X_BAT_CELL_FULL_MV - IM_D12X_BAT_CELL_EMPTY_MV);

    return (rt_uint16_t)(pct * 10);                         /* 0.1% 单位 */
}
#endif

/* ---------------- 采集一帧并写回从站 ---------------- */
static void report_one_frame(void)
{
    rt_uint16_t buf[IM_REG_D12X_CNT];
    rt_int32_t  temp_x10 = 0;
    rt_uint16_t bat_x10  = 1000;      /* 默认 100%：电源供电 / 未接电池 */
    rt_int32_t  cpu_x10  = 0;
    rt_size_t   total = 0, used = 0, max_used = 0;
    rt_uint32_t free_kb, up;

    /* 结温 0.1℃ */
    if (g_tsen) {
        s32 t = 0;
        if (hal_tsen_get_temp(g_tsen, &t) == 0)
            temp_x10 = (rt_int16_t)t;       /* 已为 0.1℃；失败保持 0 */
    }

    /* 电量 0.1%（未启用 GPAI / 无读数时默认 100%=电源供电） */
#ifdef AIC_GPAI_DRV
    bat_x10 = sample_bat_pct_x10();
#endif

    /* CPU 占用 0.1%：1s 窗口 idle 占比反推 */
#ifdef RT_USING_IDLE_HOOK
    if (g_idle_max > 0) {
        rt_uint64_t cur   = g_idle_cnt;
        rt_uint64_t delta = cur - g_idle_last;
        g_idle_last = cur;
        /* idle_pct_x10 = delta / g_idle_max * 1000 ; cpu = 1000 - idle_pct_x10 */
        rt_int32_t idle_pct_x10 = (rt_int32_t)(delta * 1000 / g_idle_max);
        cpu_x10 = 1000 - idle_pct_x10;
        if (cpu_x10 < 0)    cpu_x10 = 0;
        if (cpu_x10 > 1000) cpu_x10 = 1000;
    }
#endif

    /* 剩余内存 KB
     * ⚠️ 必须判 total > used：rt_memory_info 返回的堆统计在部分配置下会出现
     *    used >= total，此时 total-used 是「无符号下溢」，结果接近 2^32，
     *    除以 1024 后约为 4194304(0x400000)，再 & 0xFFFF 截断恰好=0 ——
     *    这就是「剩余内存显示 0」的根因。参考 dbg_d12x.c 同样做了该保护。 */
    rt_memory_info(&total, &used, &max_used);
    if (total > used)
        free_kb = (rt_uint32_t)((total - used) / 1024);
    else
        free_kb = 0;                      /* 统计不可用：保持 0，而非下溢垃圾值 */

    if (free_kb > 0xFFFF)
        free_kb = 0xFFFF;                 /* uint16 寄存器饱和，勿被截断成小值 */

    if (!g_mem_logged) {
        g_mem_logged = RT_TRUE;
        LOG_I("heap: total=%u used=%u max=%u -> free=%uKB",
              (unsigned)total, (unsigned)used, (unsigned)max_used, (unsigned)free_kb);
    }

    /* 运行秒数 */
    up = (rt_uint32_t)(rt_tick_get() / RT_TICK_PER_SECOND);

    buf[0] = (rt_uint16_t)temp_x10;          /* int16，按位保留 */
    buf[1] = bat_x10;
    buf[2] = (rt_uint16_t)cpu_x10;
    buf[3] = (rt_uint16_t)(free_kb & 0xFFFF);
    buf[4] = (rt_uint16_t)(up & 0xFFFF);

    if (im_master_write_regs(IM_REG_D12X_TEMP, IM_REG_D12X_CNT, buf) != IM_REG_D12X_CNT) {
        LOG_W("write D12x regs failed: %s", modbus_strerror(errno));
    } else {
        LOG_D("d12x -> slave: temp=%d.%dC bat=%d.%d%% cpu=%d.%d%% mem=%uKB up=%us",
              temp_x10 / 10, temp_x10 % 10,
              (int)(bat_x10 / 10), (int)(bat_x10 % 10),
              cpu_x10 / 10, cpu_x10 % 10, free_kb, up);
    }
}

static void im_d12x_entry(void *param)
{
    (void)param;

    while (g_running) {
        struct im_status *s = im_master_status();

        /* 从机离线时不写，避免 libmodbus 每周期刷错误日志 */
        if (s && s->online) {
            report_one_frame();
        }

        rt_thread_mdelay(IM_D12X_REPORT_MS);
    }
}

int im_d12x_report_start(void)
{
    if (g_running) {
        LOG_I("already started");
        return RT_EOK;
    }

    /* 结温通道 */
    hal_tsen_enable(1);
    g_tsen = hal_tsen_get_valid_ch(AIC_TSEN_SENSOR_CPU);
    if (!g_tsen)
        LOG_W("TSEN CPU ch unavailable, temp will report 0");

    /* 电量设备（可选，未接电池 / 电源供电时默认 100%） */
#ifdef AIC_GPAI_DRV
    g_gpai = (rt_adc_device_t)rt_device_find("gpai");
    if (g_gpai == RT_NULL)
        LOG_W("gpai device not found, battery default 100% (powered)");
#else
    LOG_W("AIC_GPAI_DRV not enabled, battery default 100% (powered)");
#endif

    /* CPU 占用：挂 idle 钩子 + 同窗口纯空闲基线校准 */
#ifdef RT_USING_IDLE_HOOK
    rt_thread_idle_sethook(im_idle_hook);
    g_idle_cnt = 0;
    rt_thread_mdelay(IM_D12X_REPORT_MS);     /* 测量「纯空闲」窗口的 idle 次数 */
    g_idle_max  = g_idle_cnt;
    g_idle_last = g_idle_cnt;
    if (g_idle_max == 0)
        LOG_W("idle hook did not fire, cpu usage will report 0");
#endif

    g_running = RT_TRUE;
    g_tid = rt_thread_create("im_d12x", im_d12x_entry, RT_NULL,
                             2048, 15, 10);
    if (!g_tid) {
        g_running = RT_FALSE;
        LOG_E("thread create failed");
        return -RT_ERROR;
    }

    rt_thread_startup(g_tid);
    LOG_I("D12x self-report started (%d ms), temp=%s bat=%s cpu=%s",
          IM_D12X_REPORT_MS,
          g_tsen ? "on" : "off",
#ifdef AIC_GPAI_DRV
          g_gpai ? "on" : "100%",
#else
          "100%",
#endif
#ifdef RT_USING_IDLE_HOOK
          g_idle_max ? "on" : "off"
#else
          "off"
#endif
         );
    return RT_EOK;
}

int im_d12x_report_stop(void)
{
    if (!g_running)
        return RT_EOK;

    g_running = RT_FALSE;
    if (g_tid) {
        rt_thread_mdelay(IM_D12X_REPORT_MS + 50);
        g_tid = RT_NULL;
    }
    LOG_I("D12x self-report stopped");
    return RT_EOK;
}
