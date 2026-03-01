// mmio.rs — Memory-Mapped I/O and System Primitives (Multi-Architecture)
// ======================================================================
//
// This module provides low-level hardware access primitives used by all
// drivers across all architectures. It's the single file that makes
// "portable" drivers like framebuffer.rs compile on ARM64, RISC-V, and x86_64.
//
// HOW IT WORKS:
//   Every function detects the target architecture at compile time:
//     #[cfg(target_arch = "aarch64")]  → ARM64 inline assembly (original code)
//     #[cfg(target_arch = "riscv64")]  → RISC-V inline assembly or extern calls
//     #[cfg(target_arch = "x86_64")]   → x86_64 implementations
//
//   For ARM64, everything is exactly as it was — no behavior changes.
//
//   For RISC-V:
//     - Barriers:   fence instructions (in hal_types, re-exported here)
//     - CPU hints:  wfi (in hal_types), compiler barriers where no equivalent
//     - Timing:     extern to soc/kyx1/timer.rs (uses rdtime @ 24 MHz)
//     - Cache ops:  extern to boot/riscv64/cache.S (uses Zicbom instructions)
//                   Cache ops are extern because Zicbom needs a special -march
//                   flag that only cache.S is compiled with.
//     - BCM addrs:  only defined on ARM64 (RISC-V has kyx1_regs instead)
//
// WHAT'S IN THIS FILE:
//   - Re-exports of MMIO read/write from hal_types (portable across all platforms)
//   - Re-exports of memory barriers from hal_types (dmb, dsb, isb)
//   - Re-exports of CPU power hints from hal_types (wfe, wfi, sev, cpu_relax)
//   - Timing (micros, delay_us, delay_ms)                    ← NEW here
//   - Cache management (clean, invalidate, flush)            ← NEW here
//   - Platform-specific hardware base addresses              ← NEW here
//
// WHY RE-EXPORT?
//   Portable drivers should `use crate::common::mmio` as their single import
//   for all hardware primitives. This mirrors how the C code does
//   `#include "mmio.h"` and gets everything in one shot.

#![allow(dead_code)]

// =============================================================================
// RE-EXPORTS FROM hal_types
// =============================================================================
//
// MMIO access, barriers, and CPU hints are truly portable — they use the same
// volatile reads/writes and inline assembly patterns on every architecture.
// hal_types owns the implementations; we just re-export them so portable
// drivers have a single import point.

// MMIO read/write — volatile pointer access for hardware registers
pub use crate::hal::types::{
    mmio_read8, mmio_read16, mmio_read32, mmio_read64,
    mmio_write8, mmio_write16, mmio_write32, mmio_write64,
    mmio_read32_mb, mmio_write32_mb,
};

// Memory barriers — ordering guarantees for hardware communication
pub use crate::hal::types::{dmb, dsb, isb};

// CPU power hints — sleep, wake, and spin-loop primitives
pub use crate::hal::types::{wfi, wfe, sev, cpu_relax};


// =============================================================================
// HARDWARE BASE ADDRESSES
// =============================================================================
//
// BCM addresses are only relevant on ARM64 Pi platforms.
// RISC-V platforms define their addresses in soc/kyx1/kyx1_regs.
// x86_64 platforms will define theirs in their respective SoC modules.

#[cfg(target_arch = "aarch64")]
pub mod addr {
    pub const PERIPHERAL_BASE: usize = 0x3F00_0000;
    pub const ARM_LOCAL_BASE: usize  = 0x4000_0000;
    pub const SYSTIMER_BASE: usize   = PERIPHERAL_BASE + 0x0000_3000;
    pub const SYSTIMER_CLO: usize    = SYSTIMER_BASE + 0x04;
    pub const SYSTIMER_CHI: usize    = SYSTIMER_BASE + 0x08;
}

/// Cache line size — same on Cortex-A53 and X60 cores (64 bytes).
pub const CACHE_LINE_SIZE: usize = 64;


// =============================================================================
// TIMING
// =============================================================================
//
// ARM64/BCM: Uses BCM283x system timer @ 1 MHz (inline, register reads)
// RISC-V:    Uses rdtime CSR @ 24 MHz, provided by soc/kyx1/timer.rs (extern)
// x86_64:    Will use HPET or TSC (TBD)
//
// All provide the same API: micros(), micros64(), delay_us(), delay_ms()
// with microsecond-resolution timing.

#[cfg(target_arch = "aarch64")]
mod timing {
    use super::addr::{SYSTIMER_CLO, SYSTIMER_CHI};
    use crate::hal::types::mmio_read32;

    /// Current time in microseconds (32-bit, wraps every ~71 minutes).
    ///
    /// Reads the BCM system timer counter low word directly.
    /// The system timer runs at 1 MHz, so each tick = 1 microsecond.
    #[inline(always)]
    pub fn micros() -> u32 {
        unsafe { mmio_read32(SYSTIMER_CLO) }
    }

    /// Current time in microseconds (64-bit, effectively never wraps).
    ///
    /// Reads both the high and low words of the 64-bit system timer.
    /// Re-reads if the high word changed during the read (handles wrap).
    #[inline(always)]
    pub fn micros64() -> u64 {
        unsafe {
            let hi = mmio_read32(SYSTIMER_CHI);
            let lo = mmio_read32(SYSTIMER_CLO);
            // Re-read if high word changed (handles wrap during read)
            if mmio_read32(SYSTIMER_CHI) != hi {
                let hi = mmio_read32(SYSTIMER_CHI);
                let lo = mmio_read32(SYSTIMER_CLO);
                return ((hi as u64) << 32) | (lo as u64);
            }
            ((hi as u64) << 32) | (lo as u64)
        }
    }

