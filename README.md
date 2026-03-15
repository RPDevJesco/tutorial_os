# Tutorial-OS Hardware Abstraction Layer (HAL)

A multi-platform bare-metal operating system designed to teach low-level systems programming through hands-on hardware interaction.

## Supported Platforms

| Board                           | SoC             | Architecture | Implementation Status | Build Status   |
|---------------------------------|-----------------|--------------|-----------------------|----------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ Complete            | ✅ Passing     |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ Complete            | ✅ Passing     |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ Complete            | ✅ Passing     |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ Complete            | ✅ Passing     |
| LattePanda Iota                 | N150            | x86_64       | ❌ Incomplete          | ❌ Failing     |
| LattePanda MU Compute           | N100            | x86_64       | ✅ Complete            | ✅ Passing     |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ Complete            | ✅ Passing     |

https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

## Directory Structure

```
tutorial-os/
├── hal/                        # Hardware Abstraction Layer interfaces
│   ├── hal.h                   # Master include
│   ├── hal_types.h             # Types, error codes, MMIO
│   ├── hal_platform.h          # Platform info, temp, clocks
│   ├── hal_timer.h             # Timing and delays
│   ├── hal_gpio.h              # GPIO control
│   └── hal_display.h           # Display initialization
│
│   # Each soc attempts to follow the same pattern for files
├── soc/                        # SoC-specific implementations
│   ├── bcm2710/                # Raspberry Pi 3B, 3B+, 3A+, Zero 2 W, and CM3 devices
│   │   ├── bcm2710_mailbox.h   # Mailbox Interface
│   │   ├── bcm2710_regs.h      # Register Definitions
│   │   ├── boot_soc.S          # SoC-Specific Boot Code
│   │   ├── display_dpi.c       # Display Implementation (DPI/HDMI)
│   │   ├── gpio.c              # GPIO Implementation
│   │   ├── linker.ld           # Linker Script
│   │   ├── mailbox.c           # Mailbox Implementation
│   │   ├── soc.mk              # BCM2710 Configuration
│   │   ├── soc_init.c          # Platform Initialization
│   │   └── timer.c             # Timer Implementation
│   ├── bcm2711/                # Raspberry Pi 4, CM4, Pi 400
│   │   ├── bcm2711_mailbox.h   # Mailbox Interface
│   │   ├── bcm2711_regs.h      # Register Definitions
│   │   ├── boot_soc.S          # SoC-Specific Boot Code
│   │   ├── display_dpi.c       # Display Implementation (DPI/HDMI)
│   │   ├── gpio.c              # GPIO Implementation
│   │   ├── linker.ld           # Linker Script
│   │   ├── mailbox.c           # Mailbox Implementation
│   │   ├── soc.mk              # BCM2711 Configuration
│   │   ├── soc_init.c          # Platform Initialization
│   │   └── timer.c             # Timer Implementation
│   ├── bcm2712/                # Raspberry Pi 5, CM5
│   │   ├── bcm2712_mailbox.h   # Mailbox Interface
│   │   ├── bcm2712_regs.h      # Register Definitions
│   │   ├── boot_soc.S          # SoC-Specific Boot Code
│   │   ├── display_dpi.c       # Display Implementation (DPI/HDMI)
│   │   ├── gpio.c              # GPIO Implementation
│   │   ├── linker.ld           # Linker Script
│   │   ├── mailbox.c           # Mailbox Implementation
│   │   ├── soc.mk              # BCM2712 Configuration
│   │   ├── soc_init.c          # Platform Initialization
│   │   └── timer.c             # Timer Implementation
│   ├── kyx1/                   # Orange Pi RV 2
│   │   ├── display_simplefb.c  # Display Driver
│   │   ├── blobs               # Uboot bins extracted from a build and dts files for device tree
│   │   ├── drivers             # i2c, pmic_spm8821 and sbi driver code
│   │   ├── gpio.c              # GPIO Implementation
│   │   ├── hal_platform_kyx1   # RISC-V equivalent of what soc/bcm2710/soc_init.c does for the Pi
│   │   ├── kyx1_cpu.h          # CPU Operations
│   │   ├── kyx1_regs.h         # Register Definitions
│   │   ├── linker.ld           # Linker Script
│   │   ├── soc.mk              # KYX1 Configuration
│   │   ├── soc_init.c          # Platform Initialization
│   │   ├── timer.c             # Timer Implementation
│   │   └── uart.c              # UART Driver
│   ├── lattepanda_n100/        # N100 CPU for LattePanda MU
│   │   ├── display_gop.c       # Display Driver
│   │   ├── gpio.c              # GPIO Implementation
│   │   ├── hal_platform_n100   # x86_64 equivalent of what soc/bcm2710/soc_init.c does for the Pi
│   │   ├── linker.ld           # Linker Script
│   │   ├── soc.mk              # N100 Configuration
│   │   ├── soc_init.c          # Platform Initialization
│   │   ├── timer.c             # Timer Implementation
│   │   └── uart_8250.c         # UART Driver
│   ├── jh7110/                 # Milk-V Mars
│   │   ├── display_simplefb.c  # Display Driver
│   │   ├── blobs               # dtbs files for device tree
│   │   ├── gpio.c              # GPIO Implementation
│   │   ├── hal_platform_jh7110 # RISC-V equivalent of what soc/bcm2710/soc_init.c does for the Pi
│   │   ├── jh7110_cpu.h        # CPU Operations
│   │   ├── jh7110_regs.h       # Register Definitions
│   │   ├── linker.ld           # Linker Script
│   │   ├── soc.mk              # jh7110 Configuration
│   │   ├── mmu.S               # Sv39 Page Table Setup for JH7110
│   │   ├── soc_init.c          # Platform Initialization
│   │   ├── timer.c             # Timer Implementation
│   └   └── uart.c              # UART Driver
│
├── board/                      # Board-specific configurations
│   ├── rpi-zero2w-gpi/
│   │   ├── board.mk            # Build configuration
│   │   └── boot/               # Boot files for SD card
│   │       ├── config.txt      # VideoCore GPU config
│   │       └── BOOT_FILES.md   # Instructions
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
│   │    └── mkimage.sh          # creates the img with uboot configuration
|   |
│   ├── lattepanda-mu/
│   │    ├── board.mk
│   │    ├── mkimage.py          # creates the img with PE/COFF EFI application configuration
│   │    └── mkimage.sh          # .sh wrapper for mkimage.py
│   │
│   └── orangepi-rv2/
│       ├── env_k1-x.txt
│       ├── board.mk
│       ├── boot.cmd
│       ├── DEPLOY.md
│       └── mkimage.sh          # creates the img with uboot configuration
│
├── boot/                       # Core assembly entry points
│   ├── arm64/
│   │   ├── cache.S             # Cache Maintenance Functions
│   │   ├── common_init.S       # Common Post-SoC Initialization
│   │   ├── entry.S             # Entry Point
│   │   └── vectors.S           # Exception Vector Table
│   ├── riscv64/
│   │   ├── cache.S             # Cache Maintenance Functions
│   │   ├── common_init.S       # Common Post-SoC Initialization
│   │   ├── entry.S             # Entry Point
│   │   └── vectors.S           # Exception Vector Table
│   └── x86_64/                 # Empty as we don't need it with gnu-efi
│
├── common/                     # Shared (less than) minimal libc and mmio
│   ├── mmio.h                  # Memory-Mapped I/O and System Primitives
│   ├── string.c                # Memory and String Functions
│   ├── string.h                # String and Memory Function Declarations
│   └── types.h                 # Type Definitions
│
├── drivers/                    # Portable drivers
│   ├── framebuffer/            # Drawing Definitions
│   │   ├── framebuffer.h       # 32-bit ARGB8888 Framebuffer Driver
│   └   └── framebuffer.c       # Framebuffer definitions
│
├── kernel/                     # Kernel code
│   └── main.c                  # Main application entry point
│
├── memory/                     # Memory management
│   ├── allocator.h             # TLSF-Inspired Memory Allocator Declaration
│   └── allocator.c             # TLSF-Inspired Memory Allocator
│
├── ui/                         # UI System
│   ├── core/                   # Core UI Canvas and Type definitions
│   │   ├── ui_canvas.h         # Canvas and Text Renderer Interfaces
│   │   └── ui_types.h          # Core UI Type Definitions
│   ├── themes/                 # UI Theme System
│   │   └── ui_theme.h          # UI Theme System definitions
│   ├── widgets/                # Reusable UI Widget Functions
│   │   ├── ui_widgets.h        # UI Widget definitions
│   └   └── ui_widgets.c        # UI Widget Implementations
│
├── build.sh                    # Build on Linux / MacOS
├── build.bat                   # Build on Windows
├── docker-build.sh             # Build system
├── Dockerfile                  # Build system
├── Makefile                    # Build system
└── README.md                   # This file
```

