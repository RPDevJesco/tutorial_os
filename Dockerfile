# Tutorial-OS Build Environment
#
# Multi-platform bare-metal OS cross-compilation container
#
# Usage:
#   docker build -t tutorial-os-builder .
#   docker run --rm -v $(pwd)/output:/output tutorial-os-builder
#
#   # Build specific board:
#   docker run --rm -v $(pwd)/output:/output tutorial-os-builder rpi-zero2w-gpi
#   docker run --rm -v $(pwd)/output:/output tutorial-os-builder orangepi-rv2
#   docker run --rm -v $(pwd)/output:/output tutorial-os-builder lattepanda-mu
#
#   # Interactive shell:
#   docker run --rm -it -v $(pwd)/output:/output tutorial-os-builder bash

FROM ubuntu:24.04

LABEL maintainer="Tutorial-OS"
LABEL description="ARM64 + RISC-V + x86_64 bare-metal cross-compilation environment"

ENV DEBIAN_FRONTEND=noninteractive

# =============================================================================
# Install build dependencies
# =============================================================================
#
# gcc-x86-64-linux-gnu   — x86_64 cross-toolchain for LattePanda MU (UEFI)
#                          Provides: x86_64-linux-gnu-gcc, x86_64-linux-gnu-ld
#                          Used by board/lattepanda-mu (soc/lattepanda_n100)
#
# gnu-efi                — UEFI application development library
#                          Provides: crt0-efi-x86_64.o, elf_x86_64_efi.lds,
#                                    libgnuefi.a, libefi.a, /usr/include/efi/
#                          Required for: PE/COFF EFI binary production
#                          Pipeline: gcc → ELF (via elf_x86_64_efi.lds + crt0) →
#                                    objcopy --target=efi-app-x86_64 → BOOTX64.EFI
#                          crt0 handles: ELF relocation fixups, ABI bridging
#                          (UEFI calls crt0 with ms_abi; crt0 calls efi_main sysv)
#
# gcc-riscv64-linux-gnu  — RISC-V cross-toolchain for Orange Pi RV2 / Milk-V Mars
#
# mtools                 — FAT32 image creation without root (mformat, mcopy, mmd)
#                          Used by board/lattepanda-mu/mkimage.py
#
# python3                — mkimage.py (LattePanda MU) and board tooling
#
# All ARM64 boards use the downloaded aarch64-none-elf toolchain below.

RUN apt-get update && apt-get install -y \
    build-essential \
    wget \
    xz-utils \
    make \
    git \
    parted \
    mtools \
    u-boot-tools \
    dosfstools \
    python3 \
    gcc-riscv64-linux-gnu \
    gcc-x86-64-linux-gnu \
    binutils-x86-64-linux-gnu \
    gnu-efi \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Install ARM64 bare-metal toolchain
# =============================================================================

ARG ARM_TOOLCHAIN_VERSION=13.3.rel1
ARG ARM_TOOLCHAIN_URL=https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${ARM_TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf.tar.xz

RUN wget -q ${ARM_TOOLCHAIN_URL} -O /tmp/arm-toolchain.tar.xz \
    && mkdir -p /opt/arm-gnu-toolchain \
    && tar -xf /tmp/arm-toolchain.tar.xz -C /opt/arm-gnu-toolchain --strip-components=1 \
    && rm /tmp/arm-toolchain.tar.xz

ENV PATH="/opt/arm-gnu-toolchain/bin:${PATH}"

# Verify all three toolchains and gnu-efi are available
RUN aarch64-none-elf-gcc --version | head -1 \
    && riscv64-linux-gnu-gcc --version | head -1 \
    && x86_64-linux-gnu-gcc --version | head -1 \
    && test -f /usr/lib/crt0-efi-x86_64.o && echo "gnu-efi: OK"

# =============================================================================
# Set up build environment
# =============================================================================

WORKDIR /src
COPY . /src/
RUN mkdir -p /output

COPY docker-build.sh /usr/local/bin/build.sh
RUN sed -i 's/\r$//' /usr/local/bin/build.sh

ENTRYPOINT ["/usr/local/bin/build.sh"]
CMD ["all"]