# =============================================================================
# Makefile — Tutorial-OS Unified Build System
# =============================================================================
#
# Two languages, one project, one entry point.
#
# Usage:
#   make LANG=c    BOARD=rpi-zero2w-gpi     # C build for Pi Zero 2W
#   make LANG=rust BOARD=rpi-zero2w         # Rust build for Pi Zero 2W
#   make LANG=c    BOARD=milkv-mars         # C build for Milk-V Mars
#   make LANG=rust BOARD=milkv-mars         # Rust build for Milk-V Mars
#   make LANG=c    BOARD=lattepanda-mu      # C build for LattePanda MU
#   make info                               # Show build configuration
#   make clean                              # Clean all build artifacts
#
# How this works:
#   LANG=c:    Traditional board.mk → soc.mk → gcc cascade.
#              C sources live in src/ directories alongside Rust files.
#              Assembly and linker scripts stay at crate/SoC root level.
#
#   LANG=rust: Delegates to build.sh which drives Cargo.
#              cargo build -p kernel --features board-xxx --target yyy
#              Assembly compiled via build.rs + cc crate.
#
# Both languages share:
#   - boot/          Shared assembly entry points
#   - board/         Board configs, deploy docs, boot files
#   - soc/*/linker.ld  Linker scripts (identical for both)
#   - soc/*/soc.mk     C build config (ignored by Cargo)
#   - soc/*/boot_soc.S SoC-specific assembly
#
# =============================================================================

LANG  ?= c
BOARD ?= rpi-zero2w-gpi

# =============================================================================
# LANG=rust — Delegate to Cargo via build.sh
# =============================================================================
#
# The Rust build chain is entirely Cargo-driven. The Makefile just forwards
# the board name and gets out of the way.

ifeq ($(LANG),rust)

# Strip the GPi suffix for Rust board names (Rust uses board-rpi-zero2w, not board-rpi-zero2w-gpi)
# The -gpi variant is a C-side board.mk concern (display resolution override).

.PHONY: all clean info image

all:
	@./build.sh $(BOARD) release

debug:
	@./build.sh $(BOARD) debug

clean:
	@echo "Cleaning Rust build artifacts..."
	@cargo clean 2>/dev/null || true
	@rm -rf output/
	@echo "Done."

info:
	@echo "Language: Rust"
	@echo "Board:    $(BOARD)"
	@echo "Build:    cargo build -p kernel --features board-$(BOARD) --target <arch>"

image: all
	@echo "Image creation handled by build.sh (checks board/$(BOARD)/mkimage.sh)"

else
# =============================================================================
# LANG=c — Traditional Makefile Build
# =============================================================================
#
# Source paths point into src/ directories where C files live alongside
# Rust files. Assembly and build infrastructure stay at crate root level.
#

BUILD_DIR := build/$(BOARD)

# ──── Default toolchain (ARM64, overridden by board.mk for other arches) ────
CROSS_COMPILE ?= aarch64-none-elf-

ARCH_CFLAGS  ?= -mcpu=cortex-a53 -mgeneral-regs-only
ARCH_ASFLAGS ?=

# ──── Base flags ────
#
# Include paths point into src/ directories where headers now live.
# The old layout had -Icommon -Ihal etc; now it's -Icommon/src -Ihal/src.
#
BASE_CFLAGS := -Wall -Wextra -O2 -ffreestanding -nostdlib -nostartfiles
BASE_CFLAGS += -Ihal/src
BASE_CFLAGS += -Icommon/src
BASE_CFLAGS += -Idrivers/src/framebuffer
BASE_CFLAGS += -Iui/src/core -Iui/src/themes -Iui/src/widgets

LDFLAGS := -nostdlib

# ──── Include board config ────
ifeq ($(wildcard board/$(BOARD)/board.mk),)
$(error Unknown board: $(BOARD). Check board/ directory.)
endif
include board/$(BOARD)/board.mk

ifndef SOC
$(error board/$(BOARD)/board.mk must set SOC)
endif

# ──── Include SoC config ────
ifeq ($(wildcard soc/$(SOC)/soc.mk),)
$(error Unknown SoC: $(SOC). Check soc/ directory.)
endif
include soc/$(SOC)/soc.mk

# Validate required variables from soc.mk
ifneq ($(BUILD_MODE),uefi)
ifneq ($(BUILD_MODE),pico)
ifndef LINKER_SCRIPT
$(error soc/$(SOC)/soc.mk must set LINKER_SCRIPT)
endif
endif
endif

ifndef KERNEL_NAME
$(error soc/$(SOC)/soc.mk must set KERNEL_NAME)
endif

# ──── Toolchain (derived AFTER board/soc includes) ────
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

# ──── CFLAGS assembled from base + arch + board + soc ────
CFLAGS  := $(BASE_CFLAGS) $(ARCH_CFLAGS)
CFLAGS  += $(BOARD_INCLUDES) $(BOARD_DEFINES)
CFLAGS  += $(SOC_INCLUDES) $(SOC_DEFINES) $(SOC_CFLAGS)

