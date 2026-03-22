//! Memory-Mapped I/O and port I/O primitives.
//!
//! This module is the Rust equivalent of `common/mmio.h`.  It provides:
//!
//! - **MMIO read/write** — volatile pointer access for hardware registers
//!   (works identically on ARM64, RISC-V, and x86_64 MMIO ranges).
//! - **Barrier-wrapped MMIO** — read/write bracketed by data memory barriers.
//! - **x86_64 port I/O** — `in`/`out` instructions for the separate I/O
//!   address space (legacy UART at `0x3F8`, PCI config space, etc.).
//!
//! # Safety
//!
//! All functions in this module are `unsafe` because they perform raw
//! volatile memory accesses to hardware registers.  The caller must ensure
//! the address is valid and properly mapped for the access width.

use core::ptr;

// ============================================================================
// MMIO Read / Write
// ============================================================================
//
// Volatile pointer access prevents the compiler from:
//   - Caching a register value in a CPU register across a loop
//   - Eliminating a "redundant" write (hardware sees both!)
//   - Reordering accesses (hardware registers are order-sensitive)

/// Write a 32-bit value to a memory-mapped hardware register.
///
/// # Safety
///
/// `addr` must point to a valid, mapped, 32-bit-aligned hardware register.
#[inline(always)]
pub unsafe fn write32(addr: usize, value: u32) {
    unsafe { ptr::write_volatile(addr as *mut u32, value) }
}

/// Read a 32-bit value from a memory-mapped hardware register.
///
/// # Safety
///
/// `addr` must point to a valid, mapped, 32-bit-aligned hardware register.
#[inline(always)]
pub unsafe fn read32(addr: usize) -> u32 {
    unsafe { ptr::read_volatile(addr as *const u32) }
}

/// Write a 16-bit value to a memory-mapped hardware register.
///
/// # Safety
///
/// `addr` must point to a valid, mapped, 16-bit-aligned hardware register.
#[inline(always)]
pub unsafe fn write16(addr: usize, value: u16) {
    unsafe { ptr::write_volatile(addr as *mut u16, value) }
}

/// Read a 16-bit value from a memory-mapped hardware register.
///
/// # Safety
///
/// `addr` must point to a valid, mapped, 16-bit-aligned hardware register.
#[inline(always)]
pub unsafe fn read16(addr: usize) -> u16 {
    unsafe { ptr::read_volatile(addr as *const u16) }
}

/// Write an 8-bit value to a memory-mapped hardware register.
///
/// # Safety
///
/// `addr` must point to a valid, mapped hardware register.
#[inline(always)]
pub unsafe fn write8(addr: usize, value: u8) {
    unsafe { ptr::write_volatile(addr as *mut u8, value) }
}

/// Read an 8-bit value from a memory-mapped hardware register.
///
/// # Safety
///
/// `addr` must point to a valid, mapped hardware register.
#[inline(always)]
pub unsafe fn read8(addr: usize) -> u8 {
    unsafe { ptr::read_volatile(addr as *const u8) }
}

/// Write a 64-bit value to a memory-mapped hardware register.
///
/// # Safety
///
/// `addr` must point to a valid, mapped, 64-bit-aligned hardware register.
#[inline(always)]
pub unsafe fn write64(addr: usize, value: u64) {
    unsafe { ptr::write_volatile(addr as *mut u64, value) }
}

/// Read a 64-bit value from a memory-mapped hardware register.
///
/// # Safety
///
/// `addr` must point to a valid, mapped, 64-bit-aligned hardware register.
#[inline(always)]
pub unsafe fn read64(addr: usize) -> u64 {
    unsafe { ptr::read_volatile(addr as *const u64) }
}

// ============================================================================
// Barrier-Wrapped MMIO
// ============================================================================
//
// For registers that require ordering guarantees with respect to other
// memory operations (e.g., mailbox doorbell writes, DMA control registers).

/// Data memory barrier (private, avoids dependency on `hal` crate).
#[inline(always)]
fn dmb() {
    #[cfg(target_arch = "aarch64")]
    unsafe { core::arch::asm!("dmb sy", options(nostack, preserves_flags)) }

    #[cfg(target_arch = "riscv64")]
    unsafe { core::arch::asm!("fence iorw, iorw", options(nostack, preserves_flags)) }

    #[cfg(target_arch = "x86_64")]
    unsafe { core::arch::asm!("mfence", options(nostack, preserves_flags)) }
}

/// Write a 32-bit value with data memory barriers before and after.
///
/// # Safety
///
/// Same requirements as [`write32`], plus the caller must be in a context
/// where memory barriers are meaningful (not in an exception handler that
/// has already serialized).
#[inline(always)]
pub unsafe fn write32_mb(addr: usize, value: u32) {
    dmb();
    unsafe { write32(addr, value) };
    dmb();
}

