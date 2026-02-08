/*
 * soc/bcm2711/soc_init.c - BCM2711 Platform Initialization
 *
 * Tutorial-OS: BCM2711 HAL Implementation
 *
 * Similar to BCM2710 but with Pi 4/CM4 specific board detection.
 */

#include "hal/hal_platform.h"
#include "hal/hal_timer.h"
#include "hal/hal_gpio.h"
#include "bcm2711_regs.h"
#include "bcm2711_mailbox.h"

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static bool g_platform_initialized = false;
static const char *g_board_name = "Raspberry Pi (BCM2711)";
static const char *g_soc_name = "BCM2711";
static uint32_t g_board_revision = 0;
static uint64_t g_board_serial = 0;

/* =============================================================================
 * BOARD IDENTIFICATION
 * =============================================================================
 */

static const char *decode_board_name(uint32_t revision)
{
    if (revision & (1 << 23)) {
        uint32_t type = (revision >> 4) & 0xFF;
        switch (type) {
            case 0x11: return "Raspberry Pi 4B";
            case 0x13: return "Raspberry Pi 400";
            case 0x14: return "Raspberry Pi CM4";
            case 0x15: return "Raspberry Pi CM4S";
            default:   return "Raspberry Pi (BCM2711)";
        }
    }
    return "Raspberry Pi";
}

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

    err = hal_timer_init();
    if (HAL_FAILED(err)) return err;

    err = hal_gpio_init();
    if (HAL_FAILED(err)) return err;

    bcm_mailbox_get_board_revision(&g_board_revision);
    bcm_mailbox_get_board_serial(&g_board_serial);
    g_board_name = decode_board_name(g_board_revision);

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
    if (info == NULL) return HAL_ERROR_NULL_PTR;

    info->platform_id = HAL_PLATFORM_RPI_CM4;
    info->arch = HAL_ARCH_ARM64;
    info->board_name = g_board_name;
    info->soc_name = g_soc_name;
    info->board_revision = g_board_revision;
    info->serial_number = g_board_serial;

    return HAL_SUCCESS;
}

hal_platform_id_t hal_platform_get_id(void)
{
    return HAL_PLATFORM_RPI_CM4;
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
    if (info == NULL) return HAL_ERROR_NULL_PTR;

    uint32_t arm_base, arm_size, vc_base, vc_size;

    if (!bcm_mailbox_get_arm_memory(&arm_base, &arm_size)) {
        return HAL_ERROR_HARDWARE;
    }

    bcm_mailbox_get_vc_memory(&vc_base, &vc_size);

    info->arm_base = arm_base;
    info->arm_size = arm_size;
    info->gpu_base = vc_base;
    info->gpu_size = vc_size;
    info->peripheral_base = BCM_PERIPHERAL_BASE;

    return HAL_SUCCESS;
}

size_t hal_platform_get_arm_memory(void)
{
    uint32_t base, size;
    if (bcm_mailbox_get_arm_memory(&base, &size)) {
        return size;
    }
    return 0;
}

size_t hal_platform_get_total_memory(void)
{
    uint32_t arm_base, arm_size, vc_base, vc_size;

    if (!bcm_mailbox_get_arm_memory(&arm_base, &arm_size)) {
        return 0;
    }

    if (bcm_mailbox_get_vc_memory(&vc_base, &vc_size)) {
        return arm_size + vc_size;
    }

    return arm_size;
}

/* =============================================================================
 * CLOCK INFORMATION
 * =============================================================================
 */

hal_error_t hal_platform_get_clock_info(hal_clock_info_t *info)
{
    if (info == NULL) return HAL_ERROR_NULL_PTR;

    bcm_mailbox_get_clock_rate(BCM_CLOCK_ARM, &info->arm_freq_hz);
    bcm_mailbox_get_clock_rate(BCM_CLOCK_CORE, &info->core_freq_hz);
    bcm_mailbox_get_clock_rate(BCM_CLOCK_UART, &info->uart_freq_hz);
    bcm_mailbox_get_clock_rate(BCM_CLOCK_EMMC2, &info->emmc_freq_hz);

    return HAL_SUCCESS;
}

uint32_t hal_platform_get_arm_freq(void)
{
    uint32_t freq;
    if (bcm_mailbox_get_clock_rate(BCM_CLOCK_ARM, &freq)) {
        return freq;
    }
    return 0;
}

uint32_t hal_platform_get_arm_freq_measured(void)
{
    uint32_t freq;
    if (bcm_mailbox_get_clock_measured(BCM_CLOCK_ARM, &freq)) {
        return freq;
    }
    return 0;
}

uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id)
{
    uint32_t bcm_clock;

    switch (clock_id) {
        case HAL_CLOCK_ARM:     bcm_clock = BCM_CLOCK_ARM; break;
        case HAL_CLOCK_CORE:    bcm_clock = BCM_CLOCK_CORE; break;
        case HAL_CLOCK_UART:    bcm_clock = BCM_CLOCK_UART; break;
        case HAL_CLOCK_EMMC:    bcm_clock = BCM_CLOCK_EMMC2; break;
        default:                return 0;
    }

    uint32_t freq;
    if (bcm_mailbox_get_clock_rate(bcm_clock, &freq)) {
        return freq;
    }
    return 0;
}

/* =============================================================================
 * TEMPERATURE
 * =============================================================================
 */

hal_error_t hal_platform_get_temperature(int32_t *temp_mc)
{
    if (temp_mc == NULL) return HAL_ERROR_NULL_PTR;

    uint32_t temp;
    if (!bcm_mailbox_get_temperature(&temp)) {
        return HAL_ERROR_HARDWARE;
    }

    *temp_mc = (int32_t)temp;
    return HAL_SUCCESS;
}

int32_t hal_platform_get_temp_celsius(void)
{
    uint32_t temp_mc;
    if (bcm_mailbox_get_temperature(&temp_mc)) {
        return (int32_t)(temp_mc / 1000);
    }
    return -1;
}

hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc)
{
    if (temp_mc == NULL) return HAL_ERROR_NULL_PTR;

    uint32_t temp;
    if (!bcm_mailbox_get_max_temperature(&temp)) {
        return HAL_ERROR_HARDWARE;
    }

    *temp_mc = (int32_t)temp;
    return HAL_SUCCESS;
}

/* =============================================================================
 * THROTTLE STATUS
 * =============================================================================
 */

hal_error_t hal_platform_get_throttle_status(uint32_t *status)
{
    if (status == NULL) return HAL_ERROR_NULL_PTR;

    if (!bcm_mailbox_get_throttled(status)) {
        return HAL_ERROR_HARDWARE;
    }

    return HAL_SUCCESS;
}

bool hal_platform_is_throttled(void)
{
    uint32_t status;
    if (bcm_mailbox_get_throttled(&status)) {
        return (status & 0xF) != 0;
    }
    return false;
}

/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 */

hal_error_t hal_platform_set_power(hal_device_id_t device, bool on)
{
    uint32_t bcm_device;

    switch (device) {
        case HAL_DEVICE_SD_CARD:    bcm_device = BCM_DEVICE_SD; break;
        case HAL_DEVICE_UART0:      bcm_device = BCM_DEVICE_UART0; break;
        case HAL_DEVICE_UART1:      bcm_device = BCM_DEVICE_UART1; break;
        case HAL_DEVICE_USB:        bcm_device = BCM_DEVICE_USB; break;
        default:                    return HAL_ERROR_INVALID_ARG;
    }

    if (!bcm_mailbox_set_power_state(bcm_device, on)) {
        return HAL_ERROR_HARDWARE;
    }

    return HAL_SUCCESS;
}

hal_error_t hal_platform_get_power(hal_device_id_t device, bool *on)
{
    if (on == NULL) return HAL_ERROR_NULL_PTR;

    uint32_t bcm_device;

    switch (device) {
        case HAL_DEVICE_SD_CARD:    bcm_device = BCM_DEVICE_SD; break;
        case HAL_DEVICE_UART0:      bcm_device = BCM_DEVICE_UART0; break;
        case HAL_DEVICE_UART1:      bcm_device = BCM_DEVICE_UART1; break;
        case HAL_DEVICE_USB:        bcm_device = BCM_DEVICE_USB; break;
        default:                    return HAL_ERROR_INVALID_ARG;
    }

    if (!bcm_mailbox_get_power_state(bcm_device, on)) {
        return HAL_ERROR_HARDWARE;
    }

    return HAL_SUCCESS;
}

/* =============================================================================
 * SYSTEM CONTROL
 * =============================================================================
 */

hal_error_t hal_platform_reboot(void)
{
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_platform_shutdown(void)
{
    return HAL_ERROR_NOT_SUPPORTED;
}

void hal_panic(const char *message)
{
    (void)message;
    while (1) { HAL_WFI(); }
}

void hal_debug_putc(char c) { (void)c; }
void hal_debug_puts(const char *s) { while (*s) hal_debug_putc(*s++); }
void hal_debug_printf(const char *fmt, ...) { (void)fmt; }