    /// Busy-wait for the specified number of microseconds.
    ///
    /// Uses the hardware timer for accuracy — not a cycle-counting spin loop.
    #[inline(always)]
    pub fn delay_us(us: u32) {
        let start = micros();
        while micros().wrapping_sub(start) < us {
            unsafe {
                core::arch::asm!("yield", options(nostack, nomem, preserves_flags));
            }
        }
    }

    /// Busy-wait for the specified number of milliseconds.
    #[inline(always)]
    pub fn delay_ms(ms: u32) {
        delay_us(ms * 1000);
    }
}

#[cfg(target_arch = "riscv64")]
mod timing {
    // On RISC-V, timing is provided by soc/kyx1/timer.rs which reads
    // the rdtime CSR at 24 MHz and converts to microseconds.
    // These are declared extern "C" here so portable drivers can call them.
    extern "C" {
        pub fn micros() -> u32;
        pub fn micros64() -> u64;
        pub fn delay_us(us: u32);
        pub fn delay_ms(ms: u32);
    }
}

#[cfg(target_arch = "x86_64")]
mod timing {
    // x86_64 timing will be provided by the platform SoC module
    // (HPET, TSC, or PIT depending on what's available).
    extern "C" {
        pub fn micros() -> u32;
        pub fn micros64() -> u64;
        pub fn delay_us(us: u32);
        pub fn delay_ms(ms: u32);
    }
}

// Re-export timing functions at module level.
//
// On ARM64 these are safe Rust functions (inline reads from known-good
// timer registers). On RISC-V and x86_64 they're extern "C" calls that
// require unsafe at the call site — the caller knows they're talking to
// hardware.
#[cfg(target_arch = "aarch64")]
pub use timing::{micros, micros64, delay_us, delay_ms};

#[cfg(target_arch = "riscv64")]
pub use timing::{micros, micros64, delay_us, delay_ms};

#[cfg(target_arch = "x86_64")]
pub use timing::{micros, micros64, delay_us, delay_ms};


// =============================================================================
// CACHE MANAGEMENT
// =============================================================================
//
// ARM64: dc cvac/ivac/civac instructions (inline)
// RISC-V: Zicbom cbo.clean/inval/flush instructions (extern from cache.S)
// x86_64: CLFLUSH / CLFLUSHOPT / WBINVD (TBD)
//
// WHY RISC-V CACHE OPS ARE EXTERN:
//   The cbo.* instructions require -march=rv64gc_zicbom which is only
//   enabled for cache.S (via CACHE_ASFLAGS in board.mk). If we inlined
//   them here, every .rs file would need that flag, which would fail on
//   toolchains that don't support it or pollute the flag space.
//   Calling out to cache.S keeps the special flag isolated.
//
// Same API, same semantics across all architectures:
//   clean_dcache_range()       — write dirty lines to RAM
//   invalidate_dcache_range()  — discard cached data
//   flush_dcache_range()       — clean + invalidate

/// Write dirty cache lines to DRAM. The cache lines remain valid.
///
/// Use when: CPU wrote data that a device needs to read.
/// Example: After writing pixels to framebuffer, before DPU scans them.
///
/// # Safety
/// `start` must be a valid memory address and the range `[start, start + size)`
/// must be accessible. The operation may affect adjacent data sharing the same
/// cache lines.
#[cfg(target_arch = "aarch64")]
pub unsafe fn clean_dcache_range(start: usize, size: usize) {
    let mut addr = start & !(CACHE_LINE_SIZE - 1);
    let end = start + size;
    while addr < end {
        core::arch::asm!("dc cvac, {}", in(reg) addr, options(nostack, preserves_flags));
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

/// Discard cached data (WITHOUT writing back). Force next read from DRAM.
///
/// **WARNING**: Dirty data in cache will be LOST!
///
/// Use when: Device wrote data to DRAM that CPU needs to read fresh.
/// Example: After DMA transfer completes into a receive buffer.
///
/// # Safety
/// `start` must be a valid memory address. Dirty data in the affected
/// cache lines will be discarded without write-back.
#[cfg(target_arch = "aarch64")]
pub unsafe fn invalidate_dcache_range(start: usize, size: usize) {
    let mut addr = start & !(CACHE_LINE_SIZE - 1);
    let end = start + size;
    while addr < end {
        core::arch::asm!("dc ivac, {}", in(reg) addr, options(nostack, preserves_flags));
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

/// Clean THEN invalidate — write back dirty data, then discard cache line.
///
/// Use when: Bidirectional shared buffer (both CPU and device read/write).
///
/// # Safety
/// `start` must be a valid memory address and the range must be accessible.
#[cfg(target_arch = "aarch64")]
pub unsafe fn flush_dcache_range(start: usize, size: usize) {
    let mut addr = start & !(CACHE_LINE_SIZE - 1);
    let end = start + size;
    while addr < end {
        core::arch::asm!("dc civac, {}", in(reg) addr, options(nostack, preserves_flags));
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

// On RISC-V, cache management is provided by boot/riscv64/cache.S
// which is compiled with -march=rv64gc_zicbom_zicboz.
//
// Same function names, same signatures, same semantics as ARM64.
#[cfg(target_arch = "riscv64")]
extern "C" {
    pub fn clean_dcache_range(start: usize, size: usize);
    pub fn invalidate_dcache_range(start: usize, size: usize);
    pub fn flush_dcache_range(start: usize, size: usize);
}

// x86_64 cache management — will use CLFLUSH / CLFLUSHOPT.
// Stubbed as extern until the LattePanda port is implemented.
#[cfg(target_arch = "x86_64")]
extern "C" {
    pub fn clean_dcache_range(start: usize, size: usize);
    pub fn invalidate_dcache_range(start: usize, size: usize);
    pub fn flush_dcache_range(start: usize, size: usize);
}