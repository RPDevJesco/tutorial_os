# Tutorial-OS Hardware Abstraction Layer (HAL)

A bare-metal educational operating system targeting real hardware across multiple architectures.
This is the Rust parity implementation of Tutorial-OS, designed to achieve **design-principle parity** with the C implementation,
not line-for-line structural mirroring.

## Philosophy

The C and Rust implementations share the same architectural concepts; a layered HAL, per-SoC implementations, shared portable drivers
but express them through each language's native idioms.
Where C uses a Makefile cascade (`board.mk → soc.mk → Makefile`) to enforce layered separation,
Rust achieves the same boundaries at compile time through Cargo's workspace and dependency resolution.
The structural divergence between implementations is itself a teaching moment:
two languages solving the same systems problem with fundamentally different tools.

**Parity means:**
- Same HAL contracts (expressed as Rust traits instead of C function pointer tables)
- Same hardware support (identical board and SoC coverage)
- Same boot flow (assembly entry points are shared, not reimplemented)
- Same UI and display output (Hardware Inspector renders identically)

**Parity does not mean:**
- Identical file names or directory depth
- Matching line counts or function signatures
- Forcing C patterns into Rust or vice versa

## Supported Platforms

| Board                           | SoC             | Architecture | Implementation Status | Build Status  | C Code Status | Rust Code Status |
|---------------------------------|-----------------|--------------|-----------------------|---------------|---------------|------------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ Complete            | ✅ Passing  | ✅ Complete   | ✅ Complete      |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ Complete            | ✅ Passing  | ✅ Complete   | ❌ Incomplete    |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ Complete            | ✅ Passing  | ✅ Complete   | ❌ Incomplete    |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ Complete            | ✅ Passing  | ✅ Complete   | ❌ Incomplete    |
| LattePanda Iota                 | N150            | x86_64       | ✅ Complete            | ✅ Passing  | ✅ Complete   | ❌ Incomplete    |
| LattePanda MU Compute           | N100 / N305     | x86_64       | ✅ Complete            | ✅ Passing  | ✅ Complete   | ❌ Incomplete    |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ Complete            | ✅ Passing  | ✅ Complete   | ✅ Complete      |

## Comparison: C vs Rust Implementation

| Aspect | C Implementation | Rust Implementation |
|---|---|---|
| Build system | `board.mk → soc.mk → Makefile` | Cargo workspace + feature flags |
| HAL contracts | Function pointer tables (`hal_platform_t`) | Traits (`pub trait Platform`) |
| Boundary enforcement | Convention (developer discipline) | Compile-time (crate dependencies) |
| SoC selection | Makefile include chain | `--features board-xxx` |
| Assembly integration | Direct in Makefile | `build.rs` + `cc` crate |
| External dependencies | None (freestanding C) | None at runtime (zero crates) |
| Linker scripts | Identical | Identical (shared files) |
| Boot assembly | Identical | Identical (shared files) |


https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

