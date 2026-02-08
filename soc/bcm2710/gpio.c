/*
 * soc/bcm2710/gpio.c - BCM2710 GPIO Implementation
 *
 * Tutorial-OS: BCM2710 HAL Implementation
 *
 * The core register manipulation is UNCHANGED - we just added hal_gpio_*
 * functions that provide error codes and call the existing implementation.
 */

#include "hal/hal_gpio.h"
#include "hal/hal_timer.h"  /* For delay in pull config */
#include "bcm2710_regs.h"

/* =============================================================================
 * INTERNAL: HAL to BCM Function Mapping
 * =============================================================================
 * BCM uses non-sequential encoding for GPIO functions.
 */

static uint32_t hal_to_bcm_function(hal_gpio_function_t func)
{
    switch (func) {
        case HAL_GPIO_INPUT:    return BCM_GPIO_FUNC_INPUT;
        case HAL_GPIO_OUTPUT:   return BCM_GPIO_FUNC_OUTPUT;
        case HAL_GPIO_ALT0:     return BCM_GPIO_FUNC_ALT0;
        case HAL_GPIO_ALT1:     return BCM_GPIO_FUNC_ALT1;
        case HAL_GPIO_ALT2:     return BCM_GPIO_FUNC_ALT2;
        case HAL_GPIO_ALT3:     return BCM_GPIO_FUNC_ALT3;
        case HAL_GPIO_ALT4:     return BCM_GPIO_FUNC_ALT4;
        case HAL_GPIO_ALT5:     return BCM_GPIO_FUNC_ALT5;
        default:                return BCM_GPIO_FUNC_INPUT;
    }
}

static hal_gpio_function_t bcm_to_hal_function(uint32_t bcm_func)
{
    switch (bcm_func) {
        case BCM_GPIO_FUNC_INPUT:   return HAL_GPIO_INPUT;
        case BCM_GPIO_FUNC_OUTPUT:  return HAL_GPIO_OUTPUT;
        case BCM_GPIO_FUNC_ALT0:    return HAL_GPIO_ALT0;
        case BCM_GPIO_FUNC_ALT1:    return HAL_GPIO_ALT1;
        case BCM_GPIO_FUNC_ALT2:    return HAL_GPIO_ALT2;
        case BCM_GPIO_FUNC_ALT3:    return HAL_GPIO_ALT3;
        case BCM_GPIO_FUNC_ALT4:    return HAL_GPIO_ALT4;
        case BCM_GPIO_FUNC_ALT5:    return HAL_GPIO_ALT5;
        default:                    return HAL_GPIO_INPUT;
    }
}

/* =============================================================================
 * INTERNAL: Core GPIO Operations
 * =============================================================================
 */

/*
 * Set GPIO function - existing gpio_set_function() code
 */
static void bcm_gpio_set_function(uint32_t pin, uint32_t function)
{
    /*
     * Each GPFSEL register handles 10 pins, with 3 bits per pin.
     * Register offset = (pin / 10) * 4
     * Bit position = (pin % 10) * 3
     */
    uintptr_t reg_addr = BCM_GPIO_BASE + (pin / 10) * 4;
    uint32_t shift = (pin % 10) * 3;
    uint32_t mask = 0x7 << shift;

    uint32_t val = hal_mmio_read32(reg_addr);
    val = (val & ~mask) | (function << shift);
    hal_mmio_write32(reg_addr, val);
}

/*
 * Get GPIO function
 */
static uint32_t bcm_gpio_get_function(uint32_t pin)
{
    uintptr_t reg_addr = BCM_GPIO_BASE + (pin / 10) * 4;
    uint32_t shift = (pin % 10) * 3;

    uint32_t val = hal_mmio_read32(reg_addr);
    return (val >> shift) & 0x7;
}

/*
 * Set pull resistor - existing gpio_set_pull() code
 */
