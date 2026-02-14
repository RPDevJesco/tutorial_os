/*
 * gpio.c — GPIO Driver for the Ky X1 SoC
 * ==========================================
 *
 * Provides GPIO pin control for the Ky X1's MMP-style GPIO controller.
 * This is the equivalent of drivers/gpio/gpio.c for the BCM2710.
 *
 * Ky X1 GPIO controller vs BCM2710:
 * ==================================
 *
 * BCM2710 (Pi Zero 2W):
 *   - Single controller, 54 pins
 *   - GPFSEL registers: 3 bits per pin in shared registers
 *   - GPSET/GPCLR: Write-only set/clear registers
 *   - GPLEV: Read current level
 *   - Pull resistors: Weird clock-and-wait sequence (GPPUD + GPPUDCLK)
 *   - Function select: ALT0-ALT5 modes per pin (mux is in GPIO controller)
 *
 * Ky X1:
 *   - 4 banks of 32 pins each (~128 total)
 *   - Separate SET/CLEAR direction registers (no read-modify-write needed!)
 *   - Separate SET/CLEAR output registers
 *   - Pin muxing is EXTERNAL — handled by pinctrl at 0xD401E000
 *   - Edge detection built into the GPIO controller
 *
 * The biggest API difference: On BCM, gpio_set_function() handles both GPIO
 * direction AND alternate function selection (DPI, UART, PWM). On Ky X1,
 * gpio_set_function() only sets INPUT/OUTPUT direction — alternate functions
 * are handled by the separate pinctrl subsystem.
 *
 * For Tutorial-OS, this matters most for the heartbeat LED (GPIO 96).
 * Display and UART pins are already muxed correctly by U-Boot.
 */

#include "kyx1_regs.h"
#include "types.h"

/* =============================================================================
 * MMIO HELPERS
 * =============================================================================
 */

