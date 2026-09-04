/*
 * Copyright (C) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  Huahui Mai <huahui.mai@artinchip.com>
 */

#include <stdio.h>
#include <aic_core.h>
#include <aic_hal.h>
#include <aic_hal_dbi.h>
#include <aic_hal_dsi.h>
#include <aic_hal_de.h>
#include <aic_hal_reset.h>
#include <aic_drv_de.h>
#include <artinchip_fb.h>
#include <mipi_display.h>
#include <mpp_fb.h>
#include "lvgl.h"
#include "aic_screen_ctl.h"

#ifdef LV_AIC_SCREEN_CTL

/* ========================================================================== */
/* Section: DCS Command Constants                                             */
/* ========================================================================== */
#define DCS_DISPLAY_OFF     0x28
#define DCS_SLEEP_IN        0x10
#define DCS_SLEEP_OUT       0x11
#define DCS_DISPLAY_ON      0x29

/* ========================================================================== */
/* Section: Interface-specific DCS Macros                                     */
/* ========================================================================== */
#if defined(LV_SCREEN_DBI_I8080)

#define INTERFACE_BASE      DBI_BASE

#define DCS_SEND_CMD(base, seq...)  do {                \
    u8 d[] = { seq };                                   \
    i8080_cmd_wr(base, d[0], ARRAY_SIZE(d) - 1, d + 1); \
} while (0)

#define DCS_READ_CMD(base, cmd, count)  do {             \
    i8080_cmd_wr(base, cmd, 0, NULL);                    \
    i8080_rd_ctl(base, count, 1);                        \
} while (0)
#define DCS_READ_DATA(base)  ((u8)i8080_rd_data(base))

#elif defined(LV_SCREEN_DBI_SPI)

#define INTERFACE_BASE      DBI_BASE

#define DCS_SEND_CMD(base, seq...)  do {                \
    u8 d[] = { seq };                                   \
    spi_cmd_wr(base, d[0], ARRAY_SIZE(d) - 1, d + 1);   \
} while (0)

#define DCS_READ_CMD(base, cmd, count)  do {             \
    spi_cmd_wr(base, cmd, 0, NULL);                      \
    spi_rd_ctl(base, count, 1);                          \
} while (0)
#define DCS_READ_DATA(base)  ((u8)spi_rd_data(base))

#elif defined(LV_SCREEN_DSI)

#define INTERFACE_BASE      MIPI_DSI_BASE

#define DCS_SEND_CMD(base, seq...)  do {                \
    const u8 d[] = { seq };                             \
    mipi_dsi_dcs_send(base, ARRAY_SIZE(d), d);          \
} while (0)

#define DCS_READ_CMD(base, cmd, count)  \
    ((void)(count), dsi_cmd_wr(base, MIPI_DSI_DCS_READ, 0, (u8[]){ cmd }, 1))

#define DCS_READ_DATA(base)  ((u8)(readl(base + DSI_GEN_PD_CFG) & 0xFF))

/* Forward declaration — used by DCS_SEND_CMD macro above */
static int mipi_dsi_dcs_send(void *base, size_t size, const u8 *data);

#endif

/* ========================================================================== */
/* Section: Timing Constants                                                  */
/* ========================================================================== */
/* 120 ms / 16.7 ms per frame ≈ 7.2 → 8 frames */
#define WAKE_DELAY_FRAMES       8

/* Synchronous API blocking timeout (ms) */
#define SLEEP_ENTER_TIMEOUT     100
#define SLEEP_EXIT_TIMEOUT      300

/* Reset LCD when TE single count exceeds this threshold */
#define TE_WARNING_LEVEL        5

/* ========================================================================== */
/* Section: State Machine & Context                                           */
/* ========================================================================== */
/*
 * Four-state machine — all transitions driven by state writes:
 *
 *                   screen_sleep_enter()
 *   AWAKE ──────────────────────────────────► SLEEPING
 *    ▲                                           │
 *    │ 0x29 sent                                 │ 0x28 + 0x10 sent
 *    │                                           ▼
 *   WAKING ◄────────────────────────────────── SLEEP
 *                   screen_sleep_exit()
 *                   (0x11 sent, wait N frames)
 */
