#!/bin/bash
# =============================================================================
# board/milkv-mars/mkimage.sh — Create Bootable SD Card Image
# =============================================================================
#
# Creates a complete, flashable SD card image for the Milk-V Mars
# (StarFive JH7110 SoC, RISC-V 64-bit) in SPI FLASH BOOT MODE.
#
# BOOT MODE CONTEXT:
#   The Mars has hardware boot mode pins (RGPIO_0, RGPIO_1).
#   When set to SPI flash mode (RGPIO_0=0, RGPIO_1=0):
#     - U-Boot SPL and firmware load from QSPI NOR flash on the SoM
#     - SD card is only needed for the kernel + uEnv.txt + DTBs
#     - NO firmware blobs needed on the SD card
#
#   When set to SDIO mode (RGPIO_0=1, RGPIO_1=0):
#     - Everything loads from SD card including SPL and U-Boot
#     - Requires partition1.bin (BBL) and partition2.bin (FSBL) blobs
#     - Use the official Milk-V Debian image approach instead
#
# WHY WE STILL NEED GPT WITH 3 PARTITIONS:
#   U-Boot (loaded from QSPI) expects the kernel on mmc 0:3 — partition 3.
#   Even in SPI boot mode, U-Boot looks for the FAT boot partition at
#   partition 3 specifically. We create dummy empty partitions 1 and 2
#   to push our FAT32 to the correct partition number.
#   No content is written to partitions 1 or 2.
#
# SD CARD LAYOUT:
#   Partition 1 (2MB)   — dummy, matches HiFive BBL slot  (empty)
#   Partition 2 (4MB)   — dummy, matches HiFive FSBL slot (empty)
#   Partition 3 (100MB) — FAT32, contains:
#     /tutorial-os-mars.bin
#     /uEnv.txt
#     /dtbs/  (full tree from soc/jh7110/blobs/dtbs/)
#
# BOOT SEQUENCE (SPI mode):
#   1. Power on → QSPI → U-Boot SPL → OpenSBI → U-Boot
#   2. U-Boot scans mmc 0:3 for uEnv.txt
#   3. uEnv.txt overrides bootcmd:
#        fatload mmc 0:3 0x44000000 tutorial-os-mars.bin
#        fatload mmc 0:3 0x48000000 dtbs/starfive/jh7110-visionfive-v2.dtb
#        go 0x44000000
#   4. Our _start runs with a0=hart_id, a1=DTB pointer
#
# USAGE:
#   make image BOARD=milkv-mars
#
# PREREQUISITES:
#   parted, mtools, dosfstools
#   Ubuntu/Debian: sudo apt install parted mtools dosfstools
#
# =============================================================================

set -e

# ─────────────────────────────────────────────────────────────────────────────
# Configuration
# ─────────────────────────────────────────────────────────────────────────────

BOARD="${BOARD:-milkv-mars}"
SOC="jh7110"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build/${BOARD}}"
BLOBS_DIR="${REPO_ROOT}/soc/${SOC}/blobs"
BOARD_DIR="${SCRIPT_DIR}"

KERNEL_BIN="${BUILD_DIR}/tutorial-os-mars.bin"
KERNEL_ELF="${BUILD_DIR}/kernel.elf"
UENV_TXT="${BOARD_DIR}/uEnv.txt"
OUTPUT_IMG="${BUILD_DIR}/${BOARD}.img"
DTBS_DIR="${BLOBS_DIR}/dtbs"

# GPT layout — sector offsets match official image so U-Boot finds partition 3
# Partitions 1 and 2 are empty placeholders (no content written)
PART1_START=4096;  PART1_END=8191    # 2MB dummy (BBL slot)
PART2_START=8192;  PART2_END=16383   # 4MB dummy (FSBL slot)
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
echo -e "${GREEN}  Milk-V Mars SD Image Builder${NC}"
echo -e "${GREEN}  SoC: StarFive JH7110 (RISC-V 64)${NC}"
echo -e "${GREEN}  Boot mode: SPI Flash${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Preflight: tools
# ─────────────────────────────────────────────────────────────────────────────

MISSING=0
for tool in parted mformat mcopy mmd dd; do
    command -v "$tool" &>/dev/null || { echo -e "  ${RED}MISS${NC}  '$tool'"; MISSING=1; }
done
[ $MISSING -ne 0 ] && { echo "  sudo apt install parted mtools dosfstools"; exit 1; }

# ─────────────────────────────────────────────────────────────────────────────
# Preflight: kernel
# ─────────────────────────────────────────────────────────────────────────────

if [ ! -f "${KERNEL_BIN}" ]; then
    [ -f "${KERNEL_ELF}" ] || fail "Kernel not found. Run 'make BOARD=milkv-mars' first."
    info "Generating flat binary from ELF..."
    riscv64-linux-gnu-objcopy -O binary "${KERNEL_ELF}" "${KERNEL_BIN}"
    ok "Created ${KERNEL_BIN}"
fi
KERNEL_SIZE=$(stat -c%s "${KERNEL_BIN}")
ok "Kernel:  $(( KERNEL_SIZE / 1024 )) KB"

# ─────────────────────────────────────────────────────────────────────────────
# Preflight: uEnv.txt
# ─────────────────────────────────────────────────────────────────────────────

[ -f "${UENV_TXT}" ] || fail "uEnv.txt not found: ${UENV_TXT}"
ok "uEnv.txt found"

# ─────────────────────────────────────────────────────────────────────────────
# Preflight: DTBs
# ─────────────────────────────────────────────────────────────────────────────

