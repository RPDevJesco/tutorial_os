#!/bin/bash
# =============================================================================
# mkimage.sh - Create Bootable SD Card Image.img for Radxa Rock 2A
# =============================================================================
#
# Creates a complete, flashable SD card image containing:
#   1. MBR partition table
#   2. Vendor bootloader blobs (idbloader.img + u-boot.itb) at raw offsets
#   3. FAT32 partition with Image.img + boot.scr
#
# USAGE:
#   ./board/radxa-rock2a/mkimage.sh
#
# Or via make:
#   make BOARD=radxa-rock2a image
#
# REQUIREMENTS:
#   - parted     (partition table creation)
#   - mtools     (FAT filesystem manipulation without root)
#   - u-boot-tools (mkimage for boot.scr compilation)
#
# =============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

echo "DEBUG: Received BOARD=$BOARD"
echo "DEBUG: Received BUILD_DIR=$BUILD_DIR"

# Paths (relative to repo root)
BOARD_NAME="${BOARD:-radxa-rock2a}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build/${BOARD_NAME}"
BLOBS_DIR="${REPO_ROOT}/soc/rk3528a/blobs"
BOARD_DIR="${SCRIPT_DIR}"

KERNEL_BIN="${BUILD_DIR}/Image"
BOOT_CMD="${BOARD_DIR}/boot.cmd"
BOOT_SCR="${BUILD_DIR}/boot.scr"
IMG="${BUILD_DIR}/radxa-rock2a.img"

# Image.img configuration
IMG_SIZE_MB=64
PART_START_MB=16          # Past bootloader area
PART_START_SECTOR=32768   # 16MB / 512 bytes

# Rockchip blob offsets (512-byte sectors)
IDBLOADER_SECTOR=64       # 32KB
UBOOT_SECTOR=16384        # 8MB

# =============================================================================
# Preflight Checks
# =============================================================================

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}  Radxa Rock 2A SD Image Builder${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""

# Check tools
MISSING=0
for tool in parted mformat mcopy mkimage; do
    if ! command -v $tool &>/dev/null; then
        echo -e "${RED}ERROR: '$tool' not found${NC}"
        MISSING=1
    fi
done

if [ $MISSING -ne 0 ]; then
    echo ""
    echo "Install missing tools:"
    echo "  Ubuntu/Debian: sudo apt install parted mtools u-boot-tools"
    exit 1
fi

# Check kernel
if [ ! -f "${KERNEL_BIN}" ]; then
    echo -e "${RED}ERROR: Kernel not found: ${KERNEL_BIN}${NC}"
    echo "Build first with: make BOARD=radxa-rock2a"
    exit 1
fi

# Check blobs
if [ ! -f "${BLOBS_DIR}/idbloader.img" ]; then
    echo -e "${RED}ERROR: idbloader.img not found${NC}"
    echo "Expected: ${BLOBS_DIR}/idbloader.img"
    echo ""
    echo "Get blobs from your sbc-uboot-builder:"
    echo "  cp output/rockchip/rk3528/radxa-rock-2a-rk3528/idbloader.img ${BLOBS_DIR}/"
    echo "  cp output/rockchip/rk3528/radxa-rock-2a-rk3528/u-boot.itb ${BLOBS_DIR}/"
    exit 1
fi

if [ ! -f "${BLOBS_DIR}/u-boot.itb" ]; then
    echo -e "${RED}ERROR: u-boot.itb not found${NC}"
    echo "Expected: ${BLOBS_DIR}/u-boot.itb"
    exit 1
fi

mkdir -p "${BUILD_DIR}"

# =============================================================================
# Step 1: Compile boot.scr
# =============================================================================

echo "  MKIMG  boot.scr"
mkimage -C none -A arm64 -T script -d "${BOOT_CMD}" "${BOOT_SCR}" > /dev/null 2>&1

# =============================================================================
# Step 2: Create blank image
# =============================================================================

echo "  DD     ${IMG} (${IMG_SIZE_MB}MB)"
dd if=/dev/zero of="${IMG}" bs=1M count=${IMG_SIZE_MB} status=none

# =============================================================================
# Step 3: Partition table
# =============================================================================

echo "  PART   GPT + FAT32 @ ${PART_START_MB}MB"
parted -s "${IMG}" mklabel gpt
parted -s "${IMG}" mkpart boot fat32 ${PART_START_MB}MiB 100%
parted -s "${IMG}" set 1 legacy_boot on

# =============================================================================
# Step 4: Write bootloader blobs
# =============================================================================

echo "  DD     idbloader.img @ sector ${IDBLOADER_SECTOR}"
dd if="${BLOBS_DIR}/idbloader.img" of="${IMG}" seek=${IDBLOADER_SECTOR} conv=notrunc status=none

echo "  DD     u-boot.itb @ sector ${UBOOT_SECTOR}"
dd if="${BLOBS_DIR}/u-boot.itb" of="${IMG}" seek=${UBOOT_SECTOR} conv=notrunc status=none

# =============================================================================
# Step 5: Format FAT32 and copy files
# =============================================================================

PART_OFFSET=$((PART_START_MB * 1024 * 1024))

export MTOOLSRC="${BUILD_DIR}/.mtoolsrc"
cat > "${MTOOLSRC}" << EOF
drive c: file="${IMG}" offset=${PART_OFFSET}
mtools_skip_check=1
EOF

echo "  MFMT   FAT32"
mformat -F c: 2>/dev/null

echo "  MCOPY  Image"
mcopy "${KERNEL_BIN}" c:

echo "  MCOPY  boot.scr"
mcopy "${BOOT_SCR}" c:

echo "  MCOPY  extlinux/extlinux.conf"
mmd c:/extlinux
mcopy "${BOARD_DIR}/boot/extlinux/extlinux.conf" c:/extlinux/

rm -f "${MTOOLSRC}"

# =============================================================================
# Done
# =============================================================================

KERNEL_SIZE=$(stat -f%z "${KERNEL_BIN}" 2>/dev/null || stat -c%s "${KERNEL_BIN}")
IMG_SIZE=$(stat -f%z "${IMG}" 2>/dev/null || stat -c%s "${IMG}")

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}  Image created: ${IMG}${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""
echo "  Image size:  $((IMG_SIZE / 1024 / 1024))MB"
echo "  Kernel size: ${KERNEL_SIZE} bytes"
echo ""
echo "Flash to SD card:"
echo "  sudo dd if=${IMG} of=/dev/sdX bs=1M status=progress"
echo ""
echo "Serial: UART2 @ 1500000 baud (or 115200)"
echo ""