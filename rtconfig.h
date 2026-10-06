#ifndef RT_CONFIG_H__
#define RT_CONFIG_H__

/* Project options */

#define PRJ_DEFCONFIG_FILENAME "d12x_demo68-nor_rt-thread_helloworld_defconfig"

/* -- Important: If following options have been changed, you need save & rerun menuconfig before changing any other options. */

#define PRJ_CHIP "d12x"
#define PRJ_BOARD "demo68-nor"
#define PRJ_KERNEL "rt-thread"
#define PRJ_APP "helloworld"
#define PRJ_RV_TOOLCHAIN_V2_6_1
#define PRJ_TOOLCHAIN_VER "V2.6.1"
/* end of Project options */
#define PLATFORM_LUBANLITE
#define PKGS_DIR "tools/env/packages"

/* Application options */

/* Filesystem related */

#define AIC_USING_FS_IMAGE_0
#define AIC_USING_FS_IMAGE_TYPE_FATFS_FOR_0
#define AIC_FS_IMAGE_DIR_0 "rodata/"
#define AIC_FS_IMAGE_NAME_0 "rodata.fatfs"
#define AIC_FATFS_AUTO_SIZE_FOR_0
#define AIC_FATFS_DEFAULT_VOLAB_0
#define AIC_USING_FS_IMAGE_1
#define AIC_USING_FS_IMAGE_TYPE_LITTLEFS_FOR_1
#define AIC_FS_IMAGE_DIR_1 "data/"
#define AIC_FS_IMAGE_NAME_1 "data.lfs"
#define AIC_LITTLEFS_BLOCK_SIZE_FOR_1 4096
#define AIC_LITTLEFS_PAGE_SIZE_FOR_1 256
#define AIC_USING_FS_IMAGE_TYPE_FATFS_CLUSTER_SIZE 8
#define AIC_USING_DIRENT_CACHE_FOR_READ_ONLY_FATFS

/* LVGL demo select related */

#define LPKG_USING_LVGL
#define LVGL_V_9
#define LPKG_LVGL_PATH "/packages/multimedia/LVGL/LVGL"
#define LPKG_LVGL_THREAD_PRIO 20
#define LPKG_LVGL_THREAD_STACK_SIZE 32768
#define LPKG_LVGL_DISP_REFR_PERIOD 5
#define AIC_LVGL_DEMO
#define AIC_LVGL_REOSC_DEMO
#define LV_COLOR_DEPTH 16
#define LV_CACHE_IMG_NUM 8
#define LV_CACHE_DEF_SIZE 0x800000
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 20
#define LVGL_STORAGE_PATH "/rodata/lvgl_data"
/* end of Application options */

/* Chip options */

