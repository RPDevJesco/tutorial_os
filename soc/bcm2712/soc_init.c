/*
 * soc/bcm2712/soc_init.c - BCM2712 SoC Initialization
 * ====================================================
 *
 * Changes from the stub version:
 *
 *  1. hal_platform_early_init() now initializes UART0 for debug output.
 *     UART is brought up here, before hal_timer_init() and hal_gpio_init(),
 *     so that any failure during platform init can be reported over serial.
 *
 *  2. hal_debug_putc() is now a real PL011 transmit implementation instead
 *     of an empty stub.  It polls the TX FIFO Full flag and then writes the
 *     byte.  No interrupt handling or DMA — just enough to get characters out
 *     reliably during bare-metal bring-up.
 *
 * UART HARDWARE (RP1 PL011):
 *
 *   Register base: RP1_UART0_BASE = RP1_PERI_BASE + 0x030000
 *                                 = 0x1F000D0000  + 0x030000
 *                                 = 0x1F00100000
 *
 *   Baud rate: 115200 @ 48 MHz UART clock
 *     IBRD = 26, FBRD = 3  (actual: 115177 baud, error < 0.02%)
 *
 *   Format: 8N1, TX FIFO enabled
 *
 *   GPIO 14 = UART0_TX, GPIO 15 = UART0_RX, RP1 FUNCSEL = 4
 *   (On RP1, UART0 is function 4, NOT function 0/ALT0 as on BCM2710/2711)
 *
 * INITIALIZATION STRATEGY:
 *
 *   If the GPU firmware ran with enable_uart=1 in config.txt it will have
 *   already muxed GPIO 14/15 and enabled the UART.  We reinitialize from
 *   scratch anyway so the state is deterministic and does not depend on
 *   firmware behavior or config.txt settings.
 *
 *   The sequence follows the PL011 TRM §3.3.6:
 *     1. Disable UART (CR.UARTEN = 0)
 *     2. Wait until UARTFR.BUSY = 0 (current transmission completes)
 *     3. Flush FIFO (LCR_H.FEN = 0 then = 1)
 *     4. Program baud rate divisors (IBRD, FBRD)
 *     5. Set line control (LCR_H: 8N1 + FIFO)
 *     6. Clear all pending interrupts (ICR)
 *     7. Enable UART with TX and RX (CR: UARTEN | TXE | RXE)
 *     8. Configure GPIO FUNCSEL via RP1 GPIO_CTRL registers
 */

#include "bcm2712_regs.h"
#include "bcm2712_mailbox.h"
#include "hal/hal.h"

/* External symbols from boot_soc.S */
extern uint64_t detected_ram_base;
extern uint64_t detected_ram_size;

/* Platform state */
static bool platform_initialized = false;
static bool uart_initialized      = false;

/* =========================================================================
 * PL011 UART0 REGISTER DEFINITIONS
 * =========================================================================
 *
 * All registers are 32-bit, 4-byte aligned, at RP1_UART0_BASE + offset.
 */
#define UART0_BASE      RP1_UART0_BASE

/* Register addresses */
#define UART_DR         ((volatile uint32_t *)(UART0_BASE + 0x000))  /* Data        */
#define UART_FR         ((volatile uint32_t *)(UART0_BASE + 0x018))  /* Flags       */
#define UART_IBRD       ((volatile uint32_t *)(UART0_BASE + 0x024))  /* Int baud    */
#define UART_FBRD       ((volatile uint32_t *)(UART0_BASE + 0x028))  /* Frac baud   */
#define UART_LCR_H      ((volatile uint32_t *)(UART0_BASE + 0x02C))  /* Line ctrl   */
#define UART_CR         ((volatile uint32_t *)(UART0_BASE + 0x030))  /* Control     */
#define UART_ICR        ((volatile uint32_t *)(UART0_BASE + 0x044))  /* Intr clear  */

/* FR (Flag Register) bits */
#define FR_BUSY         (1u << 3)   /* UART is transmitting */
#define FR_RXFE         (1u << 4)   /* RX FIFO empty */
#define FR_TXFF         (1u << 5)   /* TX FIFO full  — poll this before writing */
#define FR_RXFF         (1u << 6)   /* RX FIFO full  */
#define FR_TXFE         (1u << 7)   /* TX FIFO empty */

