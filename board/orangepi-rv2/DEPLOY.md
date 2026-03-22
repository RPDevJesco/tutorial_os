# Deploying Tutorial-OS on the Orange Pi RV2

## Quick Start

```bash
# Build
make BOARD=orangepi-rv2 CROSS_COMPILE=riscv64-linux-gnu-

# Copy to SD card
cp build/orangepi-rv2/tutorial-os-rv2.bin /media/sdcard/
cp board/orangepi-rv2/env_k1-x.txt
```

---

## Prerequisites

### Hardware
- Orange Pi RV2 board
- microSD card (8GB+ recommended)
- USB-C power supply (5V/3A)
- HDMI monitor + cable (display output)
- USB-to-UART adapter (for serial console, optional but recommended)

### Software
- RISC-V cross-compiler: `riscv64-linux-gnu-gcc` or `riscv64-unknown-elf-gcc`
  ```bash
  # Ubuntu/Debian
  sudo apt install gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu

  # Arch Linux
  sudo pacman -S riscv64-linux-gnu-gcc

  # macOS (Homebrew)
  brew install riscv64-elf-gcc
  ```
- `mkimage` (for boot.scr, optional): `sudo apt install u-boot-tools`

---

## Building

```bash
# Default (uses riscv64-unknown-elf- prefix)
make BOARD=orangepi-rv2

# With explicit toolchain prefix
make BOARD=orangepi-rv2 CROSS_COMPILE=riscv64-linux-gnu-

# Show build configuration
make BOARD=orangepi-rv2 info

# View disassembly
make BOARD=orangepi-rv2 disasm
```

### Build Output
```
build/orangepi-rv2/
├── tutorial-os-rv2.elf    ← ELF with debug symbols
├── tutorial-os-rv2.bin    ← Flat binary for U-Boot
└── disasm.txt             ← Full disassembly (after `make disasm`)
```

### What Gets Built

| File | Size | Description |
|------|------|-------------|
| entry.S | ~1.3K | `_start`, hart parking, SBI banner |
| common_init.S | ~2K | Stack, BSS, stvec, FP enable |
| vectors.S | ~2.8K | Trap handler, context save/restore |
| cache.S | ~2.1K | Zicbom cache management |
| uart.c | ~5.4K | SBI console + direct PXA UART |
| timer.c | ~2.9K | rdtime @ 24 MHz |
| gpio.c | ~6K | MMP GPIO, heartbeat LED |
| display_simplefb.c | ~14.5K | DTB parser → framebuffer |
| soc_init.c | ~10.6K | Platform init orchestration |
| main_rv2.c | ~20.9K | Hardware Inspector UI |
| **Total** | **~69K** | **Before framebuffer/UI widget code** |

---

## SD Card Setup

The Orange Pi RV2 ships with U-Boot pre-flashed (in SPI NOR or early SD card sectors). We only need to set up the boot partition.

### Option A: Modify Existing Linux SD Card

If you already have a working Linux SD card for the Orange Pi RV2:

```bash
# Mount the boot partition (usually partition 1)
sudo mount /dev/sdX1 /mnt

# Create extlinux directory if it doesn't exist
sudo mkdir -p /mnt/extlinux

# Back up existing boot config
sudo cp /mnt/extlinux/extlinux.conf /mnt/extlinux/extlinux.conf.linux.bak

# Copy our kernel
sudo cp build/orangepi-rv2/tutorial-os-rv2.bin /mnt/

# Copy our boot config
sudo cp board/orangepi-rv2/extlinux.conf /mnt/extlinux/extlinux.conf

# The DTB should already be present from the Linux install at:
#   /mnt/dtb/spacemit/k1-x_orangepi-rv2.dtb
ls /mnt/dtb/spacemit/k1-x_orangepi-rv2.dtb

sudo umount /mnt
```

