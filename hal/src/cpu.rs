//! CPU memory barriers, power hints, and cache management.
//!
//! This module is the Rust equivalent of the barrier/hint macros spread
//! across `hal_types.h` and `mmio.h` in the C implementation.  Every
//! function is `cfg`-gated to the target architecture and compiles to a
//! single inline assembly instruction (or a compiler fence on x86 where
//! hardware handles coherency).
//!
//! # Architecture Semantics
//!
//! | Operation | ARM64 | RISC-V | x86_64 |
//! |-----------|-------|--------|--------|
//! | [`dmb`] | `dmb sy` | `fence iorw,iorw` | `mfence` |
//! | [`dsb`] | `dsb sy` | `fence iorw,iorw` | `mfence` |
//! | [`isb`] | `isb` | `fence.i` | compiler fence |
//! | [`wfe`] | `wfe` | `wfi` | `hlt` |
//! | [`wfi`] | `wfi` | `wfi` | `hlt` |
//! | [`sev`] | `sev` | no-op | no-op |
//! | [`cpu_relax`] | `yield` | `nop` | `pause` |
//!
//! RISC-V does not distinguish between ordering-only (`dmb`) and
//! completion (`dsb`) barriers — `fence iorw,iorw` provides both
//! semantics simultaneously, matching what real RISC-V kernels do.

/// Data Memory Barrier — orders all prior memory accesses before all
/// subsequent ones.  Does **not** wait for completion.
#[inline(always)]
pub fn dmb() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("dmb sy", options(nostack, preserves_flags)) }

    #[cfg(target_arch = "riscv64")]
    unsafe { core::arch::asm!("fence iorw, iorw", options(nostack, preserves_flags)) }

    #[cfg(target_arch = "x86_64")]
    unsafe { core::arch::asm!("mfence", options(nostack, preserves_flags)) }
}

/// Data Synchronization Barrier — orders **and waits for completion** of
/// all prior memory accesses.
#[inline(always)]
pub fn dsb() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("dsb sy", options(nostack, preserves_flags)) }

    #[cfg(target_arch = "riscv64")]
    unsafe { core::arch::asm!("fence iorw, iorw", options(nostack, preserves_flags)) }

    #[cfg(target_arch = "x86_64")]
    unsafe { core::arch::asm!("mfence", options(nostack, preserves_flags)) }
}

/// Instruction Synchronization Barrier — flushes the instruction pipeline.
///
/// Required after writing executable code to memory or changing system
/// registers that affect instruction execution.
#[inline(always)]
pub fn isb() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("isb", options(nostack, preserves_flags)) }

    #[cfg(target_arch = "riscv64")]
    unsafe { core::arch::asm!("fence.i", options(nostack, preserves_flags)) }

    // x86: pipeline serialisation via a compiler fence is sufficient
    // for bare-metal use (no JIT, no segment reloads).
    #[cfg(target_arch = "x86_64")]
    core::sync::atomic::compiler_fence(core::sync::atomic::Ordering::SeqCst);
}

/// Wait For Event — low-power sleep until an event or interrupt.
///
/// On RISC-V there is no `wfe`; `wfi` is the closest equivalent.
/// On x86 `hlt` sleeps until the next interrupt.
#[inline(always)]
pub fn wfe() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("wfe", options(nomem, nostack, preserves_flags)) }

    #[cfg(target_arch = "riscv64")]
    unsafe { core::arch::asm!("wfi", options(nomem, nostack, preserves_flags)) }

    #[cfg(target_arch = "x86_64")]
    unsafe { core::arch::asm!("hlt", options(nomem, nostack, preserves_flags)) }
}

/// Wait For Interrupt — sleep until an interrupt fires.
#[inline(always)]
pub fn wfi() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("wfi", options(nomem, nostack, preserves_flags)) }

    #[cfg(target_arch = "riscv64")]
    unsafe { core::arch::asm!("wfi", options(nomem, nostack, preserves_flags)) }

    #[cfg(target_arch = "x86_64")]
    unsafe { core::arch::asm!("hlt", options(nomem, nostack, preserves_flags)) }
}

/// Send Event — wakes cores sleeping in [`wfe`].
///
/// ARM64-specific.  No-op on RISC-V and x86 (inter-core wake via IPI).
#[inline(always)]
pub fn sev() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("sev", options(nomem, nostack, preserves_flags)) }

    // RISC-V / x86: no equivalent — inter-core signaling uses IPI.
    #[cfg(not(target_arch = "aarch64"))]
    { /* intentional no-op */ }
}

