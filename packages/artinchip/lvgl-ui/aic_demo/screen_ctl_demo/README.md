# aic_screen_ctl — 屏幕模块集成指南

## 1. 概述

### 1.1 模块简介

`aic_screen_ctl` 是匠芯创（ArtInChip）Luban-Lite SDK 中用于管理屏幕的控制模块，可集成在 LVGL demo 中。它基于**四状态机**驱动，在 VSYNC 中断中安全地执行 MIPI DCS 命令（Display Off / Sleep In / Sleep Out / Display On），避免屏幕时序问题。

该模块支持三种显示接口：

- **DBI-SPI**（SPI / QSPI）
- **DBI-I8080**（并行接口）
- **DSI**（MIPI DSI）

### 1.2 核心功能

| 功能 | 说明 | 是否必须实现 |
|------|------|-------------|
| 屏幕休眠/唤醒 | 通过 DCS 命令控制面板进入/退出睡眠 | 开箱即用 |
| 屏幕亮度控制 | 通过 DCS 0x51 命令设置面板亮度（异步，VSYNC 同步） | 开箱即用 |
| TE 信号监测 | 检测 TE 信号丢失并自动复位 LCD | 可选 |
| 寄存器状态轮询 | 周期读取面板寄存器，检测 ESD 异常并自动恢复 | **需用户实现读寄存器函数** |
| CLI 调试命令 | 提供 `screen_sleep` / `screen_wake` / `screen_esd` / `screen_brightness` 命令 | 开箱即用 |

### 1.3 文件说明

将以下两个文件拷贝到你的客制化工程目录中：

| 文件 | 作用 |
|------|------|
| `aic_screen_ctl.h` | 头文件，包含所有配置宏开关和 API 声明 |
| `aic_screen_ctl.c` | 实现文件，包含状态机、ISR 回调、背光控制等全部逻辑 |

---

## 2. 配置指南

### 2.1 集成步骤

**Step 1：添加源文件**

将 `aic_screen_ctl.c` 和 `aic_screen_ctl.h` 复制到客制化 LVGL demo 目录：

```
meter_demo$ ls

    meter_ui.c
    meter_ui.h
    aic_screen_ctl.c
    aic_screen_ctl.h
```

**Step 2：调用初始化**

在你的 UI 初始化函数（如 `meter_ui_init`）中，调用 `screen_ctl_init()`：

```c
#include "aic_screen_ctl.h"

void my_ui_init(void)
{
    // ... 其他 UI 初始化 ...

#ifdef LV_AIC_SCREEN_CTL
    screen_ctl_init();
#endif
}
```

> **注意**：`screen_ctl_init()` 内部会注册 VSYNC 回调并创建监控定时器。

### 2.2 编译宏配置

所有配置宏均在 `aic_screen_ctl.h` 中定义，以下逐项说明。

#### 2.2.1 主开关（必须）

```c
#define LV_AIC_SCREEN_CTL
```

- **作用**：模块总开关。定义此宏则启用全部功能；注释掉则所有 API 变为空函数（no-op stub）。
- **默认值**：已启用。

#### 2.2.2 接口类型选择（必须，三选一）

```c
//#define LV_SCREEN_DBI_SPI      // 选择此项 → DBI SPI (QSPI) 接口
//#define LV_SCREEN_DBI_I8080    // 选择此项 → DBI i8080 并行接口
#define LV_SCREEN_DSI            // 选择此项 → MIPI DSI 接口
```

- **作用**：根据你使用的面板接口类型，**必须且只能**定义一个。模块会据此选择正确的 DCS 命令发送方式。

#### 2.2.3 TE 刷新模式与监测（可选）

```c
#define LV_SCREEN_USE_TE_FLUSH   0  // 0=不使用, 1=使用 DE AUTO 模式（依赖 TE 信号）
```

- **作用**：DE（Display Engine）在 AUTO 模式下刷新屏幕，控制指令兼容 AUTO  模式。
- **适用场景**：面板提供 TE（Tearing Effect）信号时使用。

当启用 TE 刷新模式后，可进一步开启 TE 信号监测：