## Building

```bash
# Build for Raspberry Pi Zero 2W with GPi Case
make LANG=c BOARD=rpi-zero2w-gpi

# Build for Raspberry Pi CM4
make LANG=rust BOARD=rpi-cm4-io

# Show build info
make info

# Clean
make clean

# Or use docker to build them all via the build commands
./build.bat    
./build.sh

# For milk-v mars and orange pi rv 2, there is an additional build step required as they need uboot integration
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=milkv-mars image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=orangepi-rv2 image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-mu image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-iota image
```

## Boot Files (Warning! Platform-Specific!)

Each board has different boot requirements. See `board/<name>/boot/BOOT_FILES.md` for details.

### Raspberry Pi (Zero 2W, CM4)

Boot partition needs:
```
/boot/
├── bootcode.bin      # (Pi Zero 2W only, not CM4)
├── start.elf         # (or start4.elf for CM4)
├── fixup.dat         # (or fixup4.dat for CM4)
├── config.txt        # PROVIDED in board/xxx/boot/
└── kernel8.img       # Tutorial-OS (build output)
```

Get firmware from: https://github.com/raspberrypi/firmware/tree/master/boot

## Key Design Principles

### 1. Drawing Code Stays Portable

Your `fb_*()` drawing functions don't change between platforms. The same `main.c` renders identically on ARM64, RISC-V64, and x86_64:

