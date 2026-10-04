# 板卡烧录方法 —— AiBurn 命令行（推荐）

> 适用于 demo68-nor 等 ArtInChip d12x 板卡。
> 本方法实测可绕过 AiBurn GUI 在 BROM 段会出现的 `no CSW` 烧录失败。

## 为什么 GUI 会失败，命令行能成功

- **AiBurn GUI「开始」按钮**：走 BROM 阶段 **16KB 大块突发**（`chunk 16384`），但 BROM 的串口 RX 缓冲只有 **2048 字节**（`CONFIG_AIC_DEV_UART0_RX_BUFSZ=2048`），16KB 直接冲垮 → 报 `no CSW`。
- **upgcmd 命令行**：在 BROM 阶段就用 **小块（`len 512` / `len 2048`）+ 每帧 ACK** 传输，BROM 稳接；下载完 updater 进入 bootloader 后，再自动切换到 64KB 大块 + 逐帧 CRC 校验继续。全程零丢包。

核心就一句：**不要用 GUI 烧，用命令行 upgcmd 指定 115200 烧。**

## 使用前置条件

1. 板子和电脑已连接（Type-C / USB 转串口，识别到 COM 口）。
2. 驱动已装（ArtInChip SoC / CH340 等）。
3. 有编译好的镜像：
   `output/<chip>_<board>_<kernel>_<app>/images/<chip>_<board>_v<ver>.img`

## 操作步骤

### 1. 让板子进入下载模式
任选一种（官方文档 `doc/topics/sdk/advanced/image_burn_and_config.html` 列了三种）：
- 按住板上 **UBOOT/BOOT 键**不放，再上电/按复位，2~3 秒后松开。
- 上电前**拉低 PA0**（进入 BROM 下载模式）。
- 板子已正常启动进 RT-Thread 后，在串口 shell 敲 `aicupg`（进 bootloader 升级）。

> 推荐用 **UBOOT 键/PA0** 进 BROM 下载模式：最简单、无需能敲命令。

### 2. 关掉 AiBurn GUI（避免占用串口）
AiBurn 界面上若打开，会占用 COM 口导致命令行连不上。烧录前**关闭 AiBurn**。

### 3. 用命令行烧录
找到 upgcmd（AiBurn 自带 V1.5.3，比 SDK `upgtool/` 里那个 V1.4.2 新，`image` 命令更全）：

```
"D:\Project\claude\RTthread\AiBurn-1.5.3_Setup\AiBurn\upgcmd.exe" -u COM7 -b 115200 -p image "<镜像.img 完整路径>"
```

参数说明：
- `-u COM7`：换成你板子实际使用的串口号（设备管理器里看）。
- `-b 115200`：**务必用 115200 起步**（低波特率最稳）。
- `-p`：显示进度百分比。
- `image <镜像>`：整包烧录（会自动拆成各分区逐个烧）。

**示例（本机 demo68-nor helloworld）：**
```powershell
& "D:\Project\claude\RTthread\AiBurn-1.5.3_Setup\AiBurn\upgcmd.exe" `
  -u COM7 -b 115200 -p image `
  "D:/Project/claude/RTthread/luban-lite/output/d12x_demo68-nor_rt-thread_helloworld/images/d12x_demo68-nor_v1.0.0.img"
```

### 4. 等待完成
- 会依次烧：`updater → bootloader(spl) → env → os → rodata → data`。
- 全程若看到 `Got ACK` 一路推进，说明正常。
- 最后出现 **`Progress: 100%`**（或类似 DONE）即烧录成功。
- **烧录过程中不要拔线、不要复位板子。**

### 5. 烧完验证
复位板子，正常启动会进入 RT-Thread（串口 shell 提示符 `aic>` / `msh`）或运行你的应用。

## 其它常用命令（备用）

```powershell
# 查看当前连到的设备/端口
upgcmd -l

# 只解析/查看镜像信息（不烧录）
upgcmd -i "<镜像.img>"

# 只有部分分区需要重烧时，指定 fwc 名（如只烧 kernel/os）
upgcmd -u COM7 -b 115200 image "<镜像.img>" image.target.os
```

## 排查速查
| 现象 | 原因 | 处理 |
|------|------|------|
| `No usbupg device is found` | 板子不在下载模式 / AiBurn 占用串口 | 重进下载模式；关闭 AiBurn |
| `no CSW` | 走 BROM 16KB 大块（GUI 路径） | 改用 **命令行 + 115200** |
| 握手 `baudrate not working` | 波特率不对 / 板子没在 BROM | 用 `-b 115200`；重新进下载模式 |
| 烧到一半进度卡住 | 线材/接触不良 | 换根好数据线；重进下载模式重烧 |

---

*记录于一次实证成功（os 分区写入到 rodata，全程 `Got ACK` 无 no CSW）。*