## Directory Structure
> **Recommended reading order:** Boot → Common → Memory → HAL → Drivers → UI → SoC → Board → Kernel
```
tutorial-os/
├── hal/src/                    # Hardware Abstraction Layer interfaces
│   ├── hal.h                   # Master include
│   ├── hal_types.h             # Types, error codes, MMIO
│   ├── hal_cpu.h               # CPU operations
│   ├── hal_platform.h          # Platform info, temp, clocks
│   ├── hal_timer.h             # Timing and delays
│   ├── hal_gpio.h              # GPIO control
│   ├── hal_dsi.h               # Portable DSI/DCS command layer
│   ├── hal_dma.h               # Cache coherency, address translation, and buffer ownership tracking.
│   ├── lib.rs                  # Shared libraries
│   ├── cpu.rs                  # CPU operations
│   ├── display.rs              # Display initialization
│   ├── dma.rs                  # Cache coherency, address translation, and buffer ownership tracking.
│   ├── dsi.rs                  # Portable DSI/DCS command layer
│   ├── gpio.rs                 # GPIO control
│   ├── timer.rs                # Timing and delays
│   ├── types.rs                # Types, error codes, MMIO
│   └── hal_display.h           # Display initialization
│
│   # Each soc attempts to follow the same pattern for files
├── soc                                 # SoC-specific implementations
│   ├── bcm2710                         # Raspberry Pi 3B, 3B+, 3A+, Zero 2 W, and CM3 devices
│   │   ├── boot_soc.S                  # SoC-Specific Boot Code
│   │   ├── build.rs                    # Compiles the shared ARM64 boot assembly
│   │   ├── Cargo.toml                  # bcm2710 Crate
│   │   ├── linker.ld                   # Linker Script
│   │   ├── soc.mk                      # bcm2710 Configuration
│   │   ├── /src/  
│   │   │   ├── bcm2710_mailbox.h       # Mailbox Interface
│   │   │   ├── bcm2710_regs.h          # Register Definitions
│   │   │   ├── display_dpi.c           # Display Implementation (DPI/HDMI)
│   │   │   ├── gpio.c                  # GPIO Implementation
│   │   │   ├── mailbox.c               # Mailbox Implementation
│   │   │   ├── mailbox.rs              # Mailbox Implementation
│   │   │   ├── regs.rs                 # Register Definitions
│   │   │   ├── soc_init.c              # Platform Initialization
│   │   │   ├── soc_init.rs             # Platform Initialization
│   │   │   ├── timer.c                 # Timer Implementation
│   │   │   └── timer.rs                # Timer Implementation

│   ├── jh7110/                         # Milk-V Mars
│   │   ├── blobs                       # dtbs files for device tree
│   │   ├── build.rs                    # Compiles the shared RISC-V boot assembly
│   │   ├── Cargo.toml                  # jh7110 Crate
│   │   ├── linker.ld                   # Linker Script
│   │   ├── mmu.S                       # Sv39 Page Table Setup for JH7110
│   │   ├── soc.mk                      # jh7110 Configuration
│   │   ├── /src/    
│   │   │   ├── /drivers/   
│   │   │   │   ├── mod.rs              # Shared Libraries    
│   │   │   │   ├── i2c.c               # I2C master driver for the Synopsys DesignWare
│   │   │   │   ├── i2c.h               # I2C master driver for the Synopsys DesignWare
│   │   │   │   ├── i2c.rs              # I2C master driver for the Synopsys DesignWare
│   │   │   │   ├── pmic_aaxp15060.c    # X-Powers AXP15060 PMIC Driver
│   │   │   │   ├── pmic_aaxp15060.h    # X-Powers AXP15060 PMIC Driver
│   │   │   │   ├── pmic_aaxp15060.rs   # X-Powers AXP15060 PMIC Driver
│   │   │   │   ├── sbi.c               # SBI (Supervisor Binary Interface) ecall interface
│   │   │   │   ├── sbi.h               # SBI (Supervisor Binary Interface) ecall interface
│   │   │   │   └── sbi.rs              # SBI (Supervisor Binary Interface) ecall interface   
│   │   │   ├── cache.c                 # Cache management
│   │   │   ├── cache.rs                # Cache management
│   │   │   ├── cpu.rs                  # CPU operations
│   │   │   ├── display_simplefb.c      # Display Driver
│   │   │   ├── display_simplefb.rs     # Display Driver
│   │   │   ├── gpio.c                  # GPIO Implementation
│   │   │   ├── jh7110_cpu.h            # CPU Operations
│   │   │   ├── lib.rs                  # Shared Libraries
│   │   │   ├── gpio.rs                 # GPIO Implementation
│   │   │   ├── hal_platform_jh7110.c   # RISC-V equivalent of what soc/bcm2710/soc_init.c does for the Pi
│   │   │   ├── jh7110_regs.h           # Register Definitions
│   │   │   ├── soc_init.c              # Platform Initialization
│   │   │   ├── soc_init.rs             # Platform Initialization
│   │   │   ├── timer.c                 # Timer Implementation
│   │   │   ├── timer.rs                # Timer Implementation
│   │   │   ├── uart.c                  # UART Driver
│   │   │   └── uart.rs                 # UART Driver
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
├── common/src/                 # Shared (less than) minimal libc and mmio
│   ├── lib.rs                  # Shared libraries
│   ├── mem.rs                  # Compiler-Required Memory Intrinsics
│   ├── mmio.rs                 # Memory-Mapped I/O and System Primitives
│   ├── mmio.h                  # Memory-Mapped I/O and System Primitives
│   ├── string.c                # Memory and String Functions
│   ├── string.h                # String and Memory Function Declarations
│   ├── types.rs                # Type Definitions
│   └── types.h                 # Type Definitions
│
├── drivers/src/                # Portable drivers
│   ├── framebuffer/            # Drawing Definitions
│   │   ├── fb_pixel.h          # Format-Aware Pixel Access Helpers
│   │   ├── mod.rs              # 32-bit ARGB8888 Framebuffer Driver and Format-Aware Pixel Access Helpers
│   │   ├── framebuffer.h       # 32-bit ARGB8888 Framebuffer Driver
│   └   └── framebuffer.c       # Framebuffer definitions
│
├── kernel/src/                 # Kernel code
│   ├── main.rs                 # Main application entry point
│   └── main.c                  # Main application entry point
│
├── memory/src/                 # Memory management
│   ├── lib.rs                  # Shared libraries
│   ├── allocator.rs            # TLSF-Inspired Memory Allocator Declaration
│   ├── allocator.h             # TLSF-Inspired Memory Allocator Declaration
│   └── allocator.c             # TLSF-Inspired Memory Allocator
│
├── ui/                         # UI System
│   ├── core/src/               # Core UI Canvas and Type definitions
│   │   ├── mod.rs              # Shared libraries
│   │   ├── types.rs            # Core UI Type Definitions
│   │   ├── canvas.rs           # Canvas and Text Renderer Interfaces
│   │   ├── ui_canvas.h         # Canvas and Text Renderer Interfaces
│   │   └── ui_types.h          # Core UI Type Definitions
│   ├── themes/src/             # UI Theme System
│   │   ├── mod.rs              # Shared libraries
│   │   ├── theme.rs            # UI Theme System definitions
│   │   └── ui_theme.h          # UI Theme System definitions
│   ├── widgets/src/            # Reusable UI Widget Functions
│   │   ├── mod.rs              # Shared libraries
│   │   ├── widgets.rs          # UI Widget definitions
│   │   ├── ui_widgets.h        # UI Widget definitions
│   └   └── ui_widgets.c        # UI Widget Implementations
│
├── build.sh                    # Build on Linux / MacOS
├── build.bat                   # Build on Windows
├── cargo.toml                  # Rust Build System
├── build.bat                   # Build on Windows
├── docker-build.sh             # Build system
├── Dockerfile                  # Build system
├── Makefile                    # Build system
└── README.md                   # This file
```

