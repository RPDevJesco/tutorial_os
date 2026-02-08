# =============================================================================
# boot.cmd - U-Boot Boot Script for Tutorial-OS on Radxa Rock 2A
# =============================================================================
#
# Compiled to boot.scr with:
#   mkimage -C none -A arm64 -T script -d boot.cmd boot.scr
#
# U-Boot looks for boot.scr on the FAT32 partition automatically.
# =============================================================================

echo ""
echo "========================================"
echo " Tutorial-OS Boot"
echo " Board: Radxa Rock 2A (RK3528A)"
echo "========================================"
echo ""

# Load address - must match soc/rk3528a/linker.ld
setenv loadaddr 0x40080000

echo "Loading Image to ${loadaddr}..."
load mmc 1:1 ${loadaddr} Image

if test $? -ne 0; then
    echo "ERROR: Failed to load Image!"
    echo "Make sure the file exists on the FAT32 partition."
else
    echo "Loaded successfully."
    echo "Jumping to kernel..."
    echo ""
    go ${loadaddr}
fi