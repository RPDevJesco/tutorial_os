/*
 * uart.c — UART Driver for the Ky X1 SoC
 * ==========================================
 *
 * Provides serial console output for debugging. Supports two modes:
 *
 *   1. SBI MODE (default, used during early boot):
 *      Uses OpenSBI's legacy putchar ecall. No hardware knowledge needed.
 *      This is what entry.S uses for the 'T' sign-of-life character.
 *
 *   2. DIRECT MODE (optional, after uart_init_hw):
 *      Talks directly to the PXA-compatible UART registers at 0xD4017000.
 *      Faster and works even if OpenSBI is not responding.
 *
 * PXA UART vs 16550:
 *   The Ky X1 UARTs are Marvell PXA-compatible, NOT standard 16550. The key
 *   differences are:
 *     - Registers are 4 bytes apart (reg-shift=2) instead of 1 byte
 *     - IER bit 6 (UUE) must be set to enable the UART unit
 *     - 32-bit register access width
 *   The core register set (RBR, THR, IER, LCR, LSR, etc.) is otherwise
 *   identical to 16550, so the actual TX/RX logic is very similar.
 *
 * BCM2710 comparison:
 *   On the Pi, we don't need a UART driver because the GPU initializes
 *   miniUART and the framebuffer is the primary output. On the Ky X1,
 *   UART is our lifeline for debugging before the display is working.
 *
 * This file implements the hal_uart interface if you have one, or can be
 * called directly as kyx1_uart_putc / kyx1_uart_puts.
 */

#include "kyx1_regs.h"
#include "types.h"

/* =============================================================================
 * MMIO HELPERS (local to this file)
 * =============================================================================
 * These are the same volatile read/write pattern from common/mmio.h, but
 * defined locally so this file has no external dependencies. The UART driver
 * must work before anything else is initialized.
 */

static inline void uart_write32(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t uart_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

/* =============================================================================
 * SBI CONSOLE (ecall interface)
 * =============================================================================
 * OpenSBI provides a legacy putchar function (extension ID 0x01).
 * This works out of the box because OpenSBI already initialized the UART.
 * The kernel command line has `earlycon=sbi`, confirming this path works.
 */

static void sbi_putchar(char c)
{
    register unsigned long a0 __asm__("a0") = (unsigned long)c;
    register unsigned long a7 __asm__("a7") = 0x01; /* SBI legacy putchar */
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a7)
                     : "memory");
}

static long sbi_getchar(void)
{
    register unsigned long a0 __asm__("a0");
    register unsigned long a7 __asm__("a7") = 0x02; /* SBI legacy getchar */
    __asm__ volatile("ecall"
                     : "=r"(a0)
                     : "r"(a7)
                     : "memory");
    return (long)a0; /* Returns character or -1 if none available */
}

/* =============================================================================
 * STATE
 * =============================================================================
 */

/* Which UART base we're using for direct mode */
static uintptr_t uart_base = KYX1_UART0_BASE;

/* Are we using direct hardware access or SBI? */
static bool uart_hw_initialized = false;

/* =============================================================================
 * DIRECT HARDWARE UART
 * =============================================================================
 * These functions talk directly to the PXA UART registers. Only used after
 * uart_init_hw() is called — before that, all output goes through SBI.
 */

/*
 * uart_init_hw — Initialize the PXA UART for direct access
 *
 * U-Boot already configured the baud rate (115200), so we don't need to
 * set up the divisor latch. We just need to:
 *   1. Enable the UART unit (IER bit 6 = UUE)
 *   2. Enable and reset FIFOs
 *   3. Set 8N1 line control
 *
 * If you're bringing up a UART that U-Boot didn't initialize, you'd also
 * need to configure clocks, pin mux, and the divisor latch for baud rate.
 */
