# =============================================================================
# Makefile — Tutorial-OS Top-Level Build System
# =============================================================================
#
# This Makefile supports building for multiple boards by selecting a board
# configuration file that sets the architecture, SoC, toolchain, and sources.
#
# Usage:
#   make BOARD=orangepi-rv2           Build for Orange Pi RV2 (RISC-V)
#   make BOARD=orangepi-rv2 clean     Clean build artifacts
#   make BOARD=orangepi-rv2 info      Show build configuration
#
# Future boards:
#   make BOARD=pi-zero2w-gpi          Build for Pi Zero 2W + GPi Case
#   make BOARD=rock2a                 Build for Radxa Rock 2A
#
# The build flow:
#   1. BOARD variable selects board/<BOARD>/board.mk
#   2. board.mk sets ARCH, SOC, toolchain, sources, flags
#   3. soc/<SOC>/soc.mk adds SoC-specific sources and flags
#   4. This Makefile compiles everything and links the final binary
#
# =============================================================================

# ---------------------------------------------------------------------------
# Board Selection
# ---------------------------------------------------------------------------
# Default to Orange Pi RV2 for now. Override with BOARD= on the command line.

BOARD ?= orangepi-rv2

# Validate the board exists
BOARD_MK := board/$(BOARD)/board.mk
ifeq ($(wildcard $(BOARD_MK)),)
    $(error Board "$(BOARD)" not found. Expected $(BOARD_MK))
endif

# Include board configuration — sets ARCH, SOC, CC, sources, flags, etc.
include $(BOARD_MK)

# Include SoC configuration — adds PLATFORM_OBJS, PLATFORM_INCLUDES, etc.
SOC_MK := soc/$(SOC)/soc.mk
ifneq ($(wildcard $(SOC_MK)),)
    include $(SOC_MK)
endif

# ---------------------------------------------------------------------------
# Build Directory
# ---------------------------------------------------------------------------
# Keep build artifacts out of the source tree.

BUILD_DIR := build/$(BOARD)

# ---------------------------------------------------------------------------
# Combine Everything
# ---------------------------------------------------------------------------

# All object files (from board.mk + soc.mk)
OBJS := $(ALL_OBJS) $(PLATFORM_OBJS)

# Place objects in build directory
BUILD_OBJS := $(addprefix $(BUILD_DIR)/, $(OBJS))

# Combine all flags
CFLAGS  := $(BOARD_CFLAGS) $(BOARD_INCLUDES) $(PLATFORM_INCLUDES) $(PLATFORM_CFLAGS)
ASFLAGS := $(BOARD_ASFLAGS) $(BOARD_INCLUDES) $(PLATFORM_INCLUDES)
CACHE_ASFLAGS := $(CACHE_ASFLAGS) $(BOARD_INCLUDES) $(PLATFORM_INCLUDES)
LDFLAGS := $(BOARD_LDFLAGS)

# Output files
ELF  := $(BUILD_DIR)/$(KERNEL_ELF)
BIN  := $(BUILD_DIR)/$(KERNEL_NAME)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

.PHONY: all clean info size disasm

all: $(BIN)
	@echo ""
	@echo "======================================"
	@echo " BUILD COMPLETE: $(KERNEL_NAME)"
	@echo " Board: $(BOARD_PRETTY_NAME)"
	@echo " Arch:  $(ARCH) / SoC: $(SOC)"
	@echo "======================================"
	@$(SIZE) $(ELF)

# Link ELF
$(ELF): $(BUILD_OBJS) $(LDSCRIPT)
	@echo "  LD    $@"
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(BUILD_OBJS) -o $@

# Convert ELF to flat binary (what U-Boot loads)
$(BIN): $(ELF)
	@echo "  BIN   $@"
	$(OBJCOPY) -O binary $< $@
	@echo "  Size: $$(wc -c < $@) bytes"

# ---------------------------------------------------------------------------
# Compile Rules
# ---------------------------------------------------------------------------

# C source → object
$(BUILD_DIR)/%.o: %.c
	@echo "  CC    $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly source → object (default)
$(BUILD_DIR)/%.o: %.S
	@echo "  AS    $<"
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# cache.S needs Zicbom/Zicboz extensions for cbo.clean/inval/flush/zero
# This overrides the generic rule for this one file
$(BUILD_DIR)/boot/riscv64/cache.o: boot/riscv64/cache.S
	@echo "  AS    $< (with Zicbom/Zicboz)"
	@mkdir -p $(dir $@)
	$(AS) $(CACHE_ASFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Utility Targets
# ---------------------------------------------------------------------------

clean:
	@echo "Cleaning build/$(BOARD)..."
	rm -rf $(BUILD_DIR)

info:
	@echo "Board:      $(BOARD) ($(BOARD_PRETTY_NAME))"
	@echo "Arch:       $(ARCH)"
	@echo "SoC:        $(SOC)"
	@echo "Toolchain:  $(CROSS_COMPILE)"
	@echo "CC:         $(CC)"
	@echo "CFLAGS:     $(CFLAGS)"
	@echo "LDSCRIPT:   $(LDSCRIPT)"
	@echo "Output:     $(BIN)"
	@echo ""
	@echo "Sources:"
	@for f in $(OBJS); do echo "  $$f"; done

size: $(ELF)
	$(SIZE) $(ELF)

disasm: $(ELF)
	$(OBJDUMP) -d -S $< > $(BUILD_DIR)/disasm.txt
	@echo "Disassembly written to $(BUILD_DIR)/disasm.txt"

# ---------------------------------------------------------------------------
# SD Card Deploy (board-specific)
# ---------------------------------------------------------------------------
# For the Orange Pi RV2, you'd copy the .bin to the SD card's boot partition
# and configure U-Boot to load it:
#   fatload mmc 0:1 0x11000000 tutorial-os-rv2.bin
#   go 0x11000000
#
# Or set up an extlinux.conf / boot.scr for automatic loading.

.PHONY: deploy
deploy: $(BIN)
	@echo ""
	@echo "To deploy to SD card:"
	@echo "  1. Mount the SD card's boot partition"
	@echo "  2. cp $(BIN) /path/to/sdcard/"
	@echo "  3. In U-Boot console:"
	@echo "     fatload mmc 0:1 0x11000000 $(KERNEL_NAME)"
	@echo "     go 0x11000000"
	@echo ""
	@echo "  Or use bootm if the binary has a proper header."