typedef enum {
    STATE_AWAKE,
    STATE_SLEEPING,   /* 0x28+0x10 pending: ISR will send on next vsync */
    STATE_SLEEP,      /* Panel fully asleep */
    STATE_WAKING,     /* 0x11 sent, ISR counting frames until 0x29 */
} screen_state_t;

typedef enum {
    DE_CONTINUE_MODE = 0,
    DE_SINGLE_MODE,
    DE_AUTO_MODE,
} de_mode_t;

typedef struct {
    void *regs;
    volatile screen_state_t state;
    uint32_t delay_frame_count;     /* ISR-only: frame counter during WAKING */

    volatile uint32_t vsync_count;
    volatile bool screen_esd_reset;
    uint32_t warning_level;
    uint32_t timer_count;

    struct {
        volatile u16  write_val;
        volatile bool dirty;
    } brightness;
} sleep_ctx_t;

static sleep_ctx_t g_sleep_ctx;

/* ========================================================================== */
/* Section: Customer-implemented Register Read Hooks                          */
/*                                                                            */
/* These functions are reserved for customer implementation.                  */
/* They are called from the optional register-check path (LV_SCREEN_CHECK_    */
/* REGISTER_STATUS). By default they read panel ID / status registers;        */
/* customers may replace the body with panel-specific read sequences.         */
/* ========================================================================== */

#if (defined(LV_SCREEN_DBI_SPI) || defined(LV_SCREEN_DBI_I8080)) && LV_SCREEN_CHECK_REGISTER_STATUS
static void dbi_read_screen_register(sleep_ctx_t *ctx)
{
    /*
     * Example — read a register and flag the LCD monitor timer to reset:
     *
     * int i, val[2];
     *
     * if (ctx->screen_esd_reset)
     *     return;
     *
     * DCS_READ_CMD(ctx->regs, 0x0C, 2);      // pick a register that reflects panel status
     * for (i = 0; i < 2; i++)
     *     val[i] = DCS_READ_DATA(ctx->regs);
     *
     * if (val[0] != 0xFE && val[1] != 0xFE)  // check logic per panel datasheet
     *     ctx->screen_esd_reset = true;      // set to trigger LCD reset in monitor timer
     */
}
#endif /* (DBI_SPI || DBI_I8080) && LV_SCREEN_CHECK_REGISTER_STATUS */

#if defined(LV_SCREEN_DSI) && LV_SCREEN_CHECK_REGISTER_STATUS
static void dsi_read_screen_register(sleep_ctx_t *ctx)
{
    /*
     * Example — read a register and flag the LCD monitor timer to reset:
     *
     * int val;
     *
     * if (ctx->screen_esd_reset)
     *     return;
     *
     * DCS_READ_CMD(ctx->regs, 0x0C, 1);     // pick a register that reflects panel status
     * val = DCS_READ_DATA(ctx->regs);
     *
     * if (val != 0xFE)                      // check logic per panel datasheet
     *     ctx->screen_esd_reset = true;     // set to trigger LCD reset in monitor timer
     */
}
#endif /* LV_SCREEN_DSI && LV_SCREEN_CHECK_REGISTER_STATUS */

/* ========================================================================== */
/* Section: Interface-specific Helper Functions                               */
/* ========================================================================== */

/* ---- DBI reset & enable -------------------------------------------------- */
#if defined(LV_SCREEN_DBI_SPI) || defined(LV_SCREEN_DBI_I8080)
static void dbi_reset_and_enable(void)
{
    hal_reset_assert(RESET_DBI);
    aic_udelay(2);
    hal_reset_deassert(RESET_DBI);
    aic_udelay(2);
    extern int aic_dbi_enable(void);
    aic_dbi_enable();
}
#endif

/* Unified pre-command hook: DBI needs reset before each DCS burst; DSI is no-op */
static inline void interface_pre_cmd(void)
{
#if defined(LV_SCREEN_DBI_SPI) || defined(LV_SCREEN_DBI_I8080)
    dbi_reset_and_enable();
#else
    (void)0;
#endif
}