```c
#if LV_SCREEN_USE_TE_FLUSH
#define LV_SCREEN_TE_CHECK       0  // 0=不监测, 1=监测 TE 信号
#endif
```

- **作用**：每 100ms 检查 TE 信号是否仍在触发。若连续丢失超过阈值，自动复位 LCD。
- **阈值配置**：在 `aic_screen_ctl.c` 中修改 `TE_WARNING_LEVEL`（默认为 5 次）。
- **限制条件**：依赖 `LV_SCREEN_USE_TE_FLUSH`，仅在 DE AUTO 模式下生效。

#### 2.2.4 寄存器状态检查 / ESD 检测（可选，需用户实现函数）

```c
#define LV_SCREEN_CHECK_REGISTER_STATUS  0  // 0=禁用, 1=启用
```

- **作用**：启用后，周期性读取面板状态寄存器，检测 ESD 异常。检测到异常后自动复位 LCD。
- **必须实现**：启用此功能后，**必须**根据面板 datasheet 实现读寄存器函数（详见 [2.3 节](#23-实现读寄存器函数)）。

轮询间隔通过以下宏配置：

```c
#define LV_SCREEN_CHECK_POLLING_FRAMES   (10u)
```

- **作用**：每 N 个 VSYNC 帧轮询一次面板寄存器。默认每 10 帧（约 167ms @ 60Hz）。

#### 2.2.5 DBI-SPI 专用配置（仅当选择 `LV_SCREEN_DBI_SPI` 时有效）

```c
#define LV_SPI_RD_SDA                     1
#define LV_SPI_4SDA_RD_SWITCH_PACKET_HEAD 1
#define LV_SPI_RD_PACKET_HEAD             0x03
#define LV_SPI_RD_SWITCH_CLK              1
#define LV_SPI_RD_PIXEL_CLK              (200000)  // 200 KHz
```

| 宏 | 作用 | 默认值 |
|------|------|--------|
| `LV_SPI_RD_SDA` | 读回模式：1=SDA 双向（读回在 D0），0=D0 只写+D1 只读 | 1 |
| `LV_SPI_4SDA_RD_SWITCH_PACKET_HEAD` | 4-SDA 模式下是否切换读包头的 Packet Head | 1 |
| `LV_SPI_RD_PACKET_HEAD` | 4-SDA 读回包头值（仅上述开关为 1 时生效） | 0x03 |
| `LV_SPI_RD_SWITCH_CLK` | 读寄存器时是否切换时钟频率 | 1 |
| `LV_SPI_RD_PIXEL_CLK` | 读回时的像素时钟频率 | 200 KHz |

**LV_SPI_RD_SDA** 时序原理如下：

```
/*
 * Case A: LV_SPI_RD_SDA = 1  —  SDA bidirectional, readback on D0
 *
 * SCL  ─┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──
 *       └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘
 * SDA   W7 W6 W5 W4 W3 W2 W1 W0 | R7 R6 R5 R4 R3 R2 R1 R0
 *
 * Case B: LV_SPI_RD_SDA = 0  —  D0 output only, readback on D1
 *
 * SCL  ─┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──┐┌──
 *       └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘  └┘
 * SDA(D0)  W7 W6 W5 W4 W3 W2 W1 W0  X  X  X  X  X  X  X  X
 * SDI(D1)   X  X  X  X  X  X  X  X R7 R6 R5 R4 R3 R2 R1 R0
 */
```

### 2.3 实现读寄存器函数

> 仅当启用 `LV_SCREEN_CHECK_REGISTER_STATUS` 时需要。相关函数在**中断上下文**中执行，要避免打印和延时语句。

在 `aic_screen_ctl.c` 中，根据接口类型找到对应函数并完善。

**DBI 接口（SPI / I8080）** — `dbi_read_screen_register()`：

> 请根据你的面板 Datasheet 实现，以下代码仅供参考

```c
static void dbi_read_screen_register(sleep_ctx_t *ctx)
{
    /* ---- 请根据你的面板 Datasheet 实现 ---- */

    int i, val[2];

    // 如果正在 ESD 复位中，跳过
    if (ctx->screen_esd_reset)
        return;

    // 1. 发起读命令（以 0x0C 为例，替换为你面板的状态寄存器地址）
    DCS_READ_CMD(ctx->regs, 0x0C, 2);

    // 2. 读取数据
    for (i = 0; i < 2; i++)
        val[i] = DCS_READ_DATA(ctx->regs);

    // 3. 检查返回值是否符合预期（以 0xFE 为例，替换为你的判断逻辑）
    if (val[0] != 0xFE && val[1] != 0xFE)
        ctx->screen_esd_reset = true;   // 值异常 → 触发 LCD 复位
}
```

**DSI 接口** — `dsi_read_screen_register()`：

> 请根据你的面板 Datasheet 实现，以下代码仅供参考

```c
static void dsi_read_screen_register(sleep_ctx_t *ctx)
{
    /* ---- 请根据你的面板 Datasheet 实现 ---- */

    int i, val[2];

    if (ctx->screen_esd_reset)
        return;

    // DSI 读寄存器示例
    DCS_READ_CMD(ctx->regs, 0x0C, 2);
    for (i = 0; i < 2; i++)
        val[i] = DCS_READ_DATA(ctx->regs);

    if (val[0] != 0xFE && val[1] != 0xFE)
        ctx->screen_esd_reset = true;
}
```

> **提示**：常见的可读取寄存器包括：
> - `0x09`（Read Display Status）
> - `0x0A`（Read Display Power Mode）
> - `0x0C`（Read Display Pixel Format）
> - `0xDA`（Read Display ID）
> - 面板厂商自定义的状态寄存器
>
> 具体请参考你的面板 Datasheet。

### 2.4 配置场景速查

以下是覆盖常见需求的四种典型配置组合。

**场景一：最简配置（仅休眠/唤醒，无监控）**

```c
#define LV_AIC_SCREEN_CTL
#define LV_SCREEN_DSI              // 根据接口修改
#define LV_SCREEN_USE_TE_FLUSH  0
#define LV_SCREEN_CHECK_REGISTER_STATUS  0
```

**场景二：带 TE 信号监测（DE AUTO 模式）**

```c
#define LV_AIC_SCREEN_CTL
#define LV_SCREEN_DSI
#define LV_SCREEN_USE_TE_FLUSH  1
#define LV_SCREEN_TE_CHECK      1
#define LV_SCREEN_CHECK_REGISTER_STATUS  0
```

> 需要面板硬件提供 TE 信号。无需实现读寄存器函数。

**场景三：带 ESD 检测（寄存器状态检查）**

```c
#define LV_AIC_SCREEN_CTL
#define LV_SCREEN_DSI
#define LV_SCREEN_USE_TE_FLUSH  0
#define LV_SCREEN_CHECK_REGISTER_STATUS  1
```

> **必须实现** `dsi_read_screen_register()` 函数，根据面板 Datasheet 读取指定寄存器并判断返回值。

**场景四：全功能（TE 监测 + ESD 检测）**

```c
#define LV_AIC_SCREEN_CTL
#define LV_SCREEN_DSI
#define LV_SCREEN_USE_TE_FLUSH  1
#define LV_SCREEN_TE_CHECK      1
#define LV_SCREEN_CHECK_REGISTER_STATUS  1
```

> 需要面板硬件提供 TE 信号 + 实现读寄存器函数。

---

## 3. 调试指南

### 3.1 CLI 调试命令

编译运行后，在串口终端中可使用以下命令：

```
msh />screen_sleep          # 手动进入休眠
screen sleep done

msh />screen_wake           # 手动唤醒屏幕
screen wake done

msh />screen_brightness 2048   # 设置面板亮度（范围 0-65535）
screen brightness set to 2048 (pending next vsync)

msh />screen_esd            # 手动触发 ESD 恢复（复位 LCD）
screen esd reset done
```

> `screen_esd` 命令仅在 `LV_SCREEN_TE_CHECK` 或 `LV_SCREEN_CHECK_REGISTER_STATUS` 至少一个为 1 时才可用。

---

## 4. 设计说明

### 4.1 API 参考

#### Public API

```c
void screen_ctl_init(void);            // 初始化：注册 VSYNC 回调、创建监控定时器
void screen_sleep_enter(void);         // 进入休眠：依次执行 背光关 → Display Off → Sleep In
void screen_sleep_exit(void);          // 退出休眠：依次执行 Sleep Out → 等 N 帧 → Display On → 背光开
bool screen_is_sleeping(void);         // 查询是否处于休眠状态（含过渡态）
void screen_set_brightness(u16 val);   // 设置面板亮度（0-65535，异步，下个 VSYNC 生效）
```

#### 典型用法

```c
// 用户无操作 30 秒后自动休眠
static void auto_sleep_timer_cb(lv_timer_t *timer)
{
    screen_sleep_enter();
}

// 用户触摸唤醒
static void touch_event_cb(lv_event_t *e)
{
    if (screen_is_sleeping())
        screen_sleep_exit();
}

// 亮度滑块回调：将 LVGL slider 值映射到面板亮度
static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);        // LVGL slider 值
    screen_set_brightness((u16)val);               // 异步发送 DCS 0x51
}

// 初始化时启动自动休眠定时器
lv_timer_create(auto_sleep_timer_cb, 30000, NULL);
```

#### 注意事项

1. **`screen_sleep_enter()` 是阻塞调用**：最多等待 `SLEEP_ENTER_TIMEOUT`（100ms）。如果超时，会自动回退状态并恢复背光。
2. **`screen_sleep_exit()` 是阻塞调用**：最多等待 `SLEEP_EXIT_TIMEOUT`（300ms）。唤醒需要等待 N 帧（`WAKE_DELAY_FRAMES` = 8 帧，约 120ms @ 60Hz）。
3. **`screen_set_brightness()` 是非阻塞调用**：仅记录亮度值，立即返回，实际 DCS 命令在下一个 VSYNC 中断中发送。连续快速调用时，中间值会被覆盖，仅最后一次生效。
4. **不可重入**：不要在 ISR 上下文中调用睡眠/唤醒 API。睡眠/唤醒过程依赖 VSYNC ISR 回调，API 本身在任务上下文中轮询状态。

### 4.2 状态机原理

#### 四状态转换图

```
                     screen_sleep_enter()
     AWAKE ──────────────────────────────────► SLEEPING
      ▲                                           │
      │ 0x29 (Display On) 发送完毕                │ 0x28 + 0x10 发送完毕
      │                                           ▼
     WAKING ◄────────────────────────────────── SLEEP
                     screen_sleep_exit()
                     (0x11 发送, 等待 N 帧)
```

#### 各状态说明

| 状态 | 含义 | 触发条件 |
|------|------|---------|
| `STATE_AWAKE` | 正常显示状态 | 初始化默认 / 唤醒完成 |
| `STATE_SLEEPING` | 待休眠（等待 VSYNC ISR 发送休眠命令） | API 调用 `screen_sleep_enter()` |
| `STATE_SLEEP` | 完全休眠（面板已关闭） | ISR 发送完 0x28 + 0x10 |
| `STATE_WAKING` | 待唤醒（等待 N 帧后发送 Display On） | API 调用 `screen_sleep_exit()` |

#### 为什么在 VSYNC ISR 中发送 DCS 命令

- DCS 命令要求在帧间隙中发送，避免撕裂。
- VSYNC 中断确保每次命令都在安全的时序窗口内执行。
- DBI 接口在每次发送前还会执行 `dbi_reset_and_enable()` 确保硬件模块处于就绪状态。

#### 亮度控制异步机制

`screen_set_brightness()` 采用**非阻塞异步模式**：API 仅记录亮度值并设置脏标志，实际 DCS 0x51 命令在下一个 VSYNC 中断的 `screen_vsync_work()` 中发送：

```
screen_set_brightness(val)
  │  临界区内: pending_brightness = val, brightness_dirty = true （立即返回）
  ▼
下一个 VSYNC ISR → STATE_AWAKE → screen_vsync_work()
  │  DCS_SEND_CMD(0x51, val>>8, val&0xff)   // big-endian
  │  brightness_dirty = false
```

与休眠/唤醒的阻塞等待不同，亮度设置**立即返回**，不轮询完成状态。连续快速调用时，中间值会被覆盖，仅最后一次生效（类似 LVGL 的 `lv_obj_invalidate()`）。

### 4.3 依赖关系

```
aic_screen_ctl.c
 ├── aic_core.h          — ArtInChip 核心 API（延时、临界区）
 ├── aic_hal.h           — HAL 层基础（时钟、GPIO）
 ├── aic_hal_dbi.h       — DBI HAL（i8080_cmd_wr / spi_cmd_wr 等）
 ├── aic_hal_dsi.h       — DSI HAL（dsi_cmd_wr / dsi_cmd_wr 等）
 ├── aic_hal_de.h        — DE HAL（de_set_mode / de_timing_enable 等）
 ├── aic_hal_reset.h     — 复位控制（hal_reset_assert / deassert）
 ├── aic_drv_de.h        — DE 驱动（de_register_vsync_cb）
 ├── artinchip_fb.h      — 帧缓冲（结构体定义）
 ├── mipi_display.h      — MIPI DSI DCS 命令类型定义
 ├── mpp_fb.h            — MPP 帧缓冲（AICFB_POWERON / POWEROFF）
 └── lvgl.h              — LVGL（lv_timer_create）
```

> 以上均为 Luban SDK 内部库，引入 `aic_screen_ctl.c` 后 SDK 环境已自动包含。

---

## 5. FAQ

### Q1: 我的面板不是 DSI，是 SPI 接口，能用吗？

可以。在 `aic_screen_ctl.h` 中将 `LV_SCREEN_DSI` 注释掉，改为定义 `LV_SCREEN_DBI_SPI` 即可。根据你的 SPI 读回电路，再配置 `LV_SPI_RD_SDA` 等 DBI-SPI 专用宏。

### Q2: 我不需要背光控制，怎么跳过？

将 `backlight_on()` 和 `backlight_off()` 函数体清空即可。它们不再引用任何外部 GPIO/PWM 宏。

### Q3: `screen_sleep_enter()` 返回了但屏幕没关？

检查 VSYNC 中断是否正常工作。休眠命令在 VSYNC ISR 中发送，如果 VSYNC 不触发，状态机会停留在 `STATE_SLEEPING` 直至超时。超时后 API 会自动回退并打印 `"screen_sleep_enter timeout"`。

### Q4: 如何适配我的面板的寄存器检查？

参考 [2.3 节](#23-实现读寄存器函数)，在 `dbi_read_screen_register()` 或 `dsi_read_screen_register()` 中实现你面板的读寄存器时序。宏 `DCS_READ_CMD` 和 `DCS_READ_DATA` 已按接口类型自动适配，你只需指定寄存器地址和预期值即可。

### Q5: `LV_SCREEN_CHECK_REGISTER_STATUS` 使能后屏幕闪烁或者黑屏

- `dbi_read_screen_register()/dsi_read_screen_register()` 执行时间过长，影响了刷屏时序。

    这两个函数在中断上下文中执行，生产环境要避免大段延时和 log 打印。同时延长 V 方向消隐区，为函数执行提供更宽的时间窗口。

- 中断竞争导致 vsync callback 响应不及时。

    系统中存在其他外设频繁触发中断，导致 vsync callback 无法及时响应，影响显示接口时序。
    MIPI-DSI 显示接口可能触发 `DPI FIFO OVERFLOW` 异常。

    优化中断逻辑，减少中断竞争，可考虑通过 `aicos_irq_set_prio()` 修改中断优先级, 数值越大，优先级越高。

- D21x 系列平台的 MIPI-DSI 显示接口，LP 读操作需要更高的速度

    推荐 18M, 修改 `disp_conf.h` 文件的 `MIPI_DSI_LP_RATE` 宏定义。
