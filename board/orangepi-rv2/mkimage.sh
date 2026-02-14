#!/bin/bash
# =============================================================================
# board/orangepi-rv2/mkimage.sh — Create Bootable SD Card Image
# =============================================================================
#
# Creates a complete, flashable SD card image for the Orange Pi RV2
# (SpacemiT Ky X1 SoC, RISC-V 64-bit).
#
# SPACEMIT KY X1 BOOT CHAIN:
#   1. SoC BROM reads bootinfo_sd.bin at byte 0 of SD card
#   2. bootinfo tells BROM where FSBL lives → BROM loads FSBL.bin
#   3. FSBL (U-Boot SPL) initializes DDR, then looks up GPT partitions BY NAME:
#        - "opensbi" partition → loads OpenSBI (fw_dynamic.itb)
#        - "uboot"   partition → loads U-Boot (u-boot.itb)
#   4. U-Boot imports env_k1-x.txt from boot partition root
#   5. env_k1-x.txt tells U-Boot to load tutorial-os-rv2.bin + DTB into RAM
#   6. U-Boot jumps to our kernel with DTB pointer in a1
#
# SD CARD LAYOUT (GPT with named partitions):
#
#   Byte offset | Sector | Content                | Source file
#   ------------|--------|------------------------|--------------------
#   0x000000    | 0      | bootinfo (80 bytes)    | bootinfo_sd.bin
#   0x000200    | 1      | GPT header             | (auto-generated)
#   0x040000    | 512    | fsbl partition (256K)   | FSBL.bin
#   0x080000    | 1024   | env partition (128K)    | (empty)
#   0x0A0000    | 1280   | opensbi partition (384K)| fw_dynamic.itb
#   0x100000    | 2048   | uboot partition (2M)    | u-boot.itb
#   0x400000    | 8192   | boot partition (FAT32)  | kernel + env_k1-x.txt
#
#   This layout matches the SpacemiT partition_universal.json used by
#   the vendor SDK and the Bianbu Linux images. The FSBL (SPL) finds
#   opensbi and uboot by their GPT partition names, so the names MUST
#   match exactly.
#
# USAGE:
#   make image BOARD=orangepi-rv2
#
# PREREQUISITES:
#   - parted, mtools, dosfstools
#   - Blobs in soc/kyx1/blobs/ (from uboot_soc_boards build):
#       bootinfo_sd.bin   — 80-byte BROM header
#       FSBL.bin           — U-Boot SPL + DDR init
#       fw_dynamic.itb     — OpenSBI firmware
#       u-boot.itb         — U-Boot proper (FIT image)
#   - Optional: DTB at board/orangepi-rv2/dtb/k1-x_orangepi-rv2.dtb
#
# BLOB SOURCE:
#   Build from the uboot_soc_boards project:
#     ./docker-build.sh --board orangepi-rv2
#   Then copy output/orangepi-riscv/ky-x1/orangepi-rv2-kyx1/* to soc/kyx1/blobs/
#
#   Or extract from a stock Orange Pi OS image:
#     dd if=stock.img of=bootinfo_sd.bin bs=1 count=80
#     dd if=stock.img of=FSBL.bin bs=512 skip=512 count=512
#     dd if=stock.img of=fw_dynamic.itb bs=512 skip=1280 count=768
#     dd if=stock.img of=u-boot.itb bs=512 skip=2048 count=4096
#
# =============================================================================

set -e

# ─────────────────────────────────────────────────────────────────────────────
# Configuration
# ─────────────────────────────────────────────────────────────────────────────

BOARD="${BOARD:-orangepi-rv2}"
SOC="${SOC:-kyx1}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build/${BOARD}}"
BLOBS_DIR="${REPO_ROOT}/soc/${SOC}/blobs"
BOARD_DIR="${SCRIPT_DIR}"

