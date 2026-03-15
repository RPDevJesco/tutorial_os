# Tutorial-OS 硬件抽象层（HAL）

一个多平台裸机操作系统，旨在通过动手操作硬件来教授底层系统编程。

## 支持的平台

| 开发板                           | SoC             | 架构         | 实现状态     | 构建状态     |
|---------------------------------|-----------------|-------------|-------------|-------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ 已完成    | ✅ 通过     |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ 已完成    | ✅ 通过     |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ 已完成    | ✅ 通过     |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ 已完成    | ✅ 通过     |
| LattePanda Iota                 | N150            | x86_64       | ❌ 未完成    | ❌ 失败     |
| LattePanda MU Compute           | N100            | x86_64       | ✅ 已完成    | ✅ 通过     |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ 已完成    | ✅ 通过     |

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
│   # 每个 SoC 尽量遵循相同的文件命名模式
├── soc/                        # SoC 特定实现
│   ├── bcm2710/                # Raspberry Pi 3B、3B+、3A+、Zero 2 W 和 CM3 设备
│   │   ├── bcm2710_mailbox.h   # 邮箱接口
│   │   ├── bcm2710_regs.h      # 寄存器定义
│   │   ├── boot_soc.S          # SoC 特定启动代码
│   │   ├── display_dpi.c       # 显示实现（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── linker.ld           # 链接脚本
│   │   ├── mailbox.c           # 邮箱实现
│   │   ├── soc.mk              # BCM2710 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   └── timer.c             # 计时器实现
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
│   │   └── timer.c             # 计时器实现
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
│   │   └── timer.c             # 计时器实现
│   ├── kyx1/                   # Orange Pi RV 2
│   │   ├── display_simplefb.c  # 显示驱动
│   │   ├── blobs               # 从构建中提取的 U-Boot 二进制文件和设备树 dts 文件
│   │   ├── drivers             # i2c、pmic_spm8821 和 sbi 驱动代码
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── hal_platform_kyx1   # RISC-V 版本，等效于 soc/bcm2710/soc_init.c 在 Pi 上的功能
│   │   ├── kyx1_cpu.h          # CPU 操作
│   │   ├── kyx1_regs.h         # 寄存器定义
│   │   ├── linker.ld           # 链接脚本
│   │   ├── soc.mk              # KYX1 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   ├── timer.c             # 计时器实现
│   │   └── uart.c              # UART 驱动
│   ├── lattepanda_n100/        # LattePanda MU 的 N100 CPU
│   │   ├── display_gop.c       # 显示驱动
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── hal_platform_n100   # x86_64 版本，等效于 soc/bcm2710/soc_init.c 在 Pi 上的功能
│   │   ├── linker.ld           # 链接脚本
│   │   ├── soc.mk              # N100 配置
│   │   ├── soc_init.c          # 平台初始化
│   │   ├── timer.c             # 计时器实现
│   │   └── uart_8250.c         # UART 驱动
│   ├── jh7110/                 # Milk-V Mars
│   │   ├── display_simplefb.c  # 显示驱动
│   │   ├── blobs               # 设备树 dtb 文件
│   │   ├── gpio.c              # GPIO 实现
│   │   ├── hal_platform_jh7110 # RISC-V 版本，等效于 soc/bcm2710/soc_init.c 在 Pi 上的功能
│   │   ├── jh7110_cpu.h        # CPU 操作
│   │   ├── jh7110_regs.h       # 寄存器定义
│   │   ├── linker.ld           # 链接脚本
│   │   ├── soc.mk              # JH7110 配置
│   │   ├── mmu.S               # JH7110 的 Sv39 页表设置
│   │   ├── soc_init.c          # 平台初始化
│   │   ├── timer.c             # 计时器实现
│   └   └── uart.c              # UART 驱动
│
├── board/                      # 开发板特定配置
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
│   ├── milkv-mars/
│   │    ├── uEnv.txt
│   │    ├── board.mk
│   │    ├── DEPLOY.md
│   │    └── mkimage.sh          # 创建带有 U-Boot 配置的镜像
|   |
│   ├── lattepanda-mu/
│   │    ├── board.mk
│   │    ├── mkimage.py          # 创建带有 PE/COFF EFI 应用配置的镜像
│   │    └── mkimage.sh          # mkimage.py 的 .sh 包装脚本
│   │
│   └── orangepi-rv2/
│       ├── env_k1-x.txt
│       ├── board.mk
│       ├── boot.cmd
│       ├── DEPLOY.md
│       └── mkimage.sh          # 创建带有 U-Boot 配置的镜像
│
├── boot/                       # 核心汇编入口点
│   ├── arm64/
│   │   ├── cache.S             # 缓存维护函数
│   │   ├── common_init.S       # 通用 SoC 后初始化
│   │   ├── entry.S             # 入口点
│   │   └── vectors.S           # 异常向量表
│   ├── riscv64/
│   │   ├── cache.S             # 缓存维护函数
│   │   ├── common_init.S       # 通用 SoC 后初始化
│   │   ├── entry.S             # 入口点
│   │   └── vectors.S           # 异常向量表
│   └── x86_64/                 # 空目录，使用 gnu-efi 不需要
│
├── common/                     # 共享的最小 libc 和 MMIO
│   ├── mmio.h                  # 内存映射 I/O 和系统原语
│   ├── string.c                # 内存和字符串函数
│   ├── string.h                # 字符串和内存函数声明
│   └── types.h                 # 类型定义
│
├── drivers/                    # 可移植驱动
│   ├── framebuffer/            # 绘图定义
│   │   ├── framebuffer.h       # 32 位 ARGB8888 帧缓冲驱动
│   └   └── framebuffer.c       # 帧缓冲定义
│
├── kernel/                     # 内核代码
│   └── main.c                  # 主应用入口点
│
├── memory/                     # 内存管理
│   ├── allocator.h             # 受 TLSF 启发的内存分配器声明
│   └── allocator.c             # 受 TLSF 启发的内存分配器
│
├── ui/                         # UI 系统
│   ├── core/                   # 核心 UI 画布和类型定义
│   │   ├── ui_canvas.h         # 画布和文本渲染器接口
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
# 为 Raspberry Pi Zero 2W + GPi Case 构建
make LANG=c BOARD=rpi-zero2w-gpi

