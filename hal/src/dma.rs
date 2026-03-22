//! DMA coherency and transfer contract.
//!
//! DMA is not optional in Tutorial-OS — the mailbox, framebuffer, audio,
//! and USB subsystems all involve device-mastered memory transfers.  This
//! module defines the portable contract for cache coherency, address
//! translation, and buffer ownership tracking.
//!
//! # The Fundamental Problem
//!
//! CPUs have caches.  DMA devices read/write physical DRAM directly.
//! Without explicit cache management:
//!
//! 1. **CPU → Device (TO_DEVICE):** dirty cache lines may not have reached
//!    DRAM yet.  The device reads stale data.  *Fix:* clean cache before
//!    handing the buffer to the device.
//!
//! 2. **Device → CPU (FROM_DEVICE):** the CPU cache still holds old data.
//!    *Fix:* invalidate cache before the CPU reads the buffer.
//!
//! # Ownership Model
//!
//! A DMA buffer transitions between CPU-owned and device-owned states:
//!
//! ```text
//! CPU_OWNED → prepare() → DEVICE_OWNED → complete() → CPU_OWNED
//! ```
//!
//! While device-owned, the CPU must **not** touch the buffer.

use crate::types::{HalError, HalResult};

// ============================================================================
// Transfer Direction
// ============================================================================

/// Direction of a DMA transfer — determines which cache operation is needed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Direction {
    /// CPU has written data; device will read it.
    /// Action: clean (write-back) cache lines.
    ToDevice      = 0,
    /// Device will write data; CPU will read it afterwards.
    /// Action: invalidate cache lines.
    FromDevice    = 1,
    /// Both directions, or direction unknown at prepare time.
    /// Action: flush (clean + invalidate) — most expensive.
    Bidirectional = 2,
}

// ============================================================================
// Channel Identifiers
// ============================================================================

/// Logical DMA channel — identifies the consumer/producer.
///
/// Priority order (highest → lowest):
/// `Display > Audio > Usb > SdCard > Mailbox > Network > Generic`
///
/// Display is highest because a missed frame is immediately visible.
/// Network is lowest because packet latency tolerance is much greater.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum Channel {
    Display = 0,
    Audio   = 1,
    Usb     = 2,
    SdCard  = 3,
    Mailbox = 4,
    Network = 5,
    Generic = 6,
}

impl Channel {
    /// Total number of defined channels.
    pub const COUNT: usize = 7;

    /// Human-readable name for UART diagnostics.
    pub const fn name(self) -> &'static str {
        match self {
            Self::Display => "DISPLAY",
            Self::Audio   => "AUDIO",
            Self::Usb     => "USB",
            Self::SdCard  => "SDCARD",
            Self::Mailbox => "MAILBOX",
            Self::Network => "NETWORK",
            Self::Generic => "GENERIC",
        }
    }
}

// ============================================================================
// Buffer Ownership
// ============================================================================

/// Tracks who currently owns a DMA buffer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Owner {
    /// CPU may freely read/write the buffer.
    Cpu    = 0,
    /// Device is using the buffer — CPU must not touch it.
    Device = 1,
}

// ============================================================================
// Buffer Descriptor
// ============================================================================

/// Minimum alignment for DMA buffers (cache line size on all platforms).
pub const DMA_ALIGN: usize = 64;

/// Minimum alignment for BCM mailbox buffers (low 4 bits encode channel).
pub const MAILBOX_ALIGN: usize = 16;

/// Tracks a DMA buffer's state through the prepare/complete lifecycle.
///
/// Drivers hold one of these for each active DMA buffer.
#[derive(Debug)]
pub struct DmaBuf {
    /// CPU-visible address of the buffer.
    pub cpu_addr: *mut u8,
    /// Device-visible (bus) address — populated by [`Dma::prepare`].
    pub dev_addr: usize,
    /// Buffer size in bytes.
    pub size: usize,
    /// Transfer direction.
    pub direction: Direction,
    /// Which logical channel owns this buffer.
    pub channel: Channel,
    /// Current ownership state.
    pub owner: Owner,
}

impl DmaBuf {
    /// Create a new CPU-owned buffer descriptor.
    ///
    /// `dev_addr` is set to 0; it will be populated by [`Dma::prepare`].
    pub const fn new(
        cpu_addr: *mut u8,
        size: usize,
        direction: Direction,
        channel: Channel,
    ) -> Self {
        Self {
            cpu_addr,
            dev_addr: 0,
            size,
            direction,
            channel,
            owner: Owner::Cpu,
        }
    }

