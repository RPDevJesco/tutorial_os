/*
 * soc/rk3528a/gpio.c - RK3528A GPIO Implementation
 *
 * Tutorial-OS: RK3528A HAL Implementation
 *
 * COMPLETELY DIFFERENT FROM BCM!
 *
 * Rockchip GPIO differences:
 *   - 5 GPIO banks (GPIO0-GPIO4), each with 32 pins
 *   - Pins named as GPIO<bank>_<letter><num> (e.g., GPIO1_A3)
 *   - Function selection via IOMUX registers in GRF (separate from GPIO)
 *   - Pull resistors configured in GRF, not GPIO controller
 *   - Write-enable mask in upper 16 bits of registers
 *
 * HAL pin numbering: linear 0-159 mapping
 *   Pin 0-31   = GPIO0_A0-D7
 *   Pin 32-63  = GPIO1_A0-D7
 *   Pin 64-95  = GPIO2_A0-D7
 *   Pin 96-127 = GPIO3_A0-D7
 *   Pin 128-159 = GPIO4_A0-D7
 */

#include "hal/hal_gpio.h"
#include "rk3528a_regs.h"

/* =============================================================================
 * INTERNAL HELPERS
 * =============================================================================
 */

/*
 * Convert HAL pin number to bank and pin within bank
 */
static void pin_to_bank(uint32_t pin, uint32_t *bank, uint32_t *offset)
{
    *bank = pin / RK_GPIO_PINS_PER_BANK;
    *offset = pin % RK_GPIO_PINS_PER_BANK;
}

/*
 * Get the direction register address for a pin
 * Low register (DDR_L) for pins 0-15, High register (DDR_H) for pins 16-31
 */
static uintptr_t get_ddr_reg(uint32_t bank, uint32_t offset)
{
    uintptr_t base = rk_gpio_bank_base(bank);
    if (offset < 16) {
        return base + RK_GPIO_SWPORT_DDR_L;
    } else {
        return base + RK_GPIO_SWPORT_DDR_H;
    }
}

/*
 * Get the data register address for a pin
 */
static uintptr_t get_dr_reg(uint32_t bank, uint32_t offset)
{
    uintptr_t base = rk_gpio_bank_base(bank);
    if (offset < 16) {
        return base + RK_GPIO_SWPORT_DR_L;
    } else {
        return base + RK_GPIO_SWPORT_DR_H;
    }
}

/*
 * Get bit position within 16-bit register
 */
static uint32_t get_bit_in_reg(uint32_t offset)
{
    return offset % 16;
}

/* =============================================================================
 * HAL GPIO IMPLEMENTATION
 * =============================================================================
 */