```c
// This works on ALL platforms without a single #ifdef
fb_clear(fb, 0xFF000000);
fb_fill_rect(fb, 10, 10, 100, 50, 0xFFFFFFFF);
fb_draw_string_transparent(fb, 20, 20, "Hello World!", 0xFFFFFFFF);
ui_draw_panel(fb, panel, &theme, UI_PANEL_ELEVATED);
```

### 2. HAL Abstracts Hardware Differences

Three fundamentally different paths to the same pixel on screen — the HAL makes them identical from `main.c`'s perspective:

| Feature | BCM2710/2711/2712 (ARM64) | JH7110 (RISC-V64) | x86_64 (UEFI) |
|---------|--------------------------|-------------------|----------------|
| Boot | VideoCore GPU firmware | U-Boot + OpenSBI | UEFI firmware |
| Display init | Mailbox property tags | SimpleFB from DTB | GOP protocol |
| Framebuffer | Allocated by VideoCore | Pre-configured by U-Boot | Allocated by GOP |
| Cache flush | ARM DSB + cache ops | SiFive L2 Flush64 | x86 CLFLUSH |
| Timer | MMIO System Timer | RISC-V `rdtime` CSR | HPET / TSC |
| Platform info | Mailbox queries | Fixed constants + DTB | CPUID + ACPI |

### 3. Compile-Time Platform Selection

No runtime `if (platform == X)` checks. The build system selects the correct implementation at compile time:

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 4. Contract-First HAL Design

HAL interface headers are defined before any implementation exists. Every platform implements the same contract — the drawing code never needs to know which side of the contract it's talking to.

### 5. Error Handling

HAL functions return `hal_error_t`:

```c
hal_error_t err = hal_display_init(&fb);
if (HAL_FAILED(err)) {
    if (err == HAL_ERROR_DISPLAY_MAILBOX) { ... }
}
```

---

## Adding a New Platform

1. **Create SoC directory**: `soc/newsoc/`
2. **Implement HAL interfaces**:
   - `uart.c` — UART driver (needed for debug output before display works)
   - `timer.c` — Timer and delay functions
   - `gpio.c` — GPIO control
   - `soc_init.c` — Platform initialization
   - `display_*.c` — Display driver
3. **Create register header**: `newsoc_regs.h`
4. **Create build rules**: `soc.mk`
5. **Create board config**: `board/newboard/board.mk`

**Critical checklist for SimpleFB-based displays** (U-Boot + device tree platforms):

After populating `framebuffer_t` in your `display_init`, always initialize the clip stack before returning:

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

Skipping this will cause every `fb_fill_rect`, `fb_draw_string`, and widget call to silently draw nothing while `fb_clear` continues to work — making the display pipeline appear healthy when it isn't.

---

## Platform-Specific Notes

### BCM2710 (Pi Zero 2W, Pi 3)
- Peripheral base: `0x3F000000`
- GPIO pull requires GPPUD + GPPUDCLK sequence
- Display via VideoCore mailbox property tags
- DPI output on GPIO 0–27 (ALT2) for GPi Case

### BCM2711 (Pi 4, CM4)
- Peripheral base: `0xFE000000`
- GPIO pull via direct 2-bit registers (simpler than BCM2710)
- Same mailbox interface as BCM2710

### BCM2712 (Pi 5, CM5)
- Peripheral base via RP1 southbridge
- HDMI routed through RP1 — do NOT configure DPI GPIO pins
- SET_DEPTH must be sent in a separate mailbox call before full allocation
- Verify returned pitch == width × 4; pitch == width × 2 means 16bpp allocation failed

### JH7110 (Milk-V Mars)
- DRAM base: `0x40000000`; kernel loads at `0x40200000`
- Framebuffer: `0xFE000000` (confirmed via U-Boot `bdinfo`)
- Display controller: DC8200 at `0x29400000`
- L2 Cache flush via SiFive Flush64 at `0x02010200` — `fence` alone is insufficient
- U-Boot 2021.10 does **not** inject a `simple-framebuffer` DTB node — the hardcoded fallback path is permanent for this U-Boot version, not a temporary workaround
- CPU: SiFive U74-MC, RV64IMAFDCBX — no Zicbom, no Svpbmt

### x86_64 (LattePanda IOTA / MU)
- Boot via UEFI — PE/COFF EFI application at `\EFI\BOOT\BOOTX64.EFI`
- Framebuffer allocated via GOP (Graphics Output Protocol)
- No device tree — platform info from CPUID and ACPI tables

---
Educational use. See LICENSE file.