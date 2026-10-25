# 便携式工业调试仪 · 主控（ArtInChip D12x）

> **RT-Thread 嵌入式电子设计大赛 2026 参赛作品**
> 基于 RT-Thread + LVGL v9 的单芯片手持工业现场调试终端。
>
> 本仓库基于 **ArtInChip Luban-Lite SDK** 开发，作品源码位于
> [`application/rt-thread/helloworld/debug_tool/`](application/rt-thread/helloworld/debug_tool/)。

---

## 1. 作品简介

传统工业现场调试依赖笔记本 + 串口线 + 多个软件，笨重且准备时间长。本作品把「串口调试、
Modbus 主站、示波采样、Modbus 从机监测、RS485 总线透传扫描、PWM 信号源、环境传感、本板遥测、
SD 卡录波、低功耗」等现场常用功能，集成进一块巴掌大的 D12x 开发板，配合一块星火一号从机板，
构成「单芯片手持调试仪 + 配套传感器/执行器/网关节点」的轻量调试系统。

- 主控（本仓库）：ArtInChip **D12x**，480×272 RGB 屏，RT-Thread + LVGL v9
- 从机（配套仓库）：STM32F407 **星火一号**，作 Modbus 从站、传感器节点、RS485 网关
- 上位：Python 零依赖云端服务 + 微信小程序（同工作区 `tools/cloud_server`、`miniprogram`）

> 配套从机仓库：<https://github.com/yythlss/SparkNo1>

---

## 2. 硬件平台

| 项 | 规格 |
|---|---|
| SoC | ArtInChip D12x（RISC-V，内置 RGB LCD 控制器） |
| 操作系统 | RT-Thread（内核 / 设备框架 / 消息队列 / 信号量 / DFS+elmfat / PM / FinSH） |
| GUI | LVGL v9，480×272 电阻/电容触摸 |
| 存储 | SD 卡（elmfat 文件系统，CSV 录波） |
| 串口 | uart0（console + 串口调试）、uart1（独占，与从机 TTL 直连） |
| WiFi | 板载 RW007（SPI2：CS=PF10 / INT=PG11 / RST=PG15）*注：云端由从机侧经 RW007 上行* |
| 显示 | 主界面 lv_tileview 双页滑动（环境监测页 + 应用图标网格） |

**与从机（星火一号）接线**（TTL 直连，115200/8N1，**严禁 5V，双方均为 3.3V TTL**）：

| D12x（主控 uart1） | 星火一号 | 说明 |
|---|---|---|
| PA2 (TX) | PB11 (RX) | 主控发 → 从机收 |
| PA3 (RX) | PB10 (TX) | 主控收 ← 从机发 |
| GND | GND | 共地 |

---

## 3. 自研代码目录

作品全部自研代码在 `application/rt-thread/helloworld/debug_tool/`（**37 个文件 / 约 7476 行**）：

| 模块 | 职责 |
|---|---|
| `dbg_main` | 主入口：初始化各模块、加载主菜单、创建 LVGL 消息分发定时器 |
| `dbg_msg` | 轻量消息总线，解耦生产线程与 UI 刷新（队列深 64） |
| `dbg_serial` | 串口调试（uart0）：收发、波特率切换 |
| `dbg_theme` | 全局统一配色：日/夜两套色板 + 登记式自动换色 |
| `dbg_adc` | 模拟量采集：软件信号源 / GPAI 真实采集（12-bit，6 通道） |
| `dbg_modbus` | Modbus-RTU 主站（异步事务，FC01/03/06） |
| `dbg_slave` | 独占 uart1 的从机主站轮询 + RS485 透传事务 |
| `dbg_log` | SD 卡 CSV 数据记录（环形缓冲 → 落盘） |
| `dbg_power` | 电池电量（GPAI）+ 低功耗（PM LIGHT） |
| `dbg_pwm` | PWM 信号源（输出可调占空比/频率） |
| `dbg_d12x` | 本板遥测：结温 / 电量 / CPU / 内存；并中转从机上云 |
| `ui_main_screen` 等 7 个 UI 页 | 主界面 + 串口 / 示波 / 从机仪表盘 / RS485 扫描 / PWM / 环境监测页 |

> 注：CAN 模块（`dbg_can` / `ui_can_screen`）已移除——开发板无板载 CAN 收发器。

---

## 4. 线程模型（主控）

| 线程 | 优先级 | 职责 |
|---|---|---|
| LVGL 主线程 | 高 | 界面渲染、事件回调、消息分发（10 ms 定时器） |
| `dbg_d12x` | 15 | 本板遥测采集 + 中转上报 |
| `dbglog` | 20 | SD 卡落盘（慢 I/O 独立线程，不阻塞 UI） |
| `dbg_modbus` | 21 | Modbus 异步事务 |
| `dbg_serial` | 22 | 串口透传 |
| `dbg_slv` | 22 | 独占 uart1：执行器写 → RS485 扫描 → 常规轮询（信号量串行化） |
| `dbg_adc` | 23 | ADC 采样 |

---

## 5. 快速开始

### 5.1 构建（Windows 环境）

```bat
:: 进入 SDK 自带环境
win_env.bat        (或 win_cmd.bat)

:: 确认目标板为 d12x，应用为 helloworld/debug_tool
:: 如需改配置（如开启 AIC_USING_GPAI）：
km                 (app_menuconfig)

:: 构建 bootloader + application
m                  (make_boot_and_app)
```

> 改过 menuconfig 后务必 **Clean → Build**。条件编译的两个分支都要编过再提交。

### 5.2 烧录

- 使用 AiBurn（`AiBurn-1.5.3_Setup/`）或 `upgtool/` 烧录 `output/` 下产物；
- 详见仓库内 `烧录方法-AiBurn命令行.md`。

### 5.3 运行

上电后进入主界面（双页滑动）：左页环境监测，右页应用图标网格。
点按图标进入对应功能页，Back 返回右页。

---

## 6. 关键设计参数

| 项 | 值 |
|---|---|
| 从机轮询 | 500 ms / 轮，38 寄存器，27 字节打包上传 |
| SD 录波 | 环形 32×27 B，200 ms 落盘，文件 `dbg_001~999.csv` |
| 消息总线 | 队列深 64，单条 ≤64 B |
| 云端上报（从机侧） | 5 s 摘要，HTTP POST 到 `tools/cloud_server/server.py` |
| 宏 | `DBG_SLAVE_LOOPBACK`（dbg_slave.h）、`DBG_LOG_LOOPBACK`（dbg_log.c）、`DBG_D12X_POLL_MS`（dbg_d12x.c） |

---

## 7. 开源与许可

- **Luban-Lite SDK 本体**：遵循 ArtInChip 相关开源协议（详见 SDK 原始说明），版权归广东匠芯创科技有限公司所有。
- **本作品自研代码**（`application/rt-thread/helloworld/debug_tool/`）：各源文件带 SPDX 头，以 Apache License 2.0 开源。
- SDK 通用使用说明见仓库 `doc/` 目录。

---

欢迎在 Issues 中交流，也欢迎在 RT-Thread 官方论坛的参赛帖下拍砖。