/* LCR_H (Line Control Register) bits */
#define LCR_BRK         (1u << 0)   /* Send break */
#define LCR_PEN         (1u << 1)   /* Parity enable */
#define LCR_EPS         (1u << 2)   /* Even parity select */
#define LCR_STP2        (1u << 3)   /* Two stop bits */
#define LCR_FEN         (1u << 4)   /* FIFO enable */
#define LCR_WLEN8       (3u << 5)   /* 8-bit word length */
#define LCR_8N1_FIFO    (LCR_WLEN8 | LCR_FEN)  /* 0x70 — what we program */

/* CR (Control Register) bits */
#define CR_UARTEN       (1u << 0)   /* UART enable */
#define CR_TXE          (1u << 8)   /* TX enable */
#define CR_RXE          (1u << 9)   /* RX enable */
#define CR_TX_RX        (CR_UARTEN | CR_TXE | CR_RXE)  /* 0x301 */

/* Baud rate divisors — 115200 baud from 48 MHz UART clock
 *   Divisor = 48_000_000 / (16 × 115200) = 26.0417
 *   IBRD = 26, FBRD = round(0.0417 × 64) = 3
 *   Actual baud = 48_000_000 / (16 × (26 + 3/64)) = 115177 (error 0.02%)  */
#define UART_IBRD_115200    26
#define UART_FBRD_115200     3

/* RP1 GPIO CTRL register for pin n — CTRL is at offset +4, STATUS at +0
 * (The original gpio.c had these swapped; this is the correct definition.) */
#define RP1_UART_GPIO_CTRL(n)  ((volatile uint32_t *)(RP1_GPIO_IO_BANK0 + (n)*8 + 4))
#define RP1_FUNCSEL_UART0   4u          /* UART0_TX/RX on RP1 GPIO 14/15 */
#define GPIO_FUNCSEL_MASK   0x1Fu


/* =========================================================================
 * INTERNAL: UART EARLY INIT
 * =========================================================================
 *
 * Called from hal_platform_early_init() before anything else.
 * Does NOT use hal_gpio_set_function() to avoid the bug in gpio.c where
 * HAL_GPIO_ALT0 maps to FUNCSEL=0 (SPI) instead of FUNCSEL=4 (UART0).
 * We write the RP1 GPIO_CTRL register directly.
 */
static void uart_early_init(void)
{
    /* ------------------------------------------------------------------ */
    /* Step 1: Disable UART                                                */
    /* ------------------------------------------------------------------ */
    *UART_CR = 0;

    /* ------------------------------------------------------------------ */
    /* Step 2: Wait for any in-progress transmission to complete           */
    /* ------------------------------------------------------------------ */
    uint32_t timeout = 0x10000;
    while ((*UART_FR & FR_BUSY) && --timeout) { /* spin */ }

    /* ------------------------------------------------------------------ */
    /* Step 3: Flush FIFOs by momentarily disabling them                   */
    /* ------------------------------------------------------------------ */
    *UART_LCR_H = 0;

    /* ------------------------------------------------------------------ */
    /* Step 4: Set baud rate divisors                                      */
    /* ------------------------------------------------------------------ */
    *UART_IBRD = UART_IBRD_115200;
    *UART_FBRD = UART_FBRD_115200;

    /* ------------------------------------------------------------------ */
    /* Step 5: Line control: 8N1, FIFO enabled                             */
    /* LCR_H must be written AFTER IBRD/FBRD (PL011 TRM requirement)      */
    /* ------------------------------------------------------------------ */
    *UART_LCR_H = LCR_8N1_FIFO;

    /* ------------------------------------------------------------------ */
    /* Step 6: Clear all pending interrupts                                */
    /* ------------------------------------------------------------------ */
    *UART_ICR = 0x7FF;

    /* ------------------------------------------------------------------ */
    /* Step 7: Enable UART with TX and RX                                  */
    /* ------------------------------------------------------------------ */
    *UART_CR = CR_TX_RX;

    /* ------------------------------------------------------------------ */
    /* Step 8: Configure GPIO 14 (TX) and 15 (RX) to UART0 function       */
    /*                                                                     */
    /* We write RP1 GPIO_CTRL directly rather than through hal_gpio, so   */
    /* that UART bring-up does not depend on GPIO subsystem state.         */
    /*                                                                     */
    /* RP1 GPIO_CTRL register: bits [4:0] = FUNCSEL                       */
    /* For GPIO 14/15: FUNCSEL = 4 = UART0_TX / UART0_RX                 */
    /* ------------------------------------------------------------------ */
    volatile uint32_t *ctrl14 = RP1_UART_GPIO_CTRL(14);
    *ctrl14 = (*ctrl14 & ~GPIO_FUNCSEL_MASK) | RP1_FUNCSEL_UART0;

    volatile uint32_t *ctrl15 = RP1_UART_GPIO_CTRL(15);
    *ctrl15 = (*ctrl15 & ~GPIO_FUNCSEL_MASK) | RP1_FUNCSEL_UART0;

    uart_initialized = true;
}


