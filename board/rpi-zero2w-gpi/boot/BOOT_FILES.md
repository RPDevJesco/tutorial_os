# Boot Files for Raspberry Pi Zero 2W + GPi Case 2W

## Required Files on Boot Partition

Your SD card needs a FAT32 partition with these files:

```
/boot/
├── bootcode.bin      # First stage bootloader (from Raspberry Pi firmware)
├── start.elf         # GPU firmware (from Raspberry Pi firmware)  
├── fixup.dat         # Memory configuration (from Raspberry Pi firmware)
├── config.txt        # Configuration (PROVIDED - customize as needed)
└── kernel8.img       # YOUR OS (output from build)
```

## Getting Firmware Files

Download from: https://github.com/raspberrypi/firmware/tree/master/boot

You need:
- `bootcode.bin`
- `start.elf` (or `start_x.elf` for camera support)
- `fixup.dat` (or `fixup_x.dat` to match start_x.elf)

Or copy from an existing Raspbian SD card.

## SD Card Setup

1. Format SD card with single FAT32 partition
2. Copy firmware files (bootcode.bin, start.elf, fixup.dat)
3. Copy config.txt from this directory
4. Copy your built kernel8.img

## Boot Process

1. GPU loads `bootcode.bin` from SD card
2. `bootcode.bin` loads `start.elf`
3. `start.elf` reads `config.txt` for configuration
4. `start.elf` loads `kernel8.img` at 0x80000
5. GPU releases ARM cores to start your kernel

## DPI Display Notes

The GPi Case 2W uses DPI (parallel RGB) output on GPIO 0-27.
The config.txt configures:
- 640x480 resolution at 60Hz
- 18-bit RGB666 color depth
- Custom timing for the GPi Case display

GPIO 18-19 are reserved for PWM audio output.

## Testing

If display doesn't work:
1. Check all firmware files are present
2. Verify config.txt is correct
3. Try with `gpu_mem=128` for more GPU memory
4. Check serial output for boot messages (enable_uart=1)

## Troubleshooting

- **No display**: Check DPI configuration, try different dpi_output_format
- **Wrong colors**: RGB order might be wrong, try dpi_output_format=0x6f006
- **Flickering**: Timing values might need adjustment
- **No boot**: Check bootcode.bin and start.elf are correct versions
