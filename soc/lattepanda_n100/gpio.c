/*
 * soc/lattepanda_n100/gpio.c — GPIO Stub Implementation
 *
 * Tutorial-OS: LattePanda N100 (x86_64 / UEFI) GPIO Implementation
 *
 * WHY THIS IS A STUB:
 *
 * The LattePanda MU exposes GPIO through the Intel N100's GPIO controller,
 * which is accessed via PCIe-attached I/O (ACPI namespace, not simple MMIO).
 * The GPIO controller base addresses are defined in the ACPI DSDT table and
 * vary by firmware version — there is no fixed physical address to hardcode
 * the way BCM2710/JH7110 have fixed peripheral bases.
 *
 * Properly implementing x86_64 GPIO would require:
 *   1. Parsing the ACPI DSDT to find the GPIO controller BAR
 *   2. Mapping the BAR into virtual address space (post-MMU setup)
 *   3. Implementing the Intel GPIO Controller programming model
 *      (pad config registers, community/group structure)
 *
 * For Tutorial-OS, this is intentionally out of scope for the initial
 * x86_64 bring-up chapter. The display and UART paths
 * do not require GPIO. GPIO on x86_64 is typically
 * handled by ACPI AML bytecode in production systems, not direct register
 * access as on embedded ARM64/RISC-V boards.
 *
 * This stub satisfies the hal_gpio.h contract so the build links cleanly.
 * Every function returns HAL_ERROR_NOT_SUPPORTED, which kernel/main.c
 * checks before attempting GPIO operations via HAL_OK().
 *
 * A future chapter on x86_64 GPIO could replace this file with a real
 * implementation after introducing ACPI table parsing.
 *
 * CONTRAST WITH OTHER PLATFORMS:
 *   BCM2710/2711: Fixed MMIO at 0x3F200000 / 0xFE200000 — trivial
 *   JH7110:       Fixed MMIO at 0x13040000 — trivial
 *   KyX1:         Fixed MMIO from DTS — trivial
 *   N100:         ACPI DSDT lookup required — non-trivial
 */

#include "hal/hal_types.h"
#include "hal/hal_gpio.h"

/* ============================================================
 * Initialization
 * ============================================================ */

hal_error_t hal_gpio_init(void)
{
    /*
     * No fixed MMIO base to set up. ACPI parsing would go here.
     * Return success so hal_platform_init() doesn't abort the boot.
     */
    return HAL_SUCCESS;
}

/* ============================================================
 * Pin operations — all stubs
 *
 * Each returns HAL_ERROR_NOT_SUPPORTED.
 * Callers using HAL_OK() will treat these as no-ops — correct behavior
 * for Tutorial-OS on x86_64 where GPIO is not the focus.
 * ============================================================ */

hal_error_t hal_gpio_set_function(uint32_t pin, hal_gpio_function_t function)
{
    (void)pin;
    (void)function;
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_get_function(uint32_t pin, hal_gpio_function_t *function)
{
    (void)pin;
    if (function) *function = HAL_GPIO_INPUT;
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_set_pull(uint32_t pin, hal_gpio_pull_t pull)
{
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

hal_error_t hal_gpio_write(uint32_t pin, bool value)
{
    (void)pin;
    (void)value;
    return HAL_ERROR_NOT_SUPPORTED;
}

bool hal_gpio_read(uint32_t pin)
{
    (void)pin;
    return false;
}

hal_error_t hal_gpio_toggle(uint32_t pin)
{
    (void)pin;
    return HAL_ERROR_NOT_SUPPORTED;
}

/* ============================================================
 * Pin validation
 * ============================================================ */

bool hal_gpio_is_valid(uint32_t pin)
{
    /*
     * The Intel N100 has GPIO pins, but without ACPI we can't validate
     * pin numbers meaningfully. Return false — callers will skip GPIO ops.
     */
    (void)pin;
    return false;
}

uint32_t hal_gpio_max_pin(void)
{
    /* Report 0: no pins addressable without ACPI */
    return 0;
}

/* ============================================================
 * Bulk operations
 * ============================================================ */

hal_error_t hal_gpio_set_mask(uint32_t mask, uint32_t bank)
{
    (void)mask;
    (void)bank;
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_gpio_clear_mask(uint32_t mask, uint32_t bank)
{
    (void)mask;
    (void)bank;
    return HAL_ERROR_NOT_SUPPORTED;
}

uint32_t hal_gpio_read_mask(uint32_t bank)
{
    (void)bank;
    return 0;
}
