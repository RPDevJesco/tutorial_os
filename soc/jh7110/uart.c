/*
 * uart.c — UART Driver for the StarFive JH-7110 SoC
 * ===================================================
 *
 * This implements UART output for the Milk-V Mars using UART0
 * (base address 0x10000000, connected to GPIO5/TX and GPIO6/RX).
 *
 * TWO LAYERS OF UART OUTPUT:
 * ==========================
 *
 * Layer 1 — SBI Console (always available, used by default):
 *   OpenSBI provides the SBI Debug Console extension (EID 0x4442434E).
 *   We can write characters via ecall without touching UART hardware.
 *   This works immediately from the first instruction, before any init.
 *   entry.S uses this to print 'T' at the very start of _start.
 *   The SBI console goes through OpenSBI's existing UART driver.
 *
 * Layer 2 — Direct Hardware UART (optional, slightly faster):
 *   We program the 8250 UART registers directly.
 *   This is useful for high-bandwidth debug output (register dumps, etc.)
 *   where SBI ecall overhead becomes measurable.
 *   Must call jh7110_uart_init_hw() before jh7110_uart_putc_direct().
 *
 * HARDWARE DETAILS:
 * =================
 * The JH7110 UART is a Synopsys DesignWare 8250 (DW_apb_uart).
 * This is a standard 16550-compatible UART. Unlike the PXA UART on the
 * Ky X1 (which has a 4-byte register shift and vendor-specific IER bits),
 * the 8250 uses the textbook 16550 layout:
 *
 *   Offset 0x00: THR/RBR (TX holding / RX buffer)
 *   Offset 0x04: IER (interrupt enable)
 *   Offset 0x08: IIR/FCR (interrupt ID / FIFO control)
 *   Offset 0x0C: LCR (line control, including DLAB)
 *   Offset 0x10: MCR (modem control)
 *   Offset 0x14: LSR (line status — TX ready bit is here)
 *   ...
 *
 * BAUD RATE:
 *   UART clock: 24 MHz from OSC reference clock
 *   Target baud: 115200
 *   Divisor = 24,000,000 / (16 × 115,200) = 13.02 → 13 (0.02% error)
 *
 * CONTRAST WITH KYX1:
 *   The Ky X1 uart.c implements a PXA-style UART. The tx-ready check
 *   reads a different status bit, and the "unit enable" is in IER[6]
 *   rather than being implicit. The baud divisor is also different
 *   (Ky X1 UART clock is 14.745 MHz, not 24 MHz).
 *   Despite these differences, both files present the same external API:
 *   jh7110_uart_putc(), jh7110_uart_puts(), jh7110_uart_puthex().
 */

#include "jh7110_regs.h"
#include "types.h"

/* The UART0 base address we'll use for direct hardware access */
#define UART_BASE   JH7110_UART0_BASE

/* Convenience macro: read/write UART registers */
#define UART_REG(offset)    (*((volatile uint32_t *)(UART_BASE + (offset))))

/* =============================================================================
 * SBI CONSOLE (LAYER 1 — PRIMARY OUTPUT)
 * =============================================================================
 *
 * SBI Debug Console extension (EID 0x4442434E "DBCN").
 * These ecalls go through OpenSBI which handles the actual hardware.
 *
 * We use the Legacy Console Putchar (EID 0x01) for simplicity — it's
 * supported by all OpenSBI versions and requires no capability probe.
 * The Debug Console extension (EID 0x4442434E) is newer and preferred
 * for multi-char writes but requires a version check first.
 *
 * a7 = EID (extension ID), a6 = FID (function ID), a0 = argument
 */
static inline void sbi_putchar(char c)
{
    register long a0 __asm__("a0") = (long)c;
    register long a7 __asm__("a7") = 0x01;  /* SBI legacy console putchar */
    __asm__ volatile(
        "ecall"
        : "+r"(a0)
        : "r"(a7)
        : "memory"
    );
}

