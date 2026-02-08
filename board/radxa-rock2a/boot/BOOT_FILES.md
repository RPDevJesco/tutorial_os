# Boot Files for Radxa Rock 2A (RK3528A)

## ⚠️ COMPLETELY DIFFERENT FROM RASPBERRY PI!

Rockchip boards use **U-Boot** as the bootloader, not the Pi's VideoCore GPU.
The boot process and required files are entirely different.

## Boot Partition Layout

```
/boot/
├── extlinux/
│   └── extlinux.conf    # Boot configuration (PROVIDED)
├── Image                # YOUR OS kernel (flat binary or compressed)
└── (optional) dtbs/     # Device tree blobs
    └── rockchip/
        └── rk3528a-rock-2a.dtb
```

## SD Card Layout

Unlike Pi where everything is on one FAT32 partition, Rockchip typically uses:

```
SD Card Layout:
┌──────────────────────────────────────────────────────────┐
│ Sector 0    │ MBR (partition table)                      │
├─────────────┼────────────────────────────────────────────┤
│ Sector 64   │ idbloader.img (SPL/TPL - first stage)     │
├─────────────┼────────────────────────────────────────────┤
│ Sector 16384│ u-boot.itb (U-Boot proper)                │
├─────────────┼────────────────────────────────────────────┤
│ Partition 1 │ Boot partition (FAT32 or ext4)            │
│             │ Contains: extlinux.conf, Image, dtbs      │
├─────────────┼────────────────────────────────────────────┤
│ Partition 2 │ Root filesystem (optional for bare metal) │
└─────────────┴────────────────────────────────────────────┘
```

## Getting U-Boot

### Option 1: Use Radxa's Pre-built U-Boot
Download from: https://github.com/radxa-build/rock-2a/releases

### Option 2: Build U-Boot Yourself
```bash
git clone https://github.com/radxa/u-boot.git -b stable-5.10-rock-2a
cd u-boot
make rock-2a-rk3528a_defconfig
make
```

This produces:
- `idbloader.img` (SPL/TPL)
- `u-boot.itb` (U-Boot proper)

## Writing U-Boot to SD Card

```bash
# Write idbloader at sector 64 (0x40)
sudo dd if=idbloader.img of=/dev/sdX seek=64 conv=fsync

# Write u-boot at sector 16384 (0x4000)  
sudo dd if=u-boot.itb of=/dev/sdX seek=16384 conv=fsync
```

## Creating Boot Partition

```bash
# Create partition starting at sector 32768 (16MB offset)
sudo fdisk /dev/sdX
# n -> p -> 1 -> 32768 -> +256M -> w

# Format as FAT32
sudo mkfs.vfat -F 32 /dev/sdX1

# Mount and copy files
sudo mount /dev/sdX1 /mnt
sudo mkdir -p /mnt/extlinux
sudo cp extlinux.conf /mnt/extlinux/
sudo cp Image.img /mnt/
sudo umount /mnt
```

## Kernel Image Format

Rockchip U-Boot expects the kernel as:
- `Image` - Uncompressed ARM64 kernel image
- `Image.gz` - Gzip compressed (with proper header)

**NOT `kernel8.img`** like Raspberry Pi!

To convert your kernel:
```bash
# Build produces kernel.elf
aarch64-none-elf-objcopy -O binary kernel.elf Image.img
```

Or update the Makefile to output `Image` instead of `kernel8.img`.

## Boot Process

1. RK3528A boot ROM loads `idbloader.img` from SD/eMMC
2. SPL (Secondary Program Loader) initializes DRAM
3. SPL loads U-Boot proper (`u-boot.itb`)
4. U-Boot reads `extlinux/extlinux.conf`
5. U-Boot loads `Image` to memory (typically 0x40280000)
6. U-Boot jumps to kernel entry point

## UART Debug Console

Rock 2A debug UART:
- **UART2**: GPIO header pins (TX: pin 8, RX: pin 10)
- **Baud rate**: 1500000 (1.5Mbaud!) - different from Pi's 115200

```bash
# Connect with screen or minicom
screen /dev/ttyUSB0 1500000
```

## Differences from Raspberry Pi

| Feature | Raspberry Pi | Radxa Rock 2A |
|---------|--------------|---------------|
| Bootloader | VideoCore GPU | U-Boot |
| Config file | config.txt | extlinux.conf |
| Kernel name | kernel8.img | Image |
| Boot partition | FAT32 only | FAT32 or ext4 |
| Firmware | start.elf | idbloader.img + u-boot.itb |
| Debug UART baud | 115200 | 1500000 |

## Quick Start (Assuming U-Boot is already on SD card)

If you have an SD card from Radxa with U-Boot already installed:

1. Mount the boot partition
2. Create `/extlinux/extlinux.conf` as provided
3. Copy your kernel as `Image`
4. Unmount and boot

## Troubleshooting

- **No serial output**: Check baud rate (1500000, not 115200!)
- **U-Boot doesn't load kernel**: Check extlinux.conf path and kernel name
- **Kernel doesn't start**: Verify load address matches your linker script
- **Display not working**: VOP2 needs proper initialization (complex!)

## Notes on Display

Unlike Raspberry Pi where the VideoCore firmware initializes display,
on Rockchip you need to:

1. Initialize VOP2 (Video Output Processor)
2. Configure HDMI TX
3. Set up display timing

The provided HAL has simplified VOP2 code that assumes U-Boot has
done initial display setup. For full initialization, you'll need to
add proper VOP2/HDMI driver code.
