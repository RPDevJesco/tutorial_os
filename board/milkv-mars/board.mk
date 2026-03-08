# =============================================================================
# board/milkv-mars/board.mk — Milk-V Mars Board Configuration
# =============================================================================
#
# WHAT THIS FILE DOES:
#   1. Selects the jh7110 SoC layer (StarFive JH-7110)
#   2. Overrides the default ARM64 toolchain with RISC-V
#   3. Sets RV64GC compiler/assembler flags (NOTE: U74 is RV64GC only)
#   4. Adds board-specific defines
#
# KEY DIFFERENCE FROM ORANGEPI-RV2 (kyx1):
#   The Orange Pi RV2 uses the SpacemiT X60 core which supports the full
#   RISC-V V (vector) extension, Zicbom (cache block ops), and Zifencei.
#   The Milk-V Mars uses the SiFive U74 core which is RV64GC ONLY.
#   No vector instructions, no Zicbom cache block ops.
#
#   This means:
#     - ARCH_CFLAGS uses -march=rv64gc (not rv64gcv)
#     - NO CACHE_ASFLAGS (no cache.S, U74 has no Zicbom)
#     - soc.mk does NOT include boot/riscv64/cache.S in BOOT_SOURCES
#
# BOOT CHAIN:
#   QSPI Flash → SPL → OpenSBI → U-Boot → tutorial-os-mars.bin
#   U-Boot loads the kernel via extlinux.conf from the FAT partition
#   and jumps to 0x40200000. Our _start lives exactly there.
#
# =============================================================================

# SoC selection — tells the Makefile to include soc/jh7110/soc.mk
SOC := jh7110

# ─────────────────────────────────────────────────────────────────────────────
# Toolchain override
# ─────────────────────────────────────────────────────────────────────────────
#
# The Makefile defaults to aarch64-none-elf-. We override for RISC-V.
# The same toolchain as orangepi-rv2 works here — it targets riscv64.
#

CROSS_COMPILE := riscv64-linux-gnu-

# Try alternate prefixes if the primary isn't found
ifeq ($(shell which $(CROSS_COMPILE)gcc 2>/dev/null),)
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
# IMPORTANT: The SiFive U74 implements RV64GC (G = IMAFDZicsr_Zifencei, C = compressed).
# It does NOT support:
#   - V extension (no vector instructions)
#   - Zicbom (no cache block operations: cbo.clean, cbo.inval, etc.)
#   - B extension (no bit manipulation)
#
# The orangepi-rv2 board uses -march=rv64gc too, but it also compiles
# cache.S with Zicbom extensions. On the Mars, we skip cache.S entirely
# because the U74 doesn't have those instructions. Attempting to run
# Zicbom instructions on the U74 will cause an illegal instruction trap.
#
#   -march=rv64gc       : RV64 + M/A/F/D/C (exactly what the U74 supports)
#   -mabi=lp64d         : LP64 with hardware double-float (F+D extensions)
#   -mcmodel=medany     : Position-independent for any address (load at 0x40200000)
#   -mno-relax          : Disable linker relaxation (clean relocations)
#   -fno-pic -fno-pie   : No position-independent code (bare metal, fixed address)
#

ARCH_CFLAGS := \
	-march=rv64gc -mabi=lp64d -mcmodel=medany \
	-mno-relax -fno-pic -fno-pie

ARCH_ASFLAGS := \
	-march=rv64gc -mabi=lp64d -mcmodel=medany \
	-mno-relax -ffreestanding -nostdlib

# NOTE: No CACHE_ASFLAGS here. The orangepi-rv2 board.mk defines this
# for cache.S's Zicbom instructions. The U74 doesn't have Zicbom, so
# we don't compile cache.S at all (see soc/jh7110/soc.mk).

# ─────────────────────────────────────────────────────────────────────────────
# Board-specific includes and defines
# ─────────────────────────────────────────────────────────────────────────────

BOARD_INCLUDES := \
	-Iboard/milkv-mars

BOARD_DEFINES := \
	-DBOARD_MILKV_MARS=1 \
	-DBOARD_NAME="\"Milk-V Mars\""