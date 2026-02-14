# =============================================================================
# board/orangepi-rv2/board.mk — Orange Pi RV2 Board Configuration
# =============================================================================
#
# WHAT THIS FILE DOES:
#   1. Selects the kyx1 SoC layer (SpacemiT Ky X1)
#   2. Overrides the default ARM64 toolchain with RISC-V
#   3. Sets RISC-V-specific compiler/assembler flags
#   4. Adds board-specific defines
#
# THE KEY DIFFERENCE FROM PI BOARDS:
#   Pi boards use the Makefile defaults (aarch64, cortex-a53).
#   This file overrides EVERYTHING arch-related for RISC-V.
#
# =============================================================================

# SoC selection — tells the Makefile to include soc/kyx1/soc.mk
SOC := kyx1

# ─────────────────────────────────────────────────────────────────────────────
# Toolchain override
# ─────────────────────────────────────────────────────────────────────────────
#
# The Makefile defaults to aarch64-none-elf-. We override for RISC-V.
# Try common toolchain prefix names in order of preference.
#

CROSS_COMPILE := riscv64-linux-gnu-

# Verify the toolchain exists (helpful error message if not)
ifeq ($(shell which $(CROSS_COMPILE)gcc 2>/dev/null),)
  # Try alternate prefix
  CROSS_COMPILE := riscv64-unknown-elf-
  ifeq ($(shell which $(CROSS_COMPILE)gcc 2>/dev/null),)
    CROSS_COMPILE := riscv64-elf-
    ifeq ($(shell which $(CROSS_COMPILE)gcc 2>/dev/null),)
      $(warning No RISC-V toolchain found! Tried riscv64-linux-gnu-, riscv64-unknown-elf-, riscv64-elf-)
      $(warning Install with: apt install gcc-riscv64-linux-gnu)
    endif
  endif
endif

# ─────────────────────────────────────────────────────────────────────────────
# Architecture flags — REPLACES the ARM64 defaults
# ─────────────────────────────────────────────────────────────────────────────
#
# These override ARCH_CFLAGS and ARCH_ASFLAGS which default to ARM64.
#
#   -march=rv64gc       : RV64 + M/A/F/D/C extensions (what the X60 cores support)
#   -mabi=lp64d         : LP64 with hardware double-float
#   -mcmodel=medany     : Position-independent for any address (we load at 0x11000000)
#   -mno-relax          : Disable linker relaxation (cleaner relocations)
#   -fno-pic -fno-pie   : No position-independent code (bare metal, fixed address)
#

ARCH_CFLAGS := \
	-march=rv64gc -mabi=lp64d -mcmodel=medany \
	-mno-relax -fno-pic -fno-pie

ARCH_ASFLAGS := \
	-march=rv64gc -mabi=lp64d -mcmodel=medany \
	-mno-relax -ffreestanding -nostdlib

# ─────────────────────────────────────────────────────────────────────────────
# Special flags for cache.S — needs Zicbom/Zicboz extensions
# ─────────────────────────────────────────────────────────────────────────────
#
# cache.S uses cbo.clean, cbo.inval, cbo.flush, cbo.zero instructions.
# These require the Zicbom (cache block operations) and Zicboz (cache
# block zero) extensions to be enabled in the assembler.
#
# The X60 cores support these, but GCC needs the explicit -march flag.
# The Makefile has a special rule that uses CACHE_ASFLAGS for cache.S.
#

CACHE_ASFLAGS := \
	-march=rv64gc_zicbom_zicboz -mabi=lp64d -mcmodel=medany \
	-mno-relax -ffreestanding -nostdlib

# ─────────────────────────────────────────────────────────────────────────────
# Board-specific includes and defines
# ─────────────────────────────────────────────────────────────────────────────

BOARD_INCLUDES :=

BOARD_DEFINES := \
	-DPLATFORM_KYX1=1 \
	-DBOARD_ORANGEPI_RV2=1 \
	-DNUM_HARTS=8 \
	-DTIMEBASE_FREQ=24000000