ASFLAGS := $(ARCH_ASFLAGS) $(BOARD_INCLUDES) $(BOARD_DEFINES) $(SOC_INCLUDES) $(SOC_DEFINES)

# ──── Source files (paths updated for src/ layout) ────
#
# C sources now live in src/ subdirectories:
#   common/string.c        → common/src/string.c
#   kernel/main.c          → kernel/src/main.c
#   drivers/.../fb.c       → drivers/src/framebuffer/framebuffer.c
#   memory/allocator.c     → memory/src/allocator.c
#   ui/widgets/ui_widgets.c → ui/src/widgets/ui_widgets.c
#
# Assembly and soc.mk paths are unchanged — they stay at crate root.

KERNEL_SOURCES := kernel/src/main.c
COMMON_SOURCES := common/src/string.c

ifneq ($(wildcard memory/src/allocator.c),)
MEMORY_SOURCES := memory/src/allocator.c
endif

DRIVER_SOURCES := drivers/src/framebuffer/framebuffer.c

ifneq ($(wildcard ui/src/widgets/ui_widgets.c),)
UI_SOURCES := ui/src/widgets/ui_widgets.c
endif

ALL_C_SOURCES := \
    $(KERNEL_SOURCES) \
    $(COMMON_SOURCES) \
    $(MEMORY_SOURCES) \
    $(DRIVER_SOURCES) \
    $(UI_SOURCES)

# ──── Pico SDK: CMake handles everything ────
ifeq ($(BUILD_MODE),pico)
ALL_C_SOURCES :=
BOOT_SOURCES  :=
SOC_SOURCES   :=
HAL_SOURCES   :=
endif

# ──── Object files ────
ASM_OBJECTS    := $(patsubst %.S,$(BUILD_DIR)/%.o,$(BOOT_SOURCES))
SOC_C_OBJECTS  := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOC_SOURCES))
HAL_C_OBJECTS  := $(patsubst %.c,$(BUILD_DIR)/%.o,$(HAL_SOURCES))
OTHER_C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ALL_C_SOURCES))
ALL_OBJECTS    := $(ASM_OBJECTS) $(SOC_C_OBJECTS) $(HAL_C_OBJECTS) $(OTHER_C_OBJECTS)

# ──── Canonical output names ────
ELF        := $(BUILD_DIR)/kernel.elf
BIN        := $(BUILD_DIR)/$(KERNEL_NAME)
BUILD_OBJS := $(ALL_OBJECTS)

# =============================================================================
# Targets
# =============================================================================

.PHONY: all clean info disasm image print-%

all: $(BIN)
	@echo ""
	@echo "Build complete: $(BIN)"
	@ls -lh $(BIN)

# ──── Binary output (ARM64 / RISC-V) ────
ifneq ($(BUILD_MODE),uefi)
ifneq ($(BUILD_MODE),pico)
$(BIN): $(ELF)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) $< -O binary $@
endif
endif

# ──── PE/COFF EFI application (x86_64 UEFI) ────
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

# ──── ELF link (non-UEFI) ────
ifneq ($(BUILD_MODE),uefi)
ifneq ($(BUILD_MODE),pico)
$(ELF): $(BUILD_OBJS)
	@echo "  LD    $@"
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $@ $(BUILD_OBJS)
endif
endif

# ──── ELF link (UEFI: gnu-efi pipeline) ────
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

# ──── Pico SDK (CMake substep) ────
PICO_CMAKE_BUILD := $(BUILD_DIR)/cmake-build
TARGET_PICO      := tutorial-os-pico2

ifeq ($(BUILD_MODE),pico)
$(BIN): pico-build
	@cp $(PICO_CMAKE_BUILD)/$(TARGET_PICO).uf2 $@
	@echo ""
	@echo "Build complete: $@"
	@ls -lh $@

.PHONY: pico-build
pico-build:
	@mkdir -p $(PICO_CMAKE_BUILD)
	@cd $(PICO_CMAKE_BUILD) && cmake \
	    -DPICO_BOARD=$(PICO_BOARD) \
	    -DPICO_PLATFORM=$(PICO_PLATFORM) \
	    -DPICO_SDK_PATH=$(PICO_SDK_PATH) \
	    -DTUTORIAL_OS_ROOT=$(CURDIR) \
	    $(CURDIR)/soc/rp2350
	@$(MAKE) -C $(PICO_CMAKE_BUILD) -j$$(nproc)
endif

# ──── Compilation rules ────

$(BUILD_DIR)/%.o: %.S
	@echo "  AS    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(ASFLAGS) -c $< -o $@

# Special rule for cache.S — needs Zicbom extensions (-march override)
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

# ──── Utility targets ────

clean:
	@echo "Cleaning all build artifacts..."
	@rm -rf build/
	@cargo clean 2>/dev/null || true
	@rm -rf output/
	@echo "Done."

print-%:
	@echo $($*)

info:
	@echo "Language:  C"
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

endif  # LANG=c
