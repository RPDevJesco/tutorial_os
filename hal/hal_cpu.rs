/*
 * hal/hal_cpu — CPU Operations Contract for Tutorial-OS (Rust)
 *
 * This module defines the interface that EVERY platform must provide for
 * low-level CPU operations. It's the contract between portable kernel code
 * and architecture-specific implementations.
 *
 * WHAT THIS FILE IS:
 *   A specification. It documents every operation the kernel needs from the
 *   CPU, explains when and why you'd use each one, and provides the portable
 *   subset (MMIO read/write) that works on all architectures.
 *
 * WHAT THIS FILE IS NOT:
 *   An implementation. Architecture-specific operations (barriers, cache,
 *   timing, CPU hints) are NOT defined here — they're in the platform modules
 *   that implement this trait.
 *
 * HOW IT FITS TOGETHER (Rust Edition):
 *
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │                      hal/hal_cpu                             │
 *   │  • Documents the full contract via CpuOps trait             │
 *   │  • Provides portable: mmio_read(), mmio_write() (in types)  │
 *   │  • Declares what platforms must implement                    │
 *   └──────────────┬──────────────────────────────┬────────────────┘
 *                  │                              │
 *   ┌──────────────▼──────────────┐ ┌─────────────▼────────────────┐
 *   │  soc/bcm2710/hal_cpu         │ │  soc/kyx1/hal_cpu             │
 *   │  (ARM64 / BCM2710)          │ │  (RISC-V / Ky X1)            │
 *   │                             │ │                               │
 *   │  impl CpuOps for Bcm2710   │ │  impl CpuOps for KyX1        │
 *   │  + ARM64 barriers           │ │  + RISC-V fence barriers      │
 *   │  + ARM64 cache ops          │ │  + Zicbom cache ops           │
 *   │  + BCM system timer         │ │  + rdtime @ 24 MHz            │
 *   │  + wfe/wfi/sev              │ │  + wfi                        │
 *   │  + BCM addresses            │ │  + Ky X1 addresses            │
 *   └────────────────────────────┘ └───────────────────────────────┘
 *
 * ADDING A NEW PLATFORM:
 *   1. Create your platform module (e.g., soc/myboard/hal_cpu)
 *   2. Import this trait: `use crate::hal::cpu::CpuOps;`
 *   3. Implement every method listed in the CpuOps trait
 *   4. The build system selects the correct implementation via features
 *
 * WHY TRAITS INSTEAD OF #[cfg] SWITCHING?
 *   In C, we use separate header files per platform. In Rust, traits give
 *   us the same "contract + implementations" pattern with compile-time
 *   enforcement. The compiler ensures every platform implements every
 *   required operation — no missing function linker errors at 2 AM.
 *
 *   This is analogous to how Linux has asm/barrier.h per architecture,
 *   but with Rust's type system guaranteeing completeness.
 */

#![allow(dead_code)]

// =============================================================================
// PORTABLE: MEMORY-MAPPED I/O
// =============================================================================
//
// MMIO read/write functions live in hal/hal_types because they are truly
// portable — volatile pointer access works identically on ARM64, RISC-V,
// x86, and everything else. They are re-exported here for convenience.
//
// See hal_types for: mmio_read32, mmio_write32, mmio_read8, mmio_write8, etc.

pub use super::types::{
    mmio_read8, mmio_read16, mmio_read32, mmio_read64,
    mmio_write8, mmio_write16, mmio_write32, mmio_write64,
};

// =============================================================================
// CPU OPERATIONS CONTRACT (Trait)
// =============================================================================
//
// Every platform must implement this trait. It covers:
//   1. Memory barriers (dmb, dsb, isb)
//   2. CPU power hints (wfi, wfe, sev, cpu_relax)
//   3. Timing (micros, micros64, delay_us, delay_ms)
//   4. Cache management (clean, invalidate, flush)
//
// The trait uses `&self` on a zero-sized type so the compiler can inline
// and optimize everything away — same cost as the C version's static
// inline functions.

/// The CPU operations contract.
///
/// Every platform implements this trait on a zero-sized struct. Because
/// the struct has no data, all methods are effectively static and the
/// compiler inlines them completely — zero overhead vs C.
///
/// # Example Implementation
///
/// ```rust,ignore
/// pub struct Bcm2710Cpu;
///
/// impl CpuOps for Bcm2710Cpu {
///     fn dmb(&self) {
///         unsafe { core::arch::asm!("dmb sy", options(nostack, preserves_flags)); }
///     }
///     // ... etc
/// }
/// ```
pub trait CpuOps {
    // =========================================================================
    // MEMORY BARRIERS
    // =========================================================================

    /// Data Memory Barrier — ensures all memory accesses BEFORE this point
    /// are visible to other observers BEFORE any memory accesses AFTER it.
    /// Does NOT wait for completion — just establishes ordering.
    ///
    /// ARM64: `dmb sy`  |  RISC-V: `fence iorw, iorw`  |  x86: compiler fence
    ///
    /// Use when: Between writing data and writing a "doorbell" register that
    /// tells hardware to read the data.
    fn dmb(&self);

    /// Data Synchronization Barrier — like `dmb()` but WAITS for all prior
    /// memory accesses to actually complete before continuing.
    /// Stronger (and slower) than `dmb()`.
    ///
    /// ARM64: `dsb sy`  |  RISC-V: `fence iorw, iorw`
    ///
    /// Use when: After writing to a control register that changes system
    /// behavior (e.g., enabling the MMU).
    fn dsb(&self);

