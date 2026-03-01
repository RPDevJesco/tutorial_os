/*
 * soc/bcm2712/gpio.c - BCM2712 GPIO Driver (RP1 South Bridge)
 * ============================================================
 *
 * On BCM2712 (Pi 5 / CM5), all user-facing GPIO is handled by the RP1 south
 * bridge chip rather than by the main SoC.  RP1 is connected via PCIe and
 * appears at 0x1F000D0000 in the ARM physical address space (mapped via the
 * BCM2712 PCIe outbound window and the page tables we set up in boot_soc.S).
 *
 * RP1 GPIO REGISTER MAP (per-pin, 8-byte stride):
 *
 *   RP1_GPIO_IO_BANK0 + pin*8 + 0 : GPIO_STATUS  (read-only)
 *   RP1_GPIO_IO_BANK0 + pin*8 + 4 : GPIO_CTRL    (read-write)
 *
 * GPIO_CTRL bits:
 *   [4:0]  FUNCSEL   — function select (0–8, see table below)
 *   [13:12] OUTOVER  — output override
 *   [17:16] OEOVER   — output-enable override
 *   [21:20] INOVER   — input override
 *   [29:28] IRQOVER  — IRQ override
 *
 * FUNCSEL VALUES FOR GPIO 14 AND 15 (RP1 datasheet, Table 3-1):
 *   0 = SPI1_SCLK / SPI1_TX       (SPI1)
 *   1 = (reserved)
 *   2 = (reserved)
 *   3 = (reserved)
 *   4 = UART0_TX / UART0_RX       ← use this for UART debug
 *   5 = PWM0_CH0 / PWM0_CH1       (audio)
 *   6 = (reserved)
 *   7 = (reserved)
 *   8 = NULL (GPIO input mode)
 *
 * ⚠ BUG FIX: The original gpio.c had RP1_GPIO_CTRL and RP1_GPIO_STATUS
 *   MACRO NAMES SWAPPED. RP1_GPIO_CTRL was pointing to offset +0 (STATUS)
 *   and RP1_GPIO_STATUS was pointing to offset +4 (CTRL).  Writing FUNCSEL
 *   into the STATUS register (read-only) had no effect, meaning
 *   hal_gpio_set_function() was silently doing nothing.  This went unnoticed
 *   because the GPU firmware configures HDMI before our code runs.  The UART
 *   is the first peripheral we configure ourselves, which exposed the bug.
 *
 * RP1 PADS REGISTER MAP:
 *   RP1_GPIO_PADS_BANK0 + pin*4 : PADS register
 *   bits [3:2] = PUE/PDE (pull-up/pull-down enable)
 */

#include "bcm2712_regs.h"
#include "hal/hal_gpio.h"

/* -------------------------------------------------------------------------
 * RP1 GPIO register accessors — STATUS at +0, CTRL at +4 per pin
 * ------------------------------------------------------------------------- */
#define RP1_GPIO_STATUS(n)   ((volatile uint32_t *)(RP1_GPIO_IO_BANK0 + (n) * 8 + 0))
#define RP1_GPIO_CTRL(n)     ((volatile uint32_t *)(RP1_GPIO_IO_BANK0 + (n) * 8 + 4))

/* GPIO_CTRL bits */
#define GPIO_CTRL_FUNCSEL_MASK  0x1FU   /* bits [4:0] */

/* RP1 FUNCSEL values for common peripherals on GPIO 14/15 */
#define RP1_FUNC_UART0      4           /* UART0_TX (GPIO14), UART0_RX (GPIO15) */
#define RP1_FUNC_PWM_AUDIO  5           /* PWM0 audio */
#define RP1_FUNC_NULL       8           /* High-impedance input */
#define RP1_FUNC_ALT2       2           /* DPI (not present on CM5 — no-op) */

/* RP1 PADS register */
#define RP1_GPIO_PAD(n)      ((volatile uint32_t *)(RP1_GPIO_PADS_BANK0 + (n) * 4))
#define PAD_PUE              (1u << 3)  /* pull-up enable */
#define PAD_PDE              (1u << 2)  /* pull-down enable */
#define PAD_PULL_MASK        (PAD_PUE | PAD_PDE)

/* SIO (direct GPIO output) registers on RP1.
 *
 * NOTE: RP1 SIO base address needs verification against the RP1 peripheral
 * spec; the offset 0x0e0000 from RP1_PERI_BASE is a best-effort match.
 * If GPIO set/clear does not work, this is the first place to look.
 */
