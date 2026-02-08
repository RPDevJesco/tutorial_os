# Boot Files - Libre Computer La Potato (AML-S905X-CC)

## Overview

The La Potato uses **U-Boot** as its bootloader, similar to the Radxa Rock 2A
and completely different from the Raspberry Pi's VideoCore firmware.

## Boot Partition Layout

```
/boot/ (or first FAT partition)
├── extlinux/
│   └── extlinux.conf     ← PROVIDED (boot configuration)
└── Image                 ← YOUR OS (build output)
```

## Boot Chain

```
Amlogic Boot ROM (SoC internal)
  → BL2 (Secondary Program Loader, in eMMC/SD sector 1)
    → BL30 (SCP firmware for Cortex-M3 coprocessor)
      → BL31 (ARM Trusted Firmware, runs at EL3)
        → BL33 (U-Boot, runs at EL2/EL1)
          → Reads extlinux/extlinux.conf
            → Loads "Image" to 0x01080000
              → Jumps to kernel entry point
```

This is more complex than Rockchip's boot chain (Boot ROM → SPL → U-Boot)
because Amlogic includes an SCP coprocessor and ARM Trusted Firmware stages.

## U-Boot Firmware

The La Potato ships with U-Boot pre-installed on the eMMC or can boot from SD.

**You do NOT need to install U-Boot yourself** — the board ships with it.
If you need to reinstall, Libre Computer provides firmware images at:
https://hub.libre.computer/t/aml-s905x-cc-le-potato-bootloader-information/1791

## Kernel Format

La Potato U-Boot expects a flat binary `Image` file (same as Rockchip),
not a `kernel8.img` (Pi) or `uImage` (legacy U-Boot format).

The build system produces `Image` automatically when building for this board.

```bash
make BOARD=libre-la-potato
# Output: build/libre-la-potato/Image.img
```

## Memory Addresses (U-Boot Defaults)

| Address    | Purpose           | Notes                          |
|------------|-------------------|--------------------------------|
| 0x01000000 | U-Boot TEXTBASE   | Where U-Boot itself runs       |
| 0x01080000 | Kernel load (PXE) | Where your Image gets loaded   |
| 0x08000000 | Script address    | For boot.scr                   |
| 0x08008000 | FDT address       | Device tree location           |
| 0x13000000 | Ramdisk           | For initrd (not used)          |

## Alternative Boot Methods

Besides extlinux.conf, the La Potato U-Boot supports:

### boot.ini (Environment Script)
```ini
# boot.ini - Place on first FAT partition
setenv bootargs ""
fatload mmc 0 0x01080000 Image
booti 0x01080000 - 0x08008000
```

### boot.scr (Compiled Script)
```bash
# Create boot.cmd, then compile:
mkimage -A arm64 -O linux -T script -C none -d boot.cmd boot.scr
```

### Manual U-Boot (Double-tap ESC at power-on)
```
=> fatload mmc 0 0x01080000 Image
=> booti 0x01080000
```

## UART Debug Console

La Potato debug UART (UART_AO_A):
- **TX**: Pin 8 (GPIOAO_0)
- **RX**: Pin 10 (GPIOAO_1)
- **GND**: Pin 6
- **Baud rate**: 115200 (same as Raspberry Pi!)

```bash
# Connect with screen or minicom
screen /dev/ttyUSB0 115200
```

This is much friendlier than the Rock 2A's unusual 1.5Mbaud rate.

## Differences from Other Platforms

| Feature           | Raspberry Pi       | Radxa Rock 2A    | La Potato         |
|-------------------|--------------------|-------------------|-------------------|
| Bootloader        | VideoCore GPU      | U-Boot            | U-Boot            |
| Config file       | config.txt         | extlinux.conf     | extlinux.conf     |
| Kernel name       | kernel8.img        | Image             | Image             |
| Boot partition    | FAT32 only         | FAT32 or ext4     | FAT32             |
| Firmware          | start.elf          | idbloader + u-boot| Pre-installed     |
| Debug UART baud   | 115200             | 1500000           | 115200            |
| DRAM base         | 0x00000000         | 0x40000000        | 0x00000000        |
| Kernel load addr  | 0x80000            | 0x40280000        | 0x01080000        |
| Display init      | VideoCore mailbox  | VOP2 registers    | VPU/OSD registers |

## Quick Start

1. Get an SD card with La Potato U-Boot installed (ship default or flash from Libre Computer)
2. Mount the first FAT partition
3. Create the `extlinux/` directory
4. Copy `extlinux.conf` from this directory
5. Copy your built `Image` kernel to the partition root
6. Unmount, insert in La Potato, connect HDMI, and power on

## Troubleshooting

- **No serial output**: Verify baud rate is 115200 (not 1500000 like Rock 2A!)
- **U-Boot doesn't load kernel**: Check that extlinux.conf path is correct (`extlinux/extlinux.conf` from partition root)
- **Board doesn't power on**: Red + Blue LEDs should light up immediately. If red LED blinks, power supply is inadequate (need 5V/1.5A minimum)
- **Display not working**: The VPU needs proper initialization. Current HAL assumes U-Boot has set up HDMI. If U-Boot splash screen shows, the display chain is working.
- **Kernel crashes silently**: Connect UART for debug output. Check that your linker script puts code at 0x01080000.

## Notes on Display

Like the Rockchip approach, our display driver assumes U-Boot has already
initialized the HDMI output and VPU. The S905X VPU uses an "OSD layer"
with a "canvas" indirection system for framebuffer memory addressing,
which is unique to Amlogic and more complex than both the BCM mailbox
and the Rockchip VOP2 approaches.

For full VPU initialization from scratch, refer to the Linux kernel's
`meson-drm` driver source code.
