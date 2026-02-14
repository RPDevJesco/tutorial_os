# =============================================================================
# boot.cmd — U-Boot Boot Script for Tutorial-OS (Orange Pi RV2)
# =============================================================================
#
# This is the source for a U-Boot boot script. Compile it with:
#   mkimage -C none -A riscv -T script -d boot.cmd boot.scr
#
# Place boot.scr on the FAT32 boot partition root.
# U-Boot will find and execute it automatically.
#
# ALTERNATIVE: You can also paste these commands manually into the
# U-Boot console (without the # comments) for testing.
#
# =============================================================================

echo "========================================"
echo " Tutorial-OS Bootloader (Orange Pi RV2)"
echo "========================================"

# ---------------------------------------------------------------------------
# Load the device tree blob
# ---------------------------------------------------------------------------
# U-Boot usually has a DTB already loaded (from its own boot), but we
# load it explicitly to a known address for reliability.
#
# Address 0x10F00000 is just below our kernel at 0x11000000.
# The 1MB gap is more than enough for any DTB.

echo "Loading device tree..."
fatload mmc 0:1 0x10F00000 dtb/spacemit/k1-x_orangepi-rv2.dtb
if test $? -ne 0; then
    echo "WARN: DTB not found, using U-Boot's built-in FDT"
    # Fall back to whatever DTB U-Boot has
    fdt addr ${fdt_addr}
else
    fdt addr 0x10F00000
    echo "DTB loaded at 0x10F00000"
fi

# ---------------------------------------------------------------------------
# Load the kernel binary
# ---------------------------------------------------------------------------
# 0x11000000 = our linker script's ORIGIN address.
# The binary MUST be loaded here — all code addresses are relative to this.

echo "Loading Tutorial-OS kernel..."
fatload mmc 0:1 0x11000000 tutorial-os-rv2.bin
if test $? -ne 0; then
    echo "ERROR: tutorial-os-rv2.bin not found on boot partition!"
    echo "Please copy the binary to the SD card."
    exit
fi

# Show what we loaded
echo "Kernel loaded at 0x11000000"

# ---------------------------------------------------------------------------
# Boot!
# ---------------------------------------------------------------------------
# `go` jumps directly to the address (no OS abstraction).
# Our _start expects: a0=hart_id, a1=dtb_ptr
# U-Boot's `go` command passes these automatically on RISC-V.
#
# ALTERNATIVE: If using booti (Linux Image format), you'd need:
#   booti 0x11000000 - 0x10F00000
# But that requires a proper Image header. `go` works with flat binaries.

echo "Booting Tutorial-OS..."
echo "========================================"
go 0x11000000
