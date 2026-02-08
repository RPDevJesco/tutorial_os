/*
 * soc/bcm2712/gpio.c - BCM2712 GPIO Driver
 * ========================================
 *
 * BCM2712 GPIO is handled by the RP1 south bridge chip.
 */

#include "bcm2712_regs.h"
#include "hal/hal_gpio.h"

/*
 * RP1 GPIO Register Offsets
 */
#define RP1_GPIO_CTRL(n)    ((volatile uint32_t *)(RP1_GPIO_IO_BANK0 + (n) * 8))
#define RP1_GPIO_STATUS(n)  ((volatile uint32_t *)(RP1_GPIO_IO_BANK0 + (n) * 8 + 4))

/* GPIO Control register bits */
#define GPIO_CTRL_FUNCSEL_MASK  0x1F

/* Function select values */
#define GPIO_FUNC_INPUT     0
#define GPIO_FUNC_OUTPUT    5
#define GPIO_FUNC_ALT0      0
#define GPIO_FUNC_ALT1      1
#define GPIO_FUNC_ALT2      2
#define GPIO_FUNC_ALT3      3
#define GPIO_FUNC_ALT4      4

/* SIO registers for direct GPIO control */
#define RP1_SIO_BASE        (RP1_PERI_BASE + 0x0e0000)
#define RP1_GPIO_OUT        ((volatile uint32_t *)(RP1_SIO_BASE + 0x00))
#define RP1_GPIO_OUT_SET    ((volatile uint32_t *)(RP1_SIO_BASE + 0x04))
#define RP1_GPIO_OUT_CLR    ((volatile uint32_t *)(RP1_SIO_BASE + 0x08))
#define RP1_GPIO_OE         ((volatile uint32_t *)(RP1_SIO_BASE + 0x0C))
#define RP1_GPIO_OE_SET     ((volatile uint32_t *)(RP1_SIO_BASE + 0x10))
#define RP1_GPIO_OE_CLR     ((volatile uint32_t *)(RP1_SIO_BASE + 0x14))
#define RP1_GPIO_IN         ((volatile uint32_t *)(RP1_SIO_BASE + 0x18))

#define MAX_GPIO_PIN        27

/*
 * hal_gpio_init - Initialize GPIO subsystem
 */
hal_error_t hal_gpio_init(void)
{
    /* RP1 GPIO is ready to use after boot */
    return HAL_SUCCESS;
}

/*
 * hal_gpio_is_valid - Check if pin is valid
 */
bool hal_gpio_is_valid(uint32_t pin)
{
    return pin <= MAX_GPIO_PIN;
}

/*
 * hal_gpio_max_pin - Get maximum pin number
 */
uint32_t hal_gpio_max_pin(void)
{
    return MAX_GPIO_PIN;
}

/*
 * hal_gpio_set_function - Set GPIO pin function
 */
hal_error_t hal_gpio_set_function(uint32_t pin, hal_gpio_function_t function)
{
    if (!hal_gpio_is_valid(pin)) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    volatile uint32_t *ctrl = RP1_GPIO_CTRL(pin);
    uint32_t val = *ctrl;
    val &= ~GPIO_CTRL_FUNCSEL_MASK;
    val |= (function & GPIO_CTRL_FUNCSEL_MASK);
    *ctrl = val;

    return HAL_SUCCESS;
}

/*
 * hal_gpio_get_function - Get current pin function
 */
hal_error_t hal_gpio_get_function(uint32_t pin, hal_gpio_function_t *function)
{
    if (!hal_gpio_is_valid(pin)) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }
    if (!function) {
        return HAL_ERROR_NULL_PTR;
    }

    volatile uint32_t *ctrl = RP1_GPIO_CTRL(pin);
    *function = (hal_gpio_function_t)(*ctrl & GPIO_CTRL_FUNCSEL_MASK);

    return HAL_SUCCESS;
}

/*
 * hal_gpio_set_pull - Set pull-up/pull-down
 */
hal_error_t hal_gpio_set_pull(uint32_t pin, hal_gpio_pull_t pull)
{
    if (!hal_gpio_is_valid(pin)) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    /* TODO: Implement RP1 pad control for pull resistors */
    UNUSED(pull);

    return HAL_SUCCESS;
}

/*
 * hal_gpio_set_high - Set output pin HIGH
 */
