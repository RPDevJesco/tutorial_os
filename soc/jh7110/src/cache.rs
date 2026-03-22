//! Cache management for the StarFive JH7110.
//!
//! Port of `cache.c`.  The SiFive U74 has **no Zicbom** extension, so we
//! cannot use `cbo.flush` / `cbo.clean` / `cbo.inval` instructions.
//!
//! Instead, we use the SiFive L2 cache controller's **Flush64** register
//! at `0x0201_0000 + 0x200`.  Writing a physical address to this register
//! flushes the corresponding 64-byte cache line back to DRAM, making it
//! visible to bus masters like the DC8200 display controller's DMA engine.
//!
//! # When This Is Called
//!
//! Called once after all drawing is complete, before `display_present()`.
//! For a 1920×1080×4 framebuffer: ~130,000 cache line flushes.
//! At bare-metal MMIO speeds this completes in well under 100ms.
//!
//! # Contrast with KyX1
//!
//! The KyX1 (SpacemiT X60) has Zicbom and uses `cbo.clean` loops in
//! `cache.S`.  Same HAL symbol, architecturally different implementation.

use crate::regs;

/// Flush a physical address range from L2 cache to DRAM.
///
/// Uses the SiFive L2 cache controller's Flush64 register.
/// Each write flushes one 64-byte cache line.
pub fn l2_flush_range(phys_addr: usize, size: usize) {
    let flush64 = (regs::L2_CACHE_BASE + regs::L2_FLUSH64_OFFSET) as *mut u64;
    let mut line = phys_addr & !63;
    let end = phys_addr + size;

    while line < end {
        unsafe {
            core::ptr::write_volatile(flush64, line as u64);
        }
        line += 64;
    }

    // Fence to ensure all flushes complete before we continue
    crate::cpu::dsb();
}

/// Clean the data cache for a given range (HAL-compatible name).
///
/// Maps to [`l2_flush_range`] on JH7110.
pub fn clean_dcache_range(start: usize, size: usize) {
    l2_flush_range(start, size);
}
