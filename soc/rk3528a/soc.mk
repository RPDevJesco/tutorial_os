# soc/rk3528a/soc.mk - RK3528A Configuration
#
# Radxa Rock 2A

# SoC identification
SOC_NAME := rk3528a

# Boot assembly sources
BOOT_SOURCES := \
    boot/arm64/entry.S \
    boot/arm64/vectors.S \
    boot/arm64/cache.S \
    boot/arm64/common_init.S \
    soc/rk3528a/boot_soc.S

# SoC-specific C sources (no mailbox on Rockchip!)
SOC_SOURCES := \
    soc/rk3528a/timer.c \
    soc/rk3528a/gpio.c \
    soc/rk3528a/soc_init.c \
    soc/rk3528a/display_vop2.c

# Include paths
SOC_INCLUDES := -Isoc/rk3528a

# Compiler defines
SOC_DEFINES := -DSOC_RK3528A -DUBOOT_BOOT -DPERIPHERAL_BASE=0x02000000

# Linker script
LINKER_SCRIPT := soc/rk3528a/linker.ld

# Output kernel name (Rockchip uses "Image.img")
KERNEL_NAME := Image