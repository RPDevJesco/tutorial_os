# soc/s905x/soc.mk - Amlogic S905X (Meson GXL) Configuration
#
# Libre Computer La Potato (AML-S905X-CC), Khadas VIM1, etc.

# SoC identification
SOC_NAME := s905x

# Boot assembly sources
BOOT_SOURCES := \
    boot/arm64/entry.S \
    boot/arm64/vectors.S \
    boot/arm64/cache.S \
    boot/arm64/common_init.S \
    soc/s905x/boot_soc.S

# SoC-specific C sources (no mailbox on Amlogic!)
SOC_SOURCES := \
    soc/s905x/timer.c \
    soc/s905x/gpio.c \
    soc/s905x/soc_init.c \
    soc/s905x/display_vpu.c

# Include paths
SOC_INCLUDES := -Isoc/s905x

# Compiler defines
#
# NOTE: Amlogic GXL has a split peripheral layout, not a single base:
#   CBUS:   0xC1100000  (general EE-domain peripherals)
#   AOBUS:  0xC8100000  (always-on domain: UART_AO, GPIO_AO, etc.)
#   PERIPHS: 0xC8834000 (pinmux, pad control)
#   HIU:    0xC883C000  (HHI - clocks, PLLs)
#
# We set PERIPHERAL_BASE to CBUS for compatibility with the build system,
# but the register header defines all the actual addresses.
SOC_DEFINES := -DSOC_S905X -DPERIPHERAL_BASE=0xC1100000

# Linker script
LINKER_SCRIPT := soc/s905x/linker.ld

# Output kernel name (Amlogic U-Boot uses "Image.img" like Rockchip)
KERNEL_NAME := Image