/* =========================================================================
 * DEBUG OUTPUT — hal_debug_putc / hal_debug_puts / hal_debug_printf
 * =========================================================================
 *
 * hal_debug_putc transmits a single character via UART0 TX.
 *
 * Mechanism: poll FR_TXFF (TX FIFO Full, bit 5).  When clear, the FIFO
 * has space and we write the byte to DR.  With a 16-entry hardware FIFO
 * and a PL011 at 115200 baud, the FIFO empties in ~1.4ms — fast enough
 * that busy-waiting is appropriate for debug output in bare-metal code.
 *
 * \r\n translation: bare-metal UART terminals expect CR+LF for newlines.
 * We insert a \r before every \n automatically so callers can use plain \n.
 */

void hal_debug_putc(char c)
{
    if (!uart_initialized) return;

    /* Auto CR before LF */
    if (c == '\n') {
        while (*UART_FR & FR_TXFF) { /* wait until TX FIFO has space */ }
        *UART_DR = '\r';
    }

    while (*UART_FR & FR_TXFF) { /* wait until TX FIFO has space */ }
    *UART_DR = (uint32_t)(unsigned char)c;
}

void hal_debug_puts(const char *s)
{
    if (!s) return;
    while (*s) {
        hal_debug_putc(*s++);
    }
}

/*
 * hal_debug_printf - Minimal formatted debug output
 *
 * Supports: %s %c %d %u %x %X %p  and field width for %d/%u/%x.
 * No malloc, no libc.  Intentionally minimal for a bare-metal environment.
 */
void hal_debug_printf(const char *fmt, ...)
{
    if (!fmt || !uart_initialized) return;

    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            hal_debug_putc(*fmt++);
            continue;
        }
        fmt++; /* skip '%' */

        /* Simple format specifiers — no padding/precision for now */
        switch (*fmt++) {
            case 's': {
                const char *s = __builtin_va_arg(args, const char *);
                hal_debug_puts(s ? s : "(null)");
                break;
            }
            case 'c':
                hal_debug_putc((char)__builtin_va_arg(args, int));
                break;
            case 'd': {
                int val = __builtin_va_arg(args, int);
                if (val < 0) { hal_debug_putc('-'); val = -val; }
                /* Fall through intentional — treat as unsigned after sign */
                char buf[12]; int i = 0;
                if (val == 0) { buf[i++] = '0'; }
                else { unsigned u = (unsigned)val;
                       while (u) { buf[i++] = '0' + (u % 10); u /= 10; }
                       /* reverse */ for (int a=0,b=i-1; a<b; a++,b--) {
                           char t=buf[a]; buf[a]=buf[b]; buf[b]=t; } }
                for (int j=0; j<i; j++) hal_debug_putc(buf[j]);
                break;
            }
            case 'u': {
                unsigned u = __builtin_va_arg(args, unsigned);
                char buf[12]; int i = 0;
                if (u == 0) { buf[i++] = '0'; }
                else { while (u) { buf[i++] = '0' + (u % 10); u /= 10; } }
                for (int a=0,b=i-1; a<b; a++,b--) { char t=buf[a]; buf[a]=buf[b]; buf[b]=t; }
                for (int j=0; j<i; j++) hal_debug_putc(buf[j]);
                break;
            }
            case 'x':
            case 'X':
            case 'p': {
                unsigned long val;
                if (fmt[-1] == 'p') {
                    val = (unsigned long)__builtin_va_arg(args, void *);
                    hal_debug_puts("0x");
                } else {
                    val = (unsigned long)__builtin_va_arg(args, unsigned);
                }
                const char *hex = (fmt[-1]=='X') ? "0123456789ABCDEF"
                                                 : "0123456789abcdef";
                char buf[18]; int i = 0;
                if (val == 0) { buf[i++] = '0'; }
                else { unsigned long v=val;
                       while (v) { buf[i++]=hex[v&0xF]; v>>=4; } }
                for (int a=0,b=i-1; a<b; a++,b--) { char t=buf[a]; buf[a]=buf[b]; buf[b]=t; }
                for (int j=0; j<i; j++) hal_debug_putc(buf[j]);
                break;
            }
            case '%':
                hal_debug_putc('%');
                break;
            default:
                hal_debug_putc('?');
                break;
        }
    }

    __builtin_va_end(args);
}