To switch back to Linux, restore the backup:
```bash
sudo cp /mnt/extlinux/extlinux.conf.linux.bak /mnt/extlinux/extlinux.conf
```

### Option B: Boot Script Method

```bash
# Compile boot.cmd to boot.scr
mkimage -C none -A riscv -T script -d board/orangepi-rv2/boot.cmd boot.scr

# Copy to SD card root
sudo cp boot.scr /mnt/
sudo cp build/orangepi-rv2/tutorial-os-rv2.bin /mnt/

sudo umount /mnt
```

### Option C: Manual U-Boot Console

Connect via UART (115200 baud) and interrupt U-Boot's autoboot:

```
# At the U-Boot prompt:
fatload mmc 0:1 0x11000000 tutorial-os-rv2.bin
go 0x11000000
```

---

## Serial Console

The Orange Pi RV2's UART is on the 3-pin debug header.

```
Pin 1: GND (ground)
Pin 2: TX  (board transmits, connect to adapter RX)
Pin 3: RX  (board receives, connect to adapter TX)
```

Connect at **115200 baud, 8N1**:
```bash
# Linux
screen /dev/ttyUSB0 115200

# macOS
screen /dev/tty.usbserial-* 115200

# Or use minicom/picocom
picocom -b 115200 /dev/ttyUSB0
```

### Expected Serial Output

```
T  ← SBI ecall: ASCII 'T' printed by _start to confirm we're alive
[soc] Tutorial-OS SoC Init: Ky X1 (SpacemiT)
[soc] Board: Orange Pi RV2
[soc] Boot hart: 0
[soc] ---
[soc] Phase 1: Console (SBI mode)
[soc]   UART: SBI ecall (OpenSBI)
[soc] Phase 2: Timer
[soc]   rdtime: <tick count> ticks (24 MHz)
[soc] Phase 3: GPIO
[soc]   Heartbeat LED: GPIO 96 (active low)
[soc] Phase 4: Display
[soc]   SimpleFB: <width>x<height> @ 0x<address>
[soc] Phase 5: Summary
[soc]   SoC: Ky X1 | CPU: X60 @ <freq> MHz | RAM: 4096 MB
[soc]   Display: <width>x<height> HDMI | Boot: U-Boot
[soc] Init complete.
[main] Hardware Inspector rendered. Halting.
```

---

## Debugging

### No Serial Output At All

1. **Check UART connections** — TX/RX might be swapped
2. **Check baud rate** — must be 115200
3. **Interrupt U-Boot** — press a key during the 3-second countdown
4. **Verify binary is on SD card** — `fatls mmc 0:1` in U-Boot console
5. **Check load address** — must be exactly `0x11000000`

### Serial Output But No Display

1. **Check HDMI connection** — must be connected BEFORE power-on (SimpleFB is initialized by U-Boot during boot, not by our code)
2. **Check SimpleFB in DTB** — the DTB must have a `simple-framebuffer` node. U-Boot creates this if `CONFIG_VIDEO` and `CONFIG_FDT_SIMPLEFB` are enabled.
3. **Try a known-good DTB** — extract one from a working Linux SD card
4. **Check U-Boot video** — you should see the U-Boot logo on HDMI before our code runs. If not, U-Boot's display driver isn't working.

### Display Shows But Looks Wrong

1. **Pixel format mismatch** — our parser reads the format from DTB. Check serial output for "SimpleFB: WxH @ addr, format=..."
2. **Stride mismatch** — if the image looks skewed/torn, the stride (bytes per row) is wrong
3. **Cache coherency** — if the image looks partially drawn or has artifacts, the cache flush in `kyx1_display_present()` may not be working. Check that Zicbom instructions are in the binary: `riscv64-linux-gnu-objdump -d build/orangepi-rv2/tutorial-os-rv2.elf | grep cbo`

### Exception/Crash

Check serial for the exception handler output. Common causes:
- **Illegal instruction** — a Zicbom instruction on a core that doesn't support it
- **Load access fault** — accessing a peripheral address that's not mapped
- **Store/AMO page fault** — trying to write to a read-only region

