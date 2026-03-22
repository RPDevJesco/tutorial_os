# Deploying Tutorial-OS on the Milk-V Mars

## Quick Start

```bash
# Build
make BOARD=milkv-mars

# Copy to SD card boot partition (adjust /dev/sdX1 to your card)
sudo mount /dev/sdX1 /mnt/boot
sudo cp build/milkv-mars/tutorial-os-mars.bin /mnt/boot/
sudo mkdir -p /mnt/boot/extlinux
sudo cp board/milkv-mars/extlinux.conf /mnt/boot/extlinux/
sudo umount /mnt/boot
```

---

## Prerequisites

### Hardware
- Milk-V Mars SBC (tested on 8 GB variant)
- microSD card (8 GB+)
- USB-C power supply (5V/3A minimum)
- HDMI monitor + cable (HDMI 2.0 output, DC8200 + HDMI TX)
- USB-to-UART adapter for serial console (optional but strongly recommended)
    - Connect to the 40-pin header: GPIO5 (TX, pin 8) and GPIO6 (RX, pin 10)
    - Also needs a GND connection (pin 6)

### Software
```bash
# Ubuntu/Debian
sudo apt install gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu

# macOS (Homebrew)
brew install riscv64-elf-gcc
```

---

## Building

```bash
# Default build
make BOARD=milkv-mars

# With explicit toolchain
make BOARD=milkv-mars CROSS_COMPILE=riscv64-linux-gnu-

# Show build configuration
make BOARD=milkv-mars info

# Disassemble
make BOARD=milkv-mars disasm
```

### Build Output

```
build/milkv-mars/
├── tutorial-os-mars.elf    ← ELF with debug symbols
└── tutorial-os-mars.bin    ← Flat binary for U-Boot
```

---

## SD Card Setup

The Milk-V Mars ships with U-Boot pre-installed in QSPI flash (or on the SD
card in the vendor image). We only need to modify the boot partition.

### Option A: Using the Vendor SD Image (Recommended for First Boot)