/* ---- DSI DCS send -------------------------------------------------------- */
#if defined(LV_SCREEN_DSI)
static int mipi_dsi_dcs_send(void *base, size_t size, const u8 *data)
{
    u32 type;

    switch (size) {
    case 0:
        return -EINVAL;
    case 1:
        type = MIPI_DSI_DCS_SHORT_WRITE;
        break;
    case 2:
        type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
        break;
    default:
        type = MIPI_DSI_DCS_LONG_WRITE;
        break;
    }

    dsi_cmd_wr(base, type, 0, data, size);
    return 0;
}
#endif /* LV_SCREEN_DSI */

/* ========================================================================== */
/* Section: TE Flush Helpers                                                  */
/* ========================================================================== */
static void de_video_start(void)
{
#if LV_SCREEN_USE_TE_FLUSH
    void *de = (void *)DE_BASE;
    de_set_mode(de, DE_AUTO_MODE);
#endif
}

static void de_video_continue(void)
{
#if LV_SCREEN_USE_TE_FLUSH
    void *de = (void *)DE_BASE;
    de_set_mode(de, DE_CONTINUE_MODE);
    de_timing_enable(de, 1);
#endif
}

/* ========================================================================== */
/* Section: Optional — Register Status Check                                  */
/* ========================================================================== */
#if LV_SCREEN_CHECK_REGISTER_STATUS

/* ---- DBI register read --------------------------------------------------- */
#if defined(LV_SCREEN_DBI_SPI) || defined(LV_SCREEN_DBI_I8080)

# if LV_SPI_RD_SWITCH_CLK
#  define LV_PLL_DISP_FREQ_MIN   (360 * 1000 * 1000)
#  define LV_DBI_SPI_MODE(x)     ((x >> 20) & 0x7)
#  define LV_DBI_SPI_FOEMAT(x)   ((x >> 24) & 0x7)

static u32 swicth_clk_div[][3] = {
    /* RGB565  RGB666  RGB888 */
    [0] = { 72, 108, 108 },  /* 3-line 9-bit */
    [1] = { 64,  96,  96 },  /* 4-line       */
    [2] = { 16,  24,  24 },  /* 4-SDA        */
};

static void dbi_switch_clk(sleep_ctx_t *ctx)
{
    u64 sclk_rate, pll_disp_rate;
    u32 pixclk;
    int val, i = 0;

    val = readl(ctx->regs);
    pixclk = LV_SPI_RD_PIXEL_CLK;
    sclk_rate = pixclk * swicth_clk_div[LV_DBI_SPI_MODE(val)][LV_DBI_SPI_FOEMAT(val)];
    pll_disp_rate = sclk_rate;

    while (pll_disp_rate < LV_PLL_DISP_FREQ_MIN) {
        pll_disp_rate = sclk_rate * (2 << i);
        i++;
    }

    hal_clk_set_freq(CLK_PLL_FRA2, pll_disp_rate);
    hal_clk_set_rate(CLK_SCLK, sclk_rate, pll_disp_rate);
    hal_clk_set_rate(CLK_PIX, pixclk, sclk_rate);
}
# endif /* LV_SPI_RD_SWITCH_CLK */

static void dbi_read_screen(sleep_ctx_t *ctx)
{
    if (ctx->vsync_count % LV_SCREEN_CHECK_POLLING_FRAMES != 0)
        return;

    dbi_reset_and_enable();

# if LV_SPI_RD_SWITCH_CLK
    dbi_switch_clk(ctx);
# endif

# if LV_SPI_4SDA_RD_SWITCH_PACKET_HEAD
    int head = readl(ctx->regs + DBI_QSPI_MODE);
    qspi_code_cfg(ctx->regs, LV_SPI_RD_PACKET_HEAD, 0, 0);
# endif

# if defined(LV_SCREEN_DBI_SPI)
    spi_rd_mode(ctx->regs, LV_SPI_RD_SDA);
# endif

    dbi_read_screen_register(ctx);

# if LV_SPI_4SDA_RD_SWITCH_PACKET_HEAD
    writel(head, ctx->regs + DBI_QSPI_MODE);
# endif

# if LV_SPI_RD_SWITCH_CLK
    extern int aic_dbi_clk_enable(void);
    aic_dbi_clk_enable();
    dbi_reset_and_enable();
# endif
}
#endif /* DBI_SPI || DBI_I8080 */

/* ---- DSI register read --------------------------------------------------- */
#if defined(LV_SCREEN_DSI)
static void dsi_read_screen(sleep_ctx_t *ctx)
{
    dsi_read_screen_register(ctx);
}
#endif /* LV_SCREEN_DSI */

