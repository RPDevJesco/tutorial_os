BOARD ?= rpi-zero2w-gpi
BUILD_DIR := build/$(BOARD)

CROSS_COMPILE ?= aarch64-none-elf-

ARCH_CFLAGS  ?= -mcpu=cortex-a53 -mgeneral-regs-only
ARCH_ASFLAGS ?=

BASE_CFLAGS := -Wall -Wextra -O2 -ffreestanding -nostdlib -nostartfiles
BASE_CFLAGS += -I. -Ihal -Icommon -Idrivers/framebuffer -Iui/core -Iui/themes -Iui/widgets

LDFLAGS := -nostdlib

# Include board config
ifeq ($(wildcard board/$(BOARD)/board.mk),)
$(error Unknown board: $(BOARD))
endif
include board/$(BOARD)/board.mk

ifndef SOC
$(error board/$(BOARD)/board.mk must set SOC)
endif

# Include SoC config
ifeq ($(wildcard soc/$(SOC)/soc.mk),)
$(error Unknown SoC: $(SOC))
endif
include soc/$(SOC)/soc.mk

ifneq ($(BUILD_MODE),uefi)
ifndef LINKER_SCRIPT
$(error soc/$(SOC)/soc.mk must set LINKER_SCRIPT)
endif
endif

ifndef KERNEL_NAME
$(error soc/$(SOC)/soc.mk must set KERNEL_NAME)
endif

# ──── Toolchain derived AFTER includes ────
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

# ──── CFLAGS assembled from base + arch + board + soc ────
CFLAGS  := $(BASE_CFLAGS) $(ARCH_CFLAGS)
CFLAGS  += $(BOARD_INCLUDES) $(BOARD_DEFINES)
CFLAGS  += $(SOC_INCLUDES) $(SOC_DEFINES) $(SOC_CFLAGS)

ASFLAGS := $(ARCH_ASFLAGS) $(BOARD_INCLUDES) $(BOARD_DEFINES) $(SOC_INCLUDES) $(SOC_DEFINES)

# Source files
KERNEL_SOURCES := kernel/main.c
COMMON_SOURCES := common/string.c

ifneq ($(wildcard memory/allocator.c),)
MEMORY_SOURCES := memory/allocator.c
endif

DRIVER_SOURCES := drivers/framebuffer/framebuffer.c

ifneq ($(wildcard ui/widgets/ui_widgets.c),)
UI_SOURCES := ui/widgets/ui_widgets.c
endif

ALL_C_SOURCES := \
    $(KERNEL_SOURCES) \
    $(COMMON_SOURCES) \
    $(MEMORY_SOURCES) \
    $(DRIVER_SOURCES) \
    $(UI_SOURCES)

# Object files
ASM_OBJECTS  := $(patsubst %.S,$(BUILD_DIR)/%.o,$(BOOT_SOURCES))
SOC_C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOC_SOURCES))
HAL_C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(HAL_SOURCES))
OTHER_C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ALL_C_SOURCES))
ALL_OBJECTS  := $(ASM_OBJECTS) $(SOC_C_OBJECTS) $(HAL_C_OBJECTS) $(OTHER_C_OBJECTS)

# ──── Canonical output names — used in all link and objcopy rules ────
ELF        := $(BUILD_DIR)/kernel.elf
BIN        := $(BUILD_DIR)/$(KERNEL_NAME)
BUILD_OBJS := $(ALL_OBJECTS)

# Targets
.PHONY: all clean info disasm print-%

all: $(BIN)
	@echo ""
	@echo "Build complete: $(BIN)"
	@ls -lh $(BIN)

# ──── Binary / EFI output ────

# ARM64 / U-Boot binary
ifneq ($(BUILD_MODE),uefi)
$(BIN): $(ELF)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) $< -O binary $@
endif

# PE/COFF EFI application — x86_64 UEFI
#
# Pipeline: gcc → ELF (via gnu-efi elf_x86_64_efi.lds + crt0) →
#           objcopy --target=efi-app-x86_64 → BOOTX64.EFI
#
# objcopy strips the ELF wrapper and rewrites the selected sections
# into a valid PE/COFF image with correct DllCharacteristics (DYNAMIC_BASE)
# and a properly formed .reloc table that UEFI firmware can relocate.
#
# Section selection mirrors what gnu-efi projects use — these are the
# sections the EFI loader actually cares about. Anything not listed
# (debug info, ELF metadata) is silently dropped.
ifeq ($(BUILD_MODE),uefi)
$(BIN): $(ELF)
	@echo "  EFI   $@ (objcopy ELF → PE/COFF)"
	@mkdir -p $(dir $@)
	@$(OBJCOPY) \
	    -j .text \
	    -j .sdata \
	    -j .data \
	    -j .dynamic \
	    -j .dynsym \
	    -j .rel \
	    -j .rela \
	    -j .reloc \
	    --target=efi-app-x86_64 \
	    --subsystem=10 \
	    $< $@
	@echo "  Size: $$(wc -c < $@) bytes"
