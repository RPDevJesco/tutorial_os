# Tutorial-OS 硬件抽象层（HAL）

一个多平台裸机操作系统，旨在通过动手操作硬件来教授底层系统编程。

## 支持的平台

| 开发板                           | SoC             | 架构         | 实现状态              | 构建状态        |
|---------------------------------|-----------------|--------------|-----------------------|-----------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ 已完成             | ✅ 通过         |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ 已完成             | ✅ 通过         |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ 已完成             | ✅ 通过         |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ 已完成             | ✅ 通过         |
| LattePanda Iota                 | N150            | x86_64       | ❌ 未完成             | ❌ 失败         |
| LattePanda MU Compute           | N100            | x86_64       | ❌ 未完成             | ❌ 失败         |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ 已完成             | ✅ 通过         |

https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

## 目录结构

```
tutorial-os/
├── hal/                        # 硬件抽象层接口
│   ├── hal.h                   # 主包含文件
│   ├── hal_types.h             # 类型、错误码、MMIO
│   ├── hal_platform.h          # 平台信息、温度、时钟
│   ├── hal_timer.h             # 计时与延迟
│   ├── hal_gpio.h              # GPIO 控制
│   └── hal_display.h           # 显示初始化
│
│   # 每个 SoC 尝试遵循相同的文件命名规范
├── soc/                        # SoC 特定实现
│   ├── bcm2710/                # Raspberry Pi 3B、3B+、3A+、Zero 2 W 及 CM3 设备
│   │   ├── bcm2710_mailbox.h   # 邮箱接口
│   │   ├── bcm2710_regs.h      # 寄存器定义
│   │   ├── boot_soc.S          # SoC 特定启动代码
│   │   ├── display_dpi.c       # 显示实现（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── linker.ld           # 链接脚本
│   │   ├── mailbox.c           # 邮箱实现
│   │   ├── soc.mk              # BCM2710 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   └── timer.c             # 定时器实现
│   ├── bcm2711/                # Raspberry Pi 4、CM4、Pi 400
│   │   ├── bcm2711_mailbox.h   # 邮箱接口
│   │   ├── bcm2711_regs.h      # 寄存器定义
│   │   ├── boot_soc.S          # SoC 特定启动代码
│   │   ├── display_dpi.c       # 显示实现（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── linker.ld           # 链接脚本
│   │   ├── mailbox.c           # 邮箱实现
│   │   ├── soc.mk              # BCM2711 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   └── timer.c             # 定时器实现
│   ├── bcm2712/                # Raspberry Pi 5、CM5
│   │   ├── bcm2712_mailbox.h   # 邮箱接口
│   │   ├── bcm2712_regs.h      # 寄存器定义
│   │   ├── boot_soc.S          # SoC 特定启动代码
│   │   ├── display_dpi.c       # 显示实现（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── linker.ld           # 链接脚本
│   │   ├── mailbox.c           # 邮箱实现
│   │   ├── soc.mk              # BCM2712 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   └── timer.c             # 定时器实现
│   ├── kyx1/                   # Orange Pi RV 2
│   │   ├── display_simplefb.c  # 显示驱动
│   │   ├── blobs               # 从构建中提取的 U-Boot 二进制文件及设备树 dts 文件
│   │   ├── drivers             # i2c、pmic_spm8821 及 sbi 驱动代码
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── hal_platform_kyx1   # RISC-V 等效于 soc/bcm2710/soc_init.c 对 Pi 所做的工作
│   │   ├── kyx1_cpu.h          # CPU 操作
│   │   ├── kyx1_regs.h         # 寄存器定义
│   │   ├── linker.ld           # 链接脚本
│   │   ├── soc.mk              # KYX1 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   ├── timer.c             # 定时器实现
│   │   └── uart.c              # UART 驱动
│   ├── jh7110/                 # Milk-V Mars
│   │   ├── display_simplefb.c  # 显示驱动
│   │   ├── blobs               # 设备树 dtbs 文件
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── hal_platform_jh7110 # RISC-V 等效于 soc/bcm2710/soc_init.c 对 Pi 所做的工作
│   │   ├── jh7110_cpu.h        # CPU 操作
│   │   ├── jh7110_regs.h       # 寄存器定义
│   │   ├── linker.ld           # 链接脚本
│   │   ├── soc.mk              # jh7110 配置
│   │   ├── mmu.S               # JH7110 的 Sv39 页表设置
│   │   ├── soc_init.c          # 平台初始化
│   │   ├── timer.c             # 定时器实现
│   └   └── uart.c              # UART 驱动
│
├── board/                      # 板级特定配置
│   ├── rpi-zero2w-gpi/
│   │   ├── board.mk            # 构建配置
│   │   └── boot/               # SD 卡启动文件
│   │       ├── config.txt      # VideoCore GPU 配置
│   │       └── BOOT_FILES.md   # 说明文档
│   │
│   ├── rpi-cm4-io/
│   │   ├── board.mk
│   │   └── boot/
│   │       ├── config.txt
│   │       └── BOOT_FILES.md
│   │
│   ├── milkv-mars/
│   │    ├── uEnv.txt
│   │    ├── board.mk
│   │    ├── DEPLOY.md
│   │    └── mkimage.sh          # 创建含 U-Boot 配置的镜像
│   │
│   └── orangepi-rv2/
│       ├── env_k1-x.txt
│       ├── board.mk
│       ├── boot.cmd
│       ├── DEPLOY.md
│       └── mkimage.sh          # 创建含 U-Boot 配置的镜像
│
├── boot/                       # 核心汇编入口点
│   ├── arm64/
│   │   ├── cache.S             # 缓存维护函数
│   │   ├── common_init.S       # SoC 后通用初始化
│   │   ├── entry.S             # 入口点
│   │   └── vectors.S           # 异常向量表
│   ├── riscv64/
│   │   ├── cache.S             # 缓存维护函数
│   │   ├── common_init.S       # SoC 后通用初始化
│   │   ├── entry.S             # 入口点
│   │   └── vectors.S           # 异常向量表
│   ├── x86_64/
│   │   ├── cache.S             # 缓存维护函数
│   │   ├── common_init.S       # SoC 后通用初始化
│   │   ├── entry.S             # 入口点
│   └   └── vectors.S           # 异常向量表
│
├── common/                     # 共享（精简版）最小 libc 及 mmio
│   ├── mmio.h                  # 内存映射 I/O 与系统原语
│   ├── string.c                # 内存与字符串函数
│   ├── string.h                # 字符串与内存函数声明
│   └── types.h                 # 类型定义
│
├── drivers/                    # 可移植驱动
│   ├── audio/                  # 核心音频系统驱动
│   │   ├── audio.c             # PWM 音频驱动实现
│   │   └── audio.h             # PWM 音频驱动定义
│   ├── framebuffer/            # 绘图定义
│   │   ├── framebuffer.c       # 32 位 ARGB8888 帧缓冲驱动
│   │   └── framebuffer.h       # 帧缓冲定义
│   ├── sdcard/                 # SD 卡驱动
│   │   ├── sdhost.h            # 通过 SDHOST 控制器的 SD 卡驱动
│   │   └── sdhost.c            # SD 卡驱动实现
│   ├── usb/                    # USB 主机驱动
│   │   ├── usb_host.h          # DWC2 USB 主机控制器驱动定义
│   └   └── usb_host.c          # DWC2 USB 主机控制器实现
│
├── kernel/                     # 内核代码
│   └── main.c                  # 主应用程序入口点
│
├── memory/                     # 内存管理
│   ├── allocator.h             # TLSF 启发式内存分配器声明
│   └── allocator.c             # TLSF 启发式内存分配器
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
├── build.sh                    # 在 Linux / MacOS 上构建
├── build.bat                   # 在 Windows 上构建
├── docker-build.sh             # 构建系统
├── Dockerfile                  # 构建系统
├── Makefile                    # 构建系统
└── README.md                   # 本文件
```

