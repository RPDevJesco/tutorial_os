# Tutorial-OS 硬件抽象层（HAL）

一个多平台的裸机操作系统，旨在通过亲手与硬件交互来教授底层系统编程。

## 支持的平台

| 开发板                             | SoC          | 架构     | 状态    |
| ------------------------------- | ------------ | ------ | ----- |
| Raspberry Pi Zero 2W + GPi Case | BCM2710      | ARM    | ✅ 已完成 |
| Raspberry Pi 4B / CM4           | BCM2711      | ARM    | ✅ 已完成 |
| Raspberry Pi 5 / CM5            | BCM2712      | ARM    | ❌ 未完成 |
| Radxa Rock 2A                   | RK3528A      | ARM    | ❌ 未完成 |
| LattePanda Iota                 | N150         | x86_64 | ❌ 未完成 |
| LattePanda MU Compute           | N100         | x86_64 | ❌ 未完成 |
| Orange Pi RV 2                  | KYX1         | RISC-V | ✅ 已完成 |
| Libre Le Potato                 | AML-s905X-CC | ARM    | ❌ 未完成 |

[https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b](https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b)

## 目录结构

```text
tutorial-os/
├── hal/                        # 硬件抽象层（HAL）接口
│   ├── hal.h                   # 主包含头文件
│   ├── hal_types.h             # 类型、错误码、MMIO
│   ├── hal_platform.h          # 平台信息、温度、时钟
│   ├── hal_timer.h             # 计时与延时
│   ├── hal_gpio.h              # GPIO 控制
│   └── hal_display.h           # 显示初始化
│
│   # 每个 SoC 尝试遵循相同的文件组织模式
├── soc/                        # SoC 专用实现
│   ├── bcm2710/                # Raspberry Pi 3B、3B+、3A+、Zero 2 W 与 CM3 设备
│   │   ├── bcm2710_mailbox.h   # Mailbox 接口
│   │   ├── bcm2710_regs.h      # 寄存器定义
│   │   ├── boot_soc.S          # SoC 专用启动代码
│   │   ├── display_dpi.c       # 显示实现（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── linker.ld           # 链接脚本
│   │   ├── mailbox.c           # Mailbox 实现
│   │   ├── soc.mk              # BCM2710 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   └── timer.c             # 定时器实现
│   ├── bcm2711/                # Raspberry Pi 4、CM4、Pi 400
│   │   ├── bcm2711_mailbox.h   # Mailbox 接口
│   │   ├── bcm2711_regs.h      # 寄存器定义
│   │   ├── boot_soc.S          # SoC 专用启动代码
│   │   ├── display_dpi.c       # 显示实现（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── linker.ld           # 链接脚本
│   │   ├── mailbox.c           # Mailbox 实现
│   │   ├── soc.mk              # BCM2711 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   └── timer.c             # 定时器实现
│   ├── bcm2712/                # Raspberry Pi 5、CM5
│   │   ├── bcm2712_mailbox.h   # Mailbox 接口
│   │   ├── bcm2712_regs.h      # 寄存器定义
│   │   ├── boot_soc.S          # SoC 专用启动代码
│   │   ├── display_dpi.c       # 显示实现（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── linker.ld           # 链接脚本
│   │   ├── mailbox.c           # Mailbox 实现
│   │   ├── soc.mk              # BCM2712 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   └── timer.c             # 定时器实现
│   ├── kyx1/                   # Orange Pi RV 2
│   │   ├── display_simplefb.c  # 显示驱动
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── hal_platform_kyx1.c # RISC-V 版本：对应树莓派 soc/bcm2710/soc_init.c 所做的事情
│   │   ├── kyx1_cpu.h          # CPU 操作
│   │   ├── kyx1_regs.h         # 寄存器定义
│   │   ├── linker.ld           # 链接脚本
│   │   ├── soc.mk              # KYX1 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   ├── timer.c             # 定时器实现
│   └   └── uart.c              # UART 驱动
│
├── board/                      # 开发板专用配置
│   ├── rpi-zero2w-gpi/
│   │   ├── board.mk            # 构建配置
│   │   └── boot/               # SD 卡启动文件
│   │       ├── config.txt      # VideoCore GPU 配置
│   │       └── BOOT_FILES.md   # 说明
│   │
│   ├── rpi-cm4-io/
│   │   ├── board.mk
│   │   └── boot/
│   │       ├── config.txt
│   │       └── BOOT_FILES.md
│   │
│   └── orangepi-rv2/
│       ├── env_k1-x.txt
│       ├── board.mk
│       ├── boot.cmd
│       ├── DEPLOY.md
│       └── mkimage.sh          # 生成带 u-boot 配置的 img
│
├── boot/                       # 核心汇编入口点
│   ├── arm64/
│   │   ├── cache.S             # 缓存维护函数
│   │   ├── common_init.S       # 通用 SoC 初始化后的公共初始化
│   │   ├── entry.S             # 入口点
│   │   └── vectors.S           # 异常向量表
│   ├── riscv64/
│   │   ├── cache.S             # 缓存维护函数
│   │   ├── common_init.S       # 通用 SoC 初始化后的公共初始化
│   │   ├── entry.S             # 入口点
│   │   └── vectors.S           # 异常向量表
│   ├── x86_64/
│   │   ├── cache.S             # 缓存维护函数
│   │   ├── common_init.S       # 通用 SoC 初始化后的公共初始化
│   │   ├── entry.S             # 入口点
│   └   └── vectors.S           # 异常向量表
│
├── common/                     # 共享的（比）最小 libc 与 mmio
│   ├── mmio.h                  # 内存映射 I/O 与系统原语
│   ├── string.c                # 内存与字符串函数
│   ├── string.h                # 字符串与内存函数声明
│   └── types.h                 # 类型定义
│
├── drivers/                    # 可移植驱动
│   ├── audio/                  # 核心音频系统驱动
│   │   ├── audio.c             # PWM 音频驱动实现
│   │   └── audio.h             # PWM 音频驱动定义
│   ├── framebuffer/            # UI 主题系统
│   │   ├── framebuffer.c       # 32 位 ARGB8888 帧缓冲驱动
│   │   └── framebuffer.h       # 帧缓冲定义
│   ├── sdcard/                 # SD 卡驱动
│   │   ├── sdhost.h            # 通过 SDHOST 控制器的 SD 卡驱动
│   │   └── sdhost.c            # SD 卡驱动实现
│   ├── usb/                    # USB 主机驱动
│   │   ├── usb_host.h          # DWC2 USB 主机控制器驱动定义
│   └   └── usb_host.c          # DWC2 USB 主机控制器驱动实现
│
├── kernel/                     # 内核代码
│   └── main.c                  # 主应用入口点
│
├── memory/                     # 内存管理
│   ├── allocator.h             # 借鉴 TLSF 的内存分配器声明
│   └── allocator.c             # 借鉴 TLSF 的内存分配器
│
├── ui/                         # UI 系统
│   ├── core/                   # 核心 UI 画布与类型定义
│   │   ├── ui_canvas.h         # 画布与文本渲染器接口
│   │   └── ui_types.h          # 核心 UI 类型定义
│   ├── themes/                 # UI 主题系统
│   │   └── ui_theme.h          # UI 主题系统定义
│   ├── widgets/                # 可复用 UI 组件函数
│   │   ├── ui_widgets.h        # UI 组件定义
│   └   └── ui_widgets.c        # UI 组件实现
│
├── build.sh                    # 在 Linux / MacOS 构建
├── build.bat                   # 在 Windows 构建
├── docker-build.sh             # 构建系统
├── Dockerfile                  # 构建系统
├── Makefile                    # 构建系统
└── README.md                   # 本文件
```