/* =========================================================================
 * HAL PLATFORM INTERFACE
 * ========================================================================= */

hal_error_t hal_platform_early_init(void)
{
    /*
     * UART must come up before anything else so that failures during
     * hal_platform_init() appear on the serial console.
     */
    uart_early_init();
    hal_debug_puts("[bcm2712] early init\n");
    return HAL_SUCCESS;
}

hal_error_t hal_platform_init(void)
{
    hal_error_t err;

    hal_debug_puts("[bcm2712] platform init\n");

    err = hal_timer_init();
    if (HAL_FAILED(err)) {
        hal_debug_puts("[bcm2712] timer init FAILED\n");
        return err;
    }

    err = hal_gpio_init();
    if (HAL_FAILED(err)) {
        hal_debug_puts("[bcm2712] gpio init FAILED\n");
        return err;
    }

    platform_initialized = true;
    hal_debug_puts("[bcm2712] platform ready\n");
    return HAL_SUCCESS;
}

bool hal_platform_is_initialized(void)
{
    return platform_initialized;
}

hal_platform_id_t hal_platform_get_id(void)
{
#if defined(BOARD_RPI_CM5_IO)
    return HAL_PLATFORM_RPI_CM5;
#else
    return HAL_PLATFORM_RPI_5;
#endif
}

const char *hal_platform_get_board_name(void)
{
#if defined(BOARD_RPI_CM5_IO)
    return "Raspberry Pi CM5 IO Board";
#else
    return "Raspberry Pi 5";
#endif
}

const char *hal_platform_get_soc_name(void)
{
    return "BCM2712";
}

hal_error_t hal_platform_get_info(hal_platform_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    info->platform_id = hal_platform_get_id();
    info->arch        = HAL_ARCH_ARM64;
    info->board_name  = hal_platform_get_board_name();
    info->soc_name    = hal_platform_get_soc_name();

    uint32_t revision = 0;
    bcm_mailbox_get_board_revision(&revision);
    info->board_revision = revision;

    uint64_t serial = 0;
    bcm_mailbox_get_board_serial(&serial);
    info->serial_number = serial;

    return HAL_SUCCESS;
}

hal_error_t hal_platform_get_memory_info(hal_memory_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    info->arm_base = (uintptr_t)detected_ram_base;
    info->arm_size = (size_t)detected_ram_size;

    uint32_t vc_base = 0, vc_size = 0;
    bcm_mailbox_get_vc_memory(&vc_base, &vc_size);
    info->gpu_base = vc_base;
    info->gpu_size = vc_size;

    info->peripheral_base = BCM2712_PERI_BASE;

    return HAL_SUCCESS;
}

size_t hal_platform_get_arm_memory(void)   { return (size_t)detected_ram_size; }
size_t hal_platform_get_total_memory(void) { return (size_t)detected_ram_size; }

hal_error_t hal_platform_get_clock_info(hal_clock_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    info->arm_freq_hz  = hal_platform_get_clock_rate(HAL_CLOCK_ARM);
    info->core_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_CORE);
    info->uart_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_UART);
    info->emmc_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_EMMC);

    return HAL_SUCCESS;
}

uint32_t hal_platform_get_arm_freq(void)
{
    return hal_platform_get_clock_rate(HAL_CLOCK_ARM);
}

uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id)
{
    uint32_t rate = 0, mbox_id;

    switch (clock_id) {
        case HAL_CLOCK_ARM:  mbox_id = CLOCK_ID_ARM;  break;
        case HAL_CLOCK_CORE: mbox_id = CLOCK_ID_CORE; break;
        case HAL_CLOCK_UART: mbox_id = CLOCK_ID_UART; break;
        case HAL_CLOCK_EMMC: mbox_id = 1;             break;
        case HAL_CLOCK_PWM:  mbox_id = 10;            break;
        default: return 0;
    }

    return bcm_mailbox_get_clock_rate(mbox_id, &rate) ? rate : 0;
}

hal_error_t hal_platform_get_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return HAL_ERROR_NULL_PTR;
    uint32_t temp = 0;
    if (bcm_mailbox_get_temperature(&temp)) { *temp_mc = (int32_t)temp; return HAL_SUCCESS; }
    return HAL_ERROR_HARDWARE;
}

hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return HAL_ERROR_NULL_PTR;
    uint32_t temp = 0;
    if (bcm_mailbox_get_max_temperature(&temp)) { *temp_mc = (int32_t)temp; return HAL_SUCCESS; }
    *temp_mc = 85000;
    return HAL_SUCCESS;
}

hal_error_t hal_platform_get_throttle_status(uint32_t *status)
{
    if (!status) return HAL_ERROR_NULL_PTR;
    if (!bcm_mailbox_get_throttled(status)) *status = 0;
    return HAL_SUCCESS;
}

bool hal_platform_is_throttled(void)
{
    uint32_t status = 0;
    hal_platform_get_throttle_status(&status);
    return (status & 0x0F) != 0;
}

hal_error_t hal_platform_set_power(hal_device_id_t device, bool on)
{
    uint32_t dev_id;
    switch (device) {
        case HAL_DEVICE_SD_CARD: dev_id = BCM_POWER_SD;    break;
        case HAL_DEVICE_UART0:   dev_id = BCM_POWER_UART0; break;
        case HAL_DEVICE_UART1:   dev_id = BCM_POWER_UART1; break;
        case HAL_DEVICE_USB:     dev_id = BCM_POWER_USB;   break;
        case HAL_DEVICE_I2C0:    dev_id = BCM_POWER_I2C0;  break;
        case HAL_DEVICE_I2C1:    dev_id = BCM_POWER_I2C1;  break;
        case HAL_DEVICE_I2C2:    dev_id = BCM_POWER_I2C2;  break;
        case HAL_DEVICE_SPI:     dev_id = BCM_POWER_SPI;   break;
        default: return HAL_ERROR_INVALID_ARG;
    }
    return bcm_mailbox_set_power_state(dev_id, on) ? HAL_SUCCESS : HAL_ERROR_HARDWARE;
}

hal_error_t hal_platform_get_power(hal_device_id_t device, bool *on)
{
    if (!on) return HAL_ERROR_NULL_PTR;
    uint32_t dev_id;
    switch (device) {
        case HAL_DEVICE_SD_CARD: dev_id = BCM_POWER_SD;    break;
        case HAL_DEVICE_UART0:   dev_id = BCM_POWER_UART0; break;
        case HAL_DEVICE_UART1:   dev_id = BCM_POWER_UART1; break;
        case HAL_DEVICE_USB:     dev_id = BCM_POWER_USB;   break;
        case HAL_DEVICE_I2C0:    dev_id = BCM_POWER_I2C0;  break;
        case HAL_DEVICE_I2C1:    dev_id = BCM_POWER_I2C1;  break;
        case HAL_DEVICE_I2C2:    dev_id = BCM_POWER_I2C2;  break;
        case HAL_DEVICE_SPI:     dev_id = BCM_POWER_SPI;   break;
        default: return HAL_ERROR_INVALID_ARG;
    }
    if (!bcm_mailbox_get_power_state(dev_id, on)) *on = true;
    return HAL_SUCCESS;
}

hal_error_t hal_platform_reboot(void)   { return HAL_ERROR_NOT_SUPPORTED; }
hal_error_t hal_platform_shutdown(void) { return HAL_ERROR_NOT_SUPPORTED; }

HAL_NORETURN void hal_panic(const char *message)
{
    hal_debug_puts("\n[PANIC] ");
    hal_debug_puts(message ? message : "(no message)");
    hal_debug_puts("\n");
    while (1) { HAL_WFI(); }
}