## 构建

```bash
# 为 Raspberry Pi Zero 2W（搭配 GPi Case）构建
make LANG=c BOARD=rpi-zero2w-gpi

# 为 Raspberry Pi CM4 构建
make LANG=rust BOARD=rpi-cm4-io

# 显示构建信息
make info

# 清理
make clean

# 或使用 docker 通过构建命令一次性构建全部
./build.bat    
./build.sh

# 对于 Milk-V Mars 和 Orange Pi RV 2，由于需要集成 U-Boot，还需要额外的构建步骤
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=milkv-mars image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=orangepi-rv2 image
```

## 启动文件（注意！平台特定！）

每块开发板有不同的启动要求。详见 `board/<name>/boot/BOOT_FILES.md`。

### Raspberry Pi（Zero 2W、CM4）

启动分区需要：
```
/boot/
├── bootcode.bin      # （仅 Pi Zero 2W，CM4 不需要）
├── start.elf         # （CM4 使用 start4.elf）
├── fixup.dat         # （CM4 使用 fixup4.dat）
├── config.txt        # 已提供，位于 board/xxx/boot/
└── kernel8.img       # Tutorial-OS（构建输出）
```

固件获取地址：https://github.com/raspberrypi/firmware/tree/master/boot

## 核心设计原则

### 1. 绘图代码保持可移植性

`fb_*()` 绘图函数在各平台之间不会改变。同一份 `main.c` 在 ARM64、RISC-V64 和 x86_64 上的渲染结果完全一致：

```c
// 以下代码在所有平台上均可运行，无需任何 #ifdef
fb_clear(fb, 0xFF000000);
fb_fill_rect(fb, 10, 10, 100, 50, 0xFFFFFFFF);
fb_draw_string_transparent(fb, 20, 20, "Hello World!", 0xFFFFFFFF);
ui_draw_panel(fb, panel, &theme, UI_PANEL_ELEVATED);
```

### 2. HAL 抽象硬件差异

