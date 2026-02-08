/*
 * gpio.h - GPIO Configuration for Raspberry Pi Zero 2W
 * =====================================================
 *
 * GPIO (General Purpose Input/Output) pins can be configured for:
 *   - Input (read button state, sensors)
 *   - Output (control LEDs, relays)
 *   - Alternate functions (UART, SPI, I2C, PWM, DPI display, etc.)
 *
 * For DPI display on the GPi Case 2W, we configure GPIO 0-27 to ALT2,
 * EXCEPT GPIO 18-19 which are reserved for PWM audio output.
 */

#ifndef GPIO_H
#define GPIO_H

#include "types.h"  /* Project's bare-metal type definitions */

/* =============================================================================
 * GPIO REGISTER ADDRESSES
 * =============================================================================
 */

#define PERIPHERAL_BASE     0x3F000000
#define GPIO_BASE           (PERIPHERAL_BASE + 0x00200000)

/* GPIO Function Select Registers (10 pins per register, 3 bits per pin) */
#define GPFSEL0             (GPIO_BASE + 0x00)   /* GPIO 0-9 */
#define GPFSEL1             (GPIO_BASE + 0x04)   /* GPIO 10-19 */
#define GPFSEL2             (GPIO_BASE + 0x08)   /* GPIO 20-29 */
#define GPFSEL3             (GPIO_BASE + 0x0C)   /* GPIO 30-39 */
#define GPFSEL4             (GPIO_BASE + 0x10)   /* GPIO 40-49 */
#define GPFSEL5             (GPIO_BASE + 0x14)   /* GPIO 50-53 */

/* GPIO Pin Output Set/Clear Registers */
#define GPSET0              (GPIO_BASE + 0x1C)   /* GPIO 0-31 */
#define GPSET1              (GPIO_BASE + 0x20)   /* GPIO 32-53 */
#define GPCLR0              (GPIO_BASE + 0x28)   /* GPIO 0-31 */
#define GPCLR1              (GPIO_BASE + 0x2C)   /* GPIO 32-53 */

/* GPIO Pin Level Register */
#define GPLEV0              (GPIO_BASE + 0x34)   /* GPIO 0-31 */
#define GPLEV1              (GPIO_BASE + 0x38)   /* GPIO 32-53 */

/* GPIO Pull-up/down Enable Register */
#define GPPUD               (GPIO_BASE + 0x94)
#define GPPUDCLK0           (GPIO_BASE + 0x98)   /* GPIO 0-31 */
#define GPPUDCLK1           (GPIO_BASE + 0x9C)   /* GPIO 32-53 */

/* =============================================================================
 * GPIO FUNCTION VALUES
 * =============================================================================
 */

typedef enum {
    GPIO_FUNC_INPUT  = 0,   /* 000 - Input */
    GPIO_FUNC_OUTPUT = 1,   /* 001 - Output */
    GPIO_FUNC_ALT0   = 4,   /* 100 - Alternate function 0 */
    GPIO_FUNC_ALT1   = 5,   /* 101 - Alternate function 1 */
    GPIO_FUNC_ALT2   = 6,   /* 110 - Alternate function 2 (DPI) */
    GPIO_FUNC_ALT3   = 7,   /* 111 - Alternate function 3 */
    GPIO_FUNC_ALT4   = 3,   /* 011 - Alternate function 4 */
    GPIO_FUNC_ALT5   = 2,   /* 010 - Alternate function 5 (UART, PWM) */
} gpio_function_t;

typedef enum {
    GPIO_PULL_OFF  = 0,     /* Disable pull-up/down */
    GPIO_PULL_DOWN = 1,     /* Enable pull-down */
    GPIO_PULL_UP   = 2,     /* Enable pull-up */
} gpio_pull_t;

/* =============================================================================
 * PUBLIC API
 * =============================================================================
 */

/*
 * gpio_set_function() - Set the function of a single GPIO pin
 *
 * @param pin       GPIO pin number (0-53)
 * @param function  Desired function (input, output, or alternate)
 */
void gpio_set_function(uint32_t pin, gpio_function_t function);

/*
 * gpio_set_pull() - Configure pull-up/down for a GPIO pin
 *
 * @param pin   GPIO pin number (0-53)
 * @param pull  Pull configuration (off, down, or up)
 */
void gpio_set_pull(uint32_t pin, gpio_pull_t pull);

/*
 * gpio_set_high() - Set a GPIO output pin HIGH
 */
void gpio_set_high(uint32_t pin);

/*
 * gpio_set_low() - Set a GPIO output pin LOW
 */
void gpio_set_low(uint32_t pin);

/*
 * gpio_read() - Read the current level of a GPIO pin
 *
 * Returns: true if HIGH, false if LOW
 */
bool gpio_read(uint32_t pin);

/*
 * gpio_configure_for_dpi() - Configure GPIO 0-27 for DPI display (24-bit RGB)
 *
 * IMPORTANT: GPIO 18 and 19 are EXCLUDED from DPI configuration
 * because they are used for PWM audio output on the GPi Case 2W.
 *
 * Sets up GPIO pins for the parallel RGB display interface.
 * Used by the GPi Case 2W's built-in LCD.
 *
 * Pin assignments (ALT2 function):
 *   GPIO 0-7:   Blue/Green data bits
 *   GPIO 8-15:  Green/Red data bits
 *   GPIO 16-17: HSYNC, VSYNC
 *   GPIO 18-19: RESERVED FOR PWM AUDIO (not configured here!)
 *   GPIO 20-27: Red channel / control signals
 */
void gpio_configure_for_dpi(void);

/*
 * gpio_configure_for_sd() - Configure GPIO 48-53 for SDHOST
 *
 * Sets up the GPIO pins needed for SD card access.
 */
void gpio_configure_for_sd(void);

/*
 * gpio_configure_for_audio() - Configure GPIO 18-19 for PWM audio
 *
 * Sets up the GPIO pins for stereo PWM audio output.
 * This function should be called AFTER gpio_configure_for_dpi()
 * to ensure audio pins are properly configured.
 *
 * Pin assignments (ALT5 function):
 *   GPIO 18: PWM0 -> Left channel
 *   GPIO 19: PWM1 -> Right channel
 */
void gpio_configure_for_audio(void);

#endif /* GPIO_H */