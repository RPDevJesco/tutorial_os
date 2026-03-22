# Tutorial-OS 硬件抽象层（HAL）

一个面向真实硬件、跨多架构的裸机教学操作系统。
本文档为 Tutorial-OS 的 Rust 对等实现，旨在实现与 C 实现的**设计原则对等**，
而非逐行的结构复刻。

## 设计理念

C 和 Rust 两种实现共享相同的架构概念——分层 HAL、按 SoC 划分的实现、共享的可移植驱动——
但通过各自语言的原生习惯来表达。
C 使用 Makefile 级联（`board.mk → soc.mk → Makefile`）来实现分层隔离，
Rust 则通过 Cargo 工作区和依赖解析在编译期实现同样的边界。
两种实现之间的结构差异本身就是一个教学点：
两种语言用根本不同的工具解决同一个系统问题。

**对等意味着：**
- 相同的 HAL 契约（Rust 使用 trait 而非 C 的函数指针表）
- 相同的硬件支持（完全一致的开发板和 SoC 覆盖范围）
- 相同的启动流程（共享汇编入口点，而非重新实现）
- 相同的界面和显示输出（Hardware Inspector 渲染结果完全一致）

**对等不意味着：**
- 相同的文件名或目录层级
- 匹配的代码行数或函数签名
- 将 C 的模式强加于 Rust，或反之

## 支持平台

| 开发板                           | SoC             | 架构         | 实现状态      | 构建状态      | C 代码状态    | Rust 代码状态    |
|---------------------------------|-----------------|-------------|--------------|--------------|--------------|-----------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ 完成       | ✅ 通过      | ✅ 完成      | ✅ 完成          |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ 完成       | ✅ 通过      | ✅ 完成      | ❌ 未完成        |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ 完成       | ✅ 通过      | ✅ 完成      | ❌ 未完成        |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ 完成       | ✅ 通过      | ✅ 完成      | ❌ 未完成        |
| LattePanda Iota                 | N150            | x86_64       | ✅ 完成       | ✅ 通过      | ✅ 完成      | ❌ 未完成        |
| LattePanda MU Compute           | N100 / N305     | x86_64       | ✅ 完成       | ✅ 通过      | ✅ 完成      | ❌ 未完成        |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ 完成       | ✅ 通过      | ✅ 完成      | ✅ 完成          |

## C 与 Rust 实现对比

| 方面 | C 实现 | Rust 实现 |
|------|--------|----------|
| 构建系统 | `board.mk → soc.mk → Makefile` | Cargo 工作区 + feature flags |
| HAL 契约 | 函数指针表（`hal_platform_t`） | Trait（`pub trait Platform`） |
| 边界约束 | 约定俗成（依赖开发者自律） | 编译期保证（crate 依赖关系） |
| SoC 选择 | Makefile include 链 | `--features board-xxx` |
| 汇编集成 | 直接在 Makefile 中引用 | `build.rs` + `cc` crate |
| 外部依赖 | 无（独立 C 环境） | 运行时无依赖（零 crate） |
| 链接脚本 | 共用 | 共用（相同文件） |
| 启动汇编 | 共用 | 共用（相同文件） |


https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

## 目录结构