## Building

```bash
# Build for a specific board, .bat and .sh require the same thing
# You can build for C or Rust, defaults are C, but add the rust lang parameter and you can build with Rust instead.
build.bat rpi-zero2w-gpi      :: → output/rpi-zero2w/kernel8.img
build.bat rpi-cm4 rust        :: → output/rpi-cm4/kernel8.img
build.bat rpi-5               :: → output/rpi-5/kernel8.img
build.bat orangepi-rv2        :: → output/orangepi-rv2/kernel.bin
build.bat milkv-mars          :: → output/milkv-mars/kernel.bin
build.bat lattepanda-mu       :: → output/lattepanda-mu/kernel.efi
build.bat lattepanda-iota     :: → output/lattepanda-iota/kernel.efi
build.bat clean               :: removes target/ and output/

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

### 1. HAL Abstracts Hardware Differences

Three fundamentally different paths to the same pixel on screen — the HAL makes them identical from `main.c`'s perspective:

| Feature | BCM2710/2711/2712 (ARM64) | JH7110 (RISC-V64) | x86_64 (UEFI) |
|---------|--------------------------|-------------------|----------------|
| Boot | VideoCore GPU firmware | U-Boot + OpenSBI | UEFI firmware |
| Display init | Mailbox property tags | SimpleFB from DTB | GOP protocol |
| Framebuffer | Allocated by VideoCore | Pre-configured by U-Boot | Allocated by GOP |
| Cache flush | ARM DSB + cache ops | SiFive L2 Flush64 | x86 CLFLUSH |
| Timer | MMIO System Timer | RISC-V `rdtime` CSR | HPET / TSC |
| Platform info | Mailbox queries | Fixed constants + DTB | CPUID + ACPI |

### 2. Compile-Time Platform Selection

No runtime `if (platform == X)` checks. The build system selects the correct implementation at compile time:

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 3. Contract-First HAL Design

HAL interface headers are defined before any implementation exists. Every platform implements the same contract,
the drawing code never needs to know which side of the contract it's talking to.

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
