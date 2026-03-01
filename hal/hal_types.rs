/*
 * hal/hal_types — Hardware Abstraction Layer Types
 *
 * Tutorial-OS: HAL Interface Definitions (Rust)
 *
 * This module defines fundamental types used throughout the HAL.
 * It is the Rust equivalent of hal_types.h, using Rust's type system
 * for safety while maintaining the same logical structure.
 *
 * KEY DIFFERENCES FROM C:
 *   - Fixed-width integers come from `core` (no custom typedefs needed)
 *   - Error codes use a proper Rust enum with Result<T, HalError>
 *   - MMIO access uses `core::ptr::read_volatile` / `write_volatile`
 *   - Memory barriers use `core::arch::asm!` (inline assembly)
 *   - Bit operations are methods on a newtype or standalone functions
 *   - No NULL — Rust uses Option<T> instead
 */

#![allow(dead_code)]

use core::ptr;

// =============================================================================
// HAL ERROR CODES
// =============================================================================
//
// All HAL functions return Result<T, HalError> instead of C-style error codes.
// This leverages Rust's type system to enforce error handling at compile time.
//
// Error codes are grouped by subsystem, matching the C version's 0xNNxx scheme
// for cross-language debugging compatibility.

/// HAL error type — all HAL functions return `Result<T, HalError>`.
///
/// Each variant maps to a C error code (shown in comments) so that error
/// values are consistent across the C and Rust implementations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
pub enum HalError {
    // General errors (0x00xx)
    /// Generic error
    Error               = 0x0001,
    /// Subsystem not initialized
    NotInitialized      = 0x0002,
    /// Already initialized
    AlreadyInitialized  = 0x0003,
    /// Invalid argument
    InvalidArgument     = 0x0004,
    /// NULL pointer passed (rare in Rust — usually caught at compile time)
    NullPointer         = 0x0005,
    /// Operation timed out
    Timeout             = 0x0006,
    /// Resource busy
    Busy                = 0x0007,
    /// Feature not supported on this platform
    NotSupported        = 0x0008,
    /// Out of memory
    NoMemory            = 0x0009,
    /// Hardware failure
    Hardware            = 0x000A,

    // Display errors (0x01xx)
    /// Display initialization failed
    DisplayInit         = 0x0100,
    /// Invalid display mode
    DisplayMode         = 0x0101,
    /// No framebuffer allocated
    DisplayNoFb         = 0x0102,
    /// Mailbox communication failed
    DisplayMailbox      = 0x0103,

    // GPIO errors (0x02xx)
    /// Pin number out of range
    GpioInvalidPin      = 0x0200,
    /// Invalid pin mode
    GpioInvalidMode     = 0x0201,

    // Timer errors (0x03xx)
    /// Invalid timer
    TimerInvalid        = 0x0300,

    // UART errors (0x04xx)
    /// Invalid UART number
    UartInvalid         = 0x0400,
    /// Invalid baud rate
    UartBaud            = 0x0401,
    /// Buffer overflow
    UartOverflow        = 0x0402,

    // USB errors (0x05xx)
    /// USB initialization failed
    UsbInit             = 0x0500,
    /// No device connected
    UsbNoDevice         = 0x0501,
    /// Enumeration failed
    UsbEnumFailed       = 0x0502,
    /// Transfer error
    UsbTransfer         = 0x0503,
    /// Endpoint stalled
    UsbStall            = 0x0504,

    // Storage errors (0x06xx)
    /// Storage initialization failed
    StorageInit         = 0x0600,
    /// No card inserted
    StorageNoCard       = 0x0601,
    /// Read error
    StorageRead         = 0x0602,
    /// Write error
    StorageWrite        = 0x0603,
    /// CRC error
    StorageCrc          = 0x0604,

    // Audio errors (0x07xx)
    /// Audio initialization failed
    AudioInit           = 0x0700,
    /// Unsupported format
    AudioFormat         = 0x0701,
}

/// Convenience type alias — the standard return type for all HAL operations.
///
/// In C, functions return `hal_error_t` and callers check `HAL_FAILED()`.
/// In Rust, functions return `HalResult<T>` and callers use `?` or `match`.
pub type HalResult<T> = Result<T, HalError>;

// =============================================================================
// PLATFORM IDENTIFICATION
// =============================================================================

/// Platform identifier — which board we're running on.
///
/// Values match the C enum for cross-language compatibility.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
pub enum PlatformId {
    // Raspberry Pi family
    RpiZero2W       = 0x0001,   // BCM2710
    Rpi3B           = 0x0002,   // BCM2710
    Rpi3BPlus       = 0x0003,   // BCM2710
    Rpi4B           = 0x0010,   // BCM2711
    RpiCm4          = 0x0011,   // BCM2711
    Rpi5            = 0x0020,   // BCM2712
    RpiCm5          = 0x0021,   // BCM2712

