# soc/bcm2710/soc.mk - BCM2710 Configuration
#
# Raspberry Pi Zero 2W, Pi 3B, Pi 3B+, CM3

# SoC identification
SOC_NAME := bcm2710

# Boot assembly sources (generic ARM64 + BCM2710 specific)
BOOT_SOURCES := \
    boot/arm64/entry.S \
    boot/arm64/vectors.S \
    boot/arm64/cache.S \
    boot/arm64/common_init.S \
    soc/bcm2710/boot_soc.S

# SoC-specific C sources
SOC_SOURCES := \
    soc/bcm2710/timer.c \
    soc/bcm2710/gpio.c \
    soc/bcm2710/mailbox.c \
    soc/bcm2710/soc_init.c \
    soc/bcm2710/display_dpi.c

# Include paths
SOC_INCLUDES := -Isoc/bcm2710

# Compiler defines
SOC_DEFINES := -DSOC_BCM2710 -DPERIPHERAL_BASE=0x3F000000

# Linker script
LINKER_SCRIPT := soc/bcm2710/linker.ld

# Output kernel name (Pi uses kernel8.img for 64-bit)
KERNEL_NAME := kernel8.img