/* ---- Dispatcher ---------------------------------------------------------- */
static void screen_check_status(sleep_ctx_t *ctx)
{
#if defined(LV_SCREEN_DBI_SPI) || defined(LV_SCREEN_DBI_I8080)
    dbi_read_screen(ctx);
#elif defined(LV_SCREEN_DSI)
    dsi_read_screen(ctx);
#endif
}

#endif /* LV_SCREEN_CHECK_REGISTER_STATUS */

/* ========================================================================== */
/* Section: Per-frame Work (called from VSYNC ISR while AWAKE)                */
/* ========================================================================== */
static void screen_brightness_work(sleep_ctx_t *ctx)
{
    if (ctx->brightness.dirty) {
        u16 val = ctx->brightness.write_val;
        u8 hi = val >> 8;
        u8 lo = val & 0xff;

        interface_pre_cmd();
        DCS_SEND_CMD(ctx->regs, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, hi, lo);
        ctx->brightness.dirty = false;
    }
}

static void screen_vsync_work(sleep_ctx_t *ctx)
{
#if LV_SCREEN_CHECK_REGISTER_STATUS
    screen_check_status(ctx);
#endif
    screen_brightness_work(ctx);
}

/* ========================================================================== */
/* Section: VSYNC ISR Callback                                                */
/* ========================================================================== */
static void screen_vsync_cb(void *data)
{
    sleep_ctx_t *ctx = data;
    void *regs = ctx->regs;

    switch (ctx->state) {
    case STATE_AWAKE:
        ctx->delay_frame_count = 0;
        ctx->vsync_count++;
        screen_vsync_work(ctx);
        return;

    case STATE_SLEEPING:
        de_video_continue();
        interface_pre_cmd();
        DCS_SEND_CMD(regs, DCS_DISPLAY_OFF);
        DCS_SEND_CMD(regs, DCS_SLEEP_IN);
        ctx->state = STATE_SLEEP;
        return;

    case STATE_SLEEP:
        /* Idle — waiting for API to set STATE_WAKING */
        return;

    case STATE_WAKING:
        if (ctx->delay_frame_count == 0) {
            /* First frame: issue Sleep Out */
            interface_pre_cmd();
            DCS_SEND_CMD(regs, DCS_SLEEP_OUT);
        }
        if (++ctx->delay_frame_count < WAKE_DELAY_FRAMES)
            return;
        /* Delay complete: issue Display On */
        de_video_start();
        interface_pre_cmd();
        DCS_SEND_CMD(regs, DCS_DISPLAY_ON);
        ctx->delay_frame_count = 0;
        ctx->state = STATE_AWAKE;
        return;
    }
}

/* ========================================================================== */
/* Section: Backlight Control                                                 */
/* ========================================================================== */
static void backlight_off(void)
{
#ifdef AIC_PANEL_ENABLE_GPIO
    unsigned int g, p;
    long pin;

    pin = hal_gpio_name2pin(AIC_PANEL_ENABLE_GPIO);
    g = GPIO_GROUP(pin);
    p = GPIO_GROUP_PIN(pin);

#ifndef AIC_PANEL_ENABLE_GPIO_LOW
    hal_gpio_clr_output(g, p);
#else
    hal_gpio_set_output(g, p);
#endif
#endif /* AIC_PANEL_ENABLE_GPIO */

#if defined(KERNEL_RTTHREAD) && defined(AIC_PWM_BACKLIGHT_CHANNEL)
    struct rt_device_pwm *pwm_dev;

    pwm_dev = (struct rt_device_pwm *)rt_device_find("pwm");
    rt_pwm_disable(pwm_dev, AIC_PWM_BACKLIGHT_CHANNEL);
#endif
}

