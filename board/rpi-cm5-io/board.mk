# board/rpi-cm5-io/board.mk - Raspberry Pi CM5 on IO Board
#
# CM5 with bcm2712 SoC mounted on official IO board

# SoC selection
SOC := bcm2712

# Board-specific defines
BOARD_DEFINES := -DBOARD_RPI_CM5_IO

# Board-specific includes (if any)
BOARD_INCLUDES :=