```
tutorial-os/
├── hal/src/                    # 硬件抽象层接口
│   ├── hal.h                   # 主包含文件
│   ├── hal_types.h             # 类型、错误码、MMIO
│   ├── hal_cpu.h               # CPU 操作
│   ├── hal_platform.h          # 平台信息、温度、时钟
│   ├── hal_timer.h             # 定时与延时
│   ├── hal_gpio.h              # GPIO 控制
│   ├── hal_dsi.h               # 可移植 DSI/DCS 命令层
│   ├── hal_dma.h               # 缓存一致性、地址转换与缓冲区所有权追踪
│   ├── lib.rs                  # 共享库
│   ├── cpu.rs                  # CPU 操作
│   ├── display.rs              # 显示初始化
│   ├── dma.rs                  # 缓存一致性、地址转换与缓冲区所有权追踪
│   ├── dsi.rs                  # 可移植 DSI/DCS 命令层
│   ├── gpio.rs                 # GPIO 控制
│   ├── timer.rs                # 定时与延时
│   ├── types.rs                # 类型、错误码、MMIO
│   └── hal_display.h           # 显示初始化
│
│   # 每个 SoC 尽量遵循相同的文件命名模式
├── soc                                 # SoC 专用实现
│   ├── bcm2710                         # Raspberry Pi 3B、3B+、3A+、Zero 2 W 及 CM3 设备
│   │   ├── boot_soc.S                  # SoC 专用启动代码
│   │   ├── build.rs                    # 编译共享的 ARM64 启动汇编
│   │   ├── Cargo.toml                  # bcm2710 Crate
│   │   ├── linker.ld                   # 链接脚本
│   │   ├── soc.mk                      # bcm2710 构建配置
│   │   ├── /src/  
│   │   │   ├── bcm2710_mailbox.h       # Mailbox 接口
│   │   │   ├── bcm2710_regs.h          # 寄存器定义
│   │   │   ├── display_dpi.c           # 显示实现（DPI/HDMI）
│   │   │   ├── gpio.c                  # GPIO 实现
│   │   │   ├── mailbox.c               # Mailbox 实现
│   │   │   ├── mailbox.rs              # Mailbox 实现
│   │   │   ├── regs.rs                 # 寄存器定义
│   │   │   ├── soc_init.c              # 平台初始化
│   │   │   ├── soc_init.rs             # 平台初始化
│   │   │   ├── timer.c                 # 定时器实现
│   │   │   └── timer.rs                # 定时器实现

│   ├── jh7110/                         # Milk-V Mars
│   │   ├── blobs                       # 设备树 DTB 文件
│   │   ├── build.rs                    # 编译共享的 RISC-V 启动汇编
│   │   ├── Cargo.toml                  # jh7110 Crate
│   │   ├── linker.ld                   # 链接脚本
│   │   ├── mmu.S                       # JH7110 的 Sv39 页表设置
│   │   ├── soc.mk                      # jh7110 构建配置
│   │   ├── /src/    
│   │   │   ├── /drivers/   
│   │   │   │   ├── mod.rs              # 共享库
│   │   │   │   ├── i2c.c               # Synopsys DesignWare I2C 主机驱动
│   │   │   │   ├── i2c.h               # Synopsys DesignWare I2C 主机驱动
│   │   │   │   ├── i2c.rs              # Synopsys DesignWare I2C 主机驱动
│   │   │   │   ├── pmic_aaxp15060.c    # X-Powers AXP15060 PMIC 驱动
│   │   │   │   ├── pmic_aaxp15060.h    # X-Powers AXP15060 PMIC 驱动
│   │   │   │   ├── pmic_aaxp15060.rs   # X-Powers AXP15060 PMIC 驱动
│   │   │   │   ├── sbi.c               # SBI（Supervisor Binary Interface）ecall 接口
│   │   │   │   ├── sbi.h               # SBI（Supervisor Binary Interface）ecall 接口
│   │   │   │   └── sbi.rs              # SBI（Supervisor Binary Interface）ecall 接口
│   │   │   ├── cache.c                 # 缓存管理
│   │   │   ├── cache.rs                # 缓存管理
│   │   │   ├── cpu.rs                  # CPU 操作
│   │   │   ├── display_simplefb.c      # 显示驱动
│   │   │   ├── display_simplefb.rs     # 显示驱动
│   │   │   ├── gpio.c                  # GPIO 实现
│   │   │   ├── jh7110_cpu.h            # CPU 操作
│   │   │   ├── lib.rs                  # 共享库
│   │   │   ├── gpio.rs                 # GPIO 实现
│   │   │   ├── hal_platform_jh7110.c   # RISC-V 对等于 soc/bcm2710/soc_init.c 在 Pi 上的功能
│   │   │   ├── jh7110_regs.h           # 寄存器定义
│   │   │   ├── soc_init.c              # 平台初始化
│   │   │   ├── soc_init.rs             # 平台初始化
│   │   │   ├── timer.c                 # 定时器实现
│   │   │   ├── timer.rs                # 定时器实现
│   │   │   ├── uart.c                  # UART 驱动
│   │   │   └── uart.rs                 # UART 驱动
│
├── board/                      # 开发板专用配置
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
│   │    └── mkimage.sh          # 创建包含 U-Boot 配置的镜像
|   |
│   ├── lattepanda-mu/
│   │    ├── board.mk
│   │    ├── mkimage.py          # 创建包含 PE/COFF EFI 应用配置的镜像
│   │    └── mkimage.sh          # mkimage.py 的 .sh 封装
│   │
│   └── orangepi-rv2/
│       ├── env_k1-x.txt
│       ├── board.mk
│       ├── boot.cmd
│       ├── DEPLOY.md
│       └── mkimage.sh          # 创建包含 U-Boot 配置的镜像
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
│   └── x86_64/                 # 空目录，gnu-efi 不需要此内容
│
├── common/src/                 # 共享的最小 libc 子集和 MMIO
│   ├── lib.rs                  # 共享库
│   ├── mem.rs                  # 编译器要求的内存内联函数
│   ├── mmio.rs                 # 内存映射 I/O 与系统原语
│   ├── mmio.h                  # 内存映射 I/O 与系统原语
│   ├── string.c                # 内存与字符串函数
│   ├── string.h                # 字符串与内存函数声明
│   ├── types.rs                # 类型定义
│   └── types.h                 # 类型定义
│
├── drivers/src/                # 可移植驱动
│   ├── framebuffer/            # 绘图定义
│   │   ├── fb_pixel.h          # 格式感知像素访问辅助函数
│   │   ├── mod.rs              # 32 位 ARGB8888 帧缓冲驱动及格式感知像素访问辅助函数
│   │   ├── framebuffer.h       # 32 位 ARGB8888 帧缓冲驱动
│   └   └── framebuffer.c       # 帧缓冲定义
│
├── kernel/src/                 # 内核代码
│   ├── main.rs                 # 主应用入口点
│   └── main.c                  # 主应用入口点
│
├── memory/src/                 # 内存管理
│   ├── lib.rs                  # 共享库
│   ├── allocator.rs            # 基于 TLSF 的内存分配器声明
│   ├── allocator.h             # 基于 TLSF 的内存分配器声明
│   └── allocator.c             # 基于 TLSF 的内存分配器
│
├── ui/                         # 界面系统
│   ├── core/src/               # 核心 UI 画布与类型定义
│   │   ├── mod.rs              # 共享库
│   │   ├── types.rs            # 核心 UI 类型定义
│   │   ├── canvas.rs           # 画布与文本渲染器接口
│   │   ├── ui_canvas.h         # 画布与文本渲染器接口
│   │   └── ui_types.h          # 核心 UI 类型定义
│   ├── themes/src/             # UI 主题系统
│   │   ├── mod.rs              # 共享库
│   │   ├── theme.rs            # UI 主题系统定义
│   │   └── ui_theme.h          # UI 主题系统定义
│   ├── widgets/src/            # 可复用 UI 组件函数
│   │   ├── mod.rs              # 共享库
│   │   ├── widgets.rs          # UI 组件定义
│   │   ├── ui_widgets.h        # UI 组件定义
│   └   └── ui_widgets.c        # UI 组件实现
│
├── build.sh                    # Linux / MacOS 构建脚本
├── build.bat                   # Windows 构建脚本
├── cargo.toml                  # Rust 构建系统
├── build.bat                   # Windows 构建脚本
├── docker-build.sh             # 构建系统
├── Dockerfile                  # 构建系统
├── Makefile                    # 构建系统
└── README.md                   # 本文件
```

