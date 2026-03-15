# =============================================================================
# soc/lattepanda_n100/soc.mk — Intel N100 (Alder Lake-N) SoC Build Config
# =============================================================================
#
# This file is the x86_64 equivalent of soc/bcm2710/soc.mk and soc/kyx1/soc.mk.
# It tells the Makefile exactly what to compile and how to link for the
# LattePanda MU's Intel N100 SoC booting via UEFI.
#
# KEY DIFFERENCES FROM ARM64/RISC-V BOARDS
# ─────────────────────────────────────────
# 1. BUILD_MODE = uefi
#    ARM64 and RISC-V boards link a flat ELF and objcopy it to a raw binary.
#    x86_64 must produce a PE/COFF EFI application that UEFI firmware can load.
#    The Makefile has a BUILD_MODE=uefi path that:
#      - Links an ELF via gnu-efi's elf_x86_64_efi.lds + crt0-efi-x86_64.o
#      - Converts ELF → PE/COFF with: objcopy --target=efi-app-x86_64
#
# 2. No BOOT_SOURCES assembly
#    ARM64 boards have entry.S, vectors.S, cache.S, common_init.S.
#    RISC-V boards have entry.S, common_init.S, vectors.S (± cache.S).
#    x86_64/UEFI: the firmware provides the "boot assembly" — it configures
#    long mode, loads our PE/COFF, and calls efi_main() in C. We skip all
#    the assembly startup and let efi_main() do everything from C.
#
#    NOTE: boot/x86_64/entry.S exists in the project for the non-UEFI path
#    (custom bootloader → our own GDT setup). That path is NOT used here.
#    For the UEFI path (which is what LattePanda MU uses), efi_main() is the
#    direct PE/COFF entry point. No assembly shim required.
#
# 3. KERNEL_NAME = BOOTX64.EFI
#    ARM64 boards produce kernel8.img, kernel_2711.img, etc. (raw binaries).
#    RISC-V boards produce tutorial-os-rv2.bin, tutorial-os-mars.bin.
#    x86_64 produces BOOTX64.EFI — the default fallback boot path for
#    64-bit UEFI systems. Placed at \EFI\BOOT\BOOTX64.EFI on the FAT32
#    boot partition, firmware loads it automatically with no boot entry.
#
# 4. gnu-efi for PE/COFF production
#    We use gnu-efi's crt0-efi-x86_64.o, elf_x86_64_efi.lds, and libgnuefi.a
#    to produce a valid PE/COFF that UEFI firmware will actually load.
#    We do NOT use gnu-efi's utility wrappers (InitializeLib is called but
#    we use direct Boot Services calls via uefi_call_wrapper for clarity).
#    Our own efi_types.h is no longer needed — gnu-efi's <efi.h> provides
#    all necessary UEFI types.
#
# PROVIDES:
#   BUILD_MODE      — uefi (controls Makefile link and objcopy rules)
#   LINKER_SCRIPT   — /usr/lib/elf_x86_64_efi.lds (from gnu-efi package)
#   KERNEL_NAME     — BOOTX64.EFI (PE/COFF output, placed on FAT32 SD)
#   GNUEFI_LIB      — /usr/lib (crt0-efi-x86_64.o, libgnuefi.a, libefi.a)
#   GNUEFI_INC      — /usr/include/efi (gnu-efi headers)
#   BOOT_SOURCES    — (empty — gnu-efi crt0 is the real PE entry point)
#   SOC_SOURCES     — N100 UART, GOP display, UEFI soc init
#   HAL_SOURCES     — HAL platform bridge (hal_platform_lattepanda_n100.c)
#   SOC_INCLUDES    — gnu-efi headers + local N100 headers
#   SOC_DEFINES     — SOC_LATTEPANDA_N100, HAVE_USE_MS_ABI, peripheral bases
#   SOC_CFLAGS      — bare-metal x86_64 flags + -fpic for gnu-efi pipeline
#
# =============================================================================

# ─────────────────────────────────────────────────────────────────────────────
# Toolchain — override the default aarch64 cross-compiler with x86_64
#
# The top-level Makefile defaults to CROSS_COMPILE = aarch64-none-elf-.
# We must override it here so CC, LD, and OBJCOPY all point at the x86_64
# toolchain. Using the wrong compiler (aarch64-none-elf-gcc) would produce
# ARM64 object files that an x86_64 linker cannot process, and vice versa.
#
# x86_64-linux-gnu-gcc is the standard cross-gcc for x86_64 ELF/PE targets
# on an ARM64 or other host. It ships in the gcc-x86-64-linux-gnu package.
#
# NOTE: We use gcc (not ld) as the linker driver. See the Makefile UEFI
# link rule. gcc -pie causes the linker to emit a .reloc section, which
# UEFI firmware requires to relocate our image at load time. Bare ld does
# not produce .reloc without extra effort.
# ─────────────────────────────────────────────────────────────────────────────

