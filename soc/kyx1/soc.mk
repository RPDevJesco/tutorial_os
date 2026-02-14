# =============================================================================
# soc/kyx1/soc.mk — SpacemiT Ky X1 SoC Build Configuration
# =============================================================================
#
# This file tells the Makefile which files to compile for the Ky X1 SoC.
# It's the RISC-V equivalent of soc/bcm2710/soc.mk.
#
# PROVIDES:
#   LINKER_SCRIPT   — memory layout for the linker
#   KERNEL_NAME     — output binary filename
#   BOOT_SOURCES    — assembly files (boot sequence, traps, cache)
#   SOC_SOURCES     — C files (UART, timer, GPIO, display, SoC init)
#   HAL_SOURCES     — HAL platform bridge (hal_platform_kyx1.c)
#   SOC_INCLUDES    — include paths for SoC headers
#   SOC_DEFINES     — SoC-specific preprocessor defines
#
# =============================================================================

# ─────────────────────────────────────────────────────────────────────────────
# Output configuration
# ─────────────────────────────────────────────────────────────────────────────

LINKER_SCRIPT := soc/kyx1/linker.ld
KERNEL_NAME   := tutorial-os-rv2.bin

# ─────────────────────────────────────────────────────────────────────────────
# Boot assembly sources
# ─────────────────────────────────────────────────────────────────────────────
#
# ORDER MATTERS for the linker — entry.S must be first so _start is at
# the load address (0x11000000). The linker script puts .text.boot first.
#
# entry.S       — _start, hart parking, SBI banner character, jump to init
# common_init.S — stack setup, BSS zero, FP enable, stvec, save DTB, call kernel_main
# vectors.S     — trap vector, exception/interrupt dispatch, context save/restore
# cache.S       — Zicbom cache management (cbo.clean/inval/flush/zero)
#                  NOTE: cache.S needs special CACHE_ASFLAGS (set in board.mk)

BOOT_SOURCES := \
	boot/riscv64/entry.S \
	boot/riscv64/common_init.S \
	boot/riscv64/vectors.S \
	boot/riscv64/cache.S

# ─────────────────────────────────────────────────────────────────────────────
# SoC driver sources
# ─────────────────────────────────────────────────────────────────────────────
#
# These implement the Ky X1 hardware drivers:
#
# uart.c            — PXA-compatible UART + SBI ecall fallback console
# timer.c           — rdtime @ 24 MHz (micros, delay_us, delay_ms, ticks)
# gpio.c            — MMP GPIO controller (4 banks), heartbeat LED
# display_simplefb.c — SimpleFB framebuffer from DTB (set up by U-Boot)
# soc_init.c        — SoC init orchestration (UART → Timer → GPIO → Display)

SOC_SOURCES := \
	soc/kyx1/drivers/i2c.c \
	soc/kyx1/drivers/sbi.c \
	soc/kyx1/drivers/pmic_spm8821.c \
	soc/kyx1/uart.c \
	soc/kyx1/timer.c \
	soc/kyx1/gpio.c \
	soc/kyx1/display_simplefb.c \
	soc/kyx1/soc_init.c

# ─────────────────────────────────────────────────────────────────────────────
# HAL platform bridge
# ─────────────────────────────────────────────────────────────────────────────
#
# This file implements the unified hal_platform_* API that kernel/main.c calls.
# It bridges from the portable HAL interface to the kyx1-specific functions above.
#
# This is what makes main.c work identically on Pi and RV2:
#   main.c:  hal_platform_get_info(&info)
#   BCM:     → queries VideoCore mailbox
#   Ky X1:   → returns known constants (THIS FILE)

HAL_SOURCES := \
	soc/kyx1/hal_platform_kyx1.c

# ─────────────────────────────────────────────────────────────────────────────
# Include paths and defines
# ─────────────────────────────────────────────────────────────────────────────

SOC_INCLUDES := \
	-Isoc/kyx1

SOC_DEFINES := \
	-DSOC_KYX1=1 \
	-DKYX1_PERI_BASE=0xD4000000