## 构建

```bash
# 为指定开发板构建，.bat 和 .sh 使用相同参数
# 可以选择构建 C 或 Rust 版本，默认为 C，添加 rust 语言参数即可构建 Rust 版本。
build.bat rpi-zero2w-gpi      :: → output/rpi-zero2w/kernel8.img
build.bat rpi-cm4 rust        :: → output/rpi-cm4/kernel8.img
build.bat rpi-5               :: → output/rpi-5/kernel8.img
build.bat orangepi-rv2        :: → output/orangepi-rv2/kernel.bin
build.bat milkv-mars          :: → output/milkv-mars/kernel.bin
build.bat lattepanda-mu       :: → output/lattepanda-mu/kernel.efi
build.bat lattepanda-iota     :: → output/lattepanda-iota/kernel.efi
build.bat clean               :: 删除 target/ 和 output/

# Milk-V Mars 和 Orange Pi RV 2 需要额外的构建步骤，因为它们需要 U-Boot 集成
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=milkv-mars image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=orangepi-rv2 image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-mu image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-iota image
```

## 启动文件（注意！各平台不同！）

每块开发板的启动要求不同。详情请参阅 `board/<name>/boot/BOOT_FILES.md`。

### Raspberry Pi（Zero 2W、CM4）

启动分区需要：
```
/boot/
├── bootcode.bin      # （仅 Pi Zero 2W 需要，CM4 不需要）
├── start.elf         # （CM4 使用 start4.elf）
├── fixup.dat         # （CM4 使用 fixup4.dat）
├── config.txt        # 在 board/xxx/boot/ 中提供
└── kernel8.img       # Tutorial-OS（构建输出）
```