/* =============================================================================
 * PUBLIC API — SBI-BACKED (works before jh7110_uart_init_hw())
 * =============================================================================
 */

void jh7110_uart_putc(char c)
{
    sbi_putchar(c);
}

void jh7110_uart_puts(const char *str)
{
    if (!str) return;
    while (*str) {
        sbi_putchar(*str++);
    }
}

void jh7110_uart_puthex(uint64_t val)
{
    const char hex[] = "0123456789ABCDEF";
    sbi_putchar('0');
    sbi_putchar('x');
    for (int i = 60; i >= 0; i -= 4) {
        sbi_putchar(hex[(val >> i) & 0xF]);
    }
}

void jh7110_uart_putdec(uint32_t val)
{
    char buf[12];
    int i = 0;

    if (val == 0) {
        sbi_putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        sbi_putchar(buf[--i]);
    }
}

/* =============================================================================
 * DIRECT HARDWARE UART (LAYER 2 — OPTIONAL FAST PATH)
 * =============================================================================
 *
 * Programs the 8250 UART registers directly. Call jh7110_uart_init_hw()
 * once, then jh7110_uart_putc_direct() for faster output that doesn't
 * go through SBI ecall overhead.
 *
 * INITIALIZATION SEQUENCE:
 *   1. Disable interrupts (IER = 0)
 *   2. Set DLAB=1 in LCR to access divisor registers
 *   3. Write baud divisor to DLL (low byte) and DLM (high byte)
 *   4. Clear DLAB, configure 8N1 in LCR
 *   5. Enable and reset FIFOs via FCR
 *
 * This sequence is identical for any 16550-compatible UART regardless
 * of the SoC it's embedded in — a good example of IP reuse reducing
 * porting effort.
 */
void jh7110_uart_init_hw(void)
{
    /* Step 1: Disable all UART interrupts */
    UART_REG(UART8250_IER) = 0x00;

    /* Step 2: Set DLAB=1 to access divisor registers */
    UART_REG(UART8250_LCR) = UART8250_LCR_DLAB;

    /* Step 3: Write divisor (13 for 115200 @ 24 MHz) */
    UART_REG(UART8250_DLL) = (JH7110_UART_DIVISOR) & 0xFF;
    UART_REG(UART8250_DLM) = (JH7110_UART_DIVISOR >> 8) & 0xFF;

    /* Step 4: Clear DLAB, configure 8N1 (8 data bits, no parity, 1 stop) */
    UART_REG(UART8250_LCR) = UART8250_LCR_WLS_8 |
                              UART8250_LCR_STB_1 |
                              UART8250_LCR_PEN_NONE;

    /* Step 5: Enable FIFOs, reset TX and RX FIFOs, set RX trigger level */
    UART_REG(UART8250_FCR) = UART8250_FCR_FIFO_EN |
                              UART8250_FCR_RX_RESET |
                              UART8250_FCR_TX_RESET |
                              UART8250_FCR_TRIG_14;

    /* Step 6: Enable TX (DTR + RTS asserted — modem control) */
    UART_REG(UART8250_MCR) = 0x03;

    jh7110_uart_puts("[jh7110] UART0 direct hardware init OK (115200 8N1)\n");
}

/*
 * jh7110_uart_putc_direct — write a character directly to UART hardware
 *
 * Polls the LSR THRE (Transmit Holding Register Empty) bit before writing.
 * If the TX FIFO is full, this spins until there's space.
 *
 * Only use after jh7110_uart_init_hw() has been called.
 */
void jh7110_uart_putc_direct(char c)
{
    /* Spin until TX holding register is empty (THRE bit set) */
    while (!(UART_REG(UART8250_LSR) & UART8250_LSR_THRE))
        ;

    UART_REG(UART8250_THR) = (uint32_t)c;

    /* For newline, also send carriage return (serial terminal convention) */
    if (c == '\n') {
        while (!(UART_REG(UART8250_LSR) & UART8250_LSR_THRE))
            ;
        UART_REG(UART8250_THR) = '\r';
    }
}