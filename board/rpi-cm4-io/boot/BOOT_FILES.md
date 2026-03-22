# Boot Files for Raspberry Pi CM4 + IO Board

## Required Files on Boot Partition

Your SD card (or eMMC) needs a FAT32 partition with these files:

```
/boot/
├── start4.elf        # GPU firmware for Pi 4 (from Raspberry Pi firmware)
├── fixup4.dat        # Memory configuration (from Raspberry Pi firmware)
├── config.txt        # Configuration (PROVIDED - customize as needed)
└── kernel8.img       # YOUR OS (output from build)
```

**Note:** Pi 4/CM4 does NOT need `bootcode.bin` - it has a boot ROM in SPI flash.

## Getting Firmware Files

Download from: https://github.com/raspberrypi/firmware/tree/master/boot

You need:
- `start4.elf` (Pi 4 specific)
- `fixup4.dat` (Pi 4 specific)

Or copy from an existing Raspberry Pi OS SD card.

## SD Card vs eMMC

**SD Card (CM4 Lite or with SD slot):**
- Format SD card with FAT32 partition
- Copy files as described above

**eMMC (CM4 with onboard storage):**
1. Connect CM4 IO Board to PC via USB-C (boot button held)
2. Use `rpiboot` tool to mount eMMC as USB drive
3. Format and copy files as above

## Boot Process

1. CM4 boot ROM loads `start4.elf` from storage
2. `start4.elf` reads `config.txt` for configuration
3. `start4.elf` loads `kernel8.img` at 0x80000
4. GPU releases ARM cores to start your kernel

## HDMI Notes

The CM4 IO Board has two HDMI ports:
- HDMI0 is primary
- HDMI1 is secondary

Default config uses 1080p60 on HDMI0.

## Differences from Pi Zero 2W

| Feature | Pi Zero 2W | CM4 |
|---------|------------|-----|
| Firmware | start.elf | start4.elf |
| bootcode.bin | Required | Not needed |
| GPIO pull | GPPUD sequence | Direct register |
| USB | DWC2 OTG | xHCI (USB 3.0) |

## Troubleshooting

- **No HDMI**: Try `hdmi_force_hotplug=1` and `hdmi_safe=1`
- **Wrong resolution**: Adjust `hdmi_group` and `hdmi_mode`
- **No boot from eMMC**: Use rpiboot to reflash
- **USB not working**: Check `otg_mode=1` is set