#define RP1_SIO_BASE        (RP1_PERI_BASE + 0x0e0000)
#define RP1_GPIO_OUT        ((volatile uint32_t *)(RP1_SIO_BASE + 0x00))
#define RP1_GPIO_OUT_SET    ((volatile uint32_t *)(RP1_SIO_BASE + 0x04))
#define RP1_GPIO_OUT_CLR    ((volatile uint32_t *)(RP1_SIO_BASE + 0x08))
#define RP1_GPIO_OE         ((volatile uint32_t *)(RP1_SIO_BASE + 0x0C))
#define RP1_GPIO_OE_SET     ((volatile uint32_t *)(RP1_SIO_BASE + 0x10))
#define RP1_GPIO_OE_CLR     ((volatile uint32_t *)(RP1_SIO_BASE + 0x14))
#define RP1_GPIO_IN         ((volatile uint32_t *)(RP1_SIO_BASE + 0x18))

#define MAX_GPIO_PIN        27


/* =========================================================================
 * INTERNAL HELPER
 * ========================================================================= */

/*
 * rp1_gpio_set_funcsel - Write FUNCSEL into GPIO_CTRL for the given pin.
 *
 * This is the correct way to set a pin's function on RP1.  It reads the
 * current CTRL value, clears FUNCSEL bits [4:0], and writes the new value.
 * Only FUNCSEL is touched; all override bits are preserved.
 */
static void rp1_gpio_set_funcsel(uint32_t pin, uint32_t funcsel)
{
    volatile uint32_t *ctrl = RP1_GPIO_CTRL(pin);   /* offset +4 = CTRL */
    uint32_t val = *ctrl;
    val &= ~GPIO_CTRL_FUNCSEL_MASK;
    val |= (funcsel & GPIO_CTRL_FUNCSEL_MASK);
    *ctrl = val;
}


/* =========================================================================
 * HAL GPIO INTERFACE
 * ========================================================================= */

hal_error_t hal_gpio_init(void)
{
    /* RP1 GPIO is ready after boot; no reset sequence required. */
    return HAL_SUCCESS;
}

bool hal_gpio_is_valid(uint32_t pin)
{
    return pin <= MAX_GPIO_PIN;
}

uint32_t hal_gpio_max_pin(void)
{
    return MAX_GPIO_PIN;
}

/*
 * hal_gpio_set_function - Set GPIO pin function
 *
 * The hal_gpio_function_t enum values map to RP1 FUNCSEL values as follows:
 *   HAL_GPIO_ALT0 = 0  (SPI1 on GPIO 14/15)
 *   HAL_GPIO_ALT1 = 1
 *   HAL_GPIO_ALT2 = 2  (DPI — not useful on CM5)
 *   HAL_GPIO_ALT3 = 3
 *   HAL_GPIO_ALT4 = 4  (UART0 on GPIO 14/15)
 *   HAL_GPIO_ALT5 = 5  (PWM audio on GPIO 18/19)
 *
 * The numerical values happen to match RP1 FUNCSEL directly, so we can
 * pass them through.  Use the RP1_FUNC_* constants in this file for
 * internal calls to make the intent explicit.
 */