---

## Memory Map

```
0x00000000  ┌──────────────────────────────────────┐
            │  DRAM (4 GB)                          │
            │                                       │
0x10F00000  │  DTB (loaded by U-Boot/boot script)   │  ~1 MB
0x11000000  ├──────────────────────────────────────┤ ← _start
            │  .text      (~12 KB)                  │  Code
            │  .rodata    (string literals)          │  Constants
            │  .data      (globals)                  │  Initialized data
            │  .bss       (zeroed by common_init)    │  Uninitialized data
            │  .stack     (64 KB, grows down)        │  SP starts at top
            │  .heap      (everything above)         │  Future allocator
0x2FF40000  ├──────────────────────────────────────┤
            │  DPU reserved (384 KB)                │  Display controller
            │                                       │
0x3FFFFFFF  └──────────────────────────────────────┘
            │  ... more DRAM to 4 GB ...            │

0xD4017000    UART0 (PXA-compatible)
0xD4019000    GPIO (4 banks, MMP-style)
0xD401E000    Pin controller (pinctrl)
0xD8000000    DPU (display controller — managed by U-Boot)
0xD8018000    HDMI TX (managed by U-Boot)
```

---

## Boot Chain

```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────────────────┐
│  BROM     │───>│  FSBL    │───>│ OpenSBI  │───>│  U-Boot              │
│ (on-chip) │    │ (SPI/SD) │    │ (M-mode) │    │  (S-mode supervisor) │
└──────────┘    └──────────┘    └──────────┘    └──────────┬───────────┘
                                                           │
                                    ┌──────────────────────▼───────────────┐
                                    │  Tutorial-OS                         │
                                    │  entry.S → common_init → kernel_main │
                                    │  (S-mode, all 8 harts available)     │
                                    └──────────────────────────────────────┘
```

### Register State at Entry

When U-Boot jumps to our `_start`:
- `a0` = hart ID (which CPU core, 0-7)
- `a1` = DTB pointer (device tree blob address)
- `sp` = undefined (we set our own)
- Privilege: S-mode (supervisor)
- OpenSBI: running in M-mode, handles ecalls

---

## Project File Map

Where each output file belongs in the Tutorial-OS source tree:

```
tutorial-os/
├── Makefile                              ← Makefile
├── hal/
│   └── hal_cpu.h                         ← hal_cpu.h
├── common/
│   └── mmio.h                            ← mmio.h (updated)
├── boot/riscv64/
│   ├── entry.S                           ← entry.S
│   ├── common_init.S                     ← common_init.S
│   ├── vectors.S                         ← vectors.S
│   ├── cache.S                           ← cache.S
│   └── memory_layout.ld                  ← memory_layout.ld
├── soc/kyx1/
│   ├── kyx1_regs.h                       ← kyx1_regs.h
│   ├── kyx1_cpu.h                        ← kyx1_cpu.h
│   ├── uart.c                            ← uart.c
│   ├── timer.c                           ← timer.c
│   ├── gpio.c                            ← gpio.c
│   ├── display_simplefb.c               ← display_simplefb.c
│   ├── soc_init.c                        ← soc_init.c
│   └── soc.mk                           ← soc.mk
├── kernel/
│   ├── main.c                            (existing Pi version)
│   └── main_rv2.c                        ← main_rv2.c
├── board/orangepi-rv2/
│   ├── board.mk                          ← board.mk
│   ├── extlinux.conf                     ← extlinux.conf
│   └── boot.cmd                          ← boot.cmd
└── docs/
    └── KY_X1_HARDWARE_REFERENCE.md       ← KY_X1_HARDWARE_REFERENCE.md
```

fatload mmc 0:5 0x20000000 tutorial-os-rv2.bin
booti 0x20000000 - $fdtcontroladdr