if [ -d "${DTBS_DIR}" ]; then
    DTB_COUNT=$(find "${DTBS_DIR}" -name "*.dtb" | wc -l)
    ok "DTBs: ${DTB_COUNT} files in blobs/dtbs/"
else
    warn "DTBs directory not found: ${DTBS_DIR}"
    warn "SimpleFB display init may fail without simple-framebuffer DTB node."
    warn "Copy dtbs/ from official image FAT partition to ${DTBS_DIR}"
fi

echo ""
mkdir -p "${BUILD_DIR}"

# =============================================================================
# Step 1: Blank image
# =============================================================================

info "Creating blank ${IMG_SIZE_MB} MB image..."
dd if=/dev/zero of="${OUTPUT_IMG}" bs=1M count=${IMG_SIZE_MB} status=none
ok "Blank image: ${OUTPUT_IMG}"

# =============================================================================
# Step 2: GPT partition table
#
# Partitions 1 and 2 are empty placeholders — their sector positions match
# the official image layout so U-Boot partition numbering is correct.
# No content is written to them in SPI boot mode.
# =============================================================================

info "Creating GPT partition table..."
parted -s "${OUTPUT_IMG}" mklabel gpt
parted -s --align none "${OUTPUT_IMG}" mkpart bbl  "${PART1_START}s" "${PART1_END}s"
parted -s --align none "${OUTPUT_IMG}" mkpart fsbl "${PART2_START}s" "${PART2_END}s"
parted -s --align none "${OUTPUT_IMG}" mkpart boot fat32 "${FAT_START}s" "${FAT_END}s"
ok "GPT: bbl(dummy 2MB) fsbl(dummy 4MB) boot(FAT32 ${FAT_SIZE_MB}MB @ p3)"

# =============================================================================
# Step 3: Format FAT32 boot partition (partition 3) and populate
# =============================================================================

FAT_OFFSET=$(( FAT_START * 512 ))
info "Formatting FAT32 partition 3 at offset ${FAT_OFFSET}..."

export MTOOLSRC="${BUILD_DIR}/.mtoolsrc"
cat > "${MTOOLSRC}" << EOF
drive c: file="${OUTPUT_IMG}" offset=${FAT_OFFSET}
mtools_skip_check=1
EOF

mformat -F -v "MARS-BOOT" c: 2>/dev/null
ok "FAT32 formatted (label: MARS-BOOT)"

info "Copying tutorial-os-mars.bin..."
mcopy "${KERNEL_BIN}" c:tutorial-os-mars.bin
ok "Kernel binary copied"

info "Copying uEnv.txt..."
mcopy "${UENV_TXT}" c:uEnv.txt
ok "uEnv.txt copied"

# Mirror full dtbs/ tree
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

    ok "DTB tree mirrored (${DTB_COUNT} DTBs)"
fi

rm -f "${MTOOLSRC}"

# =============================================================================
# Step 4: Verify FAT partition contents
# =============================================================================

echo ""
info "Verifying FAT partition contents..."
export MTOOLSRC="${BUILD_DIR}/.mtoolsrc"
cat > "${MTOOLSRC}" << EOF
drive c: file="${OUTPUT_IMG}" offset=${FAT_OFFSET}
mtools_skip_check=1
EOF
mdir c: 2>/dev/null | sed 's/^/    /'
rm -f "${MTOOLSRC}"

# =============================================================================
# Step 5: Deploy directory (loose files for manual SD card update)
# =============================================================================

DEPLOY_DIR="${BUILD_DIR}/boot"
rm -rf "${DEPLOY_DIR}"
mkdir -p "${DEPLOY_DIR}"
cp "${KERNEL_BIN}" "${DEPLOY_DIR}/"
cp "${UENV_TXT}" "${DEPLOY_DIR}/uEnv.txt"
[ -d "${DTBS_DIR}" ] && cp -r "${DTBS_DIR}" "${DEPLOY_DIR}/dtbs"
ok "Deploy files: ${DEPLOY_DIR}/"

if [ -d "/output" ]; then
    mkdir -p "/output/milkv-mars"
    cp "${OUTPUT_IMG}" "/output/milkv-mars/"
    cp -r "${DEPLOY_DIR}" "/output/milkv-mars/boot-files/"
    cp "${KERNEL_ELF}" "/output/milkv-mars/" 2>/dev/null || true
    ok "Docker output: /output/milkv-mars/"
fi

# =============================================================================
# Summary
# =============================================================================

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}  Image created: ${OUTPUT_IMG}${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""
echo "  Image size:  $(( $(stat -c%s "${OUTPUT_IMG}") / 1024 / 1024 )) MB"
echo "  Kernel size: $(( KERNEL_SIZE / 1024 )) KB"
echo ""
echo "  GPT layout:"
echo "    1  bbl   2MB  @ sector ${PART1_START}  (empty placeholder)"
echo "    2  fsbl  4MB  @ sector ${PART2_START}  (empty placeholder)"
echo "    3  boot  ${FAT_SIZE_MB}MB  @ sector ${FAT_START}  FAT32 (kernel + uEnv.txt + dtbs/)"
echo ""
echo "  Boot mode: SPI Flash (RGPIO_0=0, RGPIO_1=0)"
echo "  U-Boot loads from QSPI, kernel loads from SD partition 3"
echo ""
echo "Flash to SD card:"
echo "  sudo dd if=${OUTPUT_IMG} of=/dev/sdX bs=4M status=progress && sync"
echo "  Or: Balena Etcher → flash ${BOARD}.img directly"
echo ""
echo "Serial console: pin 8 (TX), pin 10 (RX), pin 6 (GND) @ 115200"
echo ""