CROSS_COMPILE := x86_64-linux-gnu-

# ─────────────────────────────────────────────────────────────────────────────
# Clear ARM64 arch defaults
#
# The top-level Makefile sets ARCH_CFLAGS ?= -mcpu=cortex-a53 -mgeneral-regs-only
# and ARCH_ASFLAGS ?= as defaults for ARM64 boards.
# We override them here with := (not ?=) to clear them for x86_64.
# Leaving -mcpu=cortex-a53 in CFLAGS would either be silently ignored or
# cause miscompilation — it must be cleared explicitly.
# ─────────────────────────────────────────────────────────────────────────────

ARCH_CFLAGS  :=
ARCH_ASFLAGS :=

# ─────────────────────────────────────────────────────────────────────────────
# Build mode — tells Makefile to use the UEFI link + objcopy path
# ─────────────────────────────────────────────────────────────────────────────

BUILD_MODE := uefi

# ─────────────────────────────────────────────────────────────────────────────
# gnu-efi paths
#
# gnu-efi provides the three pieces we need for a valid PE/COFF EFI binary:
#
#   crt0-efi-x86_64.o — The real PE/COFF entry point (_start).
#     UEFI firmware calls _start with ms_abi (ImageHandle in RCX, SystemTable
#     in RDX). crt0 runs ELF relocation fixups, then calls efi_main() with
#     System V ABI (args moved to RDI/RSI). This is why efi_main() in
#     soc_init.c does NOT have __attribute__((ms_abi)) / EFIAPI — it is
#     called by crt0 using the normal C calling convention.
#
#   elf_x86_64_efi.lds — Linker script that produces the ELF section layout
#     that objcopy --target=efi-app-x86_64 knows how to convert to PE/COFF.
#     Notably: .bss is folded into .data (UEFI loaders don't handle a
#     separate BSS section), and .reloc comes before .data.
#
#   libgnuefi.a — Provides _relocate() (called by crt0) and the
#     uefi_call_wrapper() thunk. The thunk is needed because UEFI boot
#     service function pointers use ms_abi, but our code uses System V.
#     Every call to BootServices->Foo() goes through uefi_call_wrapper().
#
#   libefi.a — Optional utility library (Print, AllocatePool wrappers, etc.)
#     Tutorial-OS does not use these wrappers — we call Boot Services
#     directly via uefi_call_wrapper. Linked anyway for _relocate symbol.
# ─────────────────────────────────────────────────────────────────────────────

GNUEFI_LIB := /usr/lib
GNUEFI_INC := /usr/include/efi

# ─────────────────────────────────────────────────────────────────────────────
# Output configuration
# ─────────────────────────────────────────────────────────────────────────────

# Linker script: gnu-efi's ELF layout for x86_64 EFI applications.
# This is the system-installed script from the gnu-efi package.
# objcopy --target=efi-app-x86_64 requires exactly this section layout.
LINKER_SCRIPT := $(GNUEFI_LIB)/elf_x86_64_efi.lds

# Final output: PE/COFF EFI application for the fallback boot path.
# UEFI firmware looks for \EFI\BOOT\BOOTX64.EFI on the FAT32 partition.
KERNEL_NAME   := BOOTX64.EFI

# ─────────────────────────────────────────────────────────────────────────────
# Boot assembly sources
# ─────────────────────────────────────────────────────────────────────────────
#
# Empty: UEFI firmware calls efi_main() directly in C.
# See the comment at the top of this file for full reasoning.
#
# Contrast with:
#   bcm2710: entry.S → common_init.S → vectors.S → cache.S
#   kyx1:    entry.S → common_init.S → vectors.S → cache.S
#   jh7110:  entry.S → common_init.S → vectors.S
#   N100:    (nothing — UEFI handles it)

BOOT_SOURCES :=

# ─────────────────────────────────────────────────────────────────────────────
# SoC driver sources
# ─────────────────────────────────────────────────────────────────────────────
#
# uart_8250.c       — COM1 @ 0x3F8, 115200 8N1, x86 port I/O (inb/outb)
#                     Provides: n100_uart_putc(), n100_uart_puts()
#
# display_gop.c     — UEFI GOP framebuffer query (before ExitBootServices)
#                     Extracts base address, dimensions, pixel format, pitch.
#                     Provides: n100_display_init(), n100_display_present()
#
# soc_init.c        — UEFI boot sequence orchestration:
#                     1. efi_main() — PE/COFF entry, called by UEFI firmware
#                     2. Query GOP framebuffer
#                     3. GetMemoryMap + ExitBootServices
#                     4. BSS zero, stack switch
#                     5. HAL platform init (CPUID cache)
#                     6. kernel_main(fb) — hand off to portable kernel

