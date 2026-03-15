/*
 * soc/lattepanda_n100/uart_8250.c — 8250/16550 UART driver (port I/O)
 *
 * Tutorial-OS: LattePanda N100 (x86_64 / UEFI) UART Implementation
 *
 * x86_64 UART is fundamentally different from ARM64/RISC-V:
 *   - Accessed via port I/O (outb/inb), NOT memory-mapped registers
 *   - COM1 lives at I/O port 0x3F8, confirmed by BIOS UART log
 *   - 115200 baud, 8N1 — matching the BIOS console configuration
 *
 * outb/inb are defined inline in this file — they are NOT in common/mmio.h
 * (which has no x86_64 port I/O section). This matches the working bring-up
 * test (main.c) which defined them as static inline functions locally.
 * This driver is adapted directly from that bring-up test.
 *
 * REGISTER MAP (relative to COM1_BASE = 0x3F8):
 *   +0  THR  Transmitter Holding Register (write)  / RBR Receiver (read)
 *   +1  IER  Interrupt Enable Register
 *   +2  IIR  Interrupt Identification Register (read) / FCR FIFO Control (write)
 *   +3  LCR  Line Control Register
 *   +4  MCR  Modem Control Register
 *   +5  LSR  Line Status Register
 *   +6  MSR  Modem Status Register
 *   +7  SCR  Scratch Register
 *
 * With DLAB=1 (LCR bit 7):
 *   +0  DLL  Divisor Latch Low byte
 *   +1  DLH  Divisor Latch High byte
 */

#include "hal/hal_types.h"

/* ============================================================
 * x86_64 Port I/O primitives
 *
 * Defined here rather than in common/mmio.h because mmio.h has no
 * x86_64 section — it only covers memory-mapped I/O (volatile pointer
 * reads/writes), not port I/O (in/out instructions).
 *
 * These are identical to what the working bring-up test used.
 * ============================================================ */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

static inline void io_delay(void)
{
    /* Write to port 0x80 (POST diagnostic port) — standard x86 I/O delay.
     * Takes ~1 µs. Used to space out UART register writes on old hardware. */
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0) : "memory");
}

/* ============================================================
 * Register offsets from base port
 * ============================================================ */
#define UART_THR        0   /* Transmitter Holding Register (DLAB=0, write) */
#define UART_RBR        0   /* Receiver Buffer Register     (DLAB=0, read)  */
#define UART_DLL        0   /* Divisor Latch Low            (DLAB=1)        */
#define UART_IER        1   /* Interrupt Enable Register    (DLAB=0)        */
#define UART_DLH        1   /* Divisor Latch High           (DLAB=1)        */
#define UART_FCR        2   /* FIFO Control Register        (write)         */
#define UART_LCR        3   /* Line Control Register                        */
#define UART_MCR        4   /* Modem Control Register                       */
#define UART_LSR        5   /* Line Status Register                         */

/* LSR bit flags */
#define LSR_DR          (1 << 0)    /* Data Ready (RX)                     */
#define LSR_THRE        (1 << 5)    /* Transmitter Holding Register Empty  */
#define LSR_TEMT        (1 << 6)    /* Transmitter Empty (shift + THR)     */

/* LCR values */
#define LCR_8N1         0x03        /* 8 data bits, no parity, 1 stop bit  */
#define LCR_DLAB        0x80        /* Divisor Latch Access Bit            */

/* FCR values */
#define FCR_FIFO_EN     (1 << 0)    /* Enable FIFOs                        */
#define FCR_RX_RESET    (1 << 1)    /* Reset receiver FIFO                 */
#define FCR_TX_RESET    (1 << 2)    /* Reset transmitter FIFO              */
#define FCR_TRIGGER_14  (3 << 6)    /* RX FIFO trigger at 14 bytes         */

/* MCR values */
#define MCR_DTR         (1 << 0)    /* Data Terminal Ready                 */
#define MCR_RTS         (1 << 1)    /* Request To Send                     */