static inline void gpio_write32(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t gpio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

/* =============================================================================
 * BANK ADDRESS CALCULATION
 * =============================================================================
 *
 * The Ky X1 GPIO controller has an unusual bank layout:
 *   Bank 0: base + 0x000
 *   Bank 1: base + 0x004
 *   Bank 2: base + 0x008
 *   Bank 3: base + 0x100  (NOT 0x00C — there's a gap!)
 *
 * This is from the DTS: reg = <0x0 0xd4019000 0x0 0x4>,
 *                              <0x0 0xd4019004 0x0 0x4>,
 *                              <0x0 0xd4019008 0x0 0x4>,
 *                              <0x0 0xd4019100 0x0 0x4>;
 *
 * Each bank controls 32 pins. Pin N is in bank (N / 32), bit (N % 32).
 */

static uintptr_t gpio_bank_base(uint32_t pin)
{
    uint32_t bank = pin / KYX1_GPIO_PINS_PER_BANK;

    switch (bank) {
    case 0: return KYX1_GPIO_BASE + KYX1_GPIO_BANK0_OFF;
    case 1: return KYX1_GPIO_BASE + KYX1_GPIO_BANK1_OFF;
    case 2: return KYX1_GPIO_BASE + KYX1_GPIO_BANK2_OFF;
    case 3: return KYX1_GPIO_BASE + KYX1_GPIO_BANK3_OFF;
    default: return 0; /* Invalid bank */
    }
}

static uint32_t gpio_pin_bit(uint32_t pin)
{
    return 1U << (pin % KYX1_GPIO_PINS_PER_BANK);
}

/* =============================================================================
 * PUBLIC API
 * =============================================================================
 * These mirror the BCM gpio.h function signatures as closely as possible
 * so the rest of Tutorial-OS can use the same patterns.
 */

/*
 * kyx1_gpio_set_output — Configure a pin as output
 *
 * Uses the Set Direction Register (SDR) — write a 1 to make the pin output.
 * This is cleaner than BCM's read-modify-write GPFSEL approach because
 * SDR is a write-only "set bit" register — no risk of race conditions.
 *
 * BCM equivalent: gpio_set_function(pin, GPIO_FUNC_OUTPUT)
 */
void kyx1_gpio_set_output(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return;

    gpio_write32(base + GPIO_SDR, gpio_pin_bit(pin));
}

/*
 * kyx1_gpio_set_input — Configure a pin as input
 *
 * Uses the Clear Direction Register (CDR) — write a 1 to make the pin input.
 *
 * BCM equivalent: gpio_set_function(pin, GPIO_FUNC_INPUT)
 */
void kyx1_gpio_set_input(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return;

    gpio_write32(base + GPIO_CDR, gpio_pin_bit(pin));
}

/*
 * kyx1_gpio_set_high — Drive an output pin HIGH
 *
 * Uses the Pin Set Register (PSR) — atomic, no read-modify-write needed.
 * Identical concept to BCM's GPSET0/GPSET1.
 */
void kyx1_gpio_set_high(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return;

    gpio_write32(base + GPIO_PSR, gpio_pin_bit(pin));
}

/*
 * kyx1_gpio_set_low — Drive an output pin LOW
 *
 * Uses the Pin Clear Register (PCR) — atomic.
 * Identical concept to BCM's GPCLR0/GPCLR1.
 */
void kyx1_gpio_set_low(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return;

    gpio_write32(base + GPIO_PCR, gpio_pin_bit(pin));
}

/*
 * kyx1_gpio_read — Read the current level of a pin
 *
 * Returns true if HIGH, false if LOW.
 * Uses the Pin Level Register (PLR) — same concept as BCM's GPLEV0/GPLEV1.
 */
bool kyx1_gpio_read(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return false;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return false;

    return (gpio_read32(base + GPIO_PLR) & gpio_pin_bit(pin)) != 0;
}

/*
 * kyx1_gpio_toggle — Toggle an output pin
 *
 * Reads current level, then sets the opposite.
 * BCM doesn't have a dedicated toggle — same read-then-set approach.
 */
void kyx1_gpio_toggle(uint32_t pin)
{
    if (kyx1_gpio_read(pin))
        kyx1_gpio_set_low(pin);
    else
        kyx1_gpio_set_high(pin);
}

/* =============================================================================
 * EDGE DETECTION
 * =============================================================================
 * The Ky X1 GPIO controller has built-in edge detection — something the
 * BCM2710 also has but we didn't use in the original Tutorial-OS.
 * Useful for button input without polling.
 */

/*
 * kyx1_gpio_enable_rising_edge — Enable rising edge detection on a pin
 */
void kyx1_gpio_enable_rising_edge(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return;

    uint32_t val = gpio_read32(base + GPIO_RER);
    gpio_write32(base + GPIO_RER, val | gpio_pin_bit(pin));
}

/*
 * kyx1_gpio_enable_falling_edge — Enable falling edge detection on a pin
 */
void kyx1_gpio_enable_falling_edge(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return;

    uint32_t val = gpio_read32(base + GPIO_FER);
    gpio_write32(base + GPIO_FER, val | gpio_pin_bit(pin));
}

/*
 * kyx1_gpio_clear_edge — Clear edge detection status for a pin
 *
 * Write 1 to clear (W1C) — same pattern as many interrupt status registers.
 */
void kyx1_gpio_clear_edge(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return;

    gpio_write32(base + GPIO_EDR, gpio_pin_bit(pin));
}

/*
 * kyx1_gpio_edge_detected — Check if an edge was detected on a pin
 */
bool kyx1_gpio_edge_detected(uint32_t pin)
{
    if (pin >= KYX1_GPIO_MAX_PINS) return false;

    uintptr_t base = gpio_bank_base(pin);
    if (!base) return false;

    return (gpio_read32(base + GPIO_EDR) & gpio_pin_bit(pin)) != 0;
}

/* =============================================================================
 * BOARD-SPECIFIC HELPERS
 * =============================================================================
 */

/*
 * kyx1_gpio_init_heartbeat_led — Configure the board heartbeat LED
 *
 * On the Orange Pi RV2, the heartbeat LED is on GPIO 96 (active low).
 * This is from the DTS: led-1 { gpios = <&gpio 96 GPIO_ACTIVE_LOW>; }
 *
 * BCM equivalent: None — the Pi Zero 2W doesn't expose a user LED
 * through GPIO in the same way (it uses the activity LED on GPIO 47).
 */
void kyx1_gpio_init_heartbeat_led(void)
{
    kyx1_gpio_set_output(KYX1_LED_GPIO);
    kyx1_gpio_set_high(KYX1_LED_GPIO); /* Active low: HIGH = LED off */
}

/*
 * kyx1_gpio_set_led — Control the heartbeat LED
 *
 * @param on  true = LED on (drive LOW, since active low)
 */
void kyx1_gpio_set_led(bool on)
{
    if (on)
        kyx1_gpio_set_low(KYX1_LED_GPIO);  /* Active low: LOW = on */
    else
        kyx1_gpio_set_high(KYX1_LED_GPIO); /* Active low: HIGH = off */
}
