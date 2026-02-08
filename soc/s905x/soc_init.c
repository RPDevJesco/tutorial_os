/*
 * soc/s905x/soc_init.c - Amlogic S905X Platform Initialization
 *
 * Tutorial-OS: S905X HAL Implementation
 *
 * Implements hal_platform.h for Amlogic S905X (Meson GXL).
 * This is the entry point for all S905X-specific initialization.
 *
 * COMPARISON WITH OTHER PLATFORMS:
 *
 *   BCM2710/BCM2711:
 *     - Platform info via VideoCore mailbox (board revision, serial, etc.)
 *     - Memory info via mailbox (ARM/GPU split)
 *     - Temperature via mailbox
 *     - Clock rates via mailbox
 *
 *   RK3528A:
 *     - Fixed/placeholder values (no mailbox available)
 *     - Temperature via TSADC registers
 *     - Clocks via CRU registers
 *
 *   S905X:
 *     - Fixed/placeholder values (no mailbox, similar to Rockchip)
 *     - Temperature via SAR ADC (different mechanism than both)
 *     - Clocks via HIU/HHI registers
 *     - Board info could come from DTB or efuse
 *
 * The pattern is the same as Rockchip: provide fixed values where
 * we can't easily query hardware, with TODOs for future improvements.
 */

#include "hal/hal_platform.h"
#include "hal/hal_timer.h"
#include "hal/hal_gpio.h"
#include "s905x_regs.h"

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static bool g_platform_initialized = false;

static const char *g_board_name = "Libre Computer La Potato";
static const char *g_soc_name = "S905X";

/* =============================================================================
 * INITIALIZATION
 * =============================================================================
 */

hal_error_t hal_platform_early_init(void)
{
    return HAL_SUCCESS;
}

hal_error_t hal_platform_init(void)
{
    hal_error_t err;

    if (g_platform_initialized) {
        return HAL_ERROR_ALREADY_INIT;
    }

    /* Initialize timer (ARM Generic Timer, same approach as Rockchip) */
    err = hal_timer_init();
    if (HAL_FAILED(err)) {
        return err;
    }

    /* Initialize GPIO */
    err = hal_gpio_init();
    if (HAL_FAILED(err)) {
        return err;
    }

    g_platform_initialized = true;
    return HAL_SUCCESS;
}

bool hal_platform_is_initialized(void)
{
    return g_platform_initialized;
}

/* =============================================================================
 * PLATFORM INFORMATION
 * =============================================================================
 */

hal_error_t hal_platform_get_info(hal_platform_info_t *info)
{
    if (info == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    info->platform_id = HAL_PLATFORM_LIBRE_POTATO;
    info->arch = HAL_ARCH_ARM64;
    info->board_name = g_board_name;
    info->soc_name = g_soc_name;
    info->board_revision = 0x20170001;  /* Placeholder */
    info->serial_number = 0;            /* Not available without efuse read */

    return HAL_SUCCESS;
}

hal_platform_id_t hal_platform_get_id(void)
{
    return HAL_PLATFORM_LIBRE_POTATO;
}

const char *hal_platform_get_board_name(void)
{
    return g_board_name;
}

const char *hal_platform_get_soc_name(void)
{
    return g_soc_name;
}

/* =============================================================================
 * MEMORY INFORMATION
 * =============================================================================
 */

hal_error_t hal_platform_get_memory_info(hal_memory_info_t *info)
{
    if (info == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    /*
     * Memory info would come from device tree or be detected at boot.
     * La Potato comes in 1GB and 2GB variants.
     *
     * KEY DIFFERENCE: Unlike BCM where GPU claims part of the RAM,
     * the S905X Mali GPU uses a carved-out region from main DRAM.
     * There's no separate "GPU memory" in the BCM sense.
     *
     * We report the full DRAM as ARM-accessible since we're bare metal
     * and the GPU isn't running.
     */

    /* Use values detected at boot (from boot_soc.S) */
    extern uint64_t detected_ram_base;
    extern uint64_t detected_ram_size;

    info->arm_base = (uintptr_t)detected_ram_base;
    info->arm_size = (size_t)detected_ram_size;
    info->gpu_base = 0;         /* No separate GPU memory on Amlogic */
    info->gpu_size = 0;
    info->peripheral_base = AML_CBUS_BASE;  /* Primary peripheral base */

    return HAL_SUCCESS;
}

size_t hal_platform_get_arm_memory(void)
{
    extern uint64_t detected_ram_size;
    return (size_t)detected_ram_size;
}

size_t hal_platform_get_total_memory(void)
{
    /* On S905X, all RAM is ARM-accessible (no GPU split like BCM) */
    return hal_platform_get_arm_memory();
}

/* =============================================================================
 * CLOCK INFORMATION
 * =============================================================================
 */

uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id)
{
    /*
     * S905X clock rates from Amlogic documentation.
     * The S905X has a complex PLL-based clock tree, but for now
     * we return fixed typical values.
     * TODO: Read actual rates from HHI clock registers.
     */
    switch (clock_id) {
        case HAL_CLOCK_ARM:     return 1512000000;  /* 1.512 GHz max */
        case HAL_CLOCK_CORE:    return 500000000;   /* 500 MHz */
        case HAL_CLOCK_UART:    return 24000000;    /* 24 MHz XTAL */
        case HAL_CLOCK_EMMC:    return 200000000;   /* 200 MHz */
        default:                return 0;
    }
}

/* =============================================================================
 * TEMPERATURE
 * =============================================================================
 */

hal_error_t hal_platform_get_temperature(int32_t *temp_mc)
{
    if (temp_mc == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    /*
     * S905X thermal sensor is in the SAR ADC block.
     * TODO: Read from TS_CFG_REG and convert ADC code to temperature.
     * For now, return a placeholder value.
     */
    *temp_mc = 45000;  /* 45°C placeholder */
    return HAL_SUCCESS;
}

hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc)
{
    if (temp_mc == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    /* S905X max recommended operating temperature */
    *temp_mc = 85000;  /* 85°C */
    return HAL_SUCCESS;
}

/* =============================================================================
 * THROTTLE STATUS
 * =============================================================================
 */

hal_error_t hal_platform_get_throttle_status(uint32_t *status)
{
    if (status == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    /*
     * Amlogic doesn't have a single throttle register like BCM.
     * TODO: Check thermal trip points and DVFS state.
     */
    *status = 0;
    return HAL_SUCCESS;
}

/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 */

hal_error_t hal_platform_get_power(hal_device_id_t device, bool *on)
{
    if (on == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    /*
     * Amlogic uses clock gating for device power control.
     * TODO: Check HHI clock gate registers for actual status.
     */
    (void)device;
    *on = true;  /* Assume always on */
    return HAL_SUCCESS;
}