/* Baud rate: clock=1.8432 MHz, divisor = clock / (16 * baud) */
#define UART_CLOCK_HZ   1843200
#define UART_BAUD       115200
#define UART_DIVISOR    (UART_CLOCK_HZ / (16 * UART_BAUD))  /* = 1 */

/* COM1 base I/O port — confirmed on LattePanda MU by BIOS UART log */
#ifndef UART_COM1_BASE
#define UART_COM1_BASE  0x3F8
#endif

static uint16_t g_uart_base = UART_COM1_BASE;

/* ============================================================
 * Internal helpers
 * ============================================================ */

static inline void uart_reg_write(uint8_t reg, uint8_t val)
{
    outb((uint16_t)(g_uart_base + reg), val);
}

static inline uint8_t uart_reg_read(uint8_t reg)
{
    return inb((uint16_t)(g_uart_base + reg));
}

/* Wait until the transmitter holding register is empty */
static void uart_wait_tx_ready(void)
{
    /*
     * Timeout guard from the bring-up test — prevents infinite spin if
     * UART is not present or broken. On real hardware (confirmed LattePanda MU)
     * this typically returns immediately.
     */
    uint32_t timeout = 0x100000;
    while (!(uart_reg_read(UART_LSR) & LSR_THRE) && --timeout) {
        io_delay();
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

/*
 * n100_uart_init — Initialize COM1 at 115200 8N1
 *
 * Called from soc_init.c early in efi_main, before ExitBootServices.
 * Safe to call while UEFI Boot Services are still active.
 */
void n100_uart_init(void)
{
    /* Disable all interrupts — we poll */
    uart_reg_write(UART_IER, 0x00);

    /* Set baud rate: enable DLAB, write divisor, clear DLAB */
    uart_reg_write(UART_LCR, LCR_DLAB);
    uart_reg_write(UART_DLL, (uint8_t)(UART_DIVISOR & 0xFF));
    uart_reg_write(UART_DLH, (uint8_t)((UART_DIVISOR >> 8) & 0xFF));
    uart_reg_write(UART_LCR, LCR_8N1);

    /* Enable and reset FIFOs, 14-byte RX trigger */
    uart_reg_write(UART_FCR, FCR_FIFO_EN | FCR_RX_RESET | FCR_TX_RESET | FCR_TRIGGER_14);

    /* Assert DTR and RTS */
    uart_reg_write(UART_MCR, MCR_DTR | MCR_RTS);
}

/*
 * n100_uart_putc — Transmit a single character
 *
 * Automatically prepends \r before \n for proper terminal output.
 * This matches the bring-up test behaviour that produced clean output
 * in minicom and the Sipeed SLogic Combo 8.
 */
void n100_uart_putc(char c)
{
    if (c == '\n') {
        uart_wait_tx_ready();
        uart_reg_write(UART_THR, '\r');
    }
    uart_wait_tx_ready();
    uart_reg_write(UART_THR, (uint8_t)c);
}

/*
 * n100_uart_puts — Transmit a null-terminated string
 */
void n100_uart_puts(const char *s)
{
    if (!s) return;
    while (*s) {
        n100_uart_putc(*s++);
    }
}

/*
 * n100_uart_puthex32 — Print a 32-bit value as "0xXXXXXXXX"
 */
void n100_uart_puthex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    n100_uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        n100_uart_putc(hex[(v >> i) & 0xF]);
}

/*
 * n100_uart_puthex64 — Print a 64-bit value as "0xXXXXXXXXXXXXXXXX"
 */
void n100_uart_puthex64(uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    n100_uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        n100_uart_putc(hex[(v >> i) & 0xF]);
}

/*
 * n100_uart_putdec — Print an unsigned 32-bit decimal value
 */
void n100_uart_putdec(uint32_t v)
{
    char buf[12];
    int i = 0;
    if (!v) { n100_uart_putc('0'); return; }
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) n100_uart_putc(buf[i]);
}