#define SOC_THEAD_SMART
#define PRJ_CUSTOM_LDS ""
#define AIC_CHIP_D12X
#define CACHE_LINE_SIZE 32
#define CPU_BASE 0x20000000
#define CPU_PSRAM_BASE 0x40000000
#define CPU_SRAM_BASE 0x30040000
#define AIC_CMU_DRV
#define AIC_CMU_DRV_V12
#define AIC_CMU_DRV_VER "12"
#define AIC_GPIO_DRV
#define AIC_GPIO_DRV_V12
#define AIC_GPIO_DRV_VER "12"
#define AIC_SYSCFG_DRV
#define AIC_SYSCFG_DRV_V12
#define AIC_SYSCFG_DRV_VER "12"
#define AIC_DMA_DRV
#define AIC_DMA_DRV_V12
#define AIC_DMA_DRV_VER "12"
#define AIC_DMA_CH_NUM 4
#define AIC_DMA_ALIGN_SIZE 8
#define AIC_UART_DRV
#define AIC_UART_DRV_V11
#define AIC_UART_DRV_VER "11"
#define AIC_UART_DEV_NUM 4
#define AIC_I2C_DRV
#define AIC_I2C_DRV_V10
#define AIC_I2C_DRV_VER "10"
#define AIC_I2C_CH_NUM 2
#define AIC_QSPI_DRV
#define AIC_QSPI_DRV_V11
#define AIC_QSPI_DRV_VER "11"
#define AIC_XSPI_DRV
#define AIC_XSPI_DRV_V11
#define AIC_XSPI_DRV_VER "11"
#define AIC_WRI_DRV
#define AIC_WRI_DRV_V12
#define AIC_WRI_DRV_VER "12"
#define AIC_WDT_DRV
#define AIC_WDT_DRV_V10
#define AIC_WDT_DRV_VER "10"
#define AIC_SDMC_DRV
#define AIC_SDMC_DRV_V11
#define AIC_SDMC_DRV_VER "11"
#define AIC_DE_DRV
#define AIC_DE_DRV_V12
#define AIC_DISP_HW_RGB
#define AIC_DISP_HW_MIPI_DBI
#define AIC_DISP_HW_MIPI_DSI
#define AIC_DISP_RGB_DRV_V12
#define AIC_DISP_MIPI_DBI_DRV_V12
#define AIC_GE_DRV
#define AIC_GE_DRV_V11
#define AIC_GE_DRV_VER "11"
#define AIC_VE_DRV
#define AIC_VE_DRV_V40
#define AIC_VE_DRV_VER "40"
#define AIC_ADCIM_DRV
#define AIC_ADCIM_DRV_V11
#define AIC_ADCIM_DRV_VER "11"
#define AIC_TSEN_DRV
#define AIC_TSEN_DRV_V20
#define AIC_TSEN_DRV_VER "20"
#define AIC_TSEN_CH_NUM 1
#define AIC_SID_DRV
#define AIC_SID_DRV_V12
#define AIC_SID_DRV_VER "12"
#define AIC_MTOP_DRV
#define AIC_CAN_DRV
#define AIC_CAN_DRV_V10
#define AIC_CAN_DRV_VER "10"
#define AIC_AUDIO_DRV
#define AIC_AUDIO_DRV_V12
#define AIC_AUDIO_DRV_VER "12"
/* end of Chip options */

/* Board options */

/* Peripheral Devices: */

/* Interface Related: */

#define AIC_USING_UART0
#define AIC_USING_UART1

/* UART0 Parameter */

#define AIC_CLK_UART0_FREQ 48000000
#define AIC_DEV_UART0_BAUDRATE 115200
#define AIC_DEV_UART0_DATABITS 8
#define AIC_DEV_UART0_STOPBITS 1
#define AIC_DEV_UART0_PARITY 0
#define AIC_DEV_UART0_RS232
#define AIC_DEV_UART0_MODE_RS232
#define AIC_DEV_UART0_MODE 0
#define AIC_UART0_FLAG 259
/* end of UART0 Parameter */

/* UART1 Parameter */

#define AIC_CLK_UART1_FREQ 48000000
#define AIC_DEV_UART1_BAUDRATE 115200
#define AIC_DEV_UART1_DATABITS 8
#define AIC_DEV_UART1_STOPBITS 1
#define AIC_DEV_UART1_PARITY 0
#define AIC_DEV_UART1_RS232
#define AIC_DEV_UART1_MODE_RS232
#define AIC_DEV_UART1_MODE 0
#define AIC_UART1_FLAG 259
/* end of UART1 Parameter */
#define AIC_USING_I2C0

/* I2C0 Parameter */

#define AIC_DEV_I2C0_10BIT 0
#define AIC_DEV_I2C0_SPEED 400000
#define AIC_DEV_I2C0_SLAVE_MODE 0
#define AIC_DEV_I2C0_STUCK_TIME 400
/* end of I2C0 Parameter */

/* I2C Global Config */

/* end of I2C Global Config */

/* Using HRTIMER */

/* end of Using HRTIMER */
#define AIC_USING_CAN0

