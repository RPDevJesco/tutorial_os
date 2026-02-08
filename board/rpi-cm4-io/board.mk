# board/rpi-cm4-io/board.mk - Board Build Configuration
#
# Raspberry Pi CM4 with IO Board
# Display: HDMI (default) or DPI via GPIO header

BOARD_NAME := rpi-cm4-io
BOARD_DISPLAY_NAME := "Raspberry Pi CM4 + IO Board"

# SoC selection
SOC := bcm2711

# Architecture
ARCH := arm64

# Include SoC build rules
include soc/$(SOC)/soc.mk

# Board-specific defines
BOARD_DEFINES := \
    -DBOARD_RPI_CM4_IO \
    -DDISPLAY_WIDTH=1920 \
    -DDISPLAY_HEIGHT=1080 \
    -DDISPLAY_TYPE_HDMI

# Kernel load address (same as Pi 4)
KERNEL_LOAD_ADDR := 0x80000

# CM4 supports both HDMI outputs
# Can also do DPI via GPIO header
