/*
 * gpio.c - GPIO Configuration Implementation
 * ==========================================
 *
 * Implements GPIO pin configuration for the BCM2710 (Pi Zero 2W).
 * Matches the Rust gpio.rs implementation.
 */

#include "gpio.h"
#include "mmio.h"

/* =============================================================================
 * BASIC GPIO OPERATIONS
 * =============================================================================
 */

void gpio_set_function(uint32_t pin, gpio_function_t function)
{
    if (pin > 53) return;

    /*
     * Each GPFSEL register handles 10 pins, with 3 bits per pin.
     * Register offset = (pin / 10) * 4
     * Bit position = (pin % 10) * 3
     */
    uintptr_t reg_addr = GPIO_BASE + (pin / 10) * 4;
    uint32_t shift = (pin % 10) * 3;
    uint32_t mask = 0x7 << shift;

    uint32_t val = mmio_read(reg_addr);
    val = (val & ~mask) | ((uint32_t)function << shift);
    mmio_write(reg_addr, val);
}

void gpio_set_pull(uint32_t pin, gpio_pull_t pull)
{
    if (pin > 53) return;

    /*
     * Pull resistor configuration requires a specific sequence:
     * 1. Write pull type to GPPUD
     * 2. Wait 150 cycles
     * 3. Write clock to GPPUDCLK0/1 for the pin
     * 4. Wait 150 cycles
     * 5. Clear GPPUD
     * 6. Clear GPPUDCLK0/1
     */
    mmio_write(GPPUD, pull);
    delay_us(150);

    if (pin < 32) {
        mmio_write(GPPUDCLK0, 1 << pin);
    } else {
        mmio_write(GPPUDCLK1, 1 << (pin - 32));
    }
    delay_us(150);

    mmio_write(GPPUD, 0);
    if (pin < 32) {
        mmio_write(GPPUDCLK0, 0);
    } else {
        mmio_write(GPPUDCLK1, 0);
    }
}

void gpio_set_high(uint32_t pin)
{
    if (pin > 53) return;

    if (pin < 32) {
        mmio_write(GPSET0, 1 << pin);
    } else {
        mmio_write(GPSET1, 1 << (pin - 32));
    }
}

void gpio_set_low(uint32_t pin)
{
    if (pin > 53) return;

    if (pin < 32) {
        mmio_write(GPCLR0, 1 << pin);
    } else {
        mmio_write(GPCLR1, 1 << (pin - 32));
    }
}

bool gpio_read(uint32_t pin)
{
    if (pin > 53) return false;

    uint32_t val;
    if (pin < 32) {
        val = mmio_read(GPLEV0);
        return (val & (1 << pin)) != 0;
    } else {
        val = mmio_read(GPLEV1);
        return (val & (1 << (pin - 32))) != 0;
    }
}

/* =============================================================================
 * DPI DISPLAY CONFIGURATION
 * =============================================================================
 */

void gpio_configure_for_dpi(void)
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

    const uint32_t ALT2 = GPIO_FUNC_ALT2;

    /*
     * GPFSEL0: GPIO 0-9 - All ALT2 for DPI
     * Bits: GPIO0=0-2, GPIO1=3-5, GPIO2=6-8, GPIO3=9-11,
     *       GPIO4=12-14, GPIO5=15-17, GPIO6=18-20, GPIO7=21-23,
     *       GPIO8=24-26, GPIO9=27-29
     */
    uint32_t gpfsel0_val = (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9)
                         | (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21)
                         | (ALT2 << 24) | (ALT2 << 27);

    /*
     * GPFSEL1: GPIO 10-19 - ALT2 for DPI, EXCEPT GPIO 18/19 (left as 0 = input)
     * Bits: GPIO10=0-2, GPIO11=3-5, GPIO12=6-8, GPIO13=9-11,
     *       GPIO14=12-14, GPIO15=15-17, GPIO16=18-20, GPIO17=21-23,
     *       GPIO18=24-26 (SKIP!), GPIO19=27-29 (SKIP!)
     */
    uint32_t gpfsel1_val = (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9)
                         | (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21);
    /* NOTE: No (ALT2 << 24) or (ALT2 << 27) - GPIO 18/19 reserved for audio! */

    /*
     * GPFSEL2: GPIO 20-27 - All ALT2 for DPI
     * Bits: GPIO20=0-2, GPIO21=3-5, GPIO22=6-8, GPIO23=9-11,
     *       GPIO24=12-14, GPIO25=15-17, GPIO26=18-20, GPIO27=21-23
     */
    uint32_t gpfsel2_val = (ALT2 << 0)  | (ALT2 << 3)  | (ALT2 << 6)  | (ALT2 << 9)
                         | (ALT2 << 12) | (ALT2 << 15) | (ALT2 << 18) | (ALT2 << 21);

    mmio_write(GPFSEL0, gpfsel0_val);
    mmio_write(GPFSEL1, gpfsel1_val);
    mmio_write(GPFSEL2, gpfsel2_val);

    /* Memory barrier to ensure GPIO configuration is complete */
    dmb();

    /*
     * Disable pull-up/down on DPI pins (GPIO 0-17 and 20-27)
     * GPIO 18/19 are handled by the audio driver.
     *
     * Bitmask: bits 0-17 and 20-27 set, bits 18-19 clear
     * 0x0FFFFFFF = all 28 bits
     * Clear bits 18 and 19: 0x0FFFFFFF & ~(1<<18) & ~(1<<19) = 0x0FF3FFFF
     */
    mmio_write(GPPUD, GPIO_PULL_OFF);
    delay_us(150);
    mmio_write(GPPUDCLK0, 0x0FF3FFFF);
    delay_us(150);
    mmio_write(GPPUD, 0);
    mmio_write(GPPUDCLK0, 0);
}