/* Storage Related: */

#define AIC_USING_QSPI0

/* SPI0 Parameter */

#define AIC_DEV_QSPI0_MAX_SRC_FREQ_HZ 100000000
#define AIC_QSPI0_BUS_WIDTH_4
#define AIC_QSPI0_BUS_WIDTH 4
#define AIC_DEV_QSPI0_DELAY_MODE 2
/* end of SPI0 Parameter */
#define AIC_QSPI0_DEVICE_SPINOR
#define AIC_QSPI0_DEVICE_SPINOR_FREQ 100000000
#define AIC_USING_SDMC1

/* SDMC1 Parameter */

#define AIC_SDMC1_BUSWIDTH4
#define AIC_SDMC1_DRV_PHASE 3
#define AIC_SDMC1_SMP_PHASE 0
#define AIC_SDMC1_CLK_FREQ 100000000
/* end of SDMC1 Parameter */

/* WLAN Related: */


/* Analog Related: */

#define AIC_USING_TSEN

/* TSENS options */

#define AIC_USING_TSEN_CPU

/* TSEN_CPU Parameter */

#define AIC_TSEN0_SOFT_MODE_SINGLE_INTER
#define AIC_TSEN0_SOFT_MODE 0
#define AIC_TSEN0_MODE 0
/* end of TSEN_CPU Parameter */
/* end of TSENS options */

/* MutiMedia Related: */

#define AIC_USING_DE

/* Display Parameter */

#define AIC_DISPLAY_DRV
#define AIC_DISP_DE_DRV
#define AIC_DISP_RGB_DRV
#define AIC_DISP_RGB
#define AIC_DI_TYPE 1

/* RGB interface options */

#define PRGB_MODE
#define AIC_RGB_MODE 0
#define AIC_PRGB_24BIT
#define AIC_RGB_FORMAT 0
#define DATA_ORDER_BGR
#define AIC_RGB_DATA_ORDER 0x00120012
#define AIC_DEGREE_90
#define AIC_RGB_CLK_CTL 1
#define AIC_RGB_DATA_MIRROR 0
/* end of RGB interface options */
#define AICFB_RGB565
#define AICFB_FORMAT 0x0e
#define AIC_PAN_DISPLAY
#define AIC_DISP_BOOTUP_LOGO
#define AIC_STARTUP_UI_SHOW
#define AICFB_ROTATE_0
#define AIC_FB_ROTATE_DEGREE 0

/* Display Panels */

#define AIC_SIMPLE_PANEL
#define AIC_PANEL_480X272_RESOLUTION
#define RGB_PANEL_MATCH_ID 2
/* end of Display Panels */
#define AIC_GPIO_BACKLIGHT
#define AIC_PANEL_ENABLE_GPIO "PE.13"
/* end of Display Parameter */
#define AIC_USING_GE

/* Graphics Engine Parameter */

#define AIC_GE_NORMAL
#define AIC_GE_DITHER
/* end of Graphics Engine Parameter */
#define AIC_USING_VE

/* System Related: */

#define AIC_USING_DMA
#define AIC_USING_WRI
#define AIC_USING_WDT

/* Mem Options */

/* SRAM parameter */

#define AIC_SRAM_SIZE 0x8000
#define AIC_SRAM_CMA_EN
#define AIC_SRAM_SW_SIZE 0x0
/* end of SRAM parameter */

/* PSRAM parameter */

#define AIC_PSRAM_SIZE 0x800000
#define AIC_PSRAM_CMA_EN
#define AIC_PSRAM_SW_SIZE 0x400000
#define AIC_PSRAM_SW_EN
/* end of PSRAM parameter */
#define AIC_XIP_RAMCODE_SIZE 0x0
#define AIC_INTERRUPTSTACK_SIZE 8192

/* Support another Flash */

#define AIC_FLASH_NUM_ONE
/* end of Support another Flash */
/* end of Mem Options */