    /// Instruction Synchronization Barrier — flushes the CPU instruction
    /// pipeline. Ensures all instructions after the ISB are fetched fresh.
    ///
    /// ARM64: `isb`  |  RISC-V: `fence.i`
    ///
    /// Use when: After modifying code in memory or changing system registers
    /// that affect instruction execution (enabling MMU, changing exception
    /// level).
    fn isb(&self);

    // =========================================================================
    // CPU POWER HINTS
    // =========================================================================

    /// Wait For Interrupt — CPU sleeps until an interrupt arrives.
    ///
    /// Available on both ARM64 and RISC-V (same mnemonic!).
    /// On x86, maps to `hlt`.
    ///
    /// Use in: Idle loops, halt conditions, parked secondary cores.
    fn wfi(&self);

    /// Wait For Event — CPU sleeps until an event occurs (not just interrupts).
    ///
    /// ARM64-specific. On RISC-V, implemented as `wfi()` or `nop`.
    /// On x86, maps to `pause`.
    ///
    /// Use in: Spinlocks (with matching `sev()` from the unlocker).
    fn wfe(&self);

    /// Send Event — wakes all cores sleeping in `wfe()`.
    ///
    /// ARM64-specific. On RISC-V, this is a no-op (use IPI instead).
    ///
    /// Use in: After releasing a spinlock, to wake waiters.
    fn sev(&self);

    /// Spin-loop hint — tells the CPU "I'm busy-waiting, don't waste power."
    ///
    /// ARM64: `yield`  |  RISC-V: `nop`  |  x86: `pause`
    ///
    /// Use in: Any busy-wait loop (polling a register, waiting for a flag).
    fn cpu_relax(&self);

    // =========================================================================
    // TIMING
    // =========================================================================

    /// Current time in microseconds (32-bit, wraps every ~71 minutes).
    ///
    /// BCM2710: Reads system timer at 1 MHz.
    /// Ky X1: Reads `rdtime` at 24 MHz, divides by 24.
    fn micros(&self) -> u32;

    /// Current time in microseconds (64-bit, effectively never wraps).
    ///
    /// Prefer this for long-duration timing. Use `micros()` for tight loops
    /// where the 64-bit division overhead matters.
    fn micros64(&self) -> u64;

    /// Busy-wait for the specified number of microseconds.
    ///
    /// MUST be accurate (not a cycle-counting spin loop).
    fn delay_us(&self, us: u32);

    /// Busy-wait for the specified number of milliseconds.
    ///
    /// Convenience wrapper: `delay_us(ms * 1000)`.
    fn delay_ms(&self, ms: u32) {
        self.delay_us(ms * 1000);
    }

    // =========================================================================
    // CACHE MANAGEMENT
    // =========================================================================

    /// The CPU's data cache line size in bytes.
    ///
    /// Both ARM Cortex-A53 and Ky X1 X60 use 64-byte cache lines.
    const CACHE_LINE_SIZE: usize;

    /// Write dirty cache lines to DRAM. The cache lines remain valid.
    ///
    /// ARM64: `dc cvac` loop  |  RISC-V: `cbo.clean` loop (Zicbom)
    ///
    /// Use when: CPU wrote data that a device needs to read.
    /// Example: After writing pixels to framebuffer, before DPU scans them.
    ///
    /// # Safety
    /// `start` must be a valid memory address and the range `[start, start + size)`
    /// must not overlap with memory used for other purposes during the operation.
    unsafe fn clean_dcache_range(&self, start: usize, size: usize);

    /// Discard cached data (WITHOUT writing back). Force next read from DRAM.
    ///
    /// ARM64: `dc ivac` loop  |  RISC-V: `cbo.inval` loop (Zicbom)
    ///
    /// **WARNING**: Dirty data in cache will be LOST!
    ///
    /// Use when: Device wrote data to DRAM that CPU needs to read fresh.
    /// Example: After DMA transfer completes into a receive buffer.
    ///
    /// # Safety
    /// `start` must be a valid memory address. Dirty data in the affected
    /// cache lines will be discarded without write-back.
    unsafe fn invalidate_dcache_range(&self, start: usize, size: usize);

    /// Clean THEN invalidate — write back dirty data, then discard cache line.
    ///
    /// ARM64: `dc civac` loop  |  RISC-V: `cbo.flush` loop (Zicbom)
    ///
    /// Use when: Bidirectional shared buffer (both CPU and device read/write).
    ///
    /// # Safety
    /// `start` must be a valid memory address and the range must be valid.
    unsafe fn flush_dcache_range(&self, start: usize, size: usize);
}

// =============================================================================
// NOTES ON THE TRAIT APPROACH
// =============================================================================
//
// Q: "Why not just use free functions with #[cfg(target_arch)] like hal_types?"
//
// A: Both approaches work! We use both in Tutorial-OS:
//
//    - hal_types uses #[cfg] for barriers and MMIO because these are truly
//      universal operations with well-known instruction mappings.
//
//    - CpuOps uses a trait because platform implementations differ in more
//      complex ways — timer register addresses, cache line sizes, clock
//      frequencies — that can't be captured in a simple #[cfg] switch.
//
//    The trait ensures the compiler checks that every platform implements
//    every operation, and the zero-sized struct pattern means there's no
//    runtime cost.
//
// Q: "Do I need to construct a Bcm2710Cpu to call these?"
//
// A: You can, but the idiomatic pattern is to have a type alias:
//
//    ```rust,ignore
//    #[cfg(feature = "bcm2710")]
//    pub type Cpu = soc::bcm2710::Bcm2710Cpu;
//
//    // Then in kernel code:
//    let cpu = Cpu;
//    cpu.delay_ms(100);
//    ```
//
//    Since `Cpu` is zero-sized, constructing it costs nothing.
