#!/usr/bin/env python3
# =============================================================================
# board/lattepanda-mu/mkimage.py
# Create a bootable FAT32 disk image containing BOOTX64.EFI
#
# Usage:
#   python3 mkimage.py <efi_binary> <output_image> [size_mb]
#
# Example (called by the Makefile's `image` target):
#   python3 board/lattepanda-mu/mkimage.py \
#       build/lattepanda-mu/BOOTX64.EFI   \
#       build/lattepanda-mu/tutorial-os-lp-mu.img \
#       64
#
# =============================================================================
# WHY PYTHON INSTEAD OF BASH
# =============================================================================
#
# Every other board in Tutorial-OS uses a bash mkimage.sh. The LattePanda MU
# is the exception, and for good reason.
#
# The other boards write U-Boot boot blobs to raw sector offsets using `dd`.
# That is a partition layout problem — copy this many bytes to that offset —
# and dd is exactly the right tool for it.
#
# The LattePanda MU image is a FAT32 filesystem layout problem. UEFI firmware
# does not look for a raw binary at a sector offset; it walks a real FAT32
# filesystem looking for \EFI\BOOT\BOOTX64.EFI. Building that correctly
# requires three things bash cannot do cleanly:
#
#   1. BINARY STRUCT PACKING FOR THE MBR PARTITION ENTRY
#      The MBR partition table entry is a 16-byte binary record with a
#      specific layout: status byte, CHS start (3 bytes), partition type
#      byte, CHS end (3 bytes), LBA start (4 bytes LE), sector count
#      (4 bytes LE). Python's struct.pack('<BBBBBBBBII', ...) writes this
#      as a single, readable, correct call. The bash equivalent is a chain
#      of `printf '\xNN'` redirects and `dd seek=` invocations — fragile,
#      unreadable, and easy to get wrong by one byte.
#
#   2. CHS GEOMETRY CALCULATION
#      MBR entries encode start and end addresses in both CHS (Cylinder/
#      Head/Sector) and LBA format. CHS is vestigial on modern hardware
#      but the MBR spec requires it. The calculation involves integer
#      division and bit packing across three bytes. Bash integer arithmetic
#      can technically do this but produces code that requires a comment
#      three times longer than the code itself to be safe to maintain.
#
#   3. COMPUTED BYTE OFFSETS IN TEMPORARY CONFIG FILES
#      mtools needs to know the byte offset of the FAT32 partition within
#      the image file (FAT_START * SECTOR = 1,048,576 bytes). This is a
#      derived value, not a constant. Generating the mtoolsrc config file
#      with the correct computed offset is natural in Python and awkward
#      in bash, especially when the offset needs to change with image size.
#
# The mtools calls (mformat, mmd, mcopy) are identical to what bash would
# do — we just invoke them via subprocess. Python is handling the binary
# math and config generation; mtools is doing the actual FAT32 work.
#
# =============================================================================

import sys
import os
import struct
import subprocess
import tempfile

# =============================================================================
# Arguments
# =============================================================================

if len(sys.argv) < 3:
    print(f"Usage: {sys.argv[0]} <efi_binary> <output_image> [size_mb]")
    sys.exit(1)

efi_src  = sys.argv[1]
img_path = sys.argv[2]
img_mb   = int(sys.argv[3]) if len(sys.argv) > 3 else 64

if not os.path.isfile(efi_src):
    print(f"ERROR: EFI binary not found: {efi_src}")
    sys.exit(1)

# =============================================================================
# Layout constants
# =============================================================================
#
# FAT_START at sector 2048 (1 MiB) is the standard alignment for MBR
# partitions on modern systems. It keeps the partition 512-byte and
# 4096-byte aligned simultaneously, which matters for flash storage.
#
# The UEFI spec only requires \EFI\BOOT\BOOTX64.EFI to exist on a FAT
# partition — it doesn't care about MBR vs GPT or partition alignment.
# MBR is simpler to write from scratch, so we use it here.

SECTOR     = 512
TOTAL_SECS = img_mb * 1024 * 1024 // SECTOR
FAT_START  = 2048                    # sectors — 1 MiB aligned
FAT_OFFSET = FAT_START * SECTOR      # bytes into the image file
FAT_SIZE   = TOTAL_SECS - FAT_START  # sectors available for FAT32

# =============================================================================
# Step 1 — Blank image
# =============================================================================

print(f"[lattepanda-iota mkimage]")
print(f"  EFI source : {efi_src} ({os.path.getsize(efi_src):,} bytes)")
print(f"  Output     : {img_path}")
print(f"  Image size : {img_mb} MiB ({TOTAL_SECS:,} sectors)")
print(f"  FAT32 start: sector {FAT_START} (offset {FAT_OFFSET:,} bytes)")
print()

print("  [1/4] Creating blank image...")
with open(img_path, 'wb') as f:
    f.write(bytes(img_mb * 1024 * 1024))
print(f"        {img_path} ({img_mb} MiB)")