hal_error_t hal_gpio_init(void)
{
    /* No global init needed for Rockchip GPIO */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_function(uint32_t pin, hal_gpio_function_t function)
{
    if (pin > RK_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    uint32_t bank, offset;
    pin_to_bank(pin, &bank, &offset);

    /*
     * For simple INPUT/OUTPUT, we only need to set the direction.
     * For alternate functions (ALT0-ALT5), we would need to configure
     * IOMUX in GRF, which is pin-specific and complex.
     *
     * For now, support INPUT and OUTPUT only.
     * Alternate functions should use rk_iomux_set() directly.
     */

    if (function == HAL_GPIO_INPUT || function == HAL_GPIO_OUTPUT) {
        uintptr_t ddr_reg = get_ddr_reg(bank, offset);
        uint32_t bit = get_bit_in_reg(offset);

        /*
         * Rockchip GPIO registers use write-enable mask:
         * Upper 16 bits are write-enable, lower 16 bits are data.
         * To modify a bit: write_mask | new_value
         */
        uint32_t write_mask = (1 << (bit + 16));
        uint32_t value = (function == HAL_GPIO_OUTPUT) ? (1 << bit) : 0;

        hal_mmio_write32(ddr_reg, write_mask | value);
        return HAL_SUCCESS;
    }

    /* ALT functions not implemented in basic HAL */
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_get_function(uint32_t pin, hal_gpio_function_t *function)
{
    if (pin > RK_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }
    if (function == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    uint32_t bank, offset;
    pin_to_bank(pin, &bank, &offset);

    uintptr_t ddr_reg = get_ddr_reg(bank, offset);
    uint32_t bit = get_bit_in_reg(offset);

    uint32_t val = hal_mmio_read32(ddr_reg);
    *function = (val & (1 << bit)) ? HAL_GPIO_OUTPUT : HAL_GPIO_INPUT;

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_pull(uint32_t pin, hal_gpio_pull_t pull)
{
    if (pin > RK_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    /*
     * Pull configuration is in GRF registers, not GPIO controller.
     * The exact register depends on which pin.
     * This is complex and pin-specific - simplified implementation.
     */

    /* TODO: Implement GRF pull configuration */
    (void)pull;
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_high(uint32_t pin)
{
    if (pin > RK_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    uint32_t bank, offset;
    pin_to_bank(pin, &bank, &offset);

    uintptr_t dr_reg = get_dr_reg(bank, offset);
    uint32_t bit = get_bit_in_reg(offset);

    /* Write with mask */
    hal_mmio_write32(dr_reg, (1 << (bit + 16)) | (1 << bit));

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_set_low(uint32_t pin)
{
    if (pin > RK_GPIO_MAX_PIN) {
        return HAL_ERROR_GPIO_INVALID_PIN;
    }

    uint32_t bank, offset;
    pin_to_bank(pin, &bank, &offset);

    uintptr_t dr_reg = get_dr_reg(bank, offset);
    uint32_t bit = get_bit_in_reg(offset);

    /* Write with mask, value = 0 */
    hal_mmio_write32(dr_reg, (1 << (bit + 16)));

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_write(uint32_t pin, bool value)
{
    return value ? hal_gpio_set_high(pin) : hal_gpio_set_low(pin);
}

bool hal_gpio_read(uint32_t pin)
{
    if (pin > RK_GPIO_MAX_PIN) {
        return false;
    }

    uint32_t bank, offset;
    pin_to_bank(pin, &bank, &offset);

    uintptr_t base = rk_gpio_bank_base(bank);
    uint32_t val = hal_mmio_read32(base + RK_GPIO_EXT_PORT);

    return (val & (1 << offset)) != 0;
}

hal_error_t hal_gpio_toggle(uint32_t pin)
{
    return hal_gpio_write(pin, !hal_gpio_read(pin));
}

bool hal_gpio_is_valid(uint32_t pin)
{
    return pin <= RK_GPIO_MAX_PIN;
}

uint32_t hal_gpio_max_pin(void)
{
    return RK_GPIO_MAX_PIN;
}

/* =============================================================================
 * BULK OPERATIONS
 * =============================================================================
 */

hal_error_t hal_gpio_set_mask(uint32_t mask, uint32_t bank)
{
    if (bank >= RK_GPIO_BANKS) {
        return HAL_ERROR_INVALID_ARG;
    }

    uintptr_t base = rk_gpio_bank_base(bank);

    /* Lower 16 bits */
    uint32_t lo_mask = mask & 0xFFFF;
    if (lo_mask) {
        hal_mmio_write32(base + RK_GPIO_SWPORT_DR_L, (lo_mask << 16) | lo_mask);
    }

    /* Upper 16 bits */
    uint32_t hi_mask = (mask >> 16) & 0xFFFF;
    if (hi_mask) {
        hal_mmio_write32(base + RK_GPIO_SWPORT_DR_H, (hi_mask << 16) | hi_mask);
    }

    return HAL_SUCCESS;
}

hal_error_t hal_gpio_clear_mask(uint32_t mask, uint32_t bank)
{
    if (bank >= RK_GPIO_BANKS) {
        return HAL_ERROR_INVALID_ARG;
    }

    uintptr_t base = rk_gpio_bank_base(bank);

    /* Lower 16 bits - write enable but value = 0 */
    uint32_t lo_mask = mask & 0xFFFF;
    if (lo_mask) {
        hal_mmio_write32(base + RK_GPIO_SWPORT_DR_L, (lo_mask << 16));
    }

    /* Upper 16 bits */
    uint32_t hi_mask = (mask >> 16) & 0xFFFF;
    if (hi_mask) {
        hal_mmio_write32(base + RK_GPIO_SWPORT_DR_H, (hi_mask << 16));
    }

    return HAL_SUCCESS;
}

uint32_t hal_gpio_read_mask(uint32_t bank)
{
    if (bank >= RK_GPIO_BANKS) {
        return 0;
    }

    uintptr_t base = rk_gpio_bank_base(bank);
    return hal_mmio_read32(base + RK_GPIO_EXT_PORT);
}

/* =============================================================================
 * PERIPHERAL CONFIGURATION
 * =============================================================================
 * RK3528A doesn't support DPI in the same way as BCM.
 * These are stubs that return NOT_SUPPORTED for non-applicable functions.
 */

hal_error_t hal_gpio_configure_dpi(void)
{
    /* RK3528A doesn't have BCM-style DPI */
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_configure_hdmi(void)
{
    /*
     * HDMI on RK3528A is handled by VOP2 and HDMI TX.
     * GPIO configuration may be needed for HPD (hot plug detect).
     * TODO: Implement based on actual board layout.
     */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_sdcard(void)
{
    /*
     * SD card pins on RK3528A vary by board.
     * Rock 2A uses specific pins that need IOMUX configuration.
     * TODO: Implement board-specific SD pin setup.
     */
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_configure_audio(void)
{
    /*
     * Audio on RK3528A uses I2S, not PWM.
     * Different pin configuration needed.
     */
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_configure_uart(uint32_t uart_num)
{
    /*
     * UART pins need IOMUX configuration.
     * UART2 is commonly used for debug console.
     * TODO: Implement IOMUX setup for UART pins.
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
 * ROCKCHIP-SPECIFIC FUNCTIONS
 * =============================================================================
 * These are not part of the HAL but useful for board-specific code.
 */

/*
 * Set IOMUX function for a pin
 *
 * This is the Rockchip way to set alternate functions.
 * The GRF register and bit position depends on the specific pin.
 *
 * @param bank      GPIO bank (0-4)
 * @param offset    Pin within bank (0-31)
 * @param function  IOMUX function (0-15, pin-specific)
 * @return          HAL_SUCCESS or error
 */
hal_error_t rk_iomux_set(uint32_t bank, uint32_t offset, uint32_t function)
{
    /*
     * IOMUX configuration is complex and varies by pin.
     * Each pin has 4 bits for function selection in GRF registers.
     *
     * TODO: Implement full IOMUX configuration table.
     * For now, this is a placeholder.
     */
    (void)bank;
    (void)offset;
    (void)function;

    return HAL_ERROR_NOT_SUPPORTED;
}