/* =============================================================================
 * SD CARD GPIO CONFIGURATION
 * =============================================================================
 */

void gpio_configure_for_sd(void)
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

    const uint32_t ALT0 = GPIO_FUNC_ALT0;

    /* GPIO 48-49 are in GPFSEL4 (bits 24-29) */
    uint32_t gpfsel4 = mmio_read(GPFSEL4);
    gpfsel4 = (gpfsel4 & 0xC0FFFFFF) | (ALT0 << 24) | (ALT0 << 27);
    mmio_write(GPFSEL4, gpfsel4);

    /* GPIO 50-53 are in GPFSEL5 (bits 0-11) */
    uint32_t gpfsel5 = mmio_read(GPFSEL5);
    gpfsel5 = (gpfsel5 & 0xFFFFF000)
            | (ALT0 << 0)    /* GPIO 50 */
            | (ALT0 << 3)    /* GPIO 51 */
            | (ALT0 << 6)    /* GPIO 52 */
            | (ALT0 << 9);   /* GPIO 53 */
    mmio_write(GPFSEL5, gpfsel5);

    /*
     * Enable pull-ups on data and command lines (GPIO 49-53)
     * SD card data lines are active-low, so pull-ups ensure
     * reliable communication when the card isn't driving.
     * CLK (GPIO 48) doesn't need a pull.
     *
     * GPIO 49-53 are bits 17-21 in GPPUDCLK1 (since they're >= 32).
     */
    mmio_write(GPPUD, GPIO_PULL_UP);
    delay_us(150);
    mmio_write(GPPUDCLK1, (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21));
    delay_us(150);
    mmio_write(GPPUD, 0);
    mmio_write(GPPUDCLK1, 0);
}

/* =============================================================================
 * AUDIO GPIO CONFIGURATION
 * =============================================================================
 */

void gpio_configure_for_audio(void)
{
    /*
     * Configure GPIO 18-19 for PWM audio (ALT5 function)
     *
     * Pin mapping:
     *   GPIO 18: PWM0 -> Left channel
     *   GPIO 19: PWM1 -> Right channel
     *
     * This function should be called AFTER gpio_configure_for_dpi()
     * since DPI intentionally leaves GPIO 18-19 unconfigured.
     */

    const uint32_t ALT5 = GPIO_FUNC_ALT5;

    /*
     * GPIO 18-19 are in GPFSEL1 (bits 24-29)
     * Read-modify-write to preserve other pin configurations
     */
    uint32_t gpfsel1 = mmio_read(GPFSEL1);
    gpfsel1 &= ~((7 << 24) | (7 << 27));  /* Clear bits for GPIO 18-19 */
    gpfsel1 |= (ALT5 << 24) | (ALT5 << 27);
    mmio_write(GPFSEL1, gpfsel1);

    /* No pull resistors needed for audio output */
    mmio_write(GPPUD, GPIO_PULL_OFF);
    delay_us(150);
    mmio_write(GPPUDCLK0, (1 << 18) | (1 << 19));
    delay_us(150);
    mmio_write(GPPUD, 0);
    mmio_write(GPPUDCLK0, 0);
}