    /// Returns `true` if the buffer is currently CPU-owned.
    pub const fn is_cpu_owned(&self) -> bool {
        matches!(self.owner, Owner::Cpu)
    }
}

// ============================================================================
// DMA Trait
// ============================================================================

/// Platform-specific DMA coherency and address-translation contract.
///
/// Each SoC implements this with its own cache flush mechanism:
///
/// | SoC | TO_DEVICE | FROM_DEVICE | Address Translation |
/// |-----|-----------|-------------|---------------------|
/// | BCM2710/2711 | `dc cvac` loop | `dc ivac` loop | `+0xC000_0000` bus alias |
/// | BCM2712 | `dc cvac` loop | `dc ivac` loop | identity (no alias) |
/// | JH7110 | L2 Flush64 MMIO | fence only (TileLink coherent) | identity |
/// | KYX1 | `cbo.clean` (Zicbom) | `cbo.inval` (Zicbom) | identity |
/// | x86_64 | SFENCE (cache coherent) | LFENCE | identity |
pub trait Dma {
    /// Transfer buffer ownership from CPU to device.
    ///
    /// Performs the appropriate cache operation for the buffer's direction
    /// and populates `buf.dev_addr`.  Sets `buf.owner = Device`.
    ///
    /// After this call the CPU must **not** read or write the buffer.
    fn prepare(&self, buf: &mut DmaBuf) -> HalResult<()>;

    /// Transfer buffer ownership back from device to CPU.
    ///
    /// Performs any post-transfer cache invalidation.
    /// Sets `buf.owner = Cpu`.
    fn complete(&self, buf: &mut DmaBuf) -> HalResult<()>;

    /// Synchronize without changing ownership (for mid-transfer sampling).
    ///
    /// Use sparingly — sharing a buffer mid-transfer is inherently racy.
    fn sync(&self, buf: &DmaBuf, dir: Direction);

    // ---- Address Translation ----

    /// Convert a CPU address to a device-visible (bus) address.
    ///
    /// On BCM2710/2711 this adds the `0xC000_0000` L2-coherent alias.
    /// On all other platforms it returns the address unchanged.
    fn to_device_addr(&self, cpu_addr: *const u8) -> usize;

    /// Convert a device-visible address back to a CPU address.
    ///
    /// Inverse of [`to_device_addr`](Dma::to_device_addr).
    fn to_cpu_addr(&self, dev_addr: usize) -> usize;

    // ---- Barriers ----

    /// Ordering barrier between two DMA channels.
    ///
    /// Ensures `from`'s writes are visible before `to` starts reading.
    fn channel_barrier(&self, from: Channel, to: Channel);

    /// Full system DMA ordering barrier (equivalent to DSB SY on ARM64).
    fn full_barrier(&self);

    // ---- Convenience ----

    /// Flush the framebuffer to DRAM before the display controller reads it.
    ///
    /// This is the most common DMA operation in Tutorial-OS.
    fn flush_framebuffer(&self, fb_addr: *const u8, size: usize);

    /// Prepare a BCM mailbox buffer for GPU DMA.
    ///
    /// Returns the device (bus) address to write to the mailbox register.
    /// Defaults to `NotSupported`; only BCM SoC crates override this.
    fn prepare_mailbox(&self, _buf: *const u8, _size: usize) -> HalResult<usize> {
        Err(HalError::NotSupported)
    }

    /// Finalize a BCM mailbox response read.
    fn complete_mailbox(&self, _buf: *const u8, _size: usize) {
        // Default no-op for non-BCM platforms.
    }

    /// Zero a buffer using the fastest available mechanism, leaving it
    /// cache-clean and ready for a FROM_DEVICE transfer.
    ///
    /// On KYX1 (Zicboz): uses `cbo.zero` — faster than memset + invalidate.
    /// On all others: falls back to a byte-wise zero + cache clean.
    fn zero_buffer(&self, buf: &mut DmaBuf);

    /// Check if an address falls within a reserved DMA region.
    fn addr_is_reserved(&self, _addr: usize, _size: usize) -> bool {
        false
    }

    /// Validate a buffer descriptor (alignment, size, ownership).
    fn buf_is_valid(&self, buf: &DmaBuf) -> bool {
        let addr = buf.cpu_addr as usize;
        // Must be cache-line aligned.
        if addr & (DMA_ALIGN - 1) != 0 {
            return false;
        }
        // Must not overlap a reserved region.
        if self.addr_is_reserved(addr, buf.size) {
            return false;
        }
        true
    }
}
