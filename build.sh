#!/usr/bin/env bash
# =============================================================================
# build.sh — Tutorial-OS Unified Build Script
# =============================================================================
#
# Single script that works on the host (wraps Docker) and inside the
# container (runs the actual build).  Detection is automatic:
#
#   Host   → checks Docker, builds image if needed, delegates inside container
#   Docker → runs make (C) or cargo (Rust) with cross-toolchains
#
# Usage:
#   ./build.sh <board> [lang] [profile]
#
#   ./build.sh rpi-zero2w-gpi              C build (default), release
#   ./build.sh rpi-zero2w rust             Rust build, release
#   ./build.sh milkv-mars rust debug       Rust build, debug
#   ./build.sh all                         All boards, C
#   ./build.sh all rust                    All boards, Rust
#   ./build.sh shell                       Interactive shell in container
#   ./build.sh clean                       Clean all artifacts
#   ./build.sh rebuild                     Rebuild Docker image from scratch
#   ./build.sh image                       Build Docker image only
#
# =============================================================================

set -euo pipefail

# =============================================================================
# Logging
# =============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $1"; }

# =============================================================================
# Usage
# =============================================================================

usage() {
    cat <<'EOF'
 Tutorial-OS — Unified Build Script
 ====================================

 Usage: ./build.sh <board> [lang] [profile]

 Languages:
   c        C implementation (default)
   rust     Rust implementation

 Boards:
   ARM64 (Raspberry Pi):
     rpi-zero2w-gpi   Pi Zero 2W, 3B, 3B+ (C: GPi 640x480)
     rpi-zero2w       Pi Zero 2W, 3B, 3B+ (Rust / C: default res)
     rpi-cm4-io       Pi 4, CM4, Pi 400 (C)
     rpi-cm4          Pi 4, CM4, Pi 400 (Rust)
     rpi-cm5-io       Pi 5, CM5 (C)
     rpi-5            Pi 5, CM5 (Rust)

   RISC-V:
     milkv-mars       Milk-V Mars (StarFive JH7110)
     orangepi-rv2     Orange Pi RV2 (SpacemiT K1)

   x86_64 (UEFI):
     lattepanda-mu    LattePanda MU (Intel N100/N305)
     lattepanda-iota  LattePanda IOTA (Intel N150)

   Cortex-M:
     pico2-lafvin     Pico 2 + LAFVIN ILI9488 (RP2350)

 Commands:
   all [lang]         Build all boards
   clean              Clean all artifacts (build/ + target/ + output/)
   shell              Interactive shell in container
   rebuild            Rebuild Docker image from scratch
   image              Build Docker image only

 Examples:
   ./build.sh rpi-zero2w-gpi              C build, release
   ./build.sh milkv-mars rust             Rust build, release
   ./build.sh milkv-mars rust debug       Rust build, debug
   ./build.sh all                         All boards, C
   ./build.sh all rust                    All boards, Rust
   ./build.sh shell                       Debug in container
   ./build.sh clean                       Clean everything
EOF
}

# =============================================================================
# Environment detection
# =============================================================================
# Inside the Docker container, /.dockerenv exists (Docker) or /run/.containerenv
# exists (Podman).  We also check for the cross-toolchains as a fallback —
# if aarch64-linux-gnu-gcc is on PATH we're almost certainly in the build
# container, not on someone's bare host.

in_container() {
    [[ -f /.dockerenv ]] || [[ -f /run/.containerenv ]] || \
        command -v aarch64-linux-gnu-gcc &>/dev/null
}

# #############################################################################
#
#  HOST SIDE — Docker wrapper
#
# #############################################################################

IMAGE_NAME="tutorial-os-builder"

check_docker() {
    if ! command -v docker &>/dev/null; then
        log_error "Docker is not installed or not in PATH."
        echo "        Install Docker Desktop: https://www.docker.com/products/docker-desktop/"
        exit 1
    fi
}

ensure_image() {
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        log_info "Docker image '$IMAGE_NAME' not found. Building (first time only)..."
        docker build -t "$IMAGE_NAME" .
        log_success "Docker image built."
    fi
}

