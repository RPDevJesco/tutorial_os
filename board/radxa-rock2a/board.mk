# board/radxa-rock2a/board.mk
#
# Radxa Rock 2A

# Select SoC
SOC := rk3528a

# Board-specific defines
BOARD_DEFINES := \
    -DBOARD_RADXA_ROCK2A \
    -DDISPLAY_WIDTH=1280 \
    -DDISPLAY_HEIGHT=720

BOARD_INCLUDES :=