static void backlight_on(void)
{
#ifdef AIC_PANEL_ENABLE_GPIO
    unsigned int g, p;
    long pin;

    pin = hal_gpio_name2pin(AIC_PANEL_ENABLE_GPIO);
    g = GPIO_GROUP(pin);
    p = GPIO_GROUP_PIN(pin);

    hal_gpio_direction_output(g, p);

#ifndef AIC_PANEL_ENABLE_GPIO_LOW
    hal_gpio_set_output(g, p);
#else
    hal_gpio_clr_output(g, p);
#endif
#endif /* AIC_PANEL_ENABLE_GPIO */

#if defined(KERNEL_RTTHREAD) && defined(AIC_PWM_BACKLIGHT_CHANNEL)
    struct rt_device_pwm *pwm_dev;

    pwm_dev = (struct rt_device_pwm *)rt_device_find("pwm");

#ifndef AIC_PWM_BACKLIGHT_BYPASS
    /* pwm frequency: 1 KHz = 1000000 ns */
    rt_pwm_set(pwm_dev, AIC_PWM_BACKLIGHT_CHANNEL,
               1000000, 10000 * AIC_PWM_BRIGHTNESS_LEVEL);
#endif
    rt_pwm_enable(pwm_dev, AIC_PWM_BACKLIGHT_CHANNEL);
#endif
}

/* ========================================================================== */
/* Section: Public API                                                        */
/* ========================================================================== */
void screen_sleep_enter(void)
{
    screen_state_t cur = g_sleep_ctx.state;

    /* Already asleep or sleep in progress — nothing to do */
    if (cur == STATE_SLEEP || cur == STATE_SLEEPING)
        return;

    /* Turn off backlight before issuing DCS sleep commands */
    backlight_off();

    /* Kick off sleep: API writes state, ISR executes on next vsync */
    size_t flag = aicos_enter_critical_section();
    g_sleep_ctx.state = STATE_SLEEPING;
    aicos_leave_critical_section(flag);

    int timeout = SLEEP_ENTER_TIMEOUT;
    while (g_sleep_ctx.state != STATE_SLEEP) {
        aic_mdelay(1);
        if (--timeout <= 0) {
            printf("screen_sleep_enter timeout\n");
            /* Cancel pending operation: revert state so API stays consistent */
            flag = aicos_enter_critical_section();
            if (g_sleep_ctx.state == STATE_SLEEPING)
                g_sleep_ctx.state = STATE_AWAKE;
            aicos_leave_critical_section(flag);
            backlight_on();
            return;
        }
    }
}

void screen_sleep_exit(void)
{
    screen_state_t cur = g_sleep_ctx.state;

    /* Already awake or wake in progress — nothing to do */
    if (cur == STATE_AWAKE || cur == STATE_WAKING)
        return;

    /* Kick off wake: API writes state, ISR executes on next vsync */
    size_t flag = aicos_enter_critical_section();
    g_sleep_ctx.state = STATE_WAKING;
    aicos_leave_critical_section(flag);

    int timeout = SLEEP_EXIT_TIMEOUT;
    while (g_sleep_ctx.state != STATE_AWAKE) {
        aic_mdelay(1);
        if (--timeout <= 0) {
            printf("screen_sleep_exit timeout\n");
            /* Cancel pending operation: revert state so API stays consistent */
            flag = aicos_enter_critical_section();
            if (g_sleep_ctx.state == STATE_WAKING) {
                g_sleep_ctx.state = STATE_SLEEP;
                g_sleep_ctx.delay_frame_count = 0;
            }
            aicos_leave_critical_section(flag);
            return;
        }
    }

    /* Display is ready — turn on backlight */
    backlight_on();
}

bool screen_is_sleeping(void)
{
    return g_sleep_ctx.state != STATE_AWAKE;
}

void screen_set_brightness(u16 brightness)
{
    size_t flag = aicos_enter_critical_section();

    g_sleep_ctx.brightness.write_val = brightness;
    g_sleep_ctx.brightness.dirty = true;
    aicos_leave_critical_section(flag);
}

u16 screen_get_brightness(void)
{
    return g_sleep_ctx.brightness.write_val;
}

/* ========================================================================== */
/* Section: Optional — LCD Monitor Timer                                      */
/* ========================================================================== */
#if LV_SCREEN_TE_CHECK || LV_SCREEN_CHECK_REGISTER_STATUS

static void screen_reset_lcd(sleep_ctx_t *ctx)
{
    struct mpp_fb *fb = mpp_fb_open();

    mpp_fb_ioctl(fb, AICFB_POWEROFF, 0);

    pr_err("screen reset\n");
    aic_mdelay(20);
    g_sleep_ctx.state = STATE_SLEEP;
    mpp_fb_ioctl(fb, AICFB_POWERON, 0);
    screen_sleep_exit();
}