/* Clocks options */

#define AIC_CLK_PLL_INT0_FREQ 396000000
#define AIC_CLK_PLL_INT1_FREQ 588000000
#define AIC_CLK_PLL_FRA0_FREQ 384000000
#define AIC_CLK_PLL_FRA2_FREQ 396000000
#define AIC_CLK_CPU_FREQ 396000000
#define AIC_CLK_AXI0_FREQ 200000000
#define AIC_CLK_AHB0_FREQ 200000000
#define AIC_CLK_APB0_FREQ 100000000
/* end of Clocks options */

/* Security Related: */

#define AIC_USING_SID

/* SID Parameter */

#define EFUSE_MAX_WORD 16
#define EFUSE_TIMING_VALUE 0x04021FF1
/* end of SID Parameter */
#define AIC_USING_SYSCFG

/* Syscfg Parameter */

#define AIC_SYSCFG_LDO1X_ENABLE
#define AIC_SYSCFG_LDO1X_VOL_VAL 8
/* end of Syscfg Parameter */
#define AIC_USING_MTOP
/* end of Board options */

/* Rt-Thread options */

#define KERNEL_RTTHREAD
#define DRIVER_HAL_EN
#define DRIVER_DRV_EN
#define RTT_DIR "kernel/rt-thread"

/* RT-Thread Kernel */

#define RT_NAME_MAX 16
#define RT_ALIGN_SIZE 4
#define RT_THREAD_PRIORITY_32
#define RT_THREAD_PRIORITY_MAX 32
#define RT_TICK_PER_SECOND 1000
#define RT_USING_OVERFLOW_CHECK
#define RT_USING_HOOK
#define RT_HOOK_USING_FUNC_PTR
#define RT_USING_IDLE_HOOK
#define RT_IDLE_HOOK_LIST_SIZE 4
#define IDLE_THREAD_STACK_SIZE 1024
#define RT_USING_TIMER_SOFT
#define RT_TIMER_THREAD_PRIO 4
#define RT_TIMER_THREAD_STACK_SIZE 512

/* kservice optimization */

/* end of kservice optimization */

/* Inter-Thread communication */

#define RT_USING_SEMAPHORE
#define RT_USING_MUTEX
#define RT_USING_EVENT
#define RT_USING_MAILBOX
#define RT_USING_MESSAGEQUEUE
/* end of Inter-Thread communication */

/* Memory Management */

#define RT_USING_MEMPOOL
#define RT_USING_MEMHEAP
#define RT_MEMHEAP_BSET_MODE
#define RT_USING_MEMHEAP_AS_HEAP
#define RT_USING_HEAP
/* end of Memory Management */

/* Kernel Device Object */

#define RT_USING_DEVICE
#define RT_USING_CONSOLE
#define RT_CONSOLEBUF_SIZE 128
#define RT_CONSOLE_DEVICE_NAME "uart1"
/* end of Kernel Device Object */
#define RT_VER_NUM 0x40101
/* end of RT-Thread Kernel */
#define ARCH_RISCV
#define ARCH_RISCV_FPU
#define ARCH_RISCV_FPU_S
#define ARCH_RISCV32

/* RT-Thread Components */

#define RT_USING_COMPONENTS_INIT
#define RT_USING_USER_MAIN
#define RT_MAIN_THREAD_STACK_SIZE 4096
#define RT_MAIN_THREAD_PRIORITY 10
#define RT_USING_LEGACY
#define RT_USING_MSH
#define RT_USING_FINSH
#define FINSH_USING_MSH
#define FINSH_THREAD_NAME "tshell"
#define FINSH_PROMPT_NAME "aic"
#define FINSH_THREAD_PRIORITY 20
#define FINSH_THREAD_STACK_SIZE 8192
#define FINSH_USING_HISTORY
#define FINSH_HISTORY_LINES 5
#define FINSH_USING_WORD_OPERATION
#define FINSH_USING_FUNC_EXT
#define FINSH_USING_SYMTAB
#define FINSH_CMD_SIZE 80
#define MSH_USING_BUILT_IN_COMMANDS
#define FINSH_USING_DESCRIPTION
#define FINSH_ARG_MAX 10
#define RT_USING_DFS
#define DFS_USING_POSIX
#define DFS_USING_WORKDIR
#define DFS_FILESYSTEMS_MAX 8
#define DFS_FILESYSTEM_TYPES_MAX 4
#define DFS_FD_MAX 16
#define RT_USING_DFS_MNTTABLE
#define RT_USING_DFS_ELMFAT

