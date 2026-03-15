#!/bin/bash
# board/lattepanda-iota/mkimage.sh
# Shim that delegates to mkimage.py — called by `make image BOARD=lattepanda-iota`
# The Makefile sets BOARD, SOC, and BUILD_DIR before calling this script.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EFI_BIN="${BUILD_DIR}/BOOTX64.EFI"
OUT_IMG="${BUILD_DIR}/tutorial-os-lp-iota.img"

if [ ! -f "${EFI_BIN}" ]; then
    echo "ERROR: EFI binary not found: ${EFI_BIN}"
    echo "       Run 'make BOARD=lattepanda-iota' first"
    exit 1
fi

python3 "${SCRIPT_DIR}/mkimage.py" "${EFI_BIN}" "${OUT_IMG}" 64