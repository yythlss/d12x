# I2C 主从环回测试 (i2c_loop)

## 一、简介
I2C 主从环回测试工具，将 SoC 上两个 I2C 控制器分别配置为主机(Master)和从机(Slave)，通过 SDA/SCL 短接实现自发自收，验证 I2C 控制器在多种速率、stuck 检测阈值、包长、7bit/10bit 地址模式、combined 格式等场景下的信号完整性与通信可靠性。

测试覆盖：
- 7bit/10bit 地址模式基本环回
- 100k/200k/400k 三档速率扫描
- SDA/SCL stuck 检测阈值扫描（50~2000 cycles）
- 1~255 字节包长扫描
- Write-then-Read combined 格式（restart）
- 7bit/10bit 总线地址扫描
- 长时间压力测试与随机数据测试
- 一键完整测试套件（三阶段：当前配置验证 → 模式功能验证 → 全功能验证）

## 二、环境依赖
- ArtInChip Luban-Lite SDK（RT-Thread）
- 硬件要求：I2C0 与 I2C1 的 SDA、SCL 引脚在板上短接（环回）
- 驱动支持：需使能 I2C 驱动（`AIC_I2C_DRV`）

## 三、目录结构
| 文件 | 位置 |
|------|------|
| `test_i2c_loop.c` | I2C 主从环回测试主程序 |
| `SConscript` | 构建脚本 |

## 四、配置说明
### Kconfig 选项
| Kconfig | 说明 | 默认值 | 依赖 |
|---------|------|--------|------|
| `AIC_I2C_DRV` | 使能 I2C 驱动 | n | - |
| `AIC_I2C0` | 使能 I2C0 控制器（Slave） | y | AIC_I2C_DRV |
| `AIC_I2C1` | 使能 I2C1 控制器（Master） | y | AIC_I2C_DRV |

通过 `scons --menuconfig` 进行配置：
```
menuconfig
  → Board options
      → Enable I2C
          → Enable I2C0
          → Enable I2C1
```

### 地址模式配置
I2C0 和 I2C1 的 7bit/10bit 地址模式在 menuconfig 中选择，测试程序 `init` 时会自动读取当前硬件配置，不强制修改：
- 两边均为 7bit 模式：`test` 命令应 PASS
- 两边均为 10bit 模式：`test10` 命令应 PASS
- 两边模式不匹配：`test`/`test10` 均应 FAIL（信号真实不通过）

### 硬件连接
| I2C1 (Master) | I2C0 (Slave) |
|---------------|--------------|
| SDA | SDA |
| SCL | SCL |
| GND | GND |

## 五、使用说明
所有命令在 MSH Shell 中以 `i2c_loop <子命令>` 形式执行。使用前必须先执行 `init` 初始化。

### `init` — 初始化测试环境
初始化 I2C 设备、注册 Slave 回调、设置默认速率 400kHz 和 stuck 检测 400 cycles，自动读取当前 menuconfig 配置的地址模式。

**使用示例：**
```bash
i2c_loop init
```
输出示例：
```
[OK] init: master=7bit slave=7bit addr=0x0A speed=400000 stuck=400
```

### `deinit` — 释放资源
关闭 I2C 设备，释放占用资源。

**使用示例：**
```bash
i2c_loop deinit
```

### `test` — 7bit 地址环回测试（当前配置）
在**当前硬件配置**下使用 7bit 地址 0x0A 进行 16 字节环回测试，不修改地址模式。
- 两边均为 7bit 模式 → PASS
- 任意一边为 10bit 模式或模式不匹配 → FAIL（真实信号行为）

**使用示例：**
```bash
i2c_loop test
```

### `test10` — 10bit 地址环回测试（临时切换）
临时将 Master 和 Slave 均切换为 10bit 模式，使用 10bit 地址 0x155 进行 16 字节环回测试，测试完成后恢复原配置。用于验证 10bit 地址模式硬件功能是否正常。

**使用示例：**
```bash
i2c_loop test10
```

### `speed <hz>` — 指定速率测试
设置 Master 时钟速率并进行 16 字节环回测试，测试完成后恢复原速率。支持 100000(100k)、200000(200k)、400000(400k) 三档。

**参数说明：**
| 参数 | 说明 |
|------|------|
| `<hz>` | 目标速率（Hz），可选 100000/200000/400000 |

**使用示例：**
```bash
i2c_loop speed 100000
```

### `stuck <cycles>` — 指定 stuck 阈值测试
设置 Master 和 Slave 的 SDA/SCL stuck 检测阈值并进行 16 字节环回测试，测试完成后恢复原阈值。

**参数说明：**
| 参数 | 说明 |
|------|------|
| `<cycles>` | stuck 检测阈值（时钟周期数），建议范围 50~2000 |

**使用示例：**
```bash
i2c_loop stuck 200
```

### `len <n>` — 指定包长测试
在当前配置下进行指定长度的环回测试。

**参数说明：**
| 参数 | 说明 |
|------|------|
| `<n>` | 测试包长（字节），范围 1~255 |

**使用示例：**
```bash
i2c_loop len 64
```

