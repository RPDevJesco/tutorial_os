# Tutorial-OS Build Environment
#
# Multi-platform bare-metal OS cross-compilation container
#
# Usage:
#   docker build -t tutorial-os-builder .
#   docker run --rm -v $(pwd)/output:/output tutorial-os-builder
#
#   # Build specific board only:
#   docker run --rm -v $(pwd)/output:/output tutorial-os-builder rpi-zero2w-gpi
#   docker run --rm -v $(pwd)/output:/output tutorial-os-builder orangepi-rv2
#
#   # Interactive shell:
#   docker run --rm -it -v $(pwd)/output:/output tutorial-os-builder bash

FROM ubuntu:24.04

LABEL maintainer="Tutorial-OS"
LABEL description="ARM64 + RISC-V bare-metal cross-compilation environment"

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# =============================================================================
# Install build dependencies
# =============================================================================

# ──── CHANGED: Added gcc-riscv64-linux-gnu for Orange Pi RV2 ────
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
    gcc-riscv64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Install ARM64 bare-metal toolchain
# =============================================================================

# ARM GNU Toolchain version (update as needed)
ARG ARM_TOOLCHAIN_VERSION=13.3.rel1
ARG ARM_TOOLCHAIN_URL=https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${ARM_TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf.tar.xz

# Download and install ARM toolchain
RUN wget -q ${ARM_TOOLCHAIN_URL} -O /tmp/arm-toolchain.tar.xz \
    && mkdir -p /opt/arm-gnu-toolchain \
    && tar -xf /tmp/arm-toolchain.tar.xz -C /opt/arm-gnu-toolchain --strip-components=1 \
    && rm /tmp/arm-toolchain.tar.xz

# Add toolchain to PATH
ENV PATH="/opt/arm-gnu-toolchain/bin:${PATH}"

# ──── CHANGED: Verify both toolchains are available ────
RUN aarch64-none-elf-gcc --version | head -1 \
    && riscv64-linux-gnu-gcc --version | head -1

# =============================================================================
# Set up build environment
# =============================================================================

# Create working directory
WORKDIR /src

# Copy source code
COPY . /src/

# Create output directory
RUN mkdir -p /output

# =============================================================================
# Build script
# =============================================================================

COPY docker-build.sh /usr/local/bin/build.sh
RUN sed -i 's/\r$//' /usr/local/bin/build.sh

# Default: build all boards
ENTRYPOINT ["/usr/local/bin/build.sh"]
CMD ["all"]