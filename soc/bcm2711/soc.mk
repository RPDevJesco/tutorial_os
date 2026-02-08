# soc/bcm2711/soc.mk - BCM2711 Configuration
#
# Raspberry Pi 4B, CM4, Pi 400

# SoC identification
SOC_NAME := bcm2711

# Boot assembly sources
BOOT_SOURCES := \
    boot/arm64/entry.S \
    boot/arm64/vectors.S \
    boot/arm64/cache.S \
    boot/arm64/common_init.S \
    soc/bcm2711/boot_soc.S

# SoC-specific C sources
SOC_SOURCES := \
    soc/bcm2711/timer.c \
    soc/bcm2711/gpio.c \
    soc/bcm2711/mailbox.c \
    soc/bcm2711/soc_init.c \
    soc/bcm2711/display_dpi.c

# Include paths
SOC_INCLUDES := -Isoc/bcm2711

# Compiler defines
SOC_DEFINES := -DSOC_BCM2711 -DPERIPHERAL_BASE=0xFE000000

# Linker script
LINKER_SCRIPT := soc/bcm2711/linker.ld

# Output kernel name
KERNEL_NAME := kernel8.img