static void screen_monitor_cb(lv_timer_t *timer)
{
    sleep_ctx_t *ctx = lv_timer_get_user_data(timer);

#if LV_SCREEN_TE_CHECK
    if (ctx->state == STATE_AWAKE) {
        if (ctx->timer_count != ctx->vsync_count) {
            ctx->timer_count = ctx->vsync_count;
            ctx->warning_level = 0;
        } else {
            ctx->warning_level++;
        }
    }
    if (ctx->warning_level > TE_WARNING_LEVEL) {
        screen_reset_lcd(ctx);
        ctx->warning_level = 0;
        return;
    }
#endif

#if LV_SCREEN_CHECK_REGISTER_STATUS
    if (ctx->screen_esd_reset) {
        ctx->screen_esd_reset = false;
        screen_reset_lcd(ctx);
    }
#endif
}

#endif /* LV_SCREEN_TE_CHECK || LV_SCREEN_CHECK_REGISTER_STATUS */

/* ========================================================================== */
/* Section: Initialization                                                    */
/* ========================================================================== */
void screen_ctl_init(void)
{
    g_sleep_ctx.regs = (void *)INTERFACE_BASE;
    g_sleep_ctx.state = STATE_AWAKE;
    g_sleep_ctx.delay_frame_count = 0;

    printf("Screen Ctl Initialization\n");

    de_register_vsync_cb(screen_vsync_cb, &g_sleep_ctx);

#if LV_SCREEN_TE_CHECK || LV_SCREEN_CHECK_REGISTER_STATUS
    lv_timer_create(screen_monitor_cb, 100, &g_sleep_ctx);
#endif
}

/* ========================================================================== */
/* Section: CLI Commands                                                      */
/* ========================================================================== */
static void sleep_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    screen_sleep_enter();
    printf("screen sleep done\n");
}
MSH_CMD_EXPORT_ALIAS(sleep_cmd, screen_sleep, put screen into sleep mode);

static void wake_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    screen_sleep_exit();
    printf("screen wake done\n");
}
MSH_CMD_EXPORT_ALIAS(wake_cmd, screen_wake, wake screen from sleep mode);

static void brightness_cmd(int argc, char **argv)
{
    unsigned long val;

    if (argc < 2) {
        printf("Usage: screen_brightness <0-65535>\n");
        return;
    }

    val = strtoul(argv[1], NULL, 0);
    if (val > 65535) {
        printf("brightness out of range (0-65535)\n");
        return;
    }

    screen_set_brightness((u16)val);
    printf("screen brightness set to %lu (pending next vsync)\n", val);
}
MSH_CMD_EXPORT_ALIAS(brightness_cmd, screen_brightness, set display brightness (0-65535));

#if LV_SCREEN_TE_CHECK || LV_SCREEN_CHECK_REGISTER_STATUS
static void esd_cmd(int argc, char **argv)
{
    int timeout = 500;
    size_t flag;

    (void)argc;
    (void)argv;

    flag = aicos_enter_critical_section();
    g_sleep_ctx.screen_esd_reset = true;
    aicos_leave_critical_section(flag);

    while (g_sleep_ctx.screen_esd_reset) {
        aic_mdelay(1);
        if (--timeout <= 0) {
            printf("screen esd reset timeout\n");
            flag = aicos_enter_critical_section();
            g_sleep_ctx.screen_esd_reset = false;
            aicos_leave_critical_section(flag);
            return;
        }
    }
    printf("screen esd reset done\n");
}
MSH_CMD_EXPORT_ALIAS(esd_cmd, screen_esd, trigger ESD recovery: reset LCD);
#endif

/* ========================================================================== */
/* Section: Stubs (when LV_AIC_SCREEN_CTL is disabled)                        */
/* ========================================================================== */
#else /* !LV_AIC_SCREEN_CTL */

void screen_ctl_init(void)
{
}

void screen_sleep_enter(void)
{
}

void screen_sleep_exit(void)
{
}

bool screen_is_sleeping(void)
{
    return false;
}

void screen_set_brightness(u16 brightness)
{
    (void)brightness;
}

u16 screen_get_brightness(void)
{
    return 0;
}

#endif /* LV_AIC_SCREEN_CTL */