/* elm-chan's FatFs, Generic FAT Filesystem Module */

#define RT_DFS_ELM_CODE_PAGE 437
#define RT_DFS_ELM_WORD_ACCESS
#define RT_DFS_ELM_USE_LFN_3
#define RT_DFS_ELM_USE_LFN 3
#define RT_DFS_ELM_LFN_UNICODE_2
#define RT_DFS_ELM_LFN_UNICODE 2
#define RT_DFS_ELM_USE_EXFAT
#define RT_DFS_ELM_MAX_LFN 255
#define RT_DFS_ELM_DRIVES 4
#define RT_DFS_ELM_MAX_SECTOR_SIZE 4096
#define RT_DFS_ELM_REENTRANT
#define RT_DFS_ELM_MUTEX_TIMEOUT 3000
/* end of elm-chan's FatFs, Generic FAT Filesystem Module */
#define RT_USING_DFS_DEVFS
#define RT_USING_DFS_ROMFS
#define RT_USING_FAL
#define FAL_DEBUG 0
#define FAL_PART_HAS_TABLE_CFG
#define FAL_USING_SFUD_PORT
#define FAL_USING_NOR_FLASH_DEV_NAME "norflash0"
#define FAL_BLK_DEVICE_RDONLY

/* Device Drivers */

#define RT_USING_DEVICE_IPC
#define RT_USING_SERIAL
#define RT_USING_SERIAL_V1
#define RT_SERIAL_USING_DMA
#define RT_SERIAL_RB_BUFSZ 64
#define RT_USING_CAN
#define RT_CAN_USING_HDR
#define RT_USING_I2C
#define RT_USING_I2C_BITOPS
#define RT_USING_PIN
#define RT_USING_ADC
#define RT_USING_PWM
#define RT_USING_MTD_NOR
#define RT_USING_PM
#define PM_TICKLESS_THRESHOLD_TIME 2
#define PM_USING_CUSTOM_CONFIG
#define PM_ENABLE_SUSPEND_SLEEP_MODE
#define PM_ENABLE_THRESHOLD_SLEEP_MODE
#define PM_LIGHT_THRESHOLD_TIME 5
#define PM_DEEP_THRESHOLD_TIME 20
#define PM_STANDBY_THRESHOLD_TIME 100
#define RT_USING_SDIO
#define RT_SDIO_STACK_SIZE 512
#define RT_SDIO_THREAD_PRIORITY 15
#define RT_MMCSD_STACK_SIZE 8192
#define RT_MMCSD_THREAD_PREORITY 22
#define RT_MMCSD_MAX_PARTITION 16
#define RT_USING_SPI
#define RT_USING_QSPI
#define RT_USING_SFUD
#define RT_SFUD_USING_SFDP
#define RT_SFUD_USING_FLASH_INFO_TABLE
#define RT_SFUD_USING_QSPI
#define RT_SFUD_SPI_MAX_HZ 50000000
#define RT_USING_WDT
#define RT_USING_AUDIO
#define RT_AUDIO_REPLAY_MP_BLOCK_SIZE 2048
#define RT_AUDIO_REPLAY_MP_BLOCK_COUNT 2
#define RT_AUDIO_RECORD_PIPE_SIZE 2048
#define RT_USING_SENSOR
#define RT_USING_SENSOR_CMD
#define RT_USING_TOUCH
#define RT_TOUCH_PIN_IRQ