KERNEL_BIN="${BUILD_DIR}/tutorial-os-rv2.bin"
KERNEL_ELF="${BUILD_DIR}/kernel.elf"
ENV_TXT="${BOARD_DIR}/env_k1-x.txt"
OUTPUT_IMG="${BUILD_DIR}/${BOARD}.img"

# DTB — extracted from stock OS or U-Boot build
DTB_FILE="${BOARD_DIR}/dtb/k1-x_orangepi-rv2.dtb"
DTB_DIR_ON_SD="dtb/spacemit"

# SpacemiT blobs (from uboot_soc_boards or stock image)
BOOTINFO="${BLOBS_DIR}/bootinfo_sd.bin"
FSBL="${BLOBS_DIR}/FSBL.bin"
OPENSBI="${BLOBS_DIR}/fw_dynamic.itb"
UBOOT="${BLOBS_DIR}/u-boot.itb"

# ─────────────────────────────────────────────────────────────────────────────
# GPT Partition Layout (sectors, 512 bytes each)
#
# These match the SpacemiT partition_universal.json and the Bianbu Linux
# images. The FSBL (SPL) locates opensbi and uboot by GPT partition name,
# so the names are critical.
# ─────────────────────────────────────────────────────────────────────────────

FSBL_START=256        # 128 KiB
FSBL_END=767          # 256 KiB partition (512 sectors)
ENV_START=768         # 384 KiB
ENV_END=895           # 64 KiB partition (128 sectors)
OPENSBI_START=896     # 448 KiB
OPENSBI_END=1663      # 384 KiB partition (768 sectors)
UBOOT_START=1664      # 832 KiB
UBOOT_END=5759        # 2 MiB partition (4096 sectors)
BOOTFS_START=8192     # 4 MiB
BOOTFS_SIZE_MB=64     # 64 MiB for kernel + env_k1-x.txt + DTB

# Total image size must fit GPT backup + all partitions
IMG_SIZE_MB=80

# ─────────────────────────────────────────────────────────────────────────────
# Colors / helpers
# ─────────────────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "  ${CYAN}INFO${NC}  $*"; }
ok()    { echo -e "  ${GREEN} OK ${NC}  $*"; }
warn()  { echo -e "  ${YELLOW}WARN${NC}  $*"; }
fail()  { echo -e "  ${RED}FAIL${NC}  $*"; exit 1; }

# ─────────────────────────────────────────────────────────────────────────────
# Preflight checks
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}  Orange Pi RV2 SD Image Builder${NC}"
echo -e "${GREEN}  SoC: SpacemiT Ky X1 (RISC-V 64)${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""

# Check tools
MISSING=0
for tool in parted mformat mcopy dd; do
    if ! command -v $tool &>/dev/null; then
        echo -e "${RED}ERROR: '$tool' not found${NC}"
        MISSING=1
    fi
done
if [ $MISSING -ne 0 ]; then
    echo ""
    echo "Install missing tools:"
    echo "  Ubuntu/Debian: sudo apt install parted mtools dosfstools"
    exit 1
fi

# Check kernel
if [ ! -f "${KERNEL_BIN}" ]; then
    if [ -f "${KERNEL_ELF}" ]; then
        info "Generating flat binary from ELF..."
        riscv64-linux-gnu-objcopy -O binary "${KERNEL_ELF}" "${KERNEL_BIN}"
        ok "Created ${KERNEL_BIN}"
    else
        fail "Kernel not found: ${KERNEL_BIN} (run 'make BOARD=orangepi-rv2' first)"
    fi
fi

# Check env_k1-x.txt
if [ ! -f "${ENV_TXT}" ]; then
    fail "env_k1-x.txt not found: ${ENV_TXT}"
fi

# Check blobs
BLOB_OK=true
for blob_entry in "bootinfo_sd.bin:${BOOTINFO}" "FSBL.bin:${FSBL}" "u-boot.itb:${UBOOT}"; do
    name="${blob_entry%%:*}"
    path="${blob_entry#*:}"
    if [ -f "$path" ]; then
        ok "$name  $(stat -c%s "$path") bytes"
    else
        echo -e "  ${RED}MISS${NC}  $name  ($path)"
        BLOB_OK=false
    fi
