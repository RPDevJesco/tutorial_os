#!/bin/bash
#
# docker-build.sh - Build script for Tutorial-OS Docker container
#
# Usage:
#   build.sh              # Build all boards
#   build.sh all          # Build all boards
#   build.sh rpi-zero2w-gpi    # Build specific board
#   build.sh rpi-zero2w-gpi rpi-cm4-io  # Build multiple boards
#   build.sh bash         # Drop into shell (for debugging)

set -e

# =============================================================================
# Configuration
# =============================================================================

# All available boards
ALL_BOARDS=(
    "rpi-zero2w-gpi"
    "rpi-cm4-io"
    "rpi-cm5-io"
    "radxa-rock2a"
    "libre-la-potato"
)

# Output directory (mounted from host)
OUTPUT_DIR="/output"

# Source directory
SRC_DIR="/src"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# =============================================================================
# Functions
# =============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_header() {
    echo ""
    echo -e "${YELLOW}---------------------------------------------------------------${NC}"
    echo -e "${YELLOW}  $1${NC}"
    echo -e "${YELLOW}---------------------------------------------------------------${NC}"
    echo ""
}

show_toolchain_info() {
    log_header "Toolchain Information"
    echo "GCC Version:"
    aarch64-none-elf-gcc --version | head -n 1
    echo ""
    echo "Binutils Version:"
    aarch64-none-elf-ld --version | head -n 1
    echo ""
}

build_board() {
    local board=$1
    local board_output_dir="${OUTPUT_DIR}/${board}"
    
    log_header "Building: ${board}"
    
    # Clean previous build for this board
    log_info "Cleaning previous build..."
    make clean BOARD="${board}" 2>/dev/null || true
    
    # Build
    log_info "Compiling..."
    if make BOARD="${board}"; then
        # Create output directory
        mkdir -p "${board_output_dir}"
        
        # Copy kernel binary
        local kernel_name
        if [[ "${board}" == radxa-* ]]; then
            kernel_name="Image"
        else
            kernel_name="kernel8.img"
        fi
        
        if [[ -f "build/${board}/${kernel_name}" ]]; then
            cp "build/${board}/${kernel_name}" "${board_output_dir}/"
            log_success "Copied ${kernel_name} to ${board_output_dir}/"
        fi
        
        # Copy ELF file (useful for debugging)
        if [[ -f "build/${board}/kernel.elf" ]]; then
            cp "build/${board}/kernel.elf" "${board_output_dir}/"
            log_info "Copied kernel.elf to ${board_output_dir}/"
        fi
        
        # Generate and copy disassembly
        if make disasm BOARD="${board}" 2>/dev/null; then
            if [[ -f "build/${board}/kernel.list" ]]; then
                cp "build/${board}/kernel.list" "${board_output_dir}/"
                log_info "Copied kernel.list to ${board_output_dir}/"
            fi
        fi
        
        # Copy board-specific boot files if they exist
        if [[ -d "board/${board}/boot" ]]; then
            cp -r "board/${board}/boot" "${board_output_dir}/"
            log_info "Copied boot files to ${board_output_dir}/boot/"
        fi
        
        # Show file sizes
        echo ""
        log_info "Output files:"
        ls -lh "${board_output_dir}/"
        
        log_success "Build complete for ${board}"
        return 0
    else
        log_error "Build failed for ${board}"
        return 1
    fi
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
    
    # Summary
    log_header "Build Summary"
    
    if [[ ${#success_boards[@]} -gt 0 ]]; then
        echo -e "${GREEN}Successful builds:${NC}"
        for board in "${success_boards[@]}"; do
            echo "  ✓ ${board}"
        done
    fi
    
    if [[ ${#failed_boards[@]} -gt 0 ]]; then
        echo -e "${RED}Failed builds:${NC}"
        for board in "${failed_boards[@]}"; do
            echo "  ✗ ${board}"
        done
        return 1
    fi
    
    echo ""
    log_info "Output directory structure:"
    find "${OUTPUT_DIR}" -type f | sort | while read -r file; do
        size=$(ls -lh "$file" | awk '{print $5}')
        echo "  ${file} (${size})"
    done
    
    return 0
}

show_usage() {
    echo "Tutorial-OS Build Script"
    echo ""
    echo "Usage: build.sh [command]"
    echo ""
    echo "Commands:"
    echo "  all                    Build all boards (default)"
    echo "  <board>                Build specific board"
    echo "  bash                   Start interactive shell"
    echo "  help                   Show this help message"
    echo ""
    echo "Available boards:"
    for board in "${ALL_BOARDS[@]}"; do
        echo "  - ${board}"
    done
    echo ""
    echo "Examples:"
    echo "  build.sh                        # Build all boards"
    echo "  build.sh rpi-zero2w-gpi         # Build single board"
    echo "  build.sh rpi-zero2w-gpi rpi-cm4-io  # Build multiple boards"
}

# =============================================================================
# Main
# =============================================================================

cd "${SRC_DIR}"

# Show toolchain info
show_toolchain_info

# Parse arguments
if [[ $# -eq 0 ]] || [[ "$1" == "all" ]]; then
    build_all
elif [[ "$1" == "bash" ]] || [[ "$1" == "shell" ]]; then
    log_info "Starting interactive shell..."
    exec /bin/bash
elif [[ "$1" == "help" ]] || [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
    show_usage
else
    # Build specified boards
    failed=0
    for board in "$@"; do
        # Check if board exists
        if [[ ! -d "board/${board}" ]]; then
            log_error "Unknown board: ${board}"
            log_info "Available boards: ${ALL_BOARDS[*]}"
            failed=1
            continue
        fi
        
        if ! build_board "${board}"; then
            failed=1
        fi
    done
    
    exit $failed
fi