1. Flash the Milk-V Mars Debian/Buildroot image to your SD card using the
   [official Milk-V Mars images](https://milkv.io/docs/mars/getting-started/images).

2. Mount the FAT boot partition:
   ```bash
   sudo mount /dev/sdX1 /mnt/boot   # adjust sdX1 to your card
   ```

3. Copy the Tutorial-OS binary:
   ```bash
   sudo cp build/milkv-mars/tutorial-os-mars.bin /mnt/boot/
   ```

4. Update the extlinux configuration:
   ```bash
   sudo mkdir -p /mnt/boot/extlinux
   sudo cp board/milkv-mars/extlinux.conf /mnt/boot/extlinux/extlinux.conf
   sudo umount /mnt/boot
   ```

5. Boot the board. U-Boot will load Tutorial-OS instead of Linux.

### Option B: Minimal SD Card from Scratch

The Mars U-Boot is in QSPI flash, so the SD card only needs a boot partition:

```bash
# Create a single FAT32 partition
sudo parted /dev/sdX mklabel msdos
sudo parted /dev/sdX mkpart primary fat32 1MiB 256MiB
sudo mkfs.vfat /dev/sdX1

# Create directory structure
sudo mount /dev/sdX1 /mnt/boot
sudo mkdir -p /mnt/boot/extlinux
sudo cp build/milkv-mars/tutorial-os-mars.bin /mnt/boot/
sudo cp board/milkv-mars/extlinux.conf /mnt/boot/extlinux/
sudo umount /mnt/boot
```

---

## Boot Sequence

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌─────────────────────────┐
│   QSPI NOR   │───>│  SPL + OpenSBI│───>│   U-Boot     │───>│     Tutorial-OS         │
│  (on-board)  │    │  (QSPI flash) │    │  (SD card or │    │  _start → common_init   │
│  BootROM     │    │  M-mode only  │    │  QSPI flash) │    │  → kernel_main          │
└──────────────┘    └──────────────┘    └──────────────┘    └─────────────────────────┘
```

### Register State When Our Code Starts

When U-Boot jumps to `_start` at 0x40200000:
- `a0` = hart ID (0 = primary U74 core)
- `a1` = DTB pointer (U-Boot's in-memory device tree blob)
- `sp` = undefined (we initialize our own 64KB stack)
- Privilege mode: S-mode (supervisor)
- OpenSBI: running in M-mode background, handles M-mode CSR ecalls
- Display: DC8200 + HDMI TX already initialized by U-Boot

### Peripheral State at Handoff

U-Boot has already initialized when our code runs:
- ✅ DDR4/LPDDR4 (8 GB, all accessible from 0x40000000)
- ✅ UART0 pinmux (GPIO5=TX, GPIO6=RX, 115200 8N1)
- ✅ DC8200 display controller + HDMI TX (1080p or native EDID resolution)
- ✅ AXP15060 PMIC (power rails stable: VDD_CPU, VDD_DDR, 3.3V)
- ✅ SD card controller (but we don't need it after boot)
- DTB injected with simple-framebuffer node (if U-Boot has CONFIG_FDT_SIMPLEFB=y)

---

## Serial Console

Connect a USB-to-UART adapter to the 40-pin header:

| Pin | Signal  | Direction |
|-----|---------|-----------|
| 6   | GND     | —         |
| 8   | GPIO5   | TX (Mars → adapter) |
| 10  | GPIO6   | RX (adapter → Mars) |

Settings: **115200 baud, 8N1, no flow control**

```bash
# Linux
screen /dev/ttyUSB0 115200
# or
minicom -D /dev/ttyUSB0 -b 115200

# macOS
screen /dev/cu.usbserial-* 115200
```

Expected first output (before display initializes):
```
T                               ← 'T' from entry.S SBI putchar
[jh7110] Boot hart: 0  DTB @ 0xXXXXXXXX
[jh7110] UART0 direct hardware init OK (115200 8N1)
[jh7110] Measuring CPU frequency...
[jh7110] CPU: ~1500 MHz
[gpio] Note: No standard user LED on Milk-V Mars SoM
[pmic] Initializing AXP15060 on I2C6 (0x36)...
[pmic] Chip ID: 0x50
[pmic] AXP15060 init OK
[simplefb] Parsing DTB for simple-framebuffer...
[simplefb] Found framebuffer:
  Address: 0xXXXXXXXX
  1920x1080 @ 32bpp
[jh7110] Display OK — 1920x1080
[jh7110] SoC init complete
```

---

## Comparison with Orange Pi RV2

The Mars and Orange Pi RV2 are Tutorial-OS's two RISC-V platforms:

| Feature           | Orange Pi RV2 (KyX1)     | Milk-V Mars (JH7110)          |
|-------------------|--------------------------|-------------------------------|
| CPU cores         | 8 × SpacemiT X60         | 4 × SiFive U74                |
| CPU ISA           | RV64GC**V** (has vector) | RV64GC only (no vector)       |
| Max clock         | 1.6 GHz                  | 1.5 GHz                       |
| UART IP           | Marvell PXA (non-standard)| DesignWare 8250 (standard)   |
| PMIC              | SPM8821 (undocumented)   | AXP15060 (documented)         |
| Display           | SimpleFB from DTB        | SimpleFB from DTB             |
| Boot address      | 0x11000000               | 0x40200000                    |
| DRAM start        | 0x00000000               | 0x40000000                    |
| Zicbom (cache ops)| ✅ Yes (cache.S compiled) | ❌ No (cache.S not compiled)  |
| HAL portability   | ~90% shared              | ~90% shared                   |

Both boards illustrate the same core lesson: RISC-V bare-metal portability
is achievable with a clean HAL, even when the underlying silicon differs.

---

## Project File Map

```
tutorial-os/
├── board/milkv-mars/
│   ├── board.mk            ← Build configuration (toolchain, -march=rv64gc)
│   ├── extlinux.conf       ← U-Boot boot script
│   └── DEPLOY.md           ← This file
├── soc/jh7110/
│   ├── soc.mk              ← SoC sources list (NO cache.S vs kyx1)
│   ├── linker.ld           ← Linker script (loads at 0x40200000)
│   ├── jh7110_regs.h       ← Peripheral base addresses
│   ├── jh7110_cpu.h        ← CPU operations (barriers, wfi, rdtime)
│   ├── uart.c              ← DW 8250 UART driver
│   ├── timer.c             ← rdtime @ 24 MHz timer
│   ├── gpio.c              ← GPIO + sys_iomux driver
│   ├── display_simplefb.c  ← DTB parser → framebuffer
│   ├── soc_init.c          ← Init orchestration
│   ├── hal_platform_jh7110.c ← HAL bridge
│   └── drivers/
│       ├── sbi.c / sbi.h         ← SBI ecalls (CPU CSR reads)
│       ├── i2c.c / i2c.h         ← DW I2C driver
│       └── pmic_axp15060.c / .h  ← AXP15060 PMIC driver
└── boot/riscv64/           ← Shared RISC-V boot assembly (entry, init, vectors)
    ├── entry.S             ← Shared (parks non-boot harts, adjusts for U74's 4 cores)
    ├── common_init.S       ← Shared (stack, BSS, fp enable, save DTB)
    └── vectors.S           ← Shared (trap handler)
    # NOTE: cache.S NOT compiled for JH7110 (U74 has no Zicbom)
```