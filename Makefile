# Tutorial-OS Makefile

BOARD ?= rpi-zero2w-gpi
BUILD_DIR := build/$(BOARD)

# Toolchain
CROSS_COMPILE ?= aarch64-none-elf-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

# Base flags - include all source directories
CFLAGS := -Wall -Wextra -O2 -ffreestanding -nostdlib -nostartfiles
CFLAGS += -mcpu=cortex-a53 -mgeneral-regs-only
CFLAGS += -I. -Ihal -Icommon -Idrivers/framebuffer -Iui/core -Iui/themes -Iui/widgets

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

ifndef LINKER_SCRIPT
$(error soc/$(SOC)/soc.mk must set LINKER_SCRIPT)
endif

ifndef KERNEL_NAME
$(error soc/$(SOC)/soc.mk must set KERNEL_NAME)
endif

CFLAGS += $(BOARD_INCLUDES) $(BOARD_DEFINES)
CFLAGS += $(SOC_INCLUDES) $(SOC_DEFINES)

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
ASM_OBJECTS := $(patsubst %.S,$(BUILD_DIR)/%.o,$(BOOT_SOURCES))
SOC_C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOC_SOURCES))
OTHER_C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ALL_C_SOURCES))
ALL_OBJECTS := $(ASM_OBJECTS) $(SOC_C_OBJECTS) $(OTHER_C_OBJECTS)

# Targets
.PHONY: all clean info disasm

all: $(BUILD_DIR)/$(KERNEL_NAME)
	@echo ""
	@echo "Build complete: $(BUILD_DIR)/$(KERNEL_NAME)"
	@ls -lh $(BUILD_DIR)/$(KERNEL_NAME)

$(BUILD_DIR)/$(KERNEL_NAME): $(BUILD_DIR)/kernel.elf
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) $< -O binary $@

$(BUILD_DIR)/kernel.elf: $(ALL_OBJECTS) $(LINKER_SCRIPT)
	@echo ""
	@echo "=== Linking $(words $(ALL_OBJECTS)) objects ==="
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $@ $(ALL_OBJECTS)

$(BUILD_DIR)/%.o: %.S
	@echo "  AS    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@echo "  CC    $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

image: $(BUILD_DIR)/$(KERNEL_NAME)
	@if [ -x board/$(BOARD)/mkimage.sh ]; then \
		BOARD=$(BOARD) SOC=$(SOC) BUILD_DIR=$(BUILD_DIR) board/$(BOARD)/mkimage.sh; \
	else \
		echo "No mkimage.sh for board $(BOARD)"; \
		exit 1; \
	fi

clean:
	@echo "Cleaning build directory..."
	@rm -rf build/

info:
	@echo "Board: $(BOARD)"
	@echo "SoC:   $(SOC)"
	@echo "Kernel: $(KERNEL_NAME)"
	@echo ""
	@echo "Boot sources:"
	@for f in $(BOOT_SOURCES); do echo "  $$f"; done
	@echo ""
	@echo "SoC sources:"
	@for f in $(SOC_SOURCES); do echo "  $$f"; done
	@echo ""
	@echo "C sources:"
	@for f in $(ALL_C_SOURCES); do echo "  $$f"; done

disasm: $(BUILD_DIR)/kernel.elf
	@$(OBJDUMP) -d $< > $(BUILD_DIR)/kernel.list
	@echo "Disassembly: $(BUILD_DIR)/kernel.list"