### `writeread` — Combined 格式测试（write-then-read/restart）
测试 I2C combined 格式：先写 1 字节寄存器地址（0x00），发送 restart 条件（无 STOP），再读回 16 字节数据。这是传感器类 I2C 设备最常用的传输格式。测试时临时切换为 7bit 模式，完成后恢复原配置。

**使用示例：**
```bash
i2c_loop writeread
```

### `scan` — 7bit 地址总线扫描
在当前配置下扫描 0x08~0x77 全部 7bit 地址，报告有 ACK 响应的设备地址。环回测试下应只发现 0x0A（7bit 模式）或无设备（10bit 模式）。

**使用示例：**
```bash
i2c_loop scan
```
输出示例：
```
--- 7bit Address Scan (0x08~0x77) ---
Found: 0x0A
Total 1 device(s)
```

### `scan10` — 10bit 地址总线扫描
临时将 Master 切换为 10bit 模式，扫描 0x000~0x3FF 全部 10bit 地址（跳过保留段 0x000~0x007 和 0x078~0x07F），报告有 ACK 响应的设备地址，扫描完成后恢复原配置。环回测试下应只发现 0x155。

**使用示例：**
```bash
i2c_loop scan10
```

### `speedsweep` — 速率扫描测试
在当前配置下依次测试 100kHz、200kHz、400kHz 三档速率下的 16 字节环回，统计通过数，测试完成后恢复原速率。

**使用示例：**
```bash
i2c_loop speedsweep
```
输出示例：
```
###### SPEED SWEEP (3 levels: 100k/200k/400k) ######
[1/3] 100000 Hz: PASS
[2/3] 200000 Hz: PASS
[3/3] 400000 Hz: PASS
###### SPEED RESULT: 3/3 ######
```

### `stucksweep` — Stuck 阈值扫描测试
在当前配置下依次测试 50/100/200/400/800/1000/2000 cycles 七档 stuck 检测阈值下的 16 字节环回，统计通过数，测试完成后恢复原阈值。

**使用示例：**
```bash
i2c_loop stucksweep
```

### `lensweep` — 包长扫描测试
在当前配置下依次测试 1/2/4/8/16/32/64/128/255 字节九种包长的环回，统计通过数。

**使用示例：**
```bash
i2c_loop lensweep
```

### `stress [n]` — 压力测试
在当前配置下连续进行 N 轮（默认 100 轮）16 字节环回测试，统计通过/失败数、耗时和每秒循环次数，用于验证长时间运行稳定性。

**参数说明：**
| 参数 | 说明 |
|------|------|
| `[n]` | 循环次数，可选，默认 100 |

**使用示例：**
```bash
i2c_loop stress 1000
```
输出示例：
```
--- Stress Test: 100 loops @ speed=400000 stuck=400 ---
[RESULT] pass=100 fail=0 total=100 time=xxxms (xxx loops/s)
```

### `random [n]` — 随机数据测试
在当前配置下进行 N 轮（默认 50 轮）随机长度（1~64 字节）、随机内容的环回测试，避免固定测试数据的模式敏感性。

**参数说明：**
| 参数 | 说明 |
|------|------|
| `[n]` | 循环次数，可选，默认 50 |

**使用示例：**
```bash
i2c_loop random 200
```

### `all` — 一键完整测试套件
三阶段完整测试，测试完成后**自动恢复**到 init 时的原始配置：

1. **Stage 1: 当前配置验证** — 不修改任何配置，直接测试当前 menuconfig 配置下的通信，真实反映配置是否正确
2. **Stage 2: 模式基本功能** — 临时切换为 7bit 匹配模式测试 7bit 功能，再临时切换为 10bit 匹配模式测试 10bit 功能，每项测完恢复
3. **Stage 3: 全功能验证** — 在 7bit 匹配模式下依次执行 writeread、speedsweep、stucksweep、lensweep、stress(100)，全面验证硬件功能

**使用示例：**
```bash
i2c_loop all
```

## 六、其他说明
- **地址模式匹配原则**：I2C 通信要求 Master 和 Slave 地址模式一致（均为 7bit 或均为 10bit），模式不匹配时 Slave 不会 ACK，测试应 FAIL——这是正确的信号行为，不是 bug
- **测试数据模式**：发送数据采用递增模式（0x00, 0x01, 0x02, ...），便于定位第一个出错字节的位置
- **Slave 回调注意事项**：ArtInChip I2C Slave 每发送/接收 1 字节触发一次中断，代码通过 `g_read_active` 标志处理多字节传输的 offset 管理，避免多字节读时每字节都重置 offset 的问题
- **寄存器修改原则**：所有模式切换、地址设置、stuck 设置均遵循 `disable → 修改 → enable` 顺序，禁止热改寄存器导致硬件状态机死锁
- **10bit 测试地址选择**：10bit 测试使用地址 0x155，其低 7 位为 0x55，与 7bit 地址 0x0A 不重叠，避免 7bit 模式下低 7 位巧合匹配导致假 PASS
- **不支持的速率**：硬件仅支持 100kHz/200kHz/400kHz 三档，请勿设置 10kHz/50kHz/1MHz 等不支持的速率，否则可能导致总线异常