host_main() {
    local cmd="${1:-}"

    case "$cmd" in
        ""|-h|--help|help)
            usage
            exit 0
            ;;
        clean)
            log_info "Cleaning all build artifacts..."
            rm -rf build/ output/
            cargo clean 2>/dev/null || true
            log_success "Clean."
            exit 0
            ;;
        shell)
            check_docker
            ensure_image
            log_info "Starting interactive shell..."
            docker run --rm -it \
                -v "$(pwd)":/src \
                -w /src \
                "$IMAGE_NAME" \
                bash
            exit 0
            ;;
        rebuild)
            check_docker
            log_info "Removing existing image..."
            docker rmi "$IMAGE_NAME" 2>/dev/null || true
            docker build -t "$IMAGE_NAME" .
            log_success "Docker image rebuilt."
            exit 0
            ;;
        image)
            check_docker
            docker build -t "$IMAGE_NAME" .
            log_success "Docker image built."
            exit 0
            ;;
    esac

    # --- Default: delegate the build into Docker ---
    check_docker
    ensure_image

    log_info "Building: $*"
    docker run --rm \
        -v "$(pwd)":/src \
        -w /src \
        "$IMAGE_NAME" \
        ./build.sh "$@"

    # Show output summary
    echo ""
    if [[ -d output/ ]]; then
        log_success "Build complete. Output:"
        find output/ -type f | sort | while read -r file; do
            local size
            size=$(ls -lh "$file" | awk '{print $5}')
            echo "  ${file} (${size})"
        done
    fi
}

# #############################################################################
#
#  CONTAINER SIDE — Actual build logic
#
# #############################################################################

# =============================================================================
# Board → Rust feature/target mapping
# =============================================================================

setup_board() {
    case "$1" in
        rpi-zero2w|rpi-zero2w-gpi|rpi-3b|rpi-3bp)
            FEATURE="board-rpi-zero2w"
            TARGET="aarch64-unknown-none"
            OUTPUT_NAME="kernel8.img"
            ARCH="arm64"
            ;;
        rpi-cm4|rpi-cm4-io|rpi-4|rpi-400)
            FEATURE="board-rpi-cm4"
            TARGET="aarch64-unknown-none"
            OUTPUT_NAME="kernel8.img"
            ARCH="arm64"
            ;;
        rpi-5|rpi-cm5|rpi-cm5-io)
            FEATURE="board-rpi5"
            TARGET="aarch64-unknown-none"
            OUTPUT_NAME="kernel8.img"
            ARCH="arm64"
            ;;
        orangepi-rv2)
            FEATURE="board-orangepi-rv2"
            TARGET="riscv64gc-unknown-none-elf"
            OUTPUT_NAME="kernel.bin"
            ARCH="riscv64"
            ;;
        milkv-mars)
            FEATURE="board-milkv-mars"
            TARGET="riscv64gc-unknown-none-elf"
            OUTPUT_NAME="kernel.bin"
            ARCH="riscv64"
            ;;
        lattepanda-mu)
            FEATURE="board-lattepanda-mu"
            TARGET="x86_64-unknown-uefi"
            OUTPUT_NAME="BOOTX64.EFI"
            ARCH="x86_64"
            ;;
        lattepanda-iota)
            FEATURE="board-lattepanda-iota"
            TARGET="x86_64-unknown-uefi"
            OUTPUT_NAME="BOOTX64.EFI"
            ARCH="x86_64"
            ;;
        *)
            log_error "Unknown board '$1'"
            echo ""
            usage
            exit 1
            ;;
    esac
}

# =============================================================================
# Find objcopy (Rust builds need ELF → binary conversion)
# =============================================================================

find_objcopy() {
    if command -v rust-objcopy &>/dev/null; then
        echo "rust-objcopy"
    elif command -v llvm-objcopy &>/dev/null; then
        echo "llvm-objcopy"
    elif command -v aarch64-none-elf-objcopy &>/dev/null; then
        echo "aarch64-none-elf-objcopy"
    elif command -v aarch64-linux-gnu-objcopy &>/dev/null; then
        echo "aarch64-linux-gnu-objcopy"
    elif command -v riscv64-linux-gnu-objcopy &>/dev/null; then
        echo "riscv64-linux-gnu-objcopy"
    else
        echo ""
    fi
}

# =============================================================================
# C build — delegate to Make
# =============================================================================

build_c() {
    local board="$1"
    local profile="${2:-release}"

    log_info "C build: BOARD=$board"

    if [[ ! -d "board/${board}" ]]; then
        log_error "Unknown board: $board (no board/${board}/ directory)"
        exit 1
    fi

    make LANG=c BOARD="$board"

    # Copy outputs
    local out_dir="output/${board}"
    mkdir -p "$out_dir"

    local kernel_name
    kernel_name=$(make -s BOARD="$board" print-KERNEL_NAME 2>/dev/null || echo "")

    if [[ -n "$kernel_name" && -f "build/${board}/${kernel_name}" ]]; then
        cp "build/${board}/${kernel_name}" "$out_dir/"
    fi

    if [[ -f "build/${board}/kernel.elf" ]]; then
        cp "build/${board}/kernel.elf" "$out_dir/"
    fi

    # Run mkimage if available
    if [[ -x "board/${board}/mkimage.sh" ]]; then
        log_info "Creating flashable image..."
        BOARD="$board" BUILD_DIR="$out_dir" board/"${board}"/mkimage.sh || true
    fi

    log_success "C build complete: $board"
}

