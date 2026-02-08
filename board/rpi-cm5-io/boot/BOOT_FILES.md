# Raspberry Pi CM5 IO Board - Boot Setup

## Overview

This directory contains boot configuration for Tutorial-OS on the
Raspberry Pi Compute Module 5 mounted on the official IO board.

## SD Card Setup

1. Format an SD card with a FAT32 partition (at least 64MB)

2. Copy these files to the SD card root:
    - `config.txt` (from this directory)
    - `kernel8.img` (from `build/rpi-cm5-io/`)

3. You also need the Pi firmware files from the official Raspberry Pi
   firmware repository:
    - `start4.elf`
    - `fixup4.dat`
    - `bcm2712-rpi-cm5-cm5io.dtb` (or appropriate DTB)

   Get these from: https://github.com/raspberrypi/firmware/tree/master/boot

## Required Files on SD Card
```
/
├── config.txt          # Boot configuration
├── kernel8.img         # Tutorial-OS kernel
├── start4.elf          # GPU firmware (from Pi firmware repo)
├── fixup4.dat          # GPU firmware fixup (from Pi firmware repo)
└── bcm2712-rpi-cm5-cm5io.dtb  # Device tree (from Pi firmware repo)
```

## UART Debug

Connect a USB-to-UART adapter to the 40-pin header:
- Pin 6:  GND
- Pin 8:  TX (UART)
- Pin 10: RX (UART)

Serial settings: 115200 baud, 8N1

## BCM2712 Notes

The CM5 uses the BCM2712 SoC (same as Pi 5) which has significant
differences from BCM2711 (Pi 4):

- **Peripheral base**: 0x107c000000 (was 0xFE000000)
- **RP1 south bridge**: GPIO and many peripherals are on RP1, not directly on SoC
- **Cores**: 4x Cortex-A76 (was A72)
- **Memory**: Up to 16GB RAM

## Building
```bash
make BOARD=rpi-cm5-io
```

Output: `build/rpi-cm5-io/kernel8.img`