    // Other ARM64 boards
    LibrePotato     = 0x0100,   // S905X
    RadxaRock2A     = 0x0200,   // RK3528A
    KickpiK2B       = 0x0300,   // H618

    // RISC-V boards
    OrangePiRV2     = 0x1000,   // SpacemiT K1

    // x86_64 boards
    // LattePandaIota will go here once assigned

    Unknown         = 0xFFFF,
}

/// CPU architecture identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Arch {
    Arm64       = 0x01,
    Arm32       = 0x02,
    RiscV64     = 0x03,
    X86_64      = 0x04,
    Unknown     = 0xFF,
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================
//
// In C these are macros. In Rust, generic functions and const fns give us
// the same zero-cost abstraction with type safety.

/// Bit mask for bit position `n` — equivalent to C's `BIT(n)`.
#[inline(always)]
pub const fn bit(n: u32) -> u32 {
    1u32 << n
}

/// 64-bit variant of `bit()`.
#[inline(always)]
pub const fn bit64(n: u32) -> u64 {
    1u64 << n
}

/// Align `x` up to the next multiple of `align`.
///
/// `align` must be a power of two.
#[inline(always)]
pub const fn align_up(x: usize, align: usize) -> usize {
    (x + align - 1) & !(align - 1)
}

/// Align `x` down to the previous multiple of `align`.
///
/// `align` must be a power of two.
#[inline(always)]
pub const fn align_down(x: usize, align: usize) -> usize {
    x & !(align - 1)
}

/// Check if `x` is aligned to `align`.
#[inline(always)]
pub const fn is_aligned(x: usize, align: usize) -> bool {
    (x & (align - 1)) == 0
}

/// Clamp `x` to the range `[lo, hi]`.
#[inline(always)]
pub const fn clamp(x: i32, lo: i32, hi: i32) -> i32 {
    if x < lo {
        lo
    } else if x > hi {
        hi
    } else {
        x
    }
}

// =============================================================================
// MEMORY BARRIERS
// =============================================================================
//
// Modern CPUs can reorder memory operations for performance. When talking to
// hardware or communicating between CPU cores, we need barriers to enforce
// ordering.
//
// In C these are macros that expand to inline assembly. In Rust we use
// `core::arch::asm!` which provides the same guarantees with better
// compiler integration.

/// Data Memory Barrier — ensures memory access ordering.
///
/// All memory accesses before this barrier are visible to other observers
/// before any memory accesses after it.
///
/// ARM64: `dmb sy`  |  RISC-V: `fence rw, rw`  |  x86: compiler fence
#[inline(always)]
pub fn dmb() {
    #[cfg(target_arch = "aarch64")]
    unsafe {
        core::arch::asm!("dmb sy", options(nostack, preserves_flags));
    }
    #[cfg(target_arch = "riscv64")]
    unsafe {
        core::arch::asm!("fence rw, rw", options(nostack, preserves_flags));
    }
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("", options(nostack, preserves_flags));
    }
}

/// Data Synchronization Barrier — waits for all prior memory accesses.
///
/// Like `dmb()` but stronger: actually waits for completion rather than
/// just establishing ordering.
///
/// ARM64: `dsb sy`  |  RISC-V: `fence rw, rw`  |  x86: `mfence`
#[inline(always)]
pub fn dsb() {
    #[cfg(target_arch = "aarch64")]
    unsafe {
        core::arch::asm!("dsb sy", options(nostack, preserves_flags));
    }
    #[cfg(target_arch = "riscv64")]
    unsafe {
        core::arch::asm!("fence rw, rw", options(nostack, preserves_flags));
    }
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("mfence", options(nostack, preserves_flags));
    }
}

/// Instruction Synchronization Barrier — flushes the instruction pipeline.
///
/// Ensures all instructions after the barrier are fetched fresh from memory.
///
/// ARM64: `isb`  |  RISC-V: `fence.i`  |  x86: not needed (strong model)
#[inline(always)]
pub fn isb() {
    #[cfg(target_arch = "aarch64")]
    unsafe {
        core::arch::asm!("isb", options(nostack, preserves_flags));
    }
    #[cfg(target_arch = "riscv64")]
    unsafe {
        core::arch::asm!("fence.i", options(nostack, preserves_flags));
    }
    #[cfg(target_arch = "x86_64")]
    {
        // x86 has a strong memory model; no ISB equivalent needed
        // but we emit a compiler fence for safety
        core::sync::atomic::compiler_fence(core::sync::atomic::Ordering::SeqCst);
    }
}