hal_error_t hal_gpio_set_high(uint32_t pin)
{
    if (!hal_gpio_is_valid(pin)) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    *RP1_GPIO_OUT_SET = (1 << pin);
    return HAL_SUCCESS;
}

/*
 * hal_gpio_set_low - Set output pin LOW
 */
hal_error_t hal_gpio_set_low(uint32_t pin)
{
    if (!hal_gpio_is_valid(pin)) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    *RP1_GPIO_OUT_CLR = (1 << pin);
    return HAL_SUCCESS;
}

/*
 * hal_gpio_write - Write value to output pin
 */
hal_error_t hal_gpio_write(uint32_t pin, bool value)
{
    return value ? hal_gpio_set_high(pin) : hal_gpio_set_low(pin);
}

/*
 * hal_gpio_read - Read input pin level
 */
bool hal_gpio_read(uint32_t pin)
{
    if (!hal_gpio_is_valid(pin)) {
        return false;
    }

    return (*RP1_GPIO_IN >> pin) & 1;
}

/*
 * hal_gpio_toggle - Toggle output pin
 */
hal_error_t hal_gpio_toggle(uint32_t pin)
{
    if (!hal_gpio_is_valid(pin)) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    uint32_t current = (*RP1_GPIO_OUT >> pin) & 1;
    return current ? hal_gpio_set_low(pin) : hal_gpio_set_high(pin);
}

/*
 * hal_gpio_set_mask - Set multiple pins high
 */
hal_error_t hal_gpio_set_mask(uint32_t mask, uint32_t bank)
{
    if (bank != 0) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }
    *RP1_GPIO_OUT_SET = mask;
    return HAL_SUCCESS;
}

/*
 * hal_gpio_clear_mask - Clear multiple pins
 */
hal_error_t hal_gpio_clear_mask(uint32_t mask, uint32_t bank)
{
    if (bank != 0) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }
    *RP1_GPIO_OUT_CLR = mask;
    return HAL_SUCCESS;
}

/*
 * hal_gpio_read_mask - Read multiple pins
 */
uint32_t hal_gpio_read_mask(uint32_t bank)
{
    if (bank != 0) {
        return 0;
    }
    return *RP1_GPIO_IN;
}

/*
 * hal_gpio_configure_dpi - Configure pins for DPI display
 */
hal_error_t hal_gpio_configure_dpi(void)
{
    /* DPI uses GPIO 0-27 with ALT2 function */
    for (uint32_t i = 0; i <= 27; i++) {
        hal_gpio_set_function(i, HAL_GPIO_ALT2);
    }
    return HAL_SUCCESS;
}

/*
 * hal_gpio_configure_hdmi - Configure pins for HDMI
 */
hal_error_t hal_gpio_configure_hdmi(void)
{
    /* HDMI doesn't require GPIO configuration on BCM2712 */
    return HAL_SUCCESS;
}

/*
 * hal_gpio_configure_sdcard - Configure pins for SD card
 */
hal_error_t hal_gpio_configure_sdcard(void)
{
    /* SD card pins are handled by dedicated SD controller */
    return HAL_SUCCESS;
}

/*
 * hal_gpio_configure_audio - Configure pins for PWM audio
 */
hal_error_t hal_gpio_configure_audio(void)
{
    /* GPIO 18-19 for PWM audio */
    hal_gpio_set_function(18, HAL_GPIO_ALT5);
    hal_gpio_set_function(19, HAL_GPIO_ALT5);
    return HAL_SUCCESS;
}

/*
 * hal_gpio_configure_uart - Configure pins for UART
 */
hal_error_t hal_gpio_configure_uart(uint32_t uart_num)
{
    if (uart_num == 0) {
        /* UART0 on GPIO 14 (TX) and GPIO 15 (RX) */
        hal_gpio_set_function(14, HAL_GPIO_ALT0);
        hal_gpio_set_function(15, HAL_GPIO_ALT0);
        return HAL_SUCCESS;
    }
    return HAL_ERROR_UART_INVALID;
}

/*
 * hal_gpio_configure_safe_shutdown - Configure safe shutdown
 */
hal_error_t hal_gpio_configure_safe_shutdown(void)
{
    return HAL_ERROR_NOT_SUPPORTED;
}

/*
 * hal_gpio_safe_shutdown_triggered - Check shutdown button
 */
bool hal_gpio_safe_shutdown_triggered(void)
{
    return false;
}