# 为 Raspberry Pi CM4 构建
make LANG=rust BOARD=rpi-cm4-io

# 显示构建信息
make info

# 清理
make clean

# 或者使用 Docker 通过构建脚本一次构建全部
./build.bat    
./build.sh

# 对于 Milk-V Mars 和 Orange Pi RV 2，需要额外的构建步骤，因为它们需要 U-Boot 集成
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=milkv-mars image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=orangepi-rv2 image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-mu image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-iota image
```

## 启动文件（注意！因平台而异！）

每个开发板有不同的启动要求。详情请参阅 `board/<名称>/boot/BOOT_FILES.md`。

### Raspberry Pi（Zero 2W、CM4）

启动分区需要：
```
/boot/
├── bootcode.bin      # （仅 Pi Zero 2W 需要，CM4 不需要）
├── start.elf         # （CM4 使用 start4.elf）
├── fixup.dat         # （CM4 使用 fixup4.dat）
├── config.txt        # 已在 board/xxx/boot/ 中提供
└── kernel8.img       # Tutorial-OS（构建输出）
```

固件下载地址：https://github.com/raspberrypi/firmware/tree/master/boot

## 核心设计原则

### 1. 绘图代码保持可移植

你的 `fb_*()` 绘图函数在不同平台之间无需修改。同一个 `main.c` 在 ARM64、RISC-V64 和 x86_64 上的渲染效果完全一致：

```c
// 以下代码在所有平台上均可运行，无需任何 #ifdef
fb_clear(fb, 0xFF000000);
fb_fill_rect(fb, 10, 10, 100, 50, 0xFFFFFFFF);
fb_draw_string_transparent(fb, 20, 20, "Hello World!", 0xFFFFFFFF);
ui_draw_panel(fb, panel, &theme, UI_PANEL_ELEVATED);
```

### 2. HAL 屏蔽硬件差异

三种截然不同的路径，将同一个像素呈现在屏幕上——HAL 使得从 `main.c` 的视角来看，它们完全相同：

| 特性 | BCM2710/2711/2712 (ARM64) | JH7110 (RISC-V64) | x86_64 (UEFI) |
|------|--------------------------|-------------------|----------------|
| 启动方式 | VideoCore GPU 固件 | U-Boot + OpenSBI | UEFI 固件 |
| 显示初始化 | 邮箱属性标签 | 来自 DTB 的 SimpleFB | GOP 协议 |
| 帧缓冲 | 由 VideoCore 分配 | 由 U-Boot 预配置 | 由 GOP 分配 |
| 缓存刷新 | ARM DSB + 缓存操作 | SiFive L2 Flush64 | x86 CLFLUSH |
| 计时器 | MMIO 系统计时器 | RISC-V `rdtime` CSR | HPET / TSC |
| 平台信息 | 邮箱查询 | 固定常量 + DTB | CPUID + ACPI |

### 3. 编译时平台选择

没有运行时的 `if (platform == X)` 检查。构建系统在编译时选择正确的实现：

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 4. 契约优先的 HAL 设计

HAL 接口头文件在任何实现存在之前就已定义。每个平台实现相同的契约——绘图代码永远不需要知道它在与契约的哪一端通信。

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
   - `uart.c` — UART 驱动（在显示工作之前需要用于调试输出）
   - `timer.c` — 计时器和延迟函数
   - `gpio.c` — GPIO 控制
   - `soc_init.c` — 平台初始化
   - `display_*.c` — 显示驱动
3. **创建寄存器头文件**：`newsoc_regs.h`
4. **创建构建规则**：`soc.mk`
5. **创建开发板配置**：`board/newboard/board.mk`

**基于 SimpleFB 显示的关键检查清单**（U-Boot + 设备树平台）：

在 `display_init` 中填充 `framebuffer_t` 之后，返回前务必初始化裁剪栈：

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

如果跳过此步骤，所有 `fb_fill_rect`、`fb_draw_string` 和组件调用都会静默地不绘制任何内容，而 `fb_clear` 继续正常工作——使得显示管线看起来正常，实际上并非如此。

---

## 平台特定说明

### BCM2710（Pi Zero 2W、Pi 3）
- 外设基地址：`0x3F000000`
- GPIO 上拉需要 GPPUD + GPPUDCLK 序列
- 通过 VideoCore 邮箱属性标签进行显示
- GPIO 0–27 上的 DPI 输出（ALT2），用于 GPi Case

### BCM2711（Pi 4、CM4）
- 外设基地址：`0xFE000000`
- GPIO 上拉通过直接 2 位寄存器（比 BCM2710 更简单）
- 与 BCM2710 使用相同的邮箱接口

### BCM2712（Pi 5、CM5）
- 外设基地址通过 RP1 南桥
- HDMI 通过 RP1 路由——请勿配置 DPI GPIO 引脚
- SET_DEPTH 必须在完整分配之前以单独的邮箱调用发送
- 验证返回的 pitch == width × 4；pitch == width × 2 意味着 16bpp 分配失败

### JH7110（Milk-V Mars）
- DRAM 基地址：`0x40000000`；内核加载于 `0x40200000`
- 帧缓冲：`0xFE000000`（通过 U-Boot `bdinfo` 确认）
- 显示控制器：DC8200 位于 `0x29400000`
- L2 缓存刷新通过 SiFive Flush64 位于 `0x02010200`——仅 `fence` 不够
- U-Boot 2021.10 **不会**注入 `simple-framebuffer` DTB 节点——硬编码回退路径对于此 U-Boot 版本是永久性的，不是临时解决方案
- CPU：SiFive U74-MC，RV64IMAFDCBX——无 Zicbom，无 Svpbmt

### x86_64（LattePanda IOTA / MU）
- 通过 UEFI 启动——PE/COFF EFI 应用位于 `\EFI\BOOT\BOOTX64.EFI`
- 帧缓冲通过 GOP（图形输出协议）分配
- 无设备树——平台信息来自 CPUID 和 ACPI 表

---
教学用途。请参阅 LICENSE 文件。