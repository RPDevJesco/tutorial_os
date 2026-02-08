/*
 * soc/bcm2711/gpio.c - BCM2711 GPIO Implementation
 *
 * Tutorial-OS: BCM2711 HAL Implementation
 *
 * Very similar to BCM2710, but with important differences:
 *   - Different peripheral base address (handled by bcm2711_regs.h)
 *   - New pull-up/down register interface (no more GPPUD/GPPUDCLK dance!)
 *   - 58 GPIO pins instead of 54
 */

#include "hal/hal_gpio.h"
#include "hal/hal_timer.h"
#include "bcm2711_regs.h"

/* =============================================================================
 * INTERNAL: HAL to BCM Function Mapping (same as BCM2710)
 * =============================================================================
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

static void bcm_gpio_set_function(uint32_t pin, uint32_t function)
{
    uintptr_t reg_addr = BCM_GPIO_BASE + (pin / 10) * 4;
    uint32_t shift = (pin % 10) * 3;
    uint32_t mask = 0x7 << shift;

    uint32_t val = hal_mmio_read32(reg_addr);
    val = (val & ~mask) | (function << shift);
    hal_mmio_write32(reg_addr, val);
}

static uint32_t bcm_gpio_get_function(uint32_t pin)
{
    uintptr_t reg_addr = BCM_GPIO_BASE + (pin / 10) * 4;
    uint32_t shift = (pin % 10) * 3;

    uint32_t val = hal_mmio_read32(reg_addr);
    return (val >> shift) & 0x7;
}

/*
 * BCM2711 Pull resistor configuration - MUCH SIMPLER than BCM2710!
 *
 * BCM2710 required: GPPUD write -> delay -> GPPUDCLK write -> delay -> clear
 * BCM2711 uses: Direct 2-bit fields in GPIO_PUP_PDN_CNTRL registers
 *
 * Each register handles 16 pins with 2 bits per pin:
 *   00 = No pull
 *   01 = Pull up
 *   10 = Pull down
 */
static void bcm_gpio_set_pull(uint32_t pin, uint32_t pull)
{
    /* Calculate which register and bit position */
    uint32_t reg_num = pin / 16;
    uint32_t bit_pos = (pin % 16) * 2;

    uintptr_t reg_addr = BCM_GPIO_PUP_PDN_CNTRL0 + (reg_num * 4);

    /* Read-modify-write */
    uint32_t val = hal_mmio_read32(reg_addr);
    val &= ~(0x3 << bit_pos);    /* Clear 2-bit field */
    val |= (pull << bit_pos);    /* Set new value */
    hal_mmio_write32(reg_addr, val);
}

/* =============================================================================
 * HAL GPIO IMPLEMENTATION
 * =============================================================================
 */

