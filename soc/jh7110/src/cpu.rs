//! CPU operations for the JH7110 SoC (SiFive U74).
//!
//! Port of `jh7110_cpu.h`.  Provides RISC-V memory barriers, power hints,
//! and CSR access.
//!
//! # Key Difference from KyX1
//!
//! The KyX1 (SpacemiT X60) supports Zicbom (cache block operations) and
//! the V extension.  The SiFive U74 implements RV64GC only — no Zicbom,
//! no Vector, no B extension.  Therefore this file:
//!
//! - **Keeps** all `fence` instructions (standard RV64GC)
//! - **Keeps** `wfi`, `cpu_relax` (standard RV64GC)
//! - **Omits** any Zicbom cache block operations (those live in `cache.rs`
//!   using the SiFive L2 Flush64 register as a workaround)

/// Cache line size for the SiFive U74 (L1 and L2 both use 64-byte lines).
pub const CACHE_LINE_SIZE: usize = 64;

// ============================================================================
// Memory Barriers
// ============================================================================

/// Data Memory Barrier — orders all memory accesses.
///
/// RISC-V has no "order only" fence (unlike ARM's `dmb`), so this is
/// identical to [`dsb`].  Slightly conservative but always correct.
#[inline(always)]
pub fn dmb() {
    unsafe {
        core::arch::asm!("fence iorw, iorw", options(nostack, preserves_flags));
    }
}

/// Data Synchronization Barrier — orders and waits for all accesses.
#[inline(always)]
pub fn dsb() {
    unsafe {
        core::arch::asm!("fence iorw, iorw", options(nostack, preserves_flags));
    }
}

/// Instruction Synchronization Barrier — flushes the instruction pipeline.
///
/// `fence.i` is from Zifencei, part of the G extension (RV64GC).
#[inline(always)]
pub fn isb() {
    unsafe {
        core::arch::asm!("fence.i", options(nostack, preserves_flags));
    }
}

// ============================================================================
// CPU Power Hints
// ============================================================================

/// Wait For Interrupt — enter low-power state until the next interrupt.
#[inline(always)]
pub fn wfi() {
    unsafe {
        core::arch::asm!("wfi", options(nomem, nostack, preserves_flags));
    }
}

/// Wait For Event — polyfill as `wfi` (RISC-V has no wfe/sev pair).
#[inline(always)]
pub fn wfe() {
    wfi();
}

/// Send Event — no-op on RISC-V (no SEV instruction).
#[inline(always)]
pub fn sev() {}

/// Hint to the CPU that this is a spin-wait loop.
#[inline(always)]
pub fn cpu_relax() {
    unsafe {
        core::arch::asm!("nop", options(nomem, nostack, preserves_flags));
    }
}

/// Enter low-power idle state.
#[inline(always)]
pub fn cpu_idle() {
    wfi();
}

// ============================================================================
// CSR Access
// ============================================================================

/// Read the RISC-V `time` CSR (counts at platform timer frequency, 24 MHz).
#[inline(always)]
pub fn read_time() -> u64 {
    let t: u64;
    unsafe {
        core::arch::asm!("rdtime {}", out(reg) t, options(nomem, nostack, preserves_flags));
    }
    t
}

/// Read the RISC-V `cycle` CSR (counts at CPU clock frequency).
#[inline(always)]
pub fn read_cycle() -> u64 {
    let c: u64;
    unsafe {
        core::arch::asm!("rdcycle {}", out(reg) c, options(nomem, nostack, preserves_flags));
    }
    c
}