done

# OpenSBI is handled specially — might be separate or bundled with u-boot
HAS_OPENSBI=false
if [ -f "${OPENSBI}" ]; then
    ok "fw_dynamic.itb  $(stat -c%s "${OPENSBI}") bytes"
    HAS_OPENSBI=true
elif [ -f "${BLOBS_DIR}/opensbi.itb" ]; then
    OPENSBI="${BLOBS_DIR}/opensbi.itb"
    ok "opensbi.itb  $(stat -c%s "${OPENSBI}") bytes"
    HAS_OPENSBI=true
elif [ -f "${BLOBS_DIR}/u-boot-opensbi.itb" ]; then
    # Vendor fork combined image — use it for BOTH partitions
    OPENSBI="${BLOBS_DIR}/u-boot-opensbi.itb"
    UBOOT="${BLOBS_DIR}/u-boot-opensbi.itb"
    ok "u-boot-opensbi.itb (combined)  $(stat -c%s "${OPENSBI}") bytes"
    HAS_OPENSBI=true
else
    warn "OpenSBI not found (fw_dynamic.itb / opensbi.itb)"
    warn "The opensbi partition will be empty."
    warn "This may work if the vendor U-Boot SPL uses combined loading."
fi

if [ "$BLOB_OK" = false ]; then
    echo ""
    echo "Missing blobs. Get them from your uboot_soc_boards build:"
    echo ""
    echo "  mkdir -p ${BLOBS_DIR}"
    echo "  cp output/orangepi-riscv/ky-x1/orangepi-rv2-kyx1/bootinfo_sd.bin ${BLOBS_DIR}/"
    echo "  cp output/orangepi-riscv/ky-x1/orangepi-rv2-kyx1/FSBL.bin         ${BLOBS_DIR}/"
    echo "  cp output/orangepi-riscv/ky-x1/orangepi-rv2-kyx1/fw_dynamic.itb   ${BLOBS_DIR}/"
    echo "  cp output/orangepi-riscv/ky-x1/orangepi-rv2-kyx1/u-boot.itb       ${BLOBS_DIR}/"
    echo ""
    echo "Or extract from a stock Orange Pi RV2 SD image:"
    echo ""
    echo "  dd if=stock.img of=bootinfo_sd.bin bs=1 count=80"
    echo "  dd if=stock.img of=FSBL.bin        bs=512 skip=${FSBL_START} count=$(( FSBL_END - FSBL_START + 1 ))"
    echo "  dd if=stock.img of=fw_dynamic.itb  bs=512 skip=${OPENSBI_START} count=$(( OPENSBI_END - OPENSBI_START + 1 ))"
    echo "  dd if=stock.img of=u-boot.itb      bs=512 skip=${UBOOT_START} count=$(( UBOOT_END - UBOOT_START + 1 ))"
    echo ""
    exit 1
fi

# DTB is optional — U-Boot may have a built-in DTB
HAS_DTB=false
if [ -f "${DTB_FILE}" ]; then
    ok "DTB: ${DTB_FILE}"
    HAS_DTB=true
else
    warn "DTB not found: ${DTB_FILE}"
    warn "U-Boot will pass its built-in DTB (should work for basic boot)."
fi

KERNEL_SIZE=$(stat -c%s "${KERNEL_BIN}")
info "Kernel: ${KERNEL_SIZE} bytes ($(( KERNEL_SIZE / 1024 )) KB)"
echo ""

mkdir -p "${BUILD_DIR}"

# =============================================================================
# Step 1: Create blank image
# =============================================================================

echo "  DD     ${OUTPUT_IMG} (${IMG_SIZE_MB}MB)"
dd if=/dev/zero of="${OUTPUT_IMG}" bs=1M count=${IMG_SIZE_MB} status=none

