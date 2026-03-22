# =============================================================================
# board/lattepanda-mu/board.mk — LattePanda MU Board Configuration
# =============================================================================
#
# WHAT THIS FILE DOES:
#   1. Selects the n100 SoC layer (Intel N100, AlderLake ULX)
#   2. Overrides the default ARM64 toolchain with x86_64
#   3. Sets x86_64-specific compiler flags (clearing ARM64 defaults)
#   4. Adds board-specific defines
#
# KEY DIFFERENCE FROM ALL OTHER BOARDS:
#   Every other board uses U-Boot as the bootloader and boots a flat ELF
#   binary via a linker script at a fixed load address. This board boots
#   via UEFI firmware (AMI Aptio). The entry point is efi_main(), not
#   _start(). There is no boot assembly, no custom linker script, and no
#   BSS zeroing or stack setup to do — the firmware handles all of that
#   before efi_main() is called.
#
#   The output is a PE/COFF EFI application (BOOTX64.EFI) placed on a
#   FAT32 partition at \EFI\BOOT\BOOTX64.EFI.
#
# HARDWARE (confirmed via UART log from bring-up project):
#   CPU:     Intel N100 (AlderLake ULX), CPUID 0xB06E0, stepping A0
#   Cores:   4 E-cores / 4 threads
#   RAM:     8192 MB LPDDR5 @ 4800 MHz
#   BIOS:    AMI Aptio LP-BS-S70NC1R200-SR-B (07/08/2024)
#   UART:    COM1 @ 0x3F8, 115200 8N1 (x86 port I/O, not MMIO)
#   Display: GOP 1920x1080, stride=1920, pixel format BGR (fmt=1)
#
# BOOT CHAIN:
#   Power → AMI Aptio UEFI → \EFI\BOOT\BOOTX64.EFI → efi_main()
#   Secure Boot must be disabled (or set to Custom mode) in BIOS.
#
# =============================================================================

# SoC selection — tells the Makefile to include soc/n100/soc.mk
SOC := n100

# Architecture identifier
ARCH := x86_64

# =============================================================================
# Toolchain
# =============================================================================
#
# gnu-efi builds use the host gcc on x86_64 machines, or x86_64-linux-gnu-
# when cross-compiling from another architecture (e.g. ARM64 build host).
# The Docker build environment uses x86_64-linux-gnu- explicitly.
#
# Unlike RISC-V where we must cross-compile, x86_64 development machines
# can build natively with no prefix at all. We try in order of preference.
#

ifeq ($(shell which x86_64-linux-gnu-gcc 2>/dev/null),)
  # No cross-compile prefix available — try native gcc
  ifeq ($(shell which gcc 2>/dev/null),)
    $(warning No x86_64 toolchain found!)
    $(warning Install with: apt install gcc-x86-64-linux-gnu)
  else
    CROSS_COMPILE :=
  endif
else
  CROSS_COMPILE := x86_64-linux-gnu-
endif

# =============================================================================
# Architecture flags — REPLACES the ARM64 defaults
# =============================================================================
#
# The Makefile defaults ARCH_CFLAGS to -mcpu=cortex-a53 -mgeneral-regs-only.
# We clear those entirely and set x86_64 equivalents.
#
# -m64                : Explicit 64-bit mode (redundant on x86_64 gcc, but clear)
# -march=x86-64       : Baseline x86_64 — no AVX or newer extensions assumed
#                       The N100 supports AVX2, but we stay conservative.
#                       UEFI boot services have already set up SSE/AVX if needed.
#
# NOTE: gnu-efi-specific flags (-fpic, -fshort-wchar, -mno-red-zone, etc.)
# live in SOC_CFLAGS in soc/n100/soc.mk, not here. Those flags
# are UEFI ABI requirements, not architecture requirements, so they belong
# at the SoC layer.
#
# NOTE: No ARCH_ASFLAGS — BOOT_SOURCES is empty for this board. There are
# no assembly files to compile.
#

ARCH_CFLAGS  := -m64 -march=x86-64
ARCH_ASFLAGS :=

# =============================================================================
# Board-specific includes and defines
# =============================================================================
#
# Display constants are known from the confirmed GOP output:
#   res=1920x1080  stride=1920  fmt=1 (BGR)
#
# DISPLAY_PIXEL_FORMAT_BGR signals to the framebuffer HAL which byte order
# to use in make_pixel(). This is confirmed hardware — not assumed.
#

BOARD_INCLUDES := \
    -Iboard/lattepanda-mu

BOARD_DEFINES := \
    -DBOARD_LATTEPANDA_MU=1 \
    -DBOARD_NAME="\"LattePanda MU\"" \
    -DDISPLAY_WIDTH=1920 \
    -DDISPLAY_HEIGHT=1080 \
    -DDISPLAY_PIXEL_FORMAT_BGR=1 \
    -DUART_COM1_BASE=0x3F8 \
    -DCPU_INTEL_N100=1