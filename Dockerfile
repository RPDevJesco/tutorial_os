# =============================================================================
# Dockerfile — Tutorial-OS Unified Build Environment
# =============================================================================
#
# Everything needed to build Tutorial-OS in both C and Rust, for all
# supported architectures: ARM64, RISC-V 64, x86_64, and ARM32 (Pico 2).
#
# Usage:
#   docker build -t tutorial-os-builder .
#
#   # Build specific board (C):
#   docker run --rm -v $(pwd):/src -w /src tutorial-os-builder \
#       make LANG=c BOARD=rpi-zero2w-gpi
#
#   # Build specific board (Rust):
#   docker run --rm -v $(pwd):/src -w /src tutorial-os-builder \
#       ./build.sh milkv-mars rust
#
#   # Build all boards:
#   docker run --rm -v $(pwd):/src -w /src tutorial-os-builder \
#       ./build.sh all c
#
#   # Interactive shell:
#   docker run --rm -it -v $(pwd):/src -w /src tutorial-os-builder bash
#
# =============================================================================

FROM ubuntu:24.04

LABEL maintainer="Tutorial-OS"
LABEL description="ARM64 + RISC-V + x86_64 + ARM32 cross-compilation + Rust nightly"

ENV DEBIAN_FRONTEND=noninteractive

# =============================================================================
# C Cross-Compilation Toolchains
# =============================================================================
#
# gcc-aarch64-linux-gnu   — ARM64 cross-toolchain (Pi, Radxa, Le Potato)
#                           Used by: soc/bcm2710, bcm2711, bcm2712, rk3528a, s905x
#
# gcc-riscv64-linux-gnu   — RISC-V 64 cross-toolchain (Orange Pi RV2, Milk-V Mars)
#                           Used by: soc/kyx1, soc/jh7110
#
# gcc-x86-64-linux-gnu    — x86_64 cross-toolchain (LattePanda MU/IOTA)
#                           Used by: soc/lattepanda_n100, soc/lattepanda_n150
#
# gnu-efi                 — UEFI application library for x86_64
#                           Provides: crt0-efi-x86_64.o, elf_x86_64_efi.lds, libgnuefi.a
#                           Pipeline: gcc → ELF → objcopy → PE/COFF (.EFI)
#
# gcc-arm-none-eabi       — ARM32 Cortex-M cross-toolchain (Pico 2 / RP2350)
#                           Used by: soc/rp2350 via CMake/Pico SDK
#
# cmake, pkg-config,
# libusb-1.0-0-dev        — Required to build picotool (RP2350 UF2 tool)
#
# mtools                  — FAT32 image creation without root (mformat, mcopy)
#                           Used by: mkimage.sh / mkimage.py for bootable images
#
# python3                 — Board tooling scripts (mkimage.py for LattePanda)
#
# =============================================================================

RUN apt-get update && apt-get install -y \
    build-essential \
    wget \
    xz-utils \
    make \
    git \
    cmake \
    pkg-config \
    libusb-1.0-0-dev \
    parted \
    mtools \
    u-boot-tools \
    dosfstools \
    python3 \
    gcc-riscv64-linux-gnu \
    gcc-x86-64-linux-gnu \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    binutils-x86-64-linux-gnu \
    gnu-efi \
    curl \
    ca-certificates \
    build-essential \
    gcc-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    gcc-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu \
    parted \
    mtools \
    dosfstools \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# ARM64 Bare-Metal Toolchain (aarch64-none-elf)
# =============================================================================
#
# The system aarch64-linux-gnu- toolchain targets Linux userspace.
# For bare-metal (no OS) ARM64, we need the aarch64-none-elf- toolchain
# from ARM's developer site. This is what the Pi boards actually use.
#
# The Rust build uses this toolchain's assembler via the cc crate.
# The C build uses it as the primary compiler.
#
# NOTE: The version URL may need updating. Check:
#   https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
#

ARG ARM_TOOLCHAIN_VERSION=14.2.rel1
ARG ARM_TOOLCHAIN_DIR=/opt/arm-gnu-toolchain

RUN wget -q "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${ARM_TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf.tar.xz" \
        -O /tmp/arm-toolchain.tar.xz \
    && mkdir -p ${ARM_TOOLCHAIN_DIR} \
    && tar xf /tmp/arm-toolchain.tar.xz -C ${ARM_TOOLCHAIN_DIR} --strip-components=1 \
    && rm /tmp/arm-toolchain.tar.xz

ENV PATH="${ARM_TOOLCHAIN_DIR}/bin:${PATH}"

# =============================================================================
# Rust Nightly + Bare-Metal Targets
# =============================================================================
#
# Rust nightly is required for:
#   - #![no_std] binary targets
#   - Inline assembly (core::arch::asm!)
#   - Target-specific ABI features
#
# Bare-metal targets:
#   aarch64-unknown-none         — ARM64 (Pi, Radxa, Le Potato)
#   riscv64gc-unknown-none-elf   — RISC-V 64 (Orange Pi RV2, Milk-V Mars)
#   x86_64-unknown-uefi          — x86_64 UEFI (LattePanda)
#
# cargo-binutils provides rust-objcopy for ELF → binary conversion.
#

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain nightly \
    && . "$HOME/.cargo/env" \
    && rustup target add aarch64-unknown-none \
    && rustup target add riscv64gc-unknown-none-elf \
    && rustup target add x86_64-unknown-uefi \
    && rustup component add llvm-tools \
    && cargo install cargo-binutils

ENV PATH="/root/.cargo/bin:${PATH}"

# Tell the cc crate which C compiler to use for each Rust target.
# This is how build.rs compiles .S assembly files for each architecture.
ENV CC_aarch64_unknown_none=aarch64-linux-gnu-gcc
ENV CC_riscv64gc_unknown_none_elf=riscv64-linux-gnu-gcc

# =============================================================================
# Working Directory
# =============================================================================

WORKDIR /src

# No baked-in ENTRYPOINT — use the container interactively or pass commands:
#   docker run ... make LANG=c BOARD=rpi-zero2w-gpi
#   docker run ... ./build.sh milkv-mars rust
#   docker run ... bash
