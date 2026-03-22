# =============================================================================
# board/pico2-lafvin/board.mk — LAFVIN Pico 2 Dev Kit Board Configuration
# =============================================================================
#
# WHAT THIS FILE DOES:
#   1. Selects the rp2350 SoC layer (RP2350, dual Cortex-M33 / Hazard3)
#   2. Overrides the default ARM64 toolchain with ARM bare-metal (Thumb)
#   3. Sets Cortex-M33-specific compiler/assembler flags
#   4. Adds board-specific defines (pin assignments, display geometry)
#
# KEY DIFFERENCES FROM ALL OTHER BOARDS:
#   This is the first microcontroller target in Tutorial-OS. Every other
#   board uses an application processor (Cortex-A, SiFive U74, SpacemiT X60,
#   Intel N100) with an MMU, GBs of RAM, and a memory-mapped framebuffer
#   (HDMI/DPI/GOP). The RP2350 is a Cortex-M33 with:
#     - No MMU (flat address space, 520 KB SRAM)
#     - No GPU or display controller — panel is SPI-attached (ILI9488)
#     - Pico SDK provides the runtime (boot2, crt0, PLL/clock init)
#     - Output is a .uf2 file for drag-and-drop USB flashing
#
#   This makes it architecturally closer to the ESP32-S3 than to any
#   existing Tutorial-OS target. It's the first HAL_ARCH_ARM32 board,
#   the first SPI display, and the first Pico SDK integration.
#
# HARDWARE (confirmed from LAFVIN Pico 2 Dev Kit silkscreen):
#   CPU:     RP2350 dual Cortex-M33 @ 150 MHz (default, up to 300 MHz)
#   RAM:     520 KB SRAM (264 KB striped bank + 256 KB non-striped)
#   Flash:   16 MB QSPI (W25Q128, XIP via boot2)
#   Display: ILI9488 320×480 TFT, SPI mode (RGB666, 3 bytes/pixel)
#            GP2=SCLK  GP3=MOSI  GP4=MISO  GP5=CS  GP6=DC  GP7=RST
#            Backlight hardwired on (no BL GPIO)
#   Touch:   I2C (GP8=SDA, GP9=SCL, GP11=INT) — not used yet
#
# BOOT CHAIN:
#   ROM bootloader → boot2 (256B, configures W25Q128 XIP) →
#   crt0 (PLL, clock tree, .bss/.data init) → main()
#   Flashed via USB drag-and-drop of .uf2 file to RP2350 mass storage.
#
# BUILD:
#   make BOARD=pico2-lafvin
#   Output: build/pico2-lafvin/tutorial-os-pico2.uf2
#
# =============================================================================

# SoC selection — tells the Makefile to include soc/rp2350/soc.mk
SOC := rp2350

# Architecture identifier — first ARM32 target in Tutorial-OS
ARCH := arm32

# ─────────────────────────────────────────────────────────────────────────────
# Toolchain override
# ─────────────────────────────────────────────────────────────────────────────
#
# The Makefile defaults to aarch64-none-elf-. We override for ARM bare-metal.
# arm-none-eabi-gcc targets Cortex-M (Thumb-2). This is a different ABI
# entirely from AArch64 — 32-bit ARM, Thumb instruction set, hardware FPU
# via FPv5-SP on the Cortex-M33.
#

CROSS_COMPILE := arm-none-eabi-

# Verify the toolchain exists
ifeq ($(shell which $(CROSS_COMPILE)gcc 2>/dev/null),)
  $(warning No ARM bare-metal toolchain found!)
  $(warning Install with: apt install gcc-arm-none-eabi)
endif

# ─────────────────────────────────────────────────────────────────────────────
# Architecture flags — REPLACES the ARM64 defaults
# ─────────────────────────────────────────────────────────────────────────────
#
# NOTE: These flags are provided for consistency with the board.mk pattern
# and are used if any sources are compiled directly by the Makefile.
# However, the primary build path is BUILD_MODE=pico, which invokes CMake.
# The Pico SDK sets its own compiler flags internally based on PICO_BOARD
# and PICO_PLATFORM. These flags serve as documentation and as a fallback
# for any files the Makefile compiles outside the CMake substep.
#
#   -mcpu=cortex-m33     : RP2350's ARM cores (ARMv8-M Mainline)
#   -mthumb              : Thumb-2 instruction set (required for Cortex-M)
#   -mfloat-abi=softfp   : Hardware FPU (FPv5-SP-D16) with soft-float ABI
#   -mfpu=fpv5-sp-d16    : Single-precision hardware FP
#
# The RP2350 also has Hazard3 RISC-V cores, but the LAFVIN board boots
# in ARM mode by default and the Pico SDK's ARM path is the standard one.
#

ARCH_CFLAGS := \
	-mcpu=cortex-m33 -mthumb \
	-mfloat-abi=softfp -mfpu=fpv5-sp-d16

ARCH_ASFLAGS := \
	-mcpu=cortex-m33 -mthumb

# ─────────────────────────────────────────────────────────────────────────────
# Board-specific includes and defines
# ─────────────────────────────────────────────────────────────────────────────
#
# Display constants from the ILI9488 panel on the LAFVIN dev kit.
# SPI pin assignments read from the board silkscreen.
#
# DISPLAY_TYPE_SPI signals to the HAL that this is a command-driven
# SPI display, not a memory-mapped framebuffer. This is the architectural
# signal that drives the shadow buffer vs. direct-draw decision in Stage 2.
#

BOARD_INCLUDES := \
	-Iboard/pico2-lafvin

BOARD_DEFINES := \
	-DBOARD_PICO2_LAFVIN=1 \
	-DBOARD_NAME="\"LAFVIN Pico 2 Dev Kit\"" \
	-DDISPLAY_WIDTH=320 \
	-DDISPLAY_HEIGHT=480 \
	-DDISPLAY_TYPE_SPI=1 \
	-DDISP_SPI_BAUD=10000000