# =============================================================================
# Rust build — cargo + objcopy
# =============================================================================

build_rust() {
    local board="$1"
    local profile="${2:-release}"

    setup_board "$board"

    echo ""
    echo "============================================================"
    echo " Tutorial-OS (Rust) Build"
    echo "============================================================"
    echo " Board:   $board"
    echo " Feature: $FEATURE"
    echo " Target:  $TARGET"
    echo " Profile: $profile"
    echo " Output:  $OUTPUT_NAME"
    echo "============================================================"
    echo ""

    # Cargo profile flag
    local cargo_profile=""
    if [[ "$profile" = "release" ]]; then
        cargo_profile="--release"
    fi

    # Step 1: Build
    echo "[1/4] Building kernel..."
    cargo build -p kernel --features "$FEATURE" --target "$TARGET" $cargo_profile

    # Determine ELF path
    local elf_path
    if [[ "$profile" = "release" ]]; then
        elf_path="target/${TARGET}/release/kernel"
    else
        elf_path="target/${TARGET}/debug/kernel"
    fi

    # Output directory
    local out_dir="output/${board}"
    mkdir -p "$out_dir"

    # UEFI targets produce .efi directly
    if [[ "$ARCH" = "x86_64" ]]; then
        echo "[2/4] Copying UEFI binary..."
        cp "${elf_path}.efi" "${out_dir}/${OUTPUT_NAME}"
    else
        # ELF → raw binary
        echo "[2/4] Converting ELF to binary..."
        local objcopy
        objcopy=$(find_objcopy)
        if [[ -z "$objcopy" ]]; then
            log_error "No objcopy found. Install cargo-binutils or a cross toolchain."
            exit 1
        fi
        $objcopy -O binary "$elf_path" "${out_dir}/${OUTPUT_NAME}"
    fi

    # Step 3: Run mkimage.sh if it exists
    echo "[3/4] Checking for image creation script..."
    if [[ -x "board/${board}/mkimage.sh" ]]; then
        BOARD="$board" BUILD_DIR="$out_dir" board/"${board}"/mkimage.sh
    else
        echo "  No mkimage.sh — raw binary only."
    fi

    # Step 4: Done
    echo "[4/4] Build complete."
    echo ""
    ls -lh "${out_dir}/${OUTPUT_NAME}"
    log_success "${out_dir}/${OUTPUT_NAME}"
}

# =============================================================================
# Build all boards
# =============================================================================

ALL_C_BOARDS="rpi-zero2w-gpi rpi-cm4-io rpi-cm5-io milkv-mars orangepi-rv2 lattepanda-mu lattepanda-iota"
ALL_RUST_BOARDS="rpi-zero2w rpi-cm4 rpi-5 milkv-mars orangepi-rv2 lattepanda-mu lattepanda-iota"

build_all() {
    local lang="${1:-c}"
    local boards

    if [[ "$lang" = "rust" ]]; then
        boards="$ALL_RUST_BOARDS"
    else
        boards="$ALL_C_BOARDS"
    fi

    local failed=""
    for board in $boards; do
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        log_info "Building: $board ($lang)"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        if [[ "$lang" = "rust" ]]; then
            build_rust "$board" release || { failed="$failed $board"; log_error "FAILED: $board"; }
        else
            build_c "$board" release || { failed="$failed $board"; log_error "FAILED: $board"; }
        fi
    done

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    if [[ -z "$failed" ]]; then
        log_success "All boards built successfully."
    else
        log_error "Failed boards:$failed"
        exit 1
    fi
}

# =============================================================================
# Container-side main
# =============================================================================

container_main() {
    local cmd="${1:-}"

    case "$cmd" in
        ""|-h|--help|help)
            usage
            exit 0
            ;;
        clean)
            log_info "Cleaning all build artifacts..."
            rm -rf build/ output/
            cargo clean 2>/dev/null || true
            log_success "Clean."
            exit 0
            ;;
        all)
            build_all "${2:-c}"
            exit 0
            ;;
    esac

    # Single board build
    local board="$1"
    local lang_arg="${2:-c}"
    local profile="${3:-release}"

    # If second arg looks like a profile, default to C
    if [[ "$lang_arg" = "debug" || "$lang_arg" = "release" ]]; then
        profile="$lang_arg"
        lang_arg="c"
    fi

    if [[ "$lang_arg" = "rust" ]]; then
        build_rust "$board" "$profile"
    elif [[ "$lang_arg" = "c" ]]; then
        build_c "$board" "$profile"
    else
        log_error "Unknown language '$lang_arg'. Use 'c' or 'rust'."
        exit 1
    fi
}

# =============================================================================
# Entry point — route to host or container logic
# =============================================================================

if in_container; then
    container_main "$@"
else
    host_main "$@"
fi