endif

# ──── ELF link ────

# Non-UEFI: standard ld with board linker script
ifneq ($(BUILD_MODE),uefi)
$(ELF): $(BUILD_OBJS)
	@echo "  LD    $@"
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $@ $(BUILD_OBJS)
endif

# UEFI: link to ELF using gnu-efi's linker script and crt0.
#
# HOW THIS WORKS
# ──────────────
# 1. crt0-efi-x86_64.o defines _start (the real PE entry point).
#    UEFI calls _start with ms_abi. crt0 runs _relocate(), then calls
#    our efi_main() with System V ABI (args in RDI/RSI). This is why
#    efi_main() does NOT need __attribute__((ms_abi)).
#
# 2. elf_x86_64_efi.lds produces the ELF section layout that objcopy
#    --target=efi-app-x86_64 expects: .reloc before .data, .bss folded
#    into .data (UEFI has no BSS concept), .hash/.gnu.hash first.
#
# 3. libgnuefi.a provides _relocate() (ELF self-relocation used by crt0)
#    and uefi_call_wrapper() for ABI-safe boot service calls.
#
# 4. -shared -Bsymbolic: required for the ELF relocation approach.
#    Without -shared the linker won't emit .dynamic/.dynsym/.reloc.
#    -Bsymbolic binds all symbol references locally (no PLT indirection).
#
# 5. -znocombreloc: prevents the linker from merging relocation sections,
#    which would break objcopy's ability to find .reloc.
#
# crt0 must be FIRST in the object list — it defines _start.
ifeq ($(BUILD_MODE),uefi)
$(ELF): $(BUILD_OBJS)
	@echo "  LD    $@ [UEFI gnu-efi pipeline]"
	@mkdir -p $(dir $@)
	$(LD) \
	    -T $(LINKER_SCRIPT) \
	    -shared \
	    -Bsymbolic \
	    -znocombreloc \
	    $(GNUEFI_LIB)/crt0-efi-x86_64.o \
	    $(BUILD_OBJS) \
	    -o $@ \
	    -L$(GNUEFI_LIB) \
	    -lgnuefi
endif

# ──── Compilation rules ────

$(BUILD_DIR)/%.o: %.S
	@echo "  AS    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(ASFLAGS) -c $< -o $@

# Special rule for cache.S — needs Zicbom extensions (-march override)
# CACHE_ASFLAGS is set by board.mk for RISC-V boards only.
ifdef CACHE_ASFLAGS
$(BUILD_DIR)/boot/riscv64/cache.o: boot/riscv64/cache.S
	@echo "  AS    $< (Zicbom/Zicboz)"
	@mkdir -p $(dir $@)
	@$(CC) $(CACHE_ASFLAGS) -c $< -o $@
endif

$(BUILD_DIR)/%.o: %.c
	@echo "  CC    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# ──── Image creation ────

image: $(BIN)
ifeq ($(BUILD_MODE),uefi)
	python3 board/$(BOARD)/mkimage.py \
	    $(BIN) \
	    $(BUILD_DIR)/tutorial-os-lp-mu.img \
	    64
else
	@if [ -x board/$(BOARD)/mkimage.sh ]; then \
	    BOARD=$(BOARD) SOC=$(SOC) BUILD_DIR=$(BUILD_DIR) board/$(BOARD)/mkimage.sh; \
	else \
	    echo "No mkimage.sh for board $(BOARD)"; \
	    exit 1; \
	fi
endif

clean:
	@echo "Cleaning build directory..."
	@rm -rf build/

# Print any Makefile variable — used by docker-build.sh to query KERNEL_NAME
# without hardcoding board-specific names in the script.
# Usage: make print-KERNEL_NAME BOARD=rpi-cm5-io
print-%:
	@echo $($*)

info:
	@echo "Board:     $(BOARD)"
	@echo "SoC:       $(SOC)"
	@echo "Toolchain: $(CROSS_COMPILE)"
	@echo "Kernel:    $(KERNEL_NAME)"
	@echo ""
	@echo "ARCH_CFLAGS:  $(ARCH_CFLAGS)"
	@echo "ARCH_ASFLAGS: $(ARCH_ASFLAGS)"
	@echo ""
	@echo "Boot sources:"
	@for f in $(BOOT_SOURCES); do echo "  $$f"; done
	@echo ""
	@echo "SoC sources:"
	@for f in $(SOC_SOURCES); do echo "  $$f"; done
	@echo ""
	@echo "HAL sources:"
	@for f in $(HAL_SOURCES); do echo "  $$f"; done
	@echo ""
	@echo "C sources:"
	@for f in $(ALL_C_SOURCES); do echo "  $$f"; done
	@echo ""
	@echo "Total objects: $(words $(ALL_OBJECTS))"

disasm: $(ELF)
	@$(OBJDUMP) -d $< > $(BUILD_DIR)/kernel.list
	@echo "Disassembly: $(BUILD_DIR)/kernel.list"