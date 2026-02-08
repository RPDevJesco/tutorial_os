# board/libre-la-potato/board.mk - Board Build Configuration
#
# Libre Computer La Potato (AML-S905X-CC)
# Display: HDMI (native HDMI 2.0b output)
#
# The La Potato is a Raspberry Pi form-factor SBC with an Amlogic S905X
# quad-core Cortex-A53 SoC. It uses U-Boot (like the Rock 2A) and
# boots via extlinux.conf.
#
# Available in 1GB and 2GB variants.
# 40-pin GPIO header is Pi-compatible layout (but NOT electrically compatible!)

BOARD_NAME := libre-la-potato
BOARD_DISPLAY_NAME := "Libre Computer La Potato"

# SoC selection
SOC := s905x

# Board-specific defines
BOARD_DEFINES := \
    -DBOARD_LIBRE_LA_POTATO \
    -DDISPLAY_WIDTH=1280 \
    -DDISPLAY_HEIGHT=720 \
    -DDISPLAY_TYPE_HDMI

BOARD_INCLUDES :=