通往屏幕上同一像素的三条根本不同的路径——从 `main.c` 的角度来看，HAL 使它们完全相同：

| 特性        | BCM2710/2711/2712（ARM64）| JH7110（RISC-V64）    | x86_64（UEFI）       |
|------------|--------------------------|----------------------|----------------------|
| 启动        | VideoCore GPU 固件        | U-Boot + OpenSBI     | UEFI 固件            |
| 显示初始化  | 邮箱属性标签               | 来自 DTB 的 SimpleFB  | GOP 协议             |
| 帧缓冲      | 由 VideoCore 分配          | 由 U-Boot 预配置      | 由 GOP 分配          |
| 缓存刷新    | ARM DSB + 缓存操作         | SiFive L2 Flush64    | x86 CLFLUSH         |
| 定时器      | MMIO 系统定时器            | RISC-V `rdtime` CSR  | HPET / TSC          |
| 平台信息    | 邮箱查询                   | 固定常量 + DTB        | CPUID + ACPI        |

### 3. 编译期平台选择

没有运行时的 `if (platform == X)` 判断。构建系统在编译时选择正确的实现：

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 4. 契约优先的 HAL 设计

HAL 接口头文件在任何实现存在之前即已定义。每个平台实现相同的契约——绘图代码永远不需要知道它在与契约的哪一侧交互。

### 5. 错误处理

HAL 函数返回 `hal_error_t`：

```c
hal_error_t err = hal_display_init(&fb);
if (HAL_FAILED(err)) {
    if (err == HAL_ERROR_DISPLAY_MAILBOX) { ... }
}
```

---

## 添加新平台

1. **创建 SoC 目录**：`soc/newsoc/`
2. **实现 HAL 接口**：
   - `uart.c` — UART 驱动（在显示正常工作之前用于调试输出）
   - `timer.c` — 定时器与延迟函数
   - `gpio.c` — GPIO 控制
   - `soc_init.c` — 平台初始化
   - `display_*.c` — 显示驱动
3. **创建寄存器头文件**：`newsoc_regs.h`
4. **创建构建规则**：`soc.mk`
5. **创建板级配置**：`board/newboard/board.mk`

**基于 SimpleFB 的显示器关键检查清单**（U-Boot + 设备树平台）：

在 `display_init` 中填充 `framebuffer_t` 后，返回前务必初始化裁剪栈：

```c
fb->clip_depth      = 0;
fb->clip_stack[0].x = 0;
fb->clip_stack[0].y = 0;
fb->clip_stack[0].w = info.width;
fb->clip_stack[0].h = info.height;
fb->dirty_count     = 0;
fb->full_dirty      = false;
fb->frame_count     = 0;
fb->initialized     = true;
```

跳过此步骤将导致每个 `fb_fill_rect`、`fb_draw_string` 及组件调用静默地什么都不绘制，而 `fb_clear` 仍然正常工作——使显示管线看起来健康，实则并非如此。

---

## 平台特定说明

### BCM2710（Pi Zero 2W、Pi 3）
- 外设基地址：`0x3F000000`
- GPIO 上下拉需要 GPPUD + GPPUDCLK 时序
- 通过 VideoCore 邮箱属性标签进行显示
- GPi Case 的 DPI 输出使用 GPIO 0–27（ALT2）

### BCM2711（Pi 4、CM4）
- 外设基地址：`0xFE000000`
- GPIO 上下拉通过直接 2 位寄存器实现（比 BCM2710 更简单）
- 与 BCM2710 相同的邮箱接口

### BCM2712（Pi 5、CM5）
- 外设基地址通过 RP1 南桥
- HDMI 通过 RP1 路由——**不要**配置 DPI GPIO 引脚
- `SET_DEPTH` 必须在完整分配之前通过独立的邮箱调用发送
- 验证返回的 pitch == 宽度 × 4；若 pitch == 宽度 × 2，表示 16bpp 分配失败

### JH7110（Milk-V Mars）
- DRAM 基地址：`0x40000000`；内核加载地址：`0x40200000`
- 帧缓冲：`0xFE000000`（通过 U-Boot `bdinfo` 确认）
- 显示控制器：DC8200，位于 `0x29400000`
- L2 缓存通过 SiFive Flush64（位于 `0x02010200`）刷新——单独使用 `fence` 不够
- U-Boot 2021.10 **不会**注入 `simple-framebuffer` DTB 节点——该版本 U-Boot 的硬编码回退路径是永久性方案，而非临时措施
- CPU：SiFive U74-MC，RV64IMAFDCBX——不支持 Zicbom，不支持 Svpbmt

### x86_64（LattePanda IOTA / MU）
- 通过 UEFI 启动——PE/COFF EFI 应用程序位于 `\EFI\BOOT\BOOTX64.EFI`
- 帧缓冲通过 GOP（图形输出协议）分配
- 无设备树——平台信息来自 CPUID 和 ACPI 表

---
教育用途。详见 LICENSE 文件。
