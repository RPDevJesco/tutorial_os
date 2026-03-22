/*
 * hal/hal_gpio.h - GPIO Pin Control Abstraction
 *
 * Tutorial-OS: HAL Interface Definitions
 *
 * This abstracts GPIO differences between platforms:
 *
 *   BCM2710/BCM2711:  Direct BCM GPIO registers
 *   bcm2712:          RP1 southbridge GPIO (via PCIe!)
 *   RK3528A:          Rockchip GPIO + pinmux
 *   S905X:            Amlogic GPIO/PINMUX
 *   H618:             Allwinner sunxi GPIO
 *   K1:               SpacemiT GPIO
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include "hal_types.h"

/* =============================================================================
 * GPIO FUNCTION MODES
 * =============================================================================
 * Generic function definitions that map to platform-specific values.
 */

typedef enum {
    HAL_GPIO_INPUT      = 0,    /* Digital input */
    HAL_GPIO_OUTPUT     = 1,    /* Digital output */
    HAL_GPIO_ALT0       = 2,    /* Alternate function 0 */
    HAL_GPIO_ALT1       = 3,    /* Alternate function 1 */
    HAL_GPIO_ALT2       = 4,    /* Alternate function 2 (DPI on BCM) */
    HAL_GPIO_ALT3       = 5,    /* Alternate function 3 */
    HAL_GPIO_ALT4       = 6,    /* Alternate function 4 */
    HAL_GPIO_ALT5       = 7,    /* Alternate function 5 (PWM on BCM) */
} hal_gpio_function_t;

/*
 * Pull resistor configuration
 * Matches existing gpio_pull_t enum.
 */
typedef enum {
    HAL_GPIO_PULL_NONE  = 0,    /* No pull resistor (floating) */
    HAL_GPIO_PULL_DOWN  = 1,    /* Pull-down resistor to GND */
    HAL_GPIO_PULL_UP    = 2,    /* Pull-up resistor to VCC */
} hal_gpio_pull_t;

/* =============================================================================
 * GPIO INITIALIZATION
 * =============================================================================
 */

/*
 * Initialize GPIO subsystem
 *
 * Called during hal_platform_init(). On BCM2710, this is a no-op.
 * On bcm2712, this must wait for RP1 initialization.
 *
 * @return  HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_init(void);

/* =============================================================================
 * BASIC GPIO OPERATIONS
 * =============================================================================
 * These match existing gpio.c functions.
 */

/*
 * Set pin function/mode
 *
 * Maps to: gpio_set_function(pin, function)
 *
 * @param pin       Pin number (platform-specific, 0-53 on BCM)
 * @param function  Desired function
 * @return          HAL_SUCCESS or HAL_ERROR_GPIO_INVALID_PIN
 */
hal_error_t hal_gpio_set_function(uint32_t pin, hal_gpio_function_t function);

/*
 * Get current pin function
 *
 * @param pin       Pin number
 * @param function  Output: current function
 * @return          HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_get_function(uint32_t pin, hal_gpio_function_t *function);

/*
 * Set pull resistor configuration
 *
 * Maps to: gpio_set_pull(pin, pull)
 *
 * @param pin   Pin number
 * @param pull  Pull configuration
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_set_pull(uint32_t pin, hal_gpio_pull_t pull);

/*
 * Set output pin HIGH
 *
 * Maps to: gpio_set_high(pin)
 *
 * @param pin   Pin number
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_set_high(uint32_t pin);

/*
 * Set output pin LOW
 *
 * Maps to: gpio_set_low(pin)
 *
 * @param pin   Pin number
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_set_low(uint32_t pin);

/*
 * Write value to output pin
 *
 * @param pin   Pin number
 * @param value true = HIGH, false = LOW
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_write(uint32_t pin, bool value);

/*
 * Read input pin level
 *
 * Maps to: gpio_read(pin)
 *
 * @param pin   Pin number
 * @return      true = HIGH, false = LOW (false for invalid pins)
 */
bool hal_gpio_read(uint32_t pin);

/*
 * Toggle output pin
 *
 * @param pin   Pin number
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_toggle(uint32_t pin);

/* =============================================================================
 * PIN VALIDATION
 * =============================================================================
 */

/*
 * Check if pin number is valid for this platform
 *
 * @param pin   Pin number
 * @return      true if valid
 */
bool hal_gpio_is_valid(uint32_t pin);