/// No-operation.
#[inline(always)]
pub fn nop() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("nop", options(nomem, nostack, preserves_flags)) }

    #[cfg(target_arch = "riscv64")]
    unsafe { core::arch::asm!("nop", options(nomem, nostack, preserves_flags)) }

    #[cfg(target_arch = "x86_64")]
    unsafe { core::arch::asm!("nop", options(nomem, nostack, preserves_flags)) }
}

/// Spin-loop hint — tells the CPU "I'm busy-waiting."
///
/// Reduces power draw and improves SMT throughput.
#[inline(always)]
pub fn cpu_relax() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("yield", options(nomem, nostack, preserves_flags)) }

    #[cfg(target_arch = "riscv64")]
    unsafe { core::arch::asm!("nop", options(nomem, nostack, preserves_flags)) }

    #[cfg(target_arch = "x86_64")]
    unsafe { core::arch::asm!("pause", options(nomem, nostack, preserves_flags)) }
}

// ============================================================================
// Cache Management (ARM64 inline, RISC-V extern, x86 no-op)
// ============================================================================

/// Cache line size in bytes (64 on Cortex-A53/A72, SiFive U74, X60, Intel N-series).
pub const CACHE_LINE_SIZE: usize = 64;

/// Clean (write-back) data cache lines covering `[start, start+len)`.
///
/// The cache lines remain valid after cleaning.
///
/// - ARM64: `dc cvac` per line + `dsb`
/// - RISC-V: implemented in `cache.S` (`cbo.clean` on KYX1) or
///   SiFive L2 Flush64 MMIO on JH7110 — provided by the SoC crate
/// - x86_64: no-op (hardware cache coherent)
#[cfg(target_arch = "aarch64")]
pub fn clean_dcache_range(start: usize, len: usize) {
    let mut addr = start & !(CACHE_LINE_SIZE - 1);
    let end = start + len;
    while addr < end {
        unsafe { core::arch::asm!("dc cvac, {}", in(reg) addr, options(nostack)) };
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

/// Invalidate data cache lines covering `[start, start+len)`.
///
/// **Dirty data in cache will be lost!**
///
/// - ARM64: `dc ivac` per line + `dsb`
#[cfg(target_arch = "aarch64")]
pub fn invalidate_dcache_range(start: usize, len: usize) {
    let mut addr = start & !(CACHE_LINE_SIZE - 1);
    let end = start + len;
    while addr < end {
        unsafe { core::arch::asm!("dc ivac, {}", in(reg) addr, options(nostack)) };
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

/// Flush (clean + invalidate) data cache lines covering `[start, start+len)`.
///
/// - ARM64: `dc civac` per line + `dsb`
#[cfg(target_arch = "aarch64")]
pub fn flush_dcache_range(start: usize, len: usize) {
    let mut addr = start & !(CACHE_LINE_SIZE - 1);
    let end = start + len;
    while addr < end {
        unsafe { core::arch::asm!("dc civac, {}", in(reg) addr, options(nostack)) };
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

// x86_64: cache management is handled by hardware — intentional no-ops.
// Drivers that call these functions compile and run correctly on all
// architectures without cfg gates at the call site.

#[cfg(target_arch = "x86_64")]
pub fn clean_dcache_range(_start: usize, _len: usize) {}

#[cfg(target_arch = "x86_64")]
pub fn invalidate_dcache_range(_start: usize, _len: usize) {}

#[cfg(target_arch = "x86_64")]
pub fn flush_dcache_range(_start: usize, _len: usize) {}

// RISC-V: cache ops are provided by each SoC crate because the
// mechanism varies (Zicbom cbo.clean on KYX1 vs SiFive L2 Flush64 on
// JH7110).  The SoC crate re-exports them under these same names for
// portable driver code.  We declare stubs here that `#[cfg(not(...))]`
// compiles away on ARM64 and x86.
//
// When building for RISC-V, the SoC crate's versions take precedence
// because SoC crates depend on `hal` (not the reverse), and the portable
// `drivers` crate calls the SoC's implementations through the DMA trait.
//
// NOTE: We intentionally do NOT provide stub implementations here for
// riscv64 — the SoC crate MUST supply them.  A missing implementation
// is a link-time error, not a silent no-op.
