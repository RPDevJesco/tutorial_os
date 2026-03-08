# =============================================================================
# soc/jh7110/soc.mk — StarFive JH-7110 SoC Build Configuration
# =============================================================================
#
# This file tells the Makefile which files to compile for the JH7110 SoC.
# It mirrors the structure of soc/kyx1/soc.mk exactly — same variables,
# same pattern, different board.
#
# COMPARISON WITH KYX1:
#   kyx1 BOOT_SOURCES includes cache.S (Zicbom support for SpacemiT X60).
#   jh7110 BOOT_SOURCES omits cache.S entirely (SiFive U74 = RV64GC only,
#   no Zicbom). Everything else — entry.S, common_init.S, vectors.S —
#   is shared because they only use standard RV64GC instructions.
#
#   This is a great illustration of how the shared boot/ infrastructure
#   works: ~90% of boot assembly is platform-agnostic, and the 10% that
#   isn't (cache.S, Zicbom instructions) simply doesn't exist here.
#
# PROVIDES:
#   LINKER_SCRIPT   — memory layout for the linker
#   KERNEL_NAME     — output binary filename
#   BOOT_SOURCES    — assembly files (entry, init, traps — NO cache.S)
#   SOC_SOURCES     — C files (UART, timer, GPIO, display, SoC init)
#   HAL_SOURCES     — HAL platform bridge (hal_platform_jh7110.c)
#   SOC_INCLUDES    — include paths for SoC headers
#   SOC_DEFINES     — SoC-specific preprocessor defines
#
# =============================================================================

# ─────────────────────────────────────────────────────────────────────────────
# Output configuration
# ─────────────────────────────────────────────────────────────────────────────

LINKER_SCRIPT := soc/jh7110/linker.ld
KERNEL_NAME   := tutorial-os-mars.bin

# ─────────────────────────────────────────────────────────────────────────────
# Boot assembly sources
# ─────────────────────────────────────────────────────────────────────────────
#
# ORDER MATTERS — entry.S must be first so _start is placed at 0x40200000.
#
# entry.S       — _start, hart parking (U74 has 4 cores, park all non-0),
#                 SBI banner character, jump to common_init
# common_init.S — stack setup, BSS zeroing, FP enable, stvec, save DTB,
#                 call kernel_main
# vectors.S     — trap vector, exception/interrupt dispatch, context save/restore
#
# DELIBERATELY MISSING: boot/riscv64/cache.S
#   The Ky X1 (SpacemiT X60) supports Zicbom cache block operations
#   (cbo.clean, cbo.inval, cbo.flush, cbo.zero). cache.S uses those
#   instructions and is compiled with special CACHE_ASFLAGS.
#
#   The SiFive U74 does NOT implement Zicbom. Executing those instructions
#   would cause an illegal instruction trap. So we simply don't include it.
#   Cache coherency on the U74 is maintained by the hardware itself.
#
# SHARED WITH KYX1: entry.S, common_init.S, vectors.S
#   These files contain only base RV64GC instructions (branches, loads,
#   stores, CSR reads/writes). They compile identically for both SoCs.
#   This is the HAL philosophy at the assembly level.

BOOT_SOURCES := \
	boot/riscv64/entry.S \
	boot/riscv64/common_init.S \
	soc/jh7110/mmu.S \
	boot/riscv64/vectors.S

# ─────────────────────────────────────────────────────────────────────────────
# SoC driver sources
# ─────────────────────────────────────────────────────────────────────────────
#
# uart.c            — DW 8250 / 16550-compatible UART (NOT PXA like kyx1)
#                     JH7110 uses standard Synopsys DesignWare 8250 UART IP.
#                     UART0 (GPIO5/GPIO6) = primary debug console at 115200.
# timer.c           — rdtime @ 24 MHz reference (identical concept to kyx1)
# gpio.c            — JH7110 GPIO controller + sys_iomux pin function select
# display_simplefb.c — SimpleFB from DTB, identical strategy to kyx1.
#                      U-Boot initializes the DC8200 + HDMI2.0 and injects
#                      a simple-framebuffer node. We read it from a1.
# soc_init.c        — Orchestration: UART → SBI → GPIO → PMIC → Display

SOC_SOURCES := \
	soc/jh7110/drivers/sbi.c \
	soc/jh7110/drivers/i2c.c \
	soc/jh7110/drivers/pmic_axp15060.c \
	soc/jh7110/uart.c \
	soc/jh7110/timer.c \
	soc/jh7110/gpio.c \
	soc/jh7110/cache.c \
	soc/jh7110/display_simplefb.c \
	soc/jh7110/soc_init.c

# ─────────────────────────────────────────────────────────────────────────────
# HAL platform bridge
# ─────────────────────────────────────────────────────────────────────────────
#
# This implements the unified hal_platform_* API so kernel/main.c doesn't
# need to know it's running on a JH7110. The same main.c that runs on
# BCM2710 and Ky X1 runs unchanged here.

HAL_SOURCES := \
	soc/jh7110/hal_platform_jh7110.c

# ─────────────────────────────────────────────────────────────────────────────
# Include paths and defines
# ─────────────────────────────────────────────────────────────────────────────

SOC_INCLUDES := \
	-Isoc/jh7110

SOC_DEFINES := \
    -DSOC_JH7110=1 \
    -DJH7110_PERI_BASE=0x10000000 \
    -DRAM_BASE=0x40000000UL \
    -DRAM_SIZE=0x40000000UL \
    -DBOOT_HART_ID=1