/*
 * Get maximum pin number for this platform
 *
 * @return  Highest valid pin number (53 for BCM2710)
 */
uint32_t hal_gpio_max_pin(void);

/* =============================================================================
 * BULK OPERATIONS
 * =============================================================================
 * For efficiency when manipulating multiple pins.
 */

/*
 * Set multiple pins high at once
 *
 * @param mask  Bitmask of pins to set (pin 0 = bit 0, etc.)
 * @param bank  Pin bank (0 = pins 0-31, 1 = pins 32-63)
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_set_mask(uint32_t mask, uint32_t bank);

/*
 * Clear multiple pins at once
 *
 * @param mask  Bitmask of pins to clear
 * @param bank  Pin bank
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_clear_mask(uint32_t mask, uint32_t bank);

/*
 * Read multiple pins at once
 *
 * @param bank  Pin bank
 * @return      Bitmask of pin levels
 */
uint32_t hal_gpio_read_mask(uint32_t bank);

/* =============================================================================
 * PERIPHERAL CONFIGURATION
 * =============================================================================
 * These configure groups of pins for specific hardware functions.
 * Maps to existing gpio_configure_for_*() functions.
 */

/*
 * Configure pins for DPI display output
 *
 * Maps to: gpio_configure_for_dpi()
 *
 * On GPi Case 2W: GPIO 0-17 and 20-27 to ALT2, skipping GPIO 18-19 for audio.
 *
 * @return  HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_gpio_configure_dpi(void);

/*
 * Configure pins for HDMI output
 *
 * On BCM2710, HDMI doesn't require GPIO configuration.
 * On other platforms, may need pin setup.
 *
 * @return  HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_gpio_configure_hdmi(void);

/*
 * Configure pins for SD card
 *
 * Maps to: gpio_configure_for_sd()
 *
 * On BCM2710: GPIO 48-53 to ALT0 for SDHOST.
 *
 * @return  HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_gpio_configure_sdcard(void);

/*
 * Configure pins for PWM audio output
 *
 * Maps to: gpio_configure_for_audio()
 *
 * On BCM2710: GPIO 18-19 to ALT5 for PWM0/PWM1.
 *
 * @return  HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_gpio_configure_audio(void);

/*
 * Configure pins for UART
 *
 * @param uart_num  UART number (0 = PL011, 1 = mini UART on BCM)
 * @return          HAL_SUCCESS or error code
 */
hal_error_t hal_gpio_configure_uart(uint32_t uart_num);

/* =============================================================================
 * SAFE SHUTDOWN PIN (Board-specific)
 * =============================================================================
 * For boards like GPi Case 2W that have a safe shutdown button.
 */

/*
 * Configure safe shutdown monitoring
 *
 * On GPi Case 2W: GPIO 26 triggers safe shutdown when pressed.
 *
 * @return  HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_gpio_configure_safe_shutdown(void);

/*
 * Check if safe shutdown has been triggered
 *
 * @return  true if shutdown button pressed
 */
bool hal_gpio_safe_shutdown_triggered(void);

/* =============================================================================
 * PLATFORM-SPECIFIC NOTES
 * =============================================================================
 *
 * BCM2710/BCM2711 (Pi Zero 2W, Pi 3, Pi 4, CM4):
 *   - 54 GPIO pins (0-53)
 *   - Function values: 000=IN, 001=OUT, 100=ALT0, 101=ALT1, etc.
 *   - Pull config requires GPPUD + GPPUDCLK sequence
 *
 * bcm2712 (Pi 5, CM5):
 *   - GPIO is on RP1 southbridge chip!
 *   - Must initialize PCIe first
 *   - Different register layout than BCM2710
 *   - 28 GPIO pins directly accessible
 *
 * RK3528A (Rock 2A):
 *   - 5 GPIO banks (GPIO0-GPIO4), 32 pins each
 *   - Separate pinmux configuration
 *   - Different pull resistor mechanism
 *
 * S905X (Le Potato):
 *   - Multiple GPIO banks (GPIOX, GPIOY, GPIOAO, etc.)
 *   - Complex pinmux in different register space
 *
 * H618 (KICKPI K2B):
 *   - Allwinner GPIO ports (PA, PB, PC, ...)
 *   - 4-bit function select per pin
 *   - Separate pull/drive strength config
 *
 * K1 RISC-V (Orange Pi RV2):
 *   - SpacemiT GPIO controller
 *   - Different register layout entirely
 */

#endif /* HAL_GPIO_H */