hal_error_t hal_gpio_init(void)
{
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_function(uint32_t pin, hal_gpio_function_t function)
{
    if (pin > BCM_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    bcm_gpio_set_function(pin, hal_to_bcm_function(function));
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

    *function = bcm_to_hal_function(bcm_gpio_get_function(pin));
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_pull(uint32_t pin, hal_gpio_pull_t pull)
{
    if (pin > BCM_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    uint32_t bcm_pull;
    switch (pull) {
        case HAL_GPIO_PULL_NONE: bcm_pull = BCM2711_GPIO_PULL_NONE; break;
        case HAL_GPIO_PULL_UP:   bcm_pull = BCM2711_GPIO_PULL_UP; break;
        case HAL_GPIO_PULL_DOWN: bcm_pull = BCM2711_GPIO_PULL_DOWN; break;
        default:                 bcm_pull = BCM2711_GPIO_PULL_NONE; break;
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

    if (pin < 32) {
        return (hal_mmio_read32(BCM_GPLEV0) & (1 << pin)) != 0;
    } else {
        return (hal_mmio_read32(BCM_GPLEV1) & (1 << (pin - 32))) != 0;
    }
}

hal_error_t hal_gpio_toggle(uint32_t pin)
{
    return hal_gpio_write(pin, !hal_gpio_read(pin));
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
 */

hal_error_t hal_gpio_configure_dpi(void)
{
    /*
     * Configure GPIO for DPI display.
     * Same pin assignments as BCM2710, just different pull config method.
     */
    const uint32_t ALT2 = BCM_GPIO_FUNC_ALT2;

    /* GPIO 0-9: ALT2 for DPI */
    hal_mmio_write32(BCM_GPFSEL0,
        (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9) |
        (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21) |
        (ALT2 << 24) | (ALT2 << 27));

    /* GPIO 10-17: ALT2 for DPI (GPIO 18-19 reserved for audio) */
    hal_mmio_write32(BCM_GPFSEL1,
        (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9) |
        (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21));

    /* GPIO 20-27: ALT2 for DPI */
    hal_mmio_write32(BCM_GPFSEL2,
        (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9) |
        (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21));

    /* Disable pulls on DPI pins (BCM2711 style - direct register writes) */
    hal_mmio_write32(BCM_GPIO_PUP_PDN_CNTRL0, 0);  /* GPIO 0-15: no pull */
    /* GPIO 16-17 no pull, 18-19 keep existing (audio), 20-27 no pull */
    uint32_t pup1 = hal_mmio_read32(BCM_GPIO_PUP_PDN_CNTRL1);
    pup1 &= 0x000F0000;  /* Keep bits for GPIO 18-19 */
    hal_mmio_write32(BCM_GPIO_PUP_PDN_CNTRL1, pup1);

    HAL_DMB();
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_hdmi(void)
{
    /* HDMI doesn't require GPIO configuration on BCM2711 */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_sdcard(void)
{
    /*
     * BCM2711 can use either SDHOST (GPIO 48-53) or EMMC2 (GPIO 22-27).
     * CM4 typically uses EMMC2 for the SD card.
     * Configure both for flexibility.
     */
    const uint32_t ALT0 = BCM_GPIO_FUNC_ALT0;
    const uint32_t ALT3 = BCM_GPIO_FUNC_ALT3;

    /* EMMC2 on GPIO 22-27 (ALT3) - default for CM4 */
    uint32_t gpfsel2 = hal_mmio_read32(BCM_GPFSEL2);
    gpfsel2 = (gpfsel2 & 0x00000FFF)  /* Keep GPIO 20-21 */
            | (ALT3 << 6)   /* GPIO 22 */
            | (ALT3 << 9)   /* GPIO 23 */
            | (ALT3 << 12)  /* GPIO 24 */
            | (ALT3 << 15)  /* GPIO 25 */
            | (ALT3 << 18)  /* GPIO 26 */
            | (ALT3 << 21); /* GPIO 27 */
    hal_mmio_write32(BCM_GPFSEL2, gpfsel2);

    /* Pull-ups on data and command lines */
    for (uint32_t pin = 23; pin <= 27; pin++) {
        bcm_gpio_set_pull(pin, BCM2711_GPIO_PULL_UP);
    }

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_audio(void)
{
    /* GPIO 18-19 for PWM audio (ALT5) */
    const uint32_t ALT5 = BCM_GPIO_FUNC_ALT5;

    uint32_t gpfsel1 = hal_mmio_read32(BCM_GPFSEL1);
    gpfsel1 = (gpfsel1 & 0xC0FFFFFF)
            | (ALT5 << 24)    /* GPIO 18 = PWM0 */
            | (ALT5 << 27);   /* GPIO 19 = PWM1 */
    hal_mmio_write32(BCM_GPFSEL1, gpfsel1);

    bcm_gpio_set_pull(18, BCM2711_GPIO_PULL_NONE);
    bcm_gpio_set_pull(19, BCM2711_GPIO_PULL_NONE);

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_uart(uint32_t uart_num)
{
    if (uart_num == 0) {
        /* UART0 (PL011) on GPIO 14-15 (ALT0) */
        bcm_gpio_set_function(14, BCM_GPIO_FUNC_ALT0);
        bcm_gpio_set_function(15, BCM_GPIO_FUNC_ALT0);
        bcm_gpio_set_pull(14, BCM2711_GPIO_PULL_NONE);
        bcm_gpio_set_pull(15, BCM2711_GPIO_PULL_UP);
    } else if (uart_num == 1) {
        /* UART1 (Mini UART) on GPIO 14-15 (ALT5) */
        bcm_gpio_set_function(14, BCM_GPIO_FUNC_ALT5);
        bcm_gpio_set_function(15, BCM_GPIO_FUNC_ALT5);
        bcm_gpio_set_pull(14, BCM2711_GPIO_PULL_NONE);
        bcm_gpio_set_pull(15, BCM2711_GPIO_PULL_UP);
    } else {
        return HAL_ERROR_INVALID_ARG;
    }

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_safe_shutdown(void)
{
    bcm_gpio_set_function(26, BCM_GPIO_FUNC_INPUT);
    bcm_gpio_set_pull(26, BCM2711_GPIO_PULL_UP);
    return HAL_SUCCESS;
}

bool hal_gpio_safe_shutdown_triggered(void)
{
    return !hal_gpio_read(26);
}
