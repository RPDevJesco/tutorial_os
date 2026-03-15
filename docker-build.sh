#!/bin/bash
#
# docker-build.sh - Build script for Tutorial-OS Docker container
#
# Usage (inside container, called via ENTRYPOINT):
#   build.sh              # Build all boards
#   build.sh all          # Build all boards
#   build.sh lattepanda-mu       # Build specific board (compile only)
#   build.sh lattepanda-mu image # Build + create flashable .img
#   build.sh bash         # Drop into shell (for debugging)

set -e

# =============================================================================
# Configuration
# =============================================================================

ALL_BOARDS=(
    "rpi-zero2w-gpi"
    "rpi-cm4-io"
    "rpi-cm5-io"
    "orangepi-rv2"
    "milkv-mars"
    "lattepanda-mu"
    "lattepanda-iota"
)

OUTPUT_DIR="/output"
SRC_DIR="/src"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $1"; }
log_header()  {
    echo ""
    echo -e "${YELLOW}---------------------------------------------------------------${NC}"
    echo -e "${YELLOW}  $1${NC}"
    echo -e "${YELLOW}---------------------------------------------------------------${NC}"
    echo ""
}

show_toolchain_info() {
    log_header "Toolchain Information"
    echo "ARM64:  $(aarch64-none-elf-gcc --version | head -1)"
    echo "RISC-V: $(riscv64-linux-gnu-gcc --version | head -1)"
    echo "x86_64: $(x86_64-linux-gnu-gcc --version | head -1)"
    echo ""
}

build_board() {
    local board=$1
    local make_image=${2:-false}
    local board_output_dir="${OUTPUT_DIR}/${board}"

    log_header "Building: ${board}"

    log_info "Cleaning previous build..."
    make clean BOARD="${board}" 2>/dev/null || true

    log_info "Compiling..."
    if ! make BOARD="${board}"; then
        log_error "Build failed for ${board}"
        return 1
    fi

    mkdir -p "${board_output_dir}"

    local kernel_name
    kernel_name=$(make -s BOARD="${board}" print-KERNEL_NAME 2>/dev/null)

    if [[ -z "${kernel_name}" ]]; then
        log_error "Could not determine KERNEL_NAME for ${board}"
        return 1
    fi

    # Copy primary kernel output
    if [[ -f "build/${board}/${kernel_name}" ]]; then
        cp "build/${board}/${kernel_name}" "${board_output_dir}/"
        log_info "Copied ${kernel_name} to ${board_output_dir}/"
    fi

    # Copy ELF for debugging
    if [[ -f "build/${board}/kernel.elf" ]]; then
        cp "build/${board}/kernel.elf" "${board_output_dir}/"
    fi

    # Create flashable image if requested or if this is a UEFI board
    local build_mode
    build_mode=$(make -s BOARD="${board}" print-BUILD_MODE 2>/dev/null || echo "")

    if [[ "${make_image}" == "true" ]] || [[ "${build_mode}" == "uefi" ]]; then
        log_info "Creating flashable image..."
        if make image BOARD="${board}"; then
            # Copy .img output
            local img_file
            img_file=$(find "build/${board}" -name "*.img" | head -1)
            if [[ -n "${img_file}" ]]; then
                cp "${img_file}" "${board_output_dir}/"
                log_info "Copied $(basename ${img_file}) to ${board_output_dir}/"
            fi
        else
            log_error "Image creation failed for ${board} (build still succeeded)"
        fi
    fi

    echo ""
    log_info "Output files:"
    ls -lh "${board_output_dir}/"

    log_success "Build complete for ${board}"
    return 0
}

build_all() {
    local failed_boards=()
    local success_boards=()

    log_header "Building All Boards"

    for board in "${ALL_BOARDS[@]}"; do
        if build_board "${board}"; then
            success_boards+=("${board}")
        else
            failed_boards+=("${board}")
        fi
    done

    log_header "Build Summary"

    if [[ ${#success_boards[@]} -gt 0 ]]; then
        echo -e "${GREEN}Successful builds:${NC}"
        for board in "${success_boards[@]}"; do
            echo "  ✓ ${board}"
        done
    fi

    if [[ ${#failed_boards[@]} -gt 0 ]]; then
        echo ""
        echo -e "${RED}Failed builds:${NC}"
        for board in "${failed_boards[@]}"; do
            echo "  ✗ ${board}"
        done
        return 1
    fi

    echo ""
    log_info "Output directory:"
    find "${OUTPUT_DIR}" -type f | sort | while read -r file; do
        size=$(ls -lh "$file" | awk '{print $5}')
        echo "  ${file} (${size})"
    done

    return 0
}

show_usage() {
    echo "Tutorial-OS Docker Build Script"
    echo ""
    echo "Usage: build.sh [board] [image]"
    echo ""
    echo "  build.sh                  Build all boards"
    echo "  build.sh all              Build all boards"
    echo "  build.sh <board>          Build specific board"
    echo "  build.sh <board> image    Build + create flashable .img"
    echo "  build.sh bash             Interactive shell"
    echo ""
    echo "Available boards:"
    for board in "${ALL_BOARDS[@]}"; do
        echo "  - ${board}"
    done
    echo ""
    echo "Examples:"
    echo "  build.sh lattepanda-mu image   # Build EFI + FAT32 .img"
    echo "  build.sh rpi-cm5-io            # Build Pi CM5"
}

# =============================================================================
# Main
# =============================================================================

cd "${SRC_DIR}"
show_toolchain_info

if [[ $# -eq 0 ]] || [[ "$1" == "all" ]]; then
    build_all
elif [[ "$1" == "bash" ]] || [[ "$1" == "shell" ]]; then
    log_info "Starting interactive shell..."
    exec /bin/bash
elif [[ "$1" == "help" ]] || [[ "$1" == "-h" ]]; then
    show_usage
else
    board="$1"
    make_image=false
    [[ "${2:-}" == "image" ]] && make_image=true

    if [[ ! -d "board/${board}" ]]; then
        log_error "Unknown board: ${board}"
        echo ""
        show_usage
        exit 1
    fi

    build_board "${board}" "${make_image}"
fi