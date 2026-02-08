#!/bin/bash
#
# build.sh - Host-side build script for Tutorial-OS
#
# This script wraps Docker commands for easy building.
#
# Usage:
#   ./build.sh              # Build all boards
#   ./build.sh all          # Build all boards
#   ./build.sh rpi-zero2w-gpi   # Build specific board
#   ./build.sh shell        # Interactive shell in container
#   ./build.sh clean        # Remove output directory
#   ./build.sh rebuild      # Rebuild Docker image and build all

set -e

# =============================================================================
# Configuration
# =============================================================================

IMAGE_NAME="tutorial-os-builder"
CONTAINER_NAME="tutorial-os-build"
OUTPUT_DIR="./output"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

# Check if Docker is installed
check_docker() {
    if ! command -v docker &> /dev/null; then
        log_error "Docker is not installed. Please install Docker first."
        exit 1
    fi
}

# Build Docker image if it doesn't exist or if Dockerfile changed
build_image() {
    local needs_build=false
    
    # Check if image exists
    if ! docker image inspect "${IMAGE_NAME}" &> /dev/null; then
        log_info "Docker image not found. Building..."
        needs_build=true
    fi
    
    if [[ "${needs_build}" == "true" ]] || [[ "$1" == "force" ]]; then
        log_info "Building Docker image: ${IMAGE_NAME}"
        docker build -t "${IMAGE_NAME}" .
        log_success "Docker image built successfully"
    fi
}

# Run build in container
run_build() {
    # Create output directory
    mkdir -p "${OUTPUT_DIR}"
    
    log_info "Starting build..."
    docker run --rm \
        -v "$(pwd)/${OUTPUT_DIR}:/output" \
        --name "${CONTAINER_NAME}" \
        "${IMAGE_NAME}" \
        "$@"
}

# Run interactive shell
run_shell() {
    mkdir -p "${OUTPUT_DIR}"
    
    log_info "Starting interactive shell..."
    docker run --rm -it \
        -v "$(pwd)/${OUTPUT_DIR}:/output" \
        --name "${CONTAINER_NAME}" \
        "${IMAGE_NAME}" \
        bash
}

# Clean output directory
clean_output() {
    if [[ -d "${OUTPUT_DIR}" ]]; then
        log_info "Removing output directory..."
        rm -rf "${OUTPUT_DIR}"
        log_success "Output directory removed"
    else
        log_info "Output directory doesn't exist"
    fi
}

# Show usage
show_usage() {
    echo "Tutorial-OS Build Script"
    echo ""
    echo "Usage: ./build.sh [command] [options]"
    echo ""
    echo "Commands:"
    echo "  all                    Build all boards (default)"
    echo "  <board>                Build specific board(s)"
    echo "  shell                  Start interactive shell in container"
    echo "  clean                  Remove output directory"
    echo "  rebuild                Rebuild Docker image and build all"
    echo "  image                  Build Docker image only"
    echo "  help                   Show this help message"
    echo ""
    echo "Available boards:"
    echo "  - rpi-zero2w-gpi       Raspberry Pi Zero 2W + GPi Case"
    echo "  - rpi-cm4-io           Raspberry Pi CM4 + IO Board"
    echo "  - radxa-rock2a         Radxa Rock 2A"
    echo ""
    echo "Examples:"
    echo "  ./build.sh                          # Build all boards"
    echo "  ./build.sh rpi-zero2w-gpi           # Build single board"
    echo "  ./build.sh rpi-zero2w-gpi rpi-cm4-io    # Build multiple boards"
    echo "  ./build.sh shell                    # Debug in container"
    echo ""
    echo "Output structure:"
    echo "  output/"
    echo "  ├── rpi-zero2w-gpi/"
    echo "  │   ├── kernel8.img"
    echo "  │   ├── kernel.elf"
    echo "  │   ├── kernel.list"
    echo "  │   └── boot/"
    echo "  │       └── config.txt"
    echo "  ├── rpi-cm4-io/"
    echo "  │   └── ..."
    echo "  └── radxa-rock2a/"
    echo "      ├── Image"
    echo "      └── boot/"
    echo "          └── extlinux/"
}

# =============================================================================
# Main
# =============================================================================

check_docker

case "${1:-all}" in
    help|-h|--help)
        show_usage
        ;;
    clean)
        clean_output
        ;;
    shell|bash)
        build_image
        run_shell
        ;;
    rebuild)
        clean_output
        build_image force
        run_build all
        ;;
    image)
        build_image force
        ;;
    *)
        build_image
        run_build "$@"
        
        # Show output summary
        echo ""
        log_success "Build complete! Output files:"
        if [[ -d "${OUTPUT_DIR}" ]]; then
            find "${OUTPUT_DIR}" -type f | sort | while read -r file; do
                size=$(ls -lh "$file" | awk '{print $5}')
                echo "  ${file} (${size})"
            done
        fi
        ;;
esac