# =============================================================================
# Step 2: Create GPT with named partitions
#
# The partition NAMES are critical — the SpacemiT FSBL (U-Boot SPL) looks up
# "opensbi" and "uboot" by name in the GPT to find where to load them from.
# =============================================================================

echo "  GPT    Creating partition table with named partitions..."

BOOTFS_END_SECTOR=$(( BOOTFS_START + (BOOTFS_SIZE_MB * 1024 * 1024 / 512) - 1 ))

# --align none: firmware partitions MUST be at exact sector offsets matching
# the SpacemiT partition_universal.json, regardless of optimal alignment.
parted -s --align none "${OUTPUT_IMG}" mklabel gpt 2>/dev/null

# Firmware partitions (raw, no filesystem — blobs dd'd directly)
parted -s --align none "${OUTPUT_IMG}" mkpart fsbl    ${FSBL_START}s    ${FSBL_END}s    2>/dev/null
parted -s --align none "${OUTPUT_IMG}" mkpart env     ${ENV_START}s     ${ENV_END}s     2>/dev/null
parted -s --align none "${OUTPUT_IMG}" mkpart opensbi ${OPENSBI_START}s ${OPENSBI_END}s 2>/dev/null
parted -s --align none "${OUTPUT_IMG}" mkpart uboot   ${UBOOT_START}s   ${UBOOT_END}s  2>/dev/null

# Boot filesystem partition (FAT32 with kernel + env_k1-x.txt)
parted -s --align none "${OUTPUT_IMG}" mkpart boot fat32 ${BOOTFS_START}s ${BOOTFS_END_SECTOR}s 2>/dev/null

ok "GPT: fsbl(256K) env(128K) opensbi(384K) uboot(2M) boot(${BOOTFS_SIZE_MB}M)"

# =============================================================================
# Step 3: Overlay bootinfo_sd.bin at byte 0
#
# The SpacemiT BROM reads the first 80 bytes of the SD card BEFORE looking at
# the GPT. This 80-byte header tells the BROM where FSBL lives and includes
# its size and checksum.
#
# It coexists with the GPT protective MBR because:
#   - bootinfo occupies bytes 0-79 (boot code area of MBR)
#   - GPT protective MBR partition entry is at bytes 446-461
#   - MBR signature (55 AA) is at bytes 510-511
#   - No overlap — BROM reads its 80 bytes, GPT parser reads its fields.
# =============================================================================

echo "  DD     bootinfo_sd.bin @ byte 0 (80 bytes, before GPT header)"
dd if="${BOOTINFO}" of="${OUTPUT_IMG}" bs=1 count=80 conv=notrunc status=none

# =============================================================================
# Step 4: Write firmware blobs to their GPT partitions
# =============================================================================

echo "  DD     FSBL.bin → fsbl partition (sector ${FSBL_START})"
dd if="${FSBL}" of="${OUTPUT_IMG}" bs=512 seek=${FSBL_START} conv=notrunc status=none

if [ "$HAS_OPENSBI" = true ]; then
    echo "  DD     OpenSBI → opensbi partition (sector ${OPENSBI_START})"
    dd if="${OPENSBI}" of="${OUTPUT_IMG}" bs=512 seek=${OPENSBI_START} conv=notrunc status=none
fi

echo "  DD     u-boot.itb → uboot partition (sector ${UBOOT_START})"
dd if="${UBOOT}" of="${OUTPUT_IMG}" bs=512 seek=${UBOOT_START} conv=notrunc status=none

# =============================================================================
# Step 5: Format boot partition as FAT32 and copy kernel files
#
# Uses mtools (no root required) to create the filesystem and copy files
# directly into the image at the boot partition offset.
#
# Boot partition root layout:
#   /tutorial-os-rv2.bin          — flat binary kernel
#   /env_k1-x.txt                — U-Boot environment (boot commands)
#   /dtb/spacemit/<board>.dtb    — device tree (optional)
# =============================================================================