/// Read a 32-bit value with data memory barriers before and after.
///
/// # Safety
///
/// Same requirements as [`read32`].
#[inline(always)]
pub unsafe fn read32_mb(addr: usize) -> u32 {
    dmb();
    let val = unsafe { read32(addr) };
    dmb();
    val
}

// ============================================================================
// Volatile Pointer Access (for framebuffer / shared memory)
// ============================================================================
//
// These operate on typed pointers rather than raw `usize` addresses,
// useful for the framebuffer driver where the base pointer is already
// a `*mut u32`.

/// Volatile write through a typed pointer.
///
/// # Safety
///
/// `ptr` must be valid, aligned, and dereferenceable.
#[inline(always)]
pub unsafe fn write_volatile_ptr<T>(ptr: *mut T, value: T) {
    unsafe { ptr::write_volatile(ptr, value) }
}

/// Volatile read through a typed pointer.
///
/// # Safety
///
/// `ptr` must be valid, aligned, and dereferenceable.
#[inline(always)]
pub unsafe fn read_volatile_ptr<T: Copy>(ptr: *const T) -> T {
    unsafe { ptr::read_volatile(ptr) }
}

// ============================================================================
// x86_64 Port I/O
// ============================================================================
//
// x86 processors have a separate 16-bit I/O address space (65 536 ports)
// accessed via IN/OUT instructions.  This is NOT memory-mapped I/O.
//
// ARM64 and RISC-V have only one address space — everything is MMIO.
// These functions only exist on x86_64.

#[cfg(target_arch = "x86_64")]
pub mod port {
    //! x86_64 port I/O — `in`/`out` instructions for the legacy I/O space.
    //!
    //! The legacy UART (COM1 at `0x3F8`) and PCI configuration space
    //! (`0xCF8`/`0xCFC`) live in this address space.

    /// Write a byte to an I/O port.
    ///
    /// # Safety
    ///
    /// `port` must be a valid I/O port for a byte-width write.
    #[inline(always)]
    pub unsafe fn outb(port: u16, value: u8) {
        unsafe {
            core::arch::asm!(
                "out dx, al",
                in("dx") port,
                in("al") value,
                options(nomem, nostack, preserves_flags),
            )
        }
    }

    /// Read a byte from an I/O port.
    ///
    /// # Safety
    ///
    /// `port` must be a valid I/O port for a byte-width read.
    #[inline(always)]
    pub unsafe fn inb(port: u16) -> u8 {
        let value: u8;
        unsafe {
            core::arch::asm!(
                "in al, dx",
                in("dx") port,
                out("al") value,
                options(nomem, nostack, preserves_flags),
            )
        }
        value
    }

    /// Write a 16-bit word to an I/O port.
    ///
    /// # Safety
    ///
    /// `port` must be a valid I/O port for a word-width write.
    #[inline(always)]
    pub unsafe fn outw(port: u16, value: u16) {
        unsafe {
            core::arch::asm!(
                "out dx, ax",
                in("dx") port,
                in("ax") value,
                options(nomem, nostack, preserves_flags),
            )
        }
    }

    /// Read a 16-bit word from an I/O port.
    ///
    /// # Safety
    ///
    /// `port` must be a valid I/O port for a word-width read.
    #[inline(always)]
    pub unsafe fn inw(port: u16) -> u16 {
        let value: u16;
        unsafe {
            core::arch::asm!(
                "in ax, dx",
                in("dx") port,
                out("ax") value,
                options(nomem, nostack, preserves_flags),
            )
        }
        value
    }

    /// Write a 32-bit dword to an I/O port.
    ///
    /// Primary use: PCI config space data register at port `0xCFC`.
    ///
    /// # Safety
    ///
    /// `port` must be a valid I/O port for a dword-width write.
    #[inline(always)]
    pub unsafe fn outl(port: u16, value: u32) {
        unsafe {
            core::arch::asm!(
                "out dx, eax",
                in("dx") port,
                in("eax") value,
                options(nomem, nostack, preserves_flags),
            )
        }
    }

    /// Read a 32-bit dword from an I/O port.
    ///
    /// # Safety
    ///
    /// `port` must be a valid I/O port for a dword-width read.
    #[inline(always)]
    pub unsafe fn inl(port: u16) -> u32 {
        let value: u32;
        unsafe {
            core::arch::asm!(
                "in eax, dx",
                in("dx") port,
                out("eax") value,
                options(nomem, nostack, preserves_flags),
            )
        }
        value
    }

    /// Insert a short delay between back-to-back port I/O accesses.
    ///
    /// Writing to port `0x80` (POST code port) causes the chipset to
    /// complete one I/O bus cycle, providing enough settling time for
    /// legacy devices like the 8250 UART at high baud rates.
    ///
    /// # Safety
    ///
    /// Must be running on x86 hardware where port `0x80` is available.
    #[inline(always)]
    pub unsafe fn io_delay() {
        unsafe { outb(0x80, 0x00) }
    }
}