# =============================================================================
# Step 2 — MBR partition table
# =============================================================================
#
# The MBR partition entry layout (16 bytes, all little-endian):
#
#   Offset  Size  Field
#   ------  ----  -----
#   0       1     Status (0x80 = bootable)
#   1       3     CHS of first sector
#   4       1     Partition type (0x0C = FAT32 LBA)
#   5       3     CHS of last sector
#   8       4     LBA of first sector
#   12      4     Number of sectors
#
# CHS values are capped at (0xFE, 0xFF, 0xFF) for LBA-addressed partitions
# larger than 8 GiB — the UEFI firmware ignores them entirely and uses the
# LBA fields, but they must be present and structurally valid.

def lba_to_chs(lba):
    """Convert LBA address to CHS tuple for MBR partition entry.

    Uses standard BIOS geometry (255 heads, 63 sectors/track).
    Returns (0xFE, 0xFF, 0xFF) for addresses beyond CHS range,
    which is correct per the MBR spec for LBA-addressed partitions.
    """
    if lba >= 1024 * 255 * 63:
        return (0xFE, 0xFF, 0xFF)
    c = lba // (255 * 63)
    r = lba % (255 * 63)
    h = r // 63
    s = (r % 63) + 1
    # CHS byte packing: H=full byte, S=[5:0] + C[9:8] in [7:6], C=[7:0]
    return (h & 0xFF, (s & 0x3F) | ((c >> 2) & 0xC0), c & 0xFF)

print("  [2/4] Writing MBR partition table...")

h1, s1, c1 = lba_to_chs(FAT_START)
h2, s2, c2 = lba_to_chs(FAT_START + FAT_SIZE - 1)

# struct.pack format: < = little-endian
#   B  status       (0x80 = bootable)
#   B  CHS start H
#   B  CHS start S
#   B  CHS start C
#   B  type         (0x0C = FAT32 with LBA addressing)
#   B  CHS end H
#   B  CHS end S
#   B  CHS end C
#   I  LBA start    (4 bytes, little-endian)
#   I  sector count (4 bytes, little-endian)
partition_entry = struct.pack(
    '<BBBBBBBBII',
    0x80,             # bootable
    h1, s1, c1,       # CHS start
    0x0C,             # FAT32 LBA
    h2, s2, c2,       # CHS end
    FAT_START,        # LBA start
    FAT_SIZE          # sector count
)

with open(img_path, 'r+b') as f:
    f.seek(0x1BE)           # MBR partition table starts at byte 446
    f.write(partition_entry)
    f.write(bytes(48))      # remaining 3 partition entries — clear to zero
    f.seek(510)
    f.write(b'\x55\xAA')   # MBR signature

print(f"        FAT32 LBA partition: start={FAT_START}, "
      f"size={FAT_SIZE} sectors ({FAT_SIZE * SECTOR // (1024*1024)} MiB)")

# =============================================================================
# Step 3 — Format FAT32 and install EFI binary using mtools
# =============================================================================
#
# mtools works directly on image files without requiring a loop device or
# root privileges. It needs to know where the FAT32 filesystem starts within
# the image — that's the `offset=` value in the mtoolsrc config.
#
# We generate the config dynamically because FAT_OFFSET is a computed value
# that changes if img_mb changes. A static bash heredoc would hardcode it.

print("  [3/4] Formatting FAT32 and copying EFI binary...")

mtoolsrc = img_path + '.mtoolsrc'
with open(mtoolsrc, 'w') as f:
    f.write('mtools_skip_check=1\n')
    f.write(f'drive c: file="{os.path.abspath(img_path)}" offset={FAT_OFFSET}\n')

env = os.environ.copy()
env['MTOOLSRC'] = mtoolsrc

def run(cmd, label=None):
    """Run a subprocess, exit with error on failure."""
    if label:
        print(f"        {label}")
    result = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"\n  ERROR running: {' '.join(cmd)}")
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)
        os.unlink(mtoolsrc)
        sys.exit(1)
    return result

run(['mformat', '-F', '-v', 'LPEFI', 'c:'],      label='mformat (FAT32, label=LPEFI)')
run(['mmd', 'c:/EFI'],                            label='mmd EFI/')
run(['mmd', 'c:/EFI/BOOT'],                       label='mmd EFI/BOOT/')
run(['mcopy', efi_src, 'c:/EFI/BOOT/BOOTX64.EFI'],
    label=f'mcopy -> EFI/BOOT/BOOTX64.EFI')

# =============================================================================
# Step 4 — Verify
# =============================================================================

print()
print("  [4/4] Verifying EFI/BOOT/ contents...")
result = subprocess.run(
    ['mdir', 'c:/EFI/BOOT/'],
    env=env, capture_output=True, text=True
)
for line in result.stdout.strip().splitlines():
    print(f"        {line}")

os.unlink(mtoolsrc)

# Final summary
img_size = os.path.getsize(img_path)
efi_size = os.path.getsize(efi_src)
print()
print(f"  Image ready: {img_path}")
print(f"    Image:     {img_size // (1024*1024)} MiB ({img_size:,} bytes)")
print(f"    BOOTX64.EFI: {efi_size:,} bytes")
print()
print("  To write to SD card:")
print(f"    sudo dd if={img_path} of=/dev/sdX bs=1M status=progress")
print(f"    (replace /dev/sdX with your SD card device)")