## 构建

```bash
# 为 Raspberry Pi Zero 2W + GPi Case 构建
make LANG=c BOARD=rpi-zero2w-gpi

# 为 Raspberry Pi CM4 构建
make LANG=rust BOARD=rpi-cm4-io

# 为 Radxa Rock 2A 构建（默认 C；不指定 LANG 也行）
make BOARD=radxa-rock2a

# 显示构建信息
make info

# 清理
make clean
```

## 启动文件（⚠️ 平台相关！）

每块开发板的启动需求不同。详见 `board/<name>/boot/BOOT_FILES.md`。

### Raspberry Pi（Zero 2W、CM4）

启动分区需要：

```text
/boot/
├── bootcode.bin      #（仅 Pi Zero 2W 需要，CM4 不需要）
├── start.elf         #（CM4 使用 start4.elf）
├── fixup.dat         #（CM4 使用 fixup4.dat）
├── config.txt        # 在 board/xxx/boot/ 中已提供
└── kernel8.img       # 你的 OS（构建输出）
```

固件获取地址： [https://github.com/raspberrypi/firmware/tree/master/boot](https://github.com/raspberrypi/firmware/tree/master/boot)

### Radxa Rock 2A（Rockchip）

**完全不同！** 使用 U-Boot，而不是 VideoCore：

```text
SD 卡：
├── [sector 64]     idbloader.img    # 来自 Radxa/U-Boot
├── [sector 16384]  u-boot.itb       # 来自 Radxa/U-Boot
└── /boot/
    ├── extlinux/
    │   └── extlinux.conf            # 已提供
    └── Image                        # 你的 OS（构建输出）
```

注意：内核名叫 `Image`，不是 `kernel8.img`！

## HAL API 概览

### 初始化

```c
#include "hal/hal.h"

void kernel_main(...) {
    // 初始化平台（定时器、GPIO、板卡检测）
    hal_platform_init();
    
    // 初始化显示
    framebuffer_t *fb;
    hal_display_init(&fb);
    
    // 你的代码写在这里……
}
```

### 定时器函数

```c
hal_delay_us(1000);              // 延时 1ms
hal_delay_ms(100);               // 延时 100ms
uint64_t ticks = hal_timer_get_ticks();  // 自启动以来的微秒数
```

### GPIO 函数

```c
hal_gpio_set_function(18, HAL_GPIO_OUTPUT);
hal_gpio_set_high(18);
hal_gpio_set_low(18);
bool level = hal_gpio_read(26);

// 外设配置
hal_gpio_configure_dpi();       // DPI 显示引脚
hal_gpio_configure_audio();     // PWM 音频引脚
hal_gpio_configure_sdcard();    // SD 卡引脚
```

### 平台信息

```c
const char *board = hal_platform_get_board_name();  // "Raspberry Pi Zero 2W"
const char *soc = hal_platform_get_soc_name();      // "BCM2710"
int32_t temp = hal_platform_get_temp_celsius();     // CPU 温度
uint32_t freq = hal_platform_get_arm_freq();        // CPU 频率
bool throttled = hal_platform_is_throttled();       // 是否发生热降频？
```

### 显示函数

```c
framebuffer_t *fb;
hal_display_init(&fb);                     // 默认分辨率
hal_display_init_with_size(800, 600, &fb); // 自定义分辨率

// 绘制后……
hal_display_present(fb);                   // 交换缓冲（带 vsync）
hal_display_present_immediate(fb);         // 交换缓冲（不带 vsync）

hal_display_set_vsync(fb, false);          // 禁用 vsync
```

## 关键设计原则

### 1. 绘图代码保持可移植

你的 `fb_*()` 绘图函数在不同平台之间不需要修改：

```c
// 这段代码在所有平台都能工作！
fb_clear(fb, 0xFF000000);
fb_draw_rect(fb, 10, 10, 100, 50, 0xFFFFFFFF);
fb_draw_string(fb, 20, 20, "Hello World!", 0xFFFFFFFF, 0xFF000000);
```

### 2. HAL 抽象硬件差异

| 功能         | BCM2710/BCM2711   | RK3528A      |
| ---------- | ----------------- | ------------ |
| 定时器        | MMIO（系统定时器）       | 系统寄存器（通用定时器） |
| GPIO 上拉/下拉 | GPPUD 序列 / 直接控制   | GRF 寄存器      |
| 显示         | VideoCore mailbox | VOP2 直接编程    |
| 平台信息       | mailbox 查询        | 固定值 / 设备树    |

### 3. 编译期选择平台

没有运行时的 `if (platform == X)` 判断。构建系统会选择正确实现：

```makefile
# board/rpi-zero2w-gpi/board.mk
SOC := bcm2710
include soc/$(SOC)/soc.mk
```

### 4. 错误处理

HAL 函数返回 `hal_error_t`：

```c
hal_error_t err = hal_display_init(&fb);
if (HAL_FAILED(err)) {
    // 处理错误——err 包含具体错误码
    if (err == HAL_ERROR_DISPLAY_MAILBOX) { ... }
}
```

## 从现有代码迁移

### 之前（直接访问硬件）

```c
#include "gpio.h"
#include "mailbox.h"
#include "framebuffer.h"

gpio_configure_for_dpi();
framebuffer_t fb;
fb_init(&fb);
fb_present(&fb);

uint32_t temp;
mailbox_get_temperature(&temp);
```

### 之后（HAL）

```c
#include "hal/hal.h"
#include "framebuffer.h"  // 绘图函数仍然需要

hal_platform_init();
hal_gpio_configure_dpi();
framebuffer_t *fb;
hal_display_init(&fb);
hal_display_present(fb);

int32_t temp = hal_platform_get_temp_celsius();
```

## 添加新平台

1. **创建 SoC 目录**：`soc/newsoc/`
2. **实现 HAL 接口**：

    * `timer.c` - 定时器/延时函数
    * `gpio.c` - GPIO 控制
    * `soc_init.c` - 平台初始化
    * `display_*.c` - 显示驱动
3. **创建寄存器头文件**：`newsoc_regs.h`
4. **创建构建规则**：`soc.mk`
5. **创建板级配置**：`board/newboard/board.mk`

## 平台特定说明

### BCM2710（Pi Zero 2W、Pi 3）

* 外设基址：`0x3F000000`
* GPIO 上拉/下拉需要 GPPUD + GPPUDCLK 序列
* 54 个 GPIO 引脚
* 使用 VideoCore mailbox 获取显示/温度/时钟信息

### BCM2711（Pi 4、CM4）

* 外设基址：`0xFE000000`
* GPIO 上拉/下拉通过直接的 2-bit 寄存器控制（更简单！）
* 58 个 GPIO 引脚
* 相同的 mailbox 接口

### RK3528A（Rock 2A）

* 外设基址：`0x02000000`
* 通过系统寄存器使用 ARM 通用定时器
* 5 个 GPIO bank × 32 引脚 = 160 引脚
* 通过 GRF 的 IOMUX 配置引脚功能
* VOP2 显示控制器
* TSADC 用于测温

## 许可证

仅供教育用途。详见 LICENSE 文件。