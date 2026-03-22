#!/bin/bash
# =============================================================================
# board/milkv-mars/mkimage.sh — Create Bootable SD Card Image (Rust)
# =============================================================================
#
# Creates a complete, flashable SD card image for the Milk-V Mars
# (StarFive JH7110 SoC, RISC-V 64-bit) in SPI FLASH BOOT MODE.
#
# BOOT MODE: SPI Flash (RGPIO_0=0, RGPIO_1=0)
#   - U-Boot SPL and firmware load from QSPI NOR flash on the SoM
#   - SD card only needed for kernel + uEnv.txt + DTBs
#   - NO firmware blobs needed on the SD card
#
# SD CARD LAYOUT:
#   Partition 1 (2MB)   — dummy, matches HiFive BBL slot  (empty)
#   Partition 2 (4MB)   — dummy, matches HiFive FSBL slot (empty)
#   Partition 3 (100MB) — FAT32, contains:
#     /tutorial-os-mars.bin
#     /uEnv.txt
#     /dtbs/  (full tree from blobs/dtbs/)
#
# USAGE:
#   ./build.sh milkv-mars && board/milkv-mars/mkimage.sh
#
# PREREQUISITES:
#   parted, mtools, dosfstools
#   Ubuntu/Debian: sudo apt install parted mtools dosfstools

set -e

BOARD="milkv-mars"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${REPO_ROOT}/output/${BOARD}"
BOARD_DIR="${SCRIPT_DIR}"

KERNEL_BIN="${BUILD_DIR}/kernel.bin"
UENV_TXT="${BOARD_DIR}/uEnv.txt"
OUTPUT_IMG="${BUILD_DIR}/${BOARD}.img"
DTBS_DIR="${REPO_ROOT}/soc/jh7110/blobs/dtbs"

# GPT layout — sector offsets match official image so U-Boot finds partition 3
PART1_START=4096;  PART1_END=8191
PART2_START=8192;  PART2_END=16383
FAT_START=16384;   FAT_SIZE_MB=100
FAT_END=$(( FAT_START + (FAT_SIZE_MB * 1024 * 1024 / 512) - 1 ))
IMG_SIZE_MB=$(( (FAT_END * 512 / 1024 / 1024) + 4 ))

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info() { echo -e "  ${CYAN}INFO${NC}  $*"; }
ok()   { echo -e "  ${GREEN} OK ${NC}  $*"; }
warn() { echo -e "  ${YELLOW}WARN${NC}  $*"; }
fail() { echo -e "  ${RED}FAIL${NC}  $*"; exit 1; }

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}  Milk-V Mars SD Image Builder (Rust)${NC}"
echo -e "${GREEN}  SoC: StarFive JH7110 (RISC-V 64)${NC}"
echo -e "${GREEN}  Boot mode: SPI Flash${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""

# Preflight: tools
MISSING=0
for tool in parted mformat mcopy mmd dd; do
    command -v "$tool" &>/dev/null || { echo -e "  ${RED}MISS${NC}  '$tool'"; MISSING=1; }
done
[ $MISSING -ne 0 ] && { echo "  sudo apt install parted mtools dosfstools"; exit 1; }

# Preflight: kernel
[ -f "${KERNEL_BIN}" ] || fail "Kernel not found at ${KERNEL_BIN}. Run './build.sh milkv-mars' first."
KERNEL_SIZE=$(stat -c%s "${KERNEL_BIN}")
ok "Kernel:  $(( KERNEL_SIZE / 1024 )) KB"

# Preflight: uEnv.txt
[ -f "${UENV_TXT}" ] || fail "uEnv.txt not found: ${UENV_TXT}"
ok "uEnv.txt found"

# Preflight: DTBs
if [ -d "${DTBS_DIR}" ]; then
    DTB_COUNT=$(find "${DTBS_DIR}" -name "*.dtb" | wc -l)
    ok "DTBs: ${DTB_COUNT} files in blobs/dtbs/"
else
    warn "DTBs directory not found: ${DTBS_DIR}"
    warn "Using \$fdtcontroladdr from U-Boot (built-in DTB)"
fi

echo ""

# Step 1: Blank image
info "Creating blank ${IMG_SIZE_MB} MB image..."
dd if=/dev/zero of="${OUTPUT_IMG}" bs=1M count=${IMG_SIZE_MB} status=none
ok "Blank image: ${OUTPUT_IMG}"

# Step 2: GPT partition table
info "Creating GPT partition table..."
parted -s "${OUTPUT_IMG}" mklabel gpt
parted -s --align none "${OUTPUT_IMG}" mkpart bbl  "${PART1_START}s" "${PART1_END}s"
parted -s --align none "${OUTPUT_IMG}" mkpart fsbl "${PART2_START}s" "${PART2_END}s"
parted -s --align none "${OUTPUT_IMG}" mkpart boot fat32 "${FAT_START}s" "${FAT_END}s"
ok "GPT: bbl(dummy) fsbl(dummy) boot(FAT32 ${FAT_SIZE_MB}MB @ p3)"

# Step 3: Format FAT32 and populate
FAT_OFFSET=$(( FAT_START * 512 ))
info "Formatting FAT32 partition 3..."

export MTOOLSRC="${BUILD_DIR}/.mtoolsrc"
cat > "${MTOOLSRC}" << EOF
drive c: file="${OUTPUT_IMG}" offset=${FAT_OFFSET}
mtools_skip_check=1
EOF

mformat -F -v "MARS-BOOT" c: 2>/dev/null
ok "FAT32 formatted"

# Rename kernel binary to match uEnv.txt expectation
mcopy "${KERNEL_BIN}" c:tutorial-os-mars.bin
ok "Kernel binary copied"

mcopy "${UENV_TXT}" c:uEnv.txt
ok "uEnv.txt copied"

if [ -d "${DTBS_DIR}" ]; then
    info "Mirroring dtbs/ tree..."
    mmd c:/dtbs 2>/dev/null || true
    find "${DTBS_DIR}" -type d | sort | while read -r dir; do
        rel="${dir#${DTBS_DIR}}"
        [ -n "$rel" ] && mmd "c:/dtbs${rel}" 2>/dev/null || true
    done
    find "${DTBS_DIR}" -type f | sort | while read -r file; do
        rel="${file#${DTBS_DIR}/}"
        mcopy "${file}" "c:/dtbs/${rel}" 2>/dev/null || warn "Could not copy: ${rel}"
    done
    ok "DTB tree mirrored"
fi

rm -f "${MTOOLSRC}"

# Summary
echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}  Image created: ${OUTPUT_IMG}${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""
echo "  Image size:  $(( $(stat -c%s "${OUTPUT_IMG}") / 1024 / 1024 )) MB"
echo "  Kernel size: $(( KERNEL_SIZE / 1024 )) KB"
echo ""
echo "Flash to SD card:"
echo "  sudo dd if=${OUTPUT_IMG} of=/dev/sdX bs=4M status=progress && sync"
echo ""
echo "Serial console: pin 8 (TX), pin 10 (RX), pin 6 (GND) @ 115200"
echo ""