BOOTFS_OFFSET=$(( BOOTFS_START * 512 ))

echo "  MFMT   FAT32 on boot @ offset ${BOOTFS_OFFSET}"

export MTOOLSRC="${BUILD_DIR}/.mtoolsrc"
cat > "${MTOOLSRC}" << EOF
drive c: file="${OUTPUT_IMG}" offset=${BOOTFS_OFFSET}
mtools_skip_check=1
EOF

mformat -F c: 2>/dev/null

echo "  MCOPY  tutorial-os-rv2.bin"
mcopy "${KERNEL_BIN}" c:tutorial-os-rv2.bin

echo "  MCOPY  env_k1-x.txt"
mcopy "${ENV_TXT}" c:env_k1-x.txt

if [ "$HAS_DTB" = true ]; then
    echo "  MCOPY  ${DTB_DIR_ON_SD}/$(basename "${DTB_FILE}")"
    mmd c:/dtb 2>/dev/null || true
    mmd c:/dtb/spacemit 2>/dev/null || true
    mcopy "${DTB_FILE}" "c:/${DTB_DIR_ON_SD}/$(basename "${DTB_FILE}")"
fi

rm -f "${MTOOLSRC}"

# =============================================================================
# Step 6: Create deploy directory (loose files for manual SD card setup)
# =============================================================================

DEPLOY_DIR="${BUILD_DIR}/boot"
rm -rf "${DEPLOY_DIR}"
mkdir -p "${DEPLOY_DIR}"

cp "${KERNEL_BIN}" "${DEPLOY_DIR}/"
cp "${ENV_TXT}" "${DEPLOY_DIR}/env_k1-x.txt"

if [ "$HAS_DTB" = true ]; then
    mkdir -p "${DEPLOY_DIR}/${DTB_DIR_ON_SD}"
    cp "${DTB_FILE}" "${DEPLOY_DIR}/${DTB_DIR_ON_SD}/"
fi

# =============================================================================
# Summary
# =============================================================================

IMG_SIZE_ACTUAL=$(stat -c%s "${OUTPUT_IMG}")

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}  Image created: ${OUTPUT_IMG}${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""
echo "  Image size:  $(( IMG_SIZE_ACTUAL / 1024 / 1024 )) MB"
echo "  Kernel size: $(( KERNEL_SIZE / 1024 )) KB"
echo ""
echo "  GPT partitions:"
echo "    1  fsbl     256K  @ sector ${FSBL_START}     FSBL.bin (SPL + DDR init)"
echo "    2  env      128K  @ sector ${ENV_START}     (U-Boot environment)"
echo "    3  opensbi  384K  @ sector ${OPENSBI_START}     fw_dynamic.itb"
echo "    4  uboot       2M  @ sector ${UBOOT_START}     u-boot.itb"
echo "    5  boot    ${BOOTFS_SIZE_MB}M  @ sector ${BOOTFS_START}     FAT32 (kernel + env_k1-x.txt)"
echo ""
echo "Flash to SD card:"
echo ""
echo "  sudo dd if=${OUTPUT_IMG} of=/dev/sdX bs=4M status=progress"
echo "  sync"
echo ""
echo "Serial console: UART0 (GPIO pins 8/10) @ 115200 baud"
echo ""
echo "  screen /dev/ttyUSB0 115200"
echo ""

# Copy to output if running in Docker
if [ -d "/output" ]; then
    info "Copying to /output for Docker export..."
    mkdir -p "/output/orangepi-rv2"
    cp "${OUTPUT_IMG}" "/output/orangepi-rv2/"
    cp -r "${DEPLOY_DIR}" "/output/orangepi-rv2/boot-files/"
    cp "${KERNEL_ELF}" "/output/orangepi-rv2/" 2>/dev/null || true
    ok "Output: /output/orangepi-rv2/"
fi