static void bcm_gpio_set_pull(uint32_t pin, uint32_t pull)
{
    /*
     * Pull resistor configuration requires a specific sequence:
     * 1. Write pull type to GPPUD
     * 2. Wait 150 cycles
     * 3. Write clock to GPPUDCLK0/1 for the pin
     * 4. Wait 150 cycles
     * 5. Clear GPPUD
     * 6. Clear GPPUDCLK0/1
     */
    hal_mmio_write32(BCM_GPPUD, pull);
    hal_delay_us(150);

    if (pin < 32) {
        hal_mmio_write32(BCM_GPPUDCLK0, 1 << pin);
    } else {
        hal_mmio_write32(BCM_GPPUDCLK1, 1 << (pin - 32));
    }
    hal_delay_us(150);

    hal_mmio_write32(BCM_GPPUD, 0);
    if (pin < 32) {
        hal_mmio_write32(BCM_GPPUDCLK0, 0);
    } else {
        hal_mmio_write32(BCM_GPPUDCLK1, 0);
    }
}

/* =============================================================================
 * HAL GPIO IMPLEMENTATION
 * =============================================================================
 */

hal_error_t hal_gpio_init(void)
{
    /* BCM2710 GPIO doesn't need special initialization */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_function(uint32_t pin, hal_gpio_function_t function)
{
    if (pin > BCM_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    uint32_t bcm_func = hal_to_bcm_function(function);
    bcm_gpio_set_function(pin, bcm_func);

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_get_function(uint32_t pin, hal_gpio_function_t *function)
{
    if (pin > BCM_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }
    if (function == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    uint32_t bcm_func = bcm_gpio_get_function(pin);
    *function = bcm_to_hal_function(bcm_func);

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_pull(uint32_t pin, hal_gpio_pull_t pull)
{
    if (pin > BCM_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    uint32_t bcm_pull;
    switch (pull) {
        case HAL_GPIO_PULL_NONE: bcm_pull = BCM_GPIO_PULL_OFF; break;
        case HAL_GPIO_PULL_DOWN: bcm_pull = BCM_GPIO_PULL_DOWN; break;
        case HAL_GPIO_PULL_UP:   bcm_pull = BCM_GPIO_PULL_UP; break;
        default:                 bcm_pull = BCM_GPIO_PULL_OFF; break;
    }

    bcm_gpio_set_pull(pin, bcm_pull);
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_high(uint32_t pin)
{
    if (pin > BCM_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    if (pin < 32) {
        hal_mmio_write32(BCM_GPSET0, 1 << pin);
    } else {
        hal_mmio_write32(BCM_GPSET1, 1 << (pin - 32));
    }

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_low(uint32_t pin)
{
    if (pin > BCM_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    if (pin < 32) {
        hal_mmio_write32(BCM_GPCLR0, 1 << pin);
    } else {
        hal_mmio_write32(BCM_GPCLR1, 1 << (pin - 32));
    }

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_write(uint32_t pin, bool value)
{
    return value ? hal_gpio_set_high(pin) : hal_gpio_set_low(pin);
}

bool hal_gpio_read(uint32_t pin)
{
    if (pin > BCM_GPIO_MAX_PIN) {
        return false;
    }

    uint32_t val;
    if (pin < 32) {
        val = hal_mmio_read32(BCM_GPLEV0);
        return (val & (1 << pin)) != 0;
    } else {
        val = hal_mmio_read32(BCM_GPLEV1);
        return (val & (1 << (pin - 32))) != 0;
    }
}

hal_error_t hal_gpio_toggle(uint32_t pin)
{
    bool current = hal_gpio_read(pin);
    return hal_gpio_write(pin, !current);
}

bool hal_gpio_is_valid(uint32_t pin)
{
    return pin <= BCM_GPIO_MAX_PIN;
}

uint32_t hal_gpio_max_pin(void)
{
    return BCM_GPIO_MAX_PIN;
}

/* =============================================================================
 * BULK OPERATIONS
 * =============================================================================
 */

hal_error_t hal_gpio_set_mask(uint32_t mask, uint32_t bank)
{
    if (bank == 0) {
        hal_mmio_write32(BCM_GPSET0, mask);
    } else if (bank == 1) {
        hal_mmio_write32(BCM_GPSET1, mask);
    } else {
        return HAL_ERROR_INVALID_ARG;
    }
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_clear_mask(uint32_t mask, uint32_t bank)
{
    if (bank == 0) {
        hal_mmio_write32(BCM_GPCLR0, mask);
    } else if (bank == 1) {
        hal_mmio_write32(BCM_GPCLR1, mask);
    } else {
        return HAL_ERROR_INVALID_ARG;
    }
    return HAL_SUCCESS;
}

uint32_t hal_gpio_read_mask(uint32_t bank)
{
    if (bank == 0) {
        return hal_mmio_read32(BCM_GPLEV0);
    } else if (bank == 1) {
        return hal_mmio_read32(BCM_GPLEV1);
    }
    return 0;
}

/* =============================================================================
 * PERIPHERAL CONFIGURATION
 * =============================================================================
 * These exist in gpio_configure_for_*() functions.
 */

hal_error_t hal_gpio_configure_dpi(void)
{
    /*
     * Configure GPIO 0-27 for DPI display output (24-bit color)
     *
     * IMPORTANT: GPIO 18 and 19 are EXCLUDED from DPI configuration
     * because they are used for PWM audio output on the GPi Case 2W.
     *
     * DPI uses ALT2 function:
     *   GPIO 0-7:   Blue/Green data bits
     *   GPIO 8-15:  Green/Red data bits
     *   GPIO 16-17: HSYNC, VSYNC
     *   GPIO 18-19: RESERVED FOR PWM AUDIO (not configured here!)
     *   GPIO 20-27: Red channel / control signals
     */

    const uint32_t ALT2 = BCM_GPIO_FUNC_ALT2;

    /* GPFSEL0: GPIO 0-9 - All ALT2 for DPI */
    uint32_t gpfsel0_val = (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9)
                         | (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21)
                         | (ALT2 << 24) | (ALT2 << 27);

    /* GPFSEL1: GPIO 10-17 ALT2, GPIO 18-19 left as input (for PWM audio) */
    uint32_t gpfsel1_val = (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9)
                         | (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21);
    /* NOTE: No (ALT2 << 24) or (ALT2 << 27) - GPIO 18/19 reserved for audio! */

    /* GPFSEL2: GPIO 20-27 - All ALT2 for DPI */
    uint32_t gpfsel2_val = (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9)
                         | (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21);

    hal_mmio_write32(BCM_GPFSEL0, gpfsel0_val);
    hal_mmio_write32(BCM_GPFSEL1, gpfsel1_val);
    hal_mmio_write32(BCM_GPFSEL2, gpfsel2_val);

    /* Memory barrier to ensure GPIO configuration is complete */
    HAL_DMB();

    /* Disable pull-up/down on DPI pins (GPIO 0-17 and 20-27) */
    hal_mmio_write32(BCM_GPPUD, BCM_GPIO_PULL_OFF);
    hal_delay_us(150);
    hal_mmio_write32(BCM_GPPUDCLK0, 0x0FF3FFFF);  /* Bits 0-17 and 20-27 */
    hal_delay_us(150);
    hal_mmio_write32(BCM_GPPUD, 0);
    hal_mmio_write32(BCM_GPPUDCLK0, 0);

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_hdmi(void)
{
    /* HDMI doesn't require GPIO configuration on BCM2710 */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_sdcard(void)
{
    /*
     * Configure GPIO 48-53 for SDHOST controller (ALT0 function)
     *
     * Pin mapping:
     *   GPIO 48: SD_CLK
     *   GPIO 49: SD_CMD
     *   GPIO 50: SD_DAT0
     *   GPIO 51: SD_DAT1
     *   GPIO 52: SD_DAT2
     *   GPIO 53: SD_DAT3
     */

    const uint32_t ALT0 = BCM_GPIO_FUNC_ALT0;

    /* GPIO 48-49 are in GPFSEL4 (bits 24-29) */
    uint32_t gpfsel4 = hal_mmio_read32(BCM_GPFSEL4);
    gpfsel4 = (gpfsel4 & 0xC0FFFFFF) | (ALT0 << 24) | (ALT0 << 27);
    hal_mmio_write32(BCM_GPFSEL4, gpfsel4);

    /* GPIO 50-53 are in GPFSEL5 (bits 0-11) */
    uint32_t gpfsel5 = hal_mmio_read32(BCM_GPFSEL5);
    gpfsel5 = (gpfsel5 & 0xFFFFF000)
            | (ALT0 << 0)    /* GPIO 50 */
            | (ALT0 << 3)    /* GPIO 51 */
            | (ALT0 << 6)    /* GPIO 52 */
            | (ALT0 << 9);   /* GPIO 53 */
    hal_mmio_write32(BCM_GPFSEL5, gpfsel5);

    /* Enable pull-ups on data and command lines (GPIO 49-53) */
    hal_mmio_write32(BCM_GPPUD, BCM_GPIO_PULL_UP);
    hal_delay_us(150);
    hal_mmio_write32(BCM_GPPUDCLK1, (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21));
    hal_delay_us(150);
    hal_mmio_write32(BCM_GPPUD, 0);
    hal_mmio_write32(BCM_GPPUDCLK1, 0);

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_audio(void)
{
    /*
     * Configure GPIO 18-19 for PWM audio (ALT5 function)
     *
     * Pin mapping:
     *   GPIO 18: PWM0 -> Left channel
     *   GPIO 19: PWM1 -> Right channel
     */

    const uint32_t ALT5 = BCM_GPIO_FUNC_ALT5;

    /* Read current GPFSEL1, modify only GPIO 18-19 (bits 24-29) */
    uint32_t gpfsel1 = hal_mmio_read32(BCM_GPFSEL1);
    gpfsel1 = (gpfsel1 & 0xC0FFFFFF)
            | (ALT5 << 24)    /* GPIO 18 = ALT5 (PWM0) */
            | (ALT5 << 27);   /* GPIO 19 = ALT5 (PWM1) */
    hal_mmio_write32(BCM_GPFSEL1, gpfsel1);

    /* No pull resistors needed for PWM output */
    hal_mmio_write32(BCM_GPPUD, BCM_GPIO_PULL_OFF);
    hal_delay_us(150);
    hal_mmio_write32(BCM_GPPUDCLK0, (1 << 18) | (1 << 19));
    hal_delay_us(150);
    hal_mmio_write32(BCM_GPPUD, 0);
    hal_mmio_write32(BCM_GPPUDCLK0, 0);

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_uart(uint32_t uart_num)
{
    if (uart_num == 0) {
        /* UART0 (PL011) uses GPIO 14 (TX) and 15 (RX) - ALT0 */
        bcm_gpio_set_function(14, BCM_GPIO_FUNC_ALT0);
        bcm_gpio_set_function(15, BCM_GPIO_FUNC_ALT0);
        bcm_gpio_set_pull(14, BCM_GPIO_PULL_OFF);
        bcm_gpio_set_pull(15, BCM_GPIO_PULL_UP);
    } else if (uart_num == 1) {
        /* UART1 (Mini UART) uses GPIO 14 (TX) and 15 (RX) - ALT5 */
        bcm_gpio_set_function(14, BCM_GPIO_FUNC_ALT5);
        bcm_gpio_set_function(15, BCM_GPIO_FUNC_ALT5);
        bcm_gpio_set_pull(14, BCM_GPIO_PULL_OFF);
        bcm_gpio_set_pull(15, BCM_GPIO_PULL_UP);
    } else {
        return HAL_ERROR_INVALID_ARG;
    }

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_safe_shutdown(void)
{
    /* GPi Case 2W uses GPIO 26 for safe shutdown button */
    bcm_gpio_set_function(26, BCM_GPIO_FUNC_INPUT);
    bcm_gpio_set_pull(26, BCM_GPIO_PULL_UP);
    return HAL_SUCCESS;
}

bool hal_gpio_safe_shutdown_triggered(void)
{
    /* GPIO 26 is active low on GPi Case */
    return !hal_gpio_read(26);
}