void kyx1_uart_init_hw(void)
{
    /*
     * Step 1: Set line control to 8N1 (8 data bits, no parity, 1 stop bit)
     * This is the same as 16550 LCR configuration.
     */
    uart_write32(uart_base + PXA_UART_LCR, PXA_LCR_WLS_8 | PXA_LCR_STB_1);

    /*
     * Step 2: Enable FIFOs and reset them
     * FCR bit 0 = FIFO enable, bit 1 = reset RX FIFO, bit 2 = reset TX FIFO
     */
    uart_write32(uart_base + PXA_UART_FCR,
                 PXA_FCR_FIFOE | PXA_FCR_RFIFOR | PXA_FCR_XFIFOR);

    /*
     * Step 3: Enable the UART unit
     * This is the PXA-specific quirk! Standard 16550 doesn't have this bit.
     * U-Boot sets CONFIG_SYS_NS16550_IER=0x40, which is exactly PXA_IER_UUE.
     * Without this bit set, the UART simply doesn't work.
     */
    uart_write32(uart_base + PXA_UART_IER, PXA_IER_UUE);

    uart_hw_initialized = true;
}

/*
 * hw_putc — Send one character via direct UART register access
 *
 * Waits for the TX holding register to be empty (LSR bit 5 = THRE),
 * then writes the character. Same logic as any 16550-style UART.
 */
static void hw_putc(char c)
{
    /* Wait for TX holding register empty */
    while (!(uart_read32(uart_base + PXA_UART_LSR) & PXA_LSR_THRE))
        ;

    /* Write character to transmit holding register */
    uart_write32(uart_base + PXA_UART_THR, (uint32_t)c);
}

/*
 * hw_getc — Read one character (non-blocking)
 *
 * Returns the character if one is available, or -1 if the RX FIFO is empty.
 */
static long hw_getc(void)
{
    if (uart_read32(uart_base + PXA_UART_LSR) & PXA_LSR_DR) {
        return (long)uart_read32(uart_base + PXA_UART_RBR);
    }
    return -1; /* No data */
}

/* =============================================================================
 * PUBLIC API
 * =============================================================================
 * These are the functions the rest of Tutorial-OS calls. They automatically
 * route through SBI or direct hardware depending on initialization state.
 */

/*
 * kyx1_uart_putc — Send one character to the console
 *
 * Handles \n → \r\n conversion automatically (serial terminals need both
 * carriage return and line feed to start a new line at column 0).
 */
void kyx1_uart_putc(char c)
{
    if (c == '\n') {
        /* Serial terminals need \r\n for proper line breaks */
        if (uart_hw_initialized)
            hw_putc('\r');
        else
            sbi_putchar('\r');
    }

    if (uart_hw_initialized)
        hw_putc(c);
    else
        sbi_putchar(c);
}

/*
 * kyx1_uart_puts — Send a string to the console
 */
void kyx1_uart_puts(const char *str)
{
    while (*str) {
        kyx1_uart_putc(*str++);
    }
}

/*
 * kyx1_uart_getc — Read one character (non-blocking)
 *
 * Returns the character or -1 if nothing available.
 */
long kyx1_uart_getc(void)
{
    if (uart_hw_initialized)
        return hw_getc();
    else
        return sbi_getchar();
}

/*
 * kyx1_uart_puthex — Print a 64-bit value as hexadecimal
 *
 * Useful for debugging addresses and register values.
 * Prints "0x" prefix followed by 16 hex digits.
 */
void kyx1_uart_puthex(uint64_t val)
{
    const char hex[] = "0123456789ABCDEF";
    kyx1_uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        kyx1_uart_putc(hex[(val >> i) & 0xF]);
    }
}

/*
 * kyx1_uart_putdec — Print a 32-bit value as decimal
 */
void kyx1_uart_putdec(uint32_t val)
{
    if (val == 0) {
        kyx1_uart_putc('0');
        return;
    }

    char buf[10];
    int i = 0;
    while (val > 0 && i < 10) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    /* Print in reverse (most significant digit first) */
    while (i > 0) {
        kyx1_uart_putc(buf[--i]);
    }
}