/// Wait For Interrupt — CPU sleeps until an interrupt arrives.
///
/// Available on ARM64 and RISC-V (same mnemonic!). On x86, uses `hlt`.
#[inline(always)]
pub fn wfi() {
    #[cfg(target_arch = "aarch64")]
    unsafe {
        core::arch::asm!("wfi", options(nostack, nomem, preserves_flags));
    }
    #[cfg(target_arch = "riscv64")]
    unsafe {
        core::arch::asm!("wfi", options(nostack, nomem, preserves_flags));
    }
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("hlt", options(nostack, nomem, preserves_flags));
    }
}

/// Wait For Event — CPU sleeps until an event occurs (ARM64-specific).
///
/// On RISC-V this falls back to `wfi()`. On x86, this uses `pause`.
#[inline(always)]
pub fn wfe() {
    #[cfg(target_arch = "aarch64")]
    unsafe {
        core::arch::asm!("wfe", options(nostack, nomem, preserves_flags));
    }
    #[cfg(target_arch = "riscv64")]
    {
        wfi(); // No WFE on RISC-V
    }
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("pause", options(nostack, nomem, preserves_flags));
    }
}

/// Send Event — wakes all cores sleeping in `wfe()` (ARM64-specific).
///
/// On RISC-V and x86, this is a no-op (use IPI instead).
#[inline(always)]
pub fn sev() {
    #[cfg(target_arch = "aarch64")]
    unsafe {
        core::arch::asm!("sev", options(nostack, nomem, preserves_flags));
    }
    // No-op on RISC-V and x86
}

/// Spin-loop hint — tells the CPU "I'm busy-waiting, don't waste power."
///
/// ARM64: `yield`  |  RISC-V: `nop`  |  x86: `pause`
#[inline(always)]
pub fn cpu_relax() {
    #[cfg(target_arch = "aarch64")]
    unsafe {
        core::arch::asm!("yield", options(nostack, nomem, preserves_flags));
    }
    #[cfg(target_arch = "riscv64")]
    unsafe {
        core::arch::asm!("nop", options(nostack, nomem, preserves_flags));
    }
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("pause", options(nostack, nomem, preserves_flags));
    }
}

// =============================================================================
// MMIO ACCESS
// =============================================================================
//
// Volatile read/write for hardware registers. In C these use volatile
// pointer casts. In Rust we use `core::ptr::read_volatile` / `write_volatile`
// which provide the same guarantees with better ergonomics.
//
// SAFETY: All MMIO functions are unsafe because the caller must ensure:
//   1. The address points to a valid hardware register
//   2. The address is properly aligned for the access width
//   3. The access has no unintended side effects at this point in time

/// Write a 32-bit value to a hardware register.
///
/// # Safety
/// `addr` must point to a valid, aligned, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_write32(addr: usize, value: u32) {
    ptr::write_volatile(addr as *mut u32, value);
}

/// Read a 32-bit value from a hardware register.
///
/// # Safety
/// `addr` must point to a valid, aligned, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_read32(addr: usize) -> u32 {
    ptr::read_volatile(addr as *const u32)
}

/// Write a 64-bit value to a hardware register.
///
/// # Safety
/// `addr` must point to a valid, aligned, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_write64(addr: usize, value: u64) {
    ptr::write_volatile(addr as *mut u64, value);
}

/// Read a 64-bit value from a hardware register.
///
/// # Safety
/// `addr` must point to a valid, aligned, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_read64(addr: usize) -> u64 {
    ptr::read_volatile(addr as *const u64)
}

/// Write a 16-bit value to a hardware register.
///
/// # Safety
/// `addr` must point to a valid, aligned, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_write16(addr: usize, value: u16) {
    ptr::write_volatile(addr as *mut u16, value);
}

/// Read a 16-bit value from a hardware register.
///
/// # Safety
/// `addr` must point to a valid, aligned, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_read16(addr: usize) -> u16 {
    ptr::read_volatile(addr as *const u16)
}

/// Write an 8-bit value to a hardware register.
///
/// # Safety
/// `addr` must point to a valid, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_write8(addr: usize, value: u8) {
    ptr::write_volatile(addr as *mut u8, value);
}

/// Read an 8-bit value from a hardware register.
///
/// # Safety
/// `addr` must point to a valid, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_read8(addr: usize) -> u8 {
    ptr::read_volatile(addr as *const u8)
}

/// Write a 32-bit value with memory barriers on both sides.
///
/// Ensures strict ordering: previous writes complete before this one,
/// and this write completes before subsequent accesses.
///
/// # Safety
/// `addr` must point to a valid, aligned, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_write32_mb(addr: usize, value: u32) {
    dmb();
    mmio_write32(addr, value);
    dmb();
}

/// Read a 32-bit value with memory barriers on both sides.
///
/// # Safety
/// `addr` must point to a valid, aligned, memory-mapped hardware register.
#[inline(always)]
pub unsafe fn mmio_read32_mb(addr: usize) -> u32 {
    dmb();
    let val = mmio_read32(addr);
    dmb();
    val
}