/* Using USB */

/* end of Using USB */
/* end of Device Drivers */

/* C/C++ and POSIX layer */

#define RT_LIBC_DEFAULT_TIMEZONE 8

/* POSIX (Portable Operating System Interface) layer */

#define RT_USING_POSIX_DELAY
#define RT_USING_POSIX_CLOCK
#define RT_USING_PTHREADS
#define PTHREAD_NUM_MAX 8

/* Interprocess Communication (IPC) */


/* Socket is in the 'Network' category */

/* end of Interprocess Communication (IPC) */
/* end of POSIX (Portable Operating System Interface) layer */
/* end of C/C++ and POSIX layer */

/* Network */

/* end of Network */

/* Utilities */

#define RT_USING_ULOG
#define ULOG_OUTPUT_LVL_I
#define ULOG_OUTPUT_LVL 6
#define ULOG_ASSERT_ENABLE
#define ULOG_LINE_BUF_SIZE 128

/* log format */

#define ULOG_USING_COLOR
#define ULOG_OUTPUT_TIME
#define ULOG_TIME_USING_TIMESTAMP
#define ULOG_OUTPUT_LEVEL
#define ULOG_OUTPUT_TAG
/* end of log format */
#define ULOG_BACKEND_USING_CONSOLE
#define ULOG_USING_FILTER
/* end of Utilities */
/* end of RT-Thread Components */

/* RT-Thread Utestcases */

/* end of RT-Thread Utestcases */
/* end of Rt-Thread options */

/* RT-Thread online packages */

/* IoT - internet of things */


/* Wi-Fi */

/* Marvell WiFi */

/* end of Marvell WiFi */

/* Wiced WiFi */

/* end of Wiced WiFi */
/* end of Wi-Fi */

/* IoT Cloud */

/* end of IoT Cloud */
/* end of IoT - internet of things */

/* security packages */

/* end of security packages */

/* language packages */

/* JSON: JavaScript Object Notation, a lightweight data-interchange format */

/* end of JSON: JavaScript Object Notation, a lightweight data-interchange format */

/* XML: Extensible Markup Language */

/* end of XML: Extensible Markup Language */
/* end of language packages */

/* multimedia packages */

/* u8g2: a monochrome graphic library */

/* end of u8g2: a monochrome graphic library */

/* PainterEngine: A cross-platform graphics application framework written in C language */

/* end of PainterEngine: A cross-platform graphics application framework written in C language */
/* end of multimedia packages */

/* tools packages */

/* end of tools packages */

/* system packages */

/* enhanced kernel services */

/* end of enhanced kernel services */

/* acceleration: Assembly language or algorithmic acceleration packages */

/* end of acceleration: Assembly language or algorithmic acceleration packages */

/* CMSIS: ARM Cortex-M Microcontroller Software Interface Standard */

/* end of CMSIS: ARM Cortex-M Microcontroller Software Interface Standard */

/* Micrium: Micrium software products porting for RT-Thread */

/* end of Micrium: Micrium software products porting for RT-Thread */
/* end of system packages */

/* peripheral libraries and drivers */


/* Kendryte SDK */

/* end of Kendryte SDK */
/* end of peripheral libraries and drivers */

/* AI packages */

/* end of AI packages */

/* miscellaneous packages */

/* project laboratory */

/* end of project laboratory */

/* samples: kernel and components samples */

/* end of samples: kernel and components samples */

/* entertainment: terminal games and other interesting software packages */

/* end of entertainment: terminal games and other interesting software packages */
/* end of miscellaneous packages */

/* Arduino libraries */


/* Projects */

/* end of Projects */

/* Sensors */

/* end of Sensors */

/* Display */

/* end of Display */

/* Timing */

