# soc/bcm2712/soc.mk - BCM2712 Configuration
#
# Raspberry Pi 5, CM5

# SoC identification
SOC_NAME := bcm2712

# Boot assembly sources
BOOT_SOURCES := \
    boot/arm64/entry.S \
    boot/arm64/vectors.S \
    boot/arm64/cache.S \
    boot/arm64/common_init.S \
    soc/bcm2712/boot_soc.S

# SoC-specific C sources
SOC_SOURCES := \
    soc/bcm2712/timer.c \
    soc/bcm2712/gpio.c \
    soc/bcm2712/mailbox.c \
    soc/bcm2712/soc_init.c \
    soc/bcm2712/display_dpi.c

# Include paths
SOC_INCLUDES := -Isoc/bcm2712

# Compiler defines
SOC_DEFINES := -DSOC_BCM2712 -DPERIPHERAL_BASE=0x107c000000ULL -DFB_BUFFER_COUNT=1

# Linker script
LINKER_SCRIPT := soc/bcm2712/linker.ld

# Output kernel name
KERNEL_NAME := kernel_2712.img