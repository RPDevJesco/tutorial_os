//! UART driver for the StarFive JH7110.
//!
//! Port of `uart.c`.  Two layers of console output:
//!
//! 1. **SBI Console** (always available): OpenSBI Legacy Console Putchar
//!    (EID 0x01).  Works from the first instruction, before any hardware
//!    init.  Used by default for all `puts`/`putc`/`puthex` output.
//!
//! 2. **Direct Hardware UART** (optional fast path): Programs the DW 8250
//!    registers at UART0 (0x10000000) directly.  Slightly faster than SBI
//!    ecall overhead for high-bandwidth debug output.
//!
//! # Contrast with KyX1
//!
//! The KyX1 uses a PXA/MMP-style UART with 4-byte register shift and
//! vendor-specific IER bits.  The JH7110 uses a textbook 16550/8250
//! layout.  Both expose the same public API.

use crate::regs;

// ============================================================================
// SBI Console (Layer 1 — Primary Output)
// ============================================================================

/// Write a single character via SBI Legacy Console Putchar (EID 0x01).
#[inline]
fn sbi_putchar(c: u8) {
    let a0 = c as u64;
    let a7: u64 = 0x01; // SBI legacy console putchar
    unsafe {
        core::arch::asm!(
            "ecall",
            inout("a0") a0 => _,
            in("a7") a7,
            options(nomem, nostack),
        );
    }
}

// ============================================================================
// Public API — SBI-backed (works before init_hw())
// ============================================================================

/// Write a single character to the console.
pub fn putc(c: u8) {
    sbi_putchar(c);
}

/// Write a null-terminated string to the console.
pub fn puts(s: &[u8]) {
    for &b in s {
        if b == 0 {
            break;
        }
        sbi_putchar(b);
    }
}

/// Write a string slice to the console.
pub fn puts_str(s: &str) {
    for &b in s.as_bytes() {
        sbi_putchar(b);
    }
}

/// Write a 64-bit value in hexadecimal with "0x" prefix.
pub fn puthex(val: u64) {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    sbi_putchar(b'0');
    sbi_putchar(b'x');
    for i in (0..16).rev() {
        sbi_putchar(HEX[((val >> (i * 4)) & 0xF) as usize]);
    }
}

/// Write a 32-bit unsigned decimal value.
pub fn putdec(val: u32) {
    if val == 0 {
        sbi_putchar(b'0');
        return;
    }

    let mut buf = [0u8; 12];
    let mut i = 0usize;
    let mut v = val;
    while v > 0 {
        buf[i] = b'0' + (v % 10) as u8;
        v /= 10;
        i += 1;
    }
    while i > 0 {
        i -= 1;
        sbi_putchar(buf[i]);
    }
}

// ============================================================================
// Direct Hardware UART (Layer 2 — Optional Fast Path)
// ============================================================================
//
// Programs the DW 8250 UART registers at UART0_BASE directly.
// Must call init_hw() before putc_direct().

const UART_BASE: usize = regs::UART0_BASE;

/// Read a UART register.
#[inline]
fn uart_read(offset: usize) -> u32 {
    unsafe { common::mmio::read32(UART_BASE + offset) }
}

/// Write a UART register.
#[inline]
fn uart_write(offset: usize, val: u32) {
    unsafe { common::mmio::write32(UART_BASE + offset, val) };
}

/// Initialize the DW 8250 UART hardware at UART0.
///
/// Programs 115200 baud, 8N1, FIFOs enabled.
/// After this call, [`putc_direct`] is available for faster output.
pub fn init_hw() {
    use regs::uart8250::*;

    // 1. Disable all UART interrupts
    uart_write(IER, 0x00);

    // 2. Set DLAB=1 to access divisor registers
    uart_write(LCR, LCR_DLAB);

    // 3. Write divisor (13 for 115200 @ 24 MHz)
    uart_write(DLL, regs::UART_DIVISOR & 0xFF);
    uart_write(DLM, (regs::UART_DIVISOR >> 8) & 0xFF);

    // 4. Clear DLAB, configure 8N1
    uart_write(LCR, LCR_WLS_8 | LCR_STB_1 | LCR_PEN_NONE);

    // 5. Enable FIFOs, reset TX and RX FIFOs, set RX trigger level
    uart_write(FCR, FCR_FIFO_EN | FCR_RX_RESET | FCR_TX_RESET | FCR_TRIG_14);

    // 6. Enable TX (DTR + RTS asserted)
    uart_write(MCR, 0x03);

    puts(b"[jh7110] UART0 direct hardware init OK (115200 8N1)\n");
}

/// Write a character directly to UART hardware (bypasses SBI).
///
/// Polls LSR THRE bit before writing.  Only use after [`init_hw`].
pub fn putc_direct(c: u8) {
    use regs::uart8250::*;

    // Spin until TX holding register is empty
    while (uart_read(LSR) & LSR_THRE) == 0 {}
    uart_write(THR, c as u32);

    // Serial terminal convention: newline → carriage return + newline
    if c == b'\n' {
        while (uart_read(LSR) & LSR_THRE) == 0 {}
        uart_write(THR, b'\r' as u32);
    }
}