SOC_SOURCES := \
	soc/lattepanda_n100/uart_8250.c \
	soc/lattepanda_n100/display_gop.c \
	soc/lattepanda_n100/soc_init.c

# ─────────────────────────────────────────────────────────────────────────────
# HAL platform bridge
# ─────────────────────────────────────────────────────────────────────────────
#
# Implements the unified hal_platform_* API that kernel/main.c calls.
# On x86_64: uses CPUID (leaf 0x01, 0x16) and MSRs (0x19C, 0x1B1)
# instead of ARM mailbox or RISC-V SBI calls.
#
# This is what makes main.c work identically on all three platforms —
# identical HAL API calls, completely different implementations underneath.

HAL_SOURCES := \
	soc/lattepanda_n100/hal_platform_lattepanda_n100.c

# gnu-efi libraries to link — order matters
# crt0 must be first (it defines _start, the PE entry point)
# libgnuefi provides _relocate (called by crt0)
# libefi is NOT linked — it defines memset/memcpy which collide with
# common/string.c. We don't use any libefi utility functions.
GNUEFI_LIBS := \
	$(GNUEFI_LIB)/crt0-efi-x86_64.o \
	-L$(GNUEFI_LIB) \
	-lgnuefi

# x86_64 timer uses TSC, no separate timer.c needed yet)
#
# Unlike ARM64 (which needs a full timer.c for the system counter) and
# RISC-V (which queries the timebase via CSRs), the x86_64 timer is
# initialized inline in soc_init.c. A separate timer.c is provided for
# completeness but its symbols are already in hal_platform_lattepanda_n100.c.
#
# Uncomment when timer.c is expanded:
# SOC_SOURCES += soc/lattepanda_n100/timer.c

# ─────────────────────────────────────────────────────────────────────────────
# Include paths
# ─────────────────────────────────────────────────────────────────────────────
#
# -Isoc/lattepanda_n100    gives access to efi_types.h (local UEFI types)
#                          without requiring gnu-efi to be installed

SOC_INCLUDES := \
	-I$(GNUEFI_INC) \
	-I$(GNUEFI_INC)/x86_64 \
	-Isoc/lattepanda_n100

# ─────────────────────────────────────────────────────────────────────────────
# Preprocessor defines
# ─────────────────────────────────────────────────────────────────────────────

SOC_DEFINES := \
	-DSOC_LATTEPANDA_N100=1 \
	-DBOARD_LATTEPANDA_MU=1 \
	-DCOM1_BASE=0x3F8 \
	-DUART_BAUD=115200 \
	-DHAVE_USE_MS_ABI=1

# ─────────────────────────────────────────────────────────────────────────────
# Extra compiler flags for UEFI bare-metal x86_64
# ─────────────────────────────────────────────────────────────────────────────
#
# -mno-red-zone
#   The "red zone" is a 128-byte area below RSP that the System V ABI
#   reserves for leaf functions. Interrupts and NMIs don't respect it.
#   UEFI interrupts can fire before ExitBootServices — without this flag,
#   firmware interrupt handlers would corrupt our stack.
#   Required for ANY bare-metal x86_64 code.
#
# -mno-sse -mno-sse2 -mno-avx -mno-avx2
#   SSE/AVX registers are not saved/restored by UEFI firmware exception
#   handlers. Enabling SSE in bare-metal code risks corrupting XMM/YMM
#   state across firmware calls. Disabled for safety.
#   Also avoids the "SIMD enabled without SSE save area" trap.
#
# -DHAVE_USE_MS_ABI=1 (set in SOC_DEFINES, not here)
#   Tells gnu-efi's efibind.h to annotate UEFI function pointer typedefs
#   with __attribute__((ms_abi)). GCC then handles the System V → ms_abi
#   ABI transition automatically at every Boot Services call site.
#   Without this, you must use uefi_call_wrapper() for every call.
#   With it, uefi_call_wrapper() becomes a transparent pass-through macro.
#
# -fpic
#   Required for the gnu-efi ELF relocation pipeline. gnu-efi's crt0 runs
#   _relocate() which patches absolute addresses in the loaded image.
#   -fpic ensures the compiler emits relocatable references that _relocate
#   can fix up. The ELF .reloc section is then converted to PE/COFF .reloc
#   by objcopy --target=efi-app-x86_64 for UEFI base relocation.

SOC_CFLAGS := \
	-mno-red-zone \
	-mno-sse \
	-mno-sse2 \
	-mno-avx \
	-mno-avx2 \
	-fpic