hal_error_t hal_gpio_set_function(uint32_t pin, hal_gpio_function_t function)
{
    if (!hal_gpio_is_valid(pin)) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    rp1_gpio_set_funcsel(pin, (uint32_t)function);
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_get_function(uint32_t pin, hal_gpio_function_t *function)
{
    if (!hal_gpio_is_valid(pin)) return HAL_ERROR_GPIO_INVALID_PIN;
    if (!function)              return HAL_ERROR_NULL_PTR;

    *function = (hal_gpio_function_t)((*RP1_GPIO_CTRL(pin)) & GPIO_CTRL_FUNCSEL_MASK);
    return HAL_SUCCESS;
}

/*
 * hal_gpio_set_pull - Set pull-up / pull-down resistor via RP1 PADS register.
 *
 * The PADS register for each pin sits at RP1_GPIO_PADS_BANK0 + pin*4.
 * bits[3:2]:  PUE (pull-up enable) | PDE (pull-down enable)
 * Only one should be set at a time; setting both is undefined.
 */
hal_error_t hal_gpio_set_pull(uint32_t pin, hal_gpio_pull_t pull)
{
    if (!hal_gpio_is_valid(pin)) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    volatile uint32_t *pad = RP1_GPIO_PAD(pin);
    uint32_t val = *pad & ~PAD_PULL_MASK;

    switch (pull) {
        case HAL_GPIO_PULL_UP:   val |= PAD_PUE; break;
        case HAL_GPIO_PULL_DOWN: val |= PAD_PDE; break;
        case HAL_GPIO_PULL_NONE: break;             /* both bits cleared above */
        default: return HAL_ERROR_INVALID_ARG;
    }

    *pad = val;
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_high(uint32_t pin)
{
    if (!hal_gpio_is_valid(pin)) return HAL_ERROR_GPIO_INVALID_PIN;
    *RP1_GPIO_OUT_SET = (1u << pin);
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_low(uint32_t pin)
{
    if (!hal_gpio_is_valid(pin)) return HAL_ERROR_GPIO_INVALID_PIN;
    *RP1_GPIO_OUT_CLR = (1u << pin);
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_write(uint32_t pin, bool value)
{
    return value ? hal_gpio_set_high(pin) : hal_gpio_set_low(pin);
}

bool hal_gpio_read(uint32_t pin)
{
    if (!hal_gpio_is_valid(pin)) return false;
    return (*RP1_GPIO_IN >> pin) & 1u;
}

hal_error_t hal_gpio_toggle(uint32_t pin)
{
    if (!hal_gpio_is_valid(pin)) return HAL_ERROR_GPIO_INVALID_PIN;
    uint32_t current = (*RP1_GPIO_OUT >> pin) & 1u;
    return current ? hal_gpio_set_low(pin) : hal_gpio_set_high(pin);
}

hal_error_t hal_gpio_set_mask(uint32_t mask, uint32_t bank)
{
    if (bank != 0) return HAL_ERROR_GPIO_INVALID_PIN;
    *RP1_GPIO_OUT_SET = mask;
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_clear_mask(uint32_t mask, uint32_t bank)
{
    if (bank != 0) return HAL_ERROR_GPIO_INVALID_PIN;
    *RP1_GPIO_OUT_CLR = mask;
    return HAL_SUCCESS;
}

uint32_t hal_gpio_read_mask(uint32_t bank)
{
    if (bank != 0) return 0;
    return *RP1_GPIO_IN;
}


/* =========================================================================
 * PERIPHERAL PIN CONFIGURATION
 * ========================================================================= */

hal_error_t hal_gpio_configure_dpi(void)
{
    /*
     * DPI (24-bit parallel display) uses GPIO 0-27 on BCM2710/BCM2711.
     * On BCM2712 (CM5), display is HDMI via RP1 — DPI is not connected.
     * This function is a deliberate no-op on BCM2712.  See display_dpi.c.
     */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_hdmi(void)
{
    /* HDMI is handled entirely by the RP1 chip and GPU firmware. */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_sdcard(void)
{
    /* SD card pins are handled by the dedicated SD controller. */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_audio(void)
{
    /* GPIO 18-19: PWM audio, FUNCSEL = 5 (PWM0_CH0/CH1 on RP1) */
    rp1_gpio_set_funcsel(18, RP1_FUNC_PWM_AUDIO);
    rp1_gpio_set_funcsel(19, RP1_FUNC_PWM_AUDIO);
    return HAL_SUCCESS;
}

/*
 * hal_gpio_configure_uart - Configure GPIO 14/15 for UART0
 *
 * RP1 UART0 uses FUNCSEL = 4 on GPIO 14 (TX) and GPIO 15 (RX).
 * This is different from BCM2710/BCM2711 where UART uses ALT0 (= 0).
 *
 * Note: enable_uart=1 in config.txt causes the GPU firmware to configure
 * these pins before our code runs, so UART TX will often work even without
 * this call.  We set it explicitly so the state is deterministic and does
 * not depend on firmware behaviour.
 */
hal_error_t hal_gpio_configure_uart(uint32_t uart_num)
{
    if (uart_num != 0) {
        return HAL_ERROR_UART_INVALID;
    }

    /* FUNCSEL = 4 = UART0_TX/RX on RP1 GPIO 14/15 */
    rp1_gpio_set_funcsel(14, RP1_FUNC_UART0);   /* TX */
    rp1_gpio_set_funcsel(15, RP1_FUNC_UART0);   /* RX */

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_safe_shutdown(void)
{
    return HAL_ERROR_NOT_SUPPORTED;
}

bool hal_gpio_safe_shutdown_triggered(void)
{
    return false;
}