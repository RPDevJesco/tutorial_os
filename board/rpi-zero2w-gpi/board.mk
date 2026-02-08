# board/rpi-zero2w-gpi/board.mk
#
# Raspberry Pi Zero 2W with RetroFlag GPi Case 2W

# Select SoC
SOC := bcm2710

# Board-specific defines
BOARD_DEFINES := \
    -DBOARD_RPI_ZERO2W_GPI \
    -DDISPLAY_WIDTH=640 \
    -DDISPLAY_HEIGHT=480

BOARD_INCLUDES :=