固件获取地址：https://github.com/raspberrypi/firmware/tree/master/boot

## 核心设计原则

### 1. HAL 抽象硬件差异

三条截然不同的路径通向屏幕上的同一个像素——HAL 使它们从 `main.c` 的视角看起来完全一致：

| 功能 | BCM2710/2711/2712（ARM64） | JH7110（RISC-V64） | x86_64（UEFI） |
|------|---------------------------|-------------------|----------------|
| 启动 | VideoCore GPU 固件 | U-Boot + OpenSBI | UEFI 固件 |
| 显示初始化 | Mailbox 属性标签 | 来自 DTB 的 SimpleFB | GOP 协议 |
| 帧缓冲 | 由 VideoCore 分配 | U-Boot 预配置 | 由 GOP 分配 |
| 缓存刷新 | ARM DSB + 缓存操作 | SiFive L2 Flush64 | x86 CLFLUSH |
| 定时器 | MMIO 系统定时器 | RISC-V `rdtime` CSR | HPET / TSC |
| 平台信息 | Mailbox 查询 | 固定常量 + DTB | CPUID + ACPI |

### 2. 编译期平台选择

没有运行时的 `if (platform == X)` 检查。构建系统在编译期选择正确的实现：

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 3. 契约优先的 HAL 设计

HAL 接口头文件在任何实现存在之前即已定义。每个平台实现相同的契约，
绘图代码永远不需要知道它在与契约的哪一侧对话。

---

## 平台专用说明

### BCM2710（Pi Zero 2W、Pi 3）
- 外设基址：`0x3F000000`
- GPIO 上拉需要 GPPUD + GPPUDCLK 序列
- 通过 VideoCore Mailbox 属性标签进行显示
- GPi Case 使用 GPIO 0–27（ALT2）的 DPI 输出

### BCM2711（Pi 4、CM4）
- 外设基址：`0xFE000000`
- GPIO 上拉通过直接的 2 位寄存器实现（比 BCM2710 简单）
- 与 BCM2710 使用相同的 Mailbox 接口

### BCM2712（Pi 5、CM5）
- 外设基址通过 RP1 南桥
- HDMI 通过 RP1 路由——**不要**配置 DPI GPIO 引脚
- SET_DEPTH 必须在完整分配之前通过单独的 Mailbox 调用发送
- 验证返回的 pitch == width × 4；如果 pitch == width × 2 则表示 16bpp 分配失败

### JH7110（Milk-V Mars）
- DRAM 基址：`0x40000000`；内核加载于 `0x40200000`
- 帧缓冲：`0xFE000000`（通过 U-Boot `bdinfo` 确认）
- 显示控制器：DC8200 位于 `0x29400000`
- L2 缓存刷新通过 SiFive Flush64 位于 `0x02010200`——仅 `fence` 不够
- U-Boot 2021.10 **不会**注入 `simple-framebuffer` DTB 节点——硬编码的回退路径对于此 U-Boot 版本是永久性的，并非临时解决方案
- CPU：SiFive U74-MC，RV64IMAFDCBX——无 Zicbom，无 Svpbmt

### x86_64（LattePanda IOTA / MU）
- 通过 UEFI 启动——PE/COFF EFI 应用位于 `\EFI\BOOT\BOOTX64.EFI`
- 帧缓冲通过 GOP（Graphics Output Protocol）分配
- 无设备树——平台信息来自 CPUID 和 ACPI 表

---
教学用途。详见 LICENSE 文件。