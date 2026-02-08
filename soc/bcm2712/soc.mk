# soc/bcm2712/soc.mk - BCM2712 Configuration

SOC_NAME := bcm2712

BOOT_SOURCES := \
    boot/arm64/entry.S \
    boot/arm64/vectors.S \
    boot/arm64/cache.S \
    boot/arm64/common_init.S \
    soc/bcm2712/boot_soc.S

SOC_SOURCES := \
    soc/bcm2712/timer.c \
    soc/bcm2712/gpio.c \
    soc/bcm2712/mailbox.c \
    soc/bcm2712/soc_init.c \
    soc/bcm2712/display_dpi.c

SOC_INCLUDES := -Isoc/bcm2712

SOC_DEFINES := -DSOC_BCM2712 -DPERIPHERAL_BASE=0x107c000000

LINKER_SCRIPT := soc/bcm2712/linker.ld

KERNEL_NAME := kernel8.img