/* end of Timing */

/* Data Processing */

/* end of Data Processing */

/* Data Storage */

/* Communication */

/* Device Control */

/* end of Device Control */

/* Other */

/* Signal IO */

/* end of Signal IO */

/* Uncategorized */

/* end of Arduino libraries */
/* end of RT-Thread online packages */

/* Local packages options */

/* Third-party packages options */

#define LPKG_USING_CPU_USAGE
#define LPKG_USING_CHERRYUSB_V1_5_0
#define LPKG_USING_LITTLEFS
#define LPKG_LITTLEFS_USING_MTD_NOR
#define LPKG_LITTLEFS_PATH "/packages/third-party/littlefs"
#define LPKG_USING_LITTLEFS_LATEST_VERSION
#define LFS_READ_SIZE 16
#define LFS_PROG_SIZE 256
#define LFS_BLOCK_SIZE 4096
#define LFS_CACHE_SIZE 4096
#define LFS_BLOCK_CYCLES -1
#define LFS_THREADSAFE
#define LFS_LOOKAHEAD_MAX 128
#define LPKG_LITTLEFS_VER "latest"
#define RT_DEF_LFS_DRIVERS 2

/* BenchMark Tests */

#define AIC_PRINT_FLOAT_CUSTOM
/* end of BenchMark Tests */
#define LPKG_USING_MINIMP3
#define LPKG_USING_FDTLIB
#define LPKG_USING_FDTLIB_CRC32_VERIFY
/* end of Third-party packages options */

/* ArtInChip packages options */

#define LPKG_MPP
#define AIC_MPP_PLAYER_INTERFACE
#define AIC_MPP_PLAYER_VIDEO_EXT_RENDER
#define AIC_MPP_FB_TEST
#define LPKG_USING_ENV
#define LPKG_USING_USERID
#define USERID_IN_SPINOR
#define USERID_PARTITION_NAME "userid"
#define AIC_USING_THREAD_STACK_TRACE
/* end of ArtInChip packages options */
/* end of Local packages options */

/* Drivers options */

#define AIC_GPIO_IRQ_DRV_EN
#define AIC_SPINOR_DRV

/* Peripheral */

#define LPKG_USING_SFUD
#define AIC_USING_TOUCH
#define AIC_USING_CTP
#define AIC_TOUCH_PANEL_GT911
#define AIC_TOUCH_PANEL_I2C_CHAN "i2c0"
#define AIC_TOUCH_PANEL_RST_PIN "PA.10"
#define AIC_TOUCH_PANEL_INT_PIN "PA.11"
#define AIC_TOUCH_X_COORDINATE_RANGE 1024
#define AIC_TOUCH_Y_COORDINATE_RANGE 600
#define AIC_SCREEN_REAL_X_RESOLUTION 1024
#define AIC_SCREEN_REAL_Y_RESOLUTION 600
#define AIC_SCREEN_RELATIVE_ROTATION_DEGREE 0
#define AIC_TOUCH_REPORT_X_COORDINATE 1024
#define AIC_TOUCH_REPORT_Y_COORDINATE 600
#define AIC_TOUCH_PANEL_NAME "gt911"

/* External Audio Codec Support */

/* end of External Audio Codec Support */
/* end of Peripheral */

/* Drivers debug */

#define SHOW_PRINT_TIME
#define AIC_FB_DRV_DEBUG
/* end of Drivers debug */

/* Drivers examples */

#define AIC_CMU_DRV_TEST
#define AIC_GPIO_DRV_TEST
#define AIC_DMA_DRV_TEST
#define AIC_DISPLAY_TEST
#define AIC_MTOP_DRV_TEST
#define AIC_CAN_DRV_TEST
#define AIC_WDT_DRV_TEST
#define AIC_TSEN_TEST
#define AIC_USERID_BARE_TEST
/* end of Drivers examples */
/* end of Drivers options */

#endif
