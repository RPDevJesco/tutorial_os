# board/rpi-cm5-io/board.mk - Raspberry Pi CM5 on IO Board
#
# CM5 with bcm2712 SoC mounted on official IO board

BOARD_NAME := rpi-cm5-io
BOARD_DISPLAY_NAME := "Raspberry Pi CM5 + IO Board"

# SoC selection — soc/$(SOC)/soc.mk is included by the top-level Makefile
SOC := bcm2712

# Architecture
ARCH := arm64

# Cortex-A76 (ARMv8.2-A) — overrides the Makefile default of cortex-a53.
# -mgeneral-regs-only: no FP/SIMD in kernel code (consistent with other boards).
ARCH_CFLAGS  := -mcpu=cortex-a76 -mgeneral-regs-only
ARCH_ASFLAGS :=

# Board-specific defines
BOARD_DEFINES := \
    -DBOARD_RPI_CM5_IO \
    -DDISPLAY_WIDTH=1920 \
    -DDISPLAY_HEIGHT=1080 \
    -DDISPLAY_TYPE_HDMI

# CM5 supports both HDMI outputs.
# Can also do DPI via GPIO header.