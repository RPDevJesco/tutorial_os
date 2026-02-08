/*
 * soc/s905x/gpio.c - Amlogic S905X GPIO Implementation
 *
 * Tutorial-OS: S905X HAL Implementation
 *
 * GPIO ON AMLOGIC IS VERY DIFFERENT FROM BOTH BCM AND ROCKCHIP:
 *
 * BCM2710/BCM2711:
 *   - Single GPIO controller with GPFSEL/GPSET/GPCLR registers
 *   - Pull resistors via GPPUD sequence (BCM2710) or direct (BCM2711)
 *   - All GPIOs in one address range
 *
 * RK3528A:
 *   - Multiple GPIO banks, each with its own register block
 *   - Pin mux via GRF (General Register Files) with write-enable masks
 *
 * S905X (Amlogic Meson GXL):
 *   - TWO separate GPIO domains: AO (Always-On) and EE (Everything Else)
 *   - AO GPIOs: GPIOAO_0-9, controlled via AOBUS registers
 *   - EE GPIOs: GPIOX, GPIODV, GPIOH, GPIOZ, CARD, BOOT via PERIPHS
 *   - Output Enable is ACTIVE LOW (0 = output, 1 = input) — watch out!
 *   - Pin mux is separate per domain (AO mux in AOBUS, EE mux in PERIPHS)
 *
 * The "active-low output enable" is a common source of bugs.
 * On BCM, GPFSEL=1 means output. On Amlogic, OEN=0 means output.
 */

#include "hal/hal_gpio.h"
#include "hal/hal_timer.h"
#include "hal/hal_types.h"
#include "s905x_regs.h"

/* =============================================================================
 * INTERNAL HELPERS
 * =============================================================================
 */

/*
 * Amlogic GPIO bank identifier
 *
 * Each bank has its own set of OEN/OUT/IN registers.
 * The La Potato's 40-pin header primarily uses GPIODV and GPIOX pins.
 */
typedef enum {
    AML_BANK_GPIOAO = 0,   /* Always-On domain (GPIOAO_0 - GPIOAO_9) */
    AML_BANK_GPIOX,        /* EE domain (GPIOX_0 - GPIOX_18) */
    AML_BANK_GPIODV,       /* EE domain (GPIODV_0 - GPIODV_29) */
    AML_BANK_GPIOH,        /* EE domain (GPIOH_0 - GPIOH_9) */
    AML_BANK_BOOT,         /* EE domain (BOOT_0 - BOOT_15) */
    AML_BANK_CARD,         /* EE domain (CARD_0 - CARD_6) */
    AML_BANK_GPIOZ,        /* EE domain (GPIOZ_0 - GPIOZ_15) */
    AML_BANK_COUNT
} aml_gpio_bank_t;

/* Register addresses for each bank */
typedef struct {
    uintptr_t oen_reg;     /* Output Enable Negative register */
    uintptr_t out_reg;     /* Output value register */
    uintptr_t in_reg;      /* Input value register */
    uint32_t  pin_count;   /* Number of pins in bank */
} aml_gpio_bank_info_t;

static const aml_gpio_bank_info_t g_bank_info[AML_BANK_COUNT] = {
    /* GPIOAO: AO domain, different register layout */
    [AML_BANK_GPIOAO] = {
        .oen_reg   = AML_AO_GPIO_O_EN_N,
        .out_reg   = AML_AO_GPIO_O_EN_N,   /* OEN and OUT share a register! */
        .in_reg    = AML_AO_GPIO_I,
        .pin_count = AML_AO_GPIO_COUNT
    },
    /* EE domain banks */
    [AML_BANK_GPIOX] = {
        .oen_reg   = AML_PREG_PAD_GPIO0_EN_N,
        .out_reg   = AML_PREG_PAD_GPIO0_O,
        .in_reg    = AML_PREG_PAD_GPIO0_I,
        .pin_count = 19
    },
    [AML_BANK_GPIODV] = {
        .oen_reg   = AML_PREG_PAD_GPIO1_EN_N,
        .out_reg   = AML_PREG_PAD_GPIO1_O,
        .in_reg    = AML_PREG_PAD_GPIO1_I,
        .pin_count = 30
    },
    [AML_BANK_GPIOH] = {
        .oen_reg   = AML_PREG_PAD_GPIO2_EN_N,
        .out_reg   = AML_PREG_PAD_GPIO2_O,
        .in_reg    = AML_PREG_PAD_GPIO2_I,
        .pin_count = 10
    },
    [AML_BANK_BOOT] = {
        .oen_reg   = AML_PREG_PAD_GPIO3_EN_N,
        .out_reg   = AML_PREG_PAD_GPIO3_O,
        .in_reg    = AML_PREG_PAD_GPIO3_I,
        .pin_count = 16
    },
    [AML_BANK_CARD] = {
        .oen_reg   = AML_PREG_PAD_GPIO4_EN_N,
        .out_reg   = AML_PREG_PAD_GPIO4_O,
        .in_reg    = AML_PREG_PAD_GPIO4_I,
        .pin_count = 7
    },
    [AML_BANK_GPIOZ] = {
        .oen_reg   = AML_PREG_PAD_GPIO5_EN_N,
        .out_reg   = AML_PREG_PAD_GPIO5_O,
        .in_reg    = AML_PREG_PAD_GPIO5_I,
        .pin_count = 16
    },
};

/* =============================================================================
 * INITIALIZATION
 * =============================================================================
 */

hal_error_t hal_gpio_init(void)
{
    /*
     * GPIO controller is always accessible on S905X.
     * No special initialization required beyond what firmware has done.
     */
    return HAL_SUCCESS;
}

/* =============================================================================
 * BASIC GPIO OPERATIONS
 * =============================================================================
 */

hal_error_t hal_gpio_set_function(uint32_t pin, hal_gpio_function_t function)
{
    /*
     * On Amlogic, "function" is controlled via pinmux registers,
     * not through the GPIO data registers like BCM's GPFSEL.
     *
     * Setting a pin to GPIO mode means clearing its pinmux bits to 0.
     * Setting to an alternate function means writing the function number.
     *
     * TODO: Implement full pinmux table mapping for La Potato header pins.
     * For now, this is a placeholder.
     */
    (void)pin;
    (void)function;
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_set_pull(uint32_t pin, hal_gpio_pull_t pull)
{
    /*
     * Amlogic has separate pull enable and pull direction registers
     * for each GPIO bank. The AO domain has its own pull registers.
     *
     * Unlike BCM2710's complex GPPUD timing sequence, Amlogic pulls
     * are set by direct register writes (more like BCM2711).
     *
     * TODO: Implement per-bank pull configuration.
     */
    (void)pin;
    (void)pull;
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_set_high(uint32_t pin)
{
    (void)pin;
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_set_low(uint32_t pin)
{
    (void)pin;
    return HAL_ERROR_NOT_SUPPORTED;
}

bool hal_gpio_read(uint32_t pin)
{
    (void)pin;
    return false;
}

/* =============================================================================
 * PERIPHERAL PIN CONFIGURATION
 * =============================================================================
 * These are stubs that return NOT_SUPPORTED for non-applicable functions.
 */

hal_error_t hal_gpio_configure_dpi(void)
{
    /* S905X doesn't have BCM-style DPI */
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_configure_hdmi(void)
{
    /*
     * HDMI on S905X is handled by the VPU and HDMI TX controller.
     * The HDMI pins are dedicated (not muxed with GPIO).
     * No GPIO configuration needed.
     */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_sdcard(void)
{
    /*
     * SD card on La Potato uses the CARD bank pins.
     * These need pinmux configuration via PERIPHS registers.
     * TODO: Implement CARD pin mux setup.
     */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_audio(void)
{
    /*
     * Audio on S905X uses I2S (not PWM like BCM).
     * Different pin configuration needed.
     */
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_configure_uart(uint32_t uart_num)
{
    /*
     * UART_AO_A is the debug console, typically pre-configured by U-Boot.
     * UART_AO_A TX = GPIOAO_0, RX = GPIOAO_1
     *
     * Other UARTs need pinmux configuration.
     * TODO: Implement pinmux setup for UART pins.
     */
    if (uart_num > 2) {
        return HAL_ERROR_INVALID_ARG;
    }
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_safe_shutdown(void)
{
    /* Board-specific - depends on hardware design */
    return HAL_ERROR_NOT_SUPPORTED;
}

bool hal_gpio_safe_shutdown_triggered(void)
{
    return false;
}

/* =============================================================================
 * AMLOGIC-SPECIFIC FUNCTIONS
 * =============================================================================
 * These are not part of the HAL but useful for board-specific code.
 */

/*
 * Set pinmux function for an EE-domain pin
 *
 * @param mux_reg   Pinmux register address (AML_PERIPHS_PIN_MUX_*)
 * @param bit       Bit position within the register
 * @param enable    true to set the mux bit, false to clear it
 * @return          HAL_SUCCESS or error
 */
hal_error_t aml_pinmux_set(uintptr_t mux_reg, uint32_t bit, bool enable)
{
    /*
     * Amlogic pinmux is simpler than Rockchip's GRF:
     * Each bit in a pinmux register enables one alternate function.
     * Setting the bit to 1 enables the function, 0 reverts to GPIO.
     *
     * Unlike Rockchip, there's no write-enable mask in the upper 16 bits.
     * It's a straightforward read-modify-write.
     *
     * TODO: Implement with proper MMIO helpers.
     */
    (void)mux_reg;
    (void)bit;
    (void)enable;

    return HAL_ERROR_NOT_SUPPORTED;
}
