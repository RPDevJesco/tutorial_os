/*
 * soc/bcm2712/soc_init.c - BCM2712 SoC Initialization
 * ====================================================
 */

#include "bcm2712_regs.h"
#include "bcm2712_mailbox.h"
#include "hal/hal.h"

/* External symbols from boot_soc.S */
extern uint64_t detected_ram_base;
extern uint64_t detected_ram_size;

/* Platform state */
static bool platform_initialized = false;

/*
 * hal_platform_early_init - Early platform initialization
 */
hal_error_t hal_platform_early_init(void)
{
    return HAL_SUCCESS;
}

/*
 * hal_platform_init - Full platform initialization
 */
hal_error_t hal_platform_init(void)
{
    hal_error_t err;

    err = hal_timer_init();
    if (HAL_FAILED(err)) return err;

    err = hal_gpio_init();
    if (HAL_FAILED(err)) return err;

    platform_initialized = true;
    return HAL_SUCCESS;
}

/*
 * hal_platform_is_initialized - Check if platform is initialized
 */
bool hal_platform_is_initialized(void)
{
    return platform_initialized;
}

/*
 * hal_platform_get_id - Get platform ID
 */
hal_platform_id_t hal_platform_get_id(void)
{
#if defined(BOARD_RPI_CM5_IO)
    return HAL_PLATFORM_RPI_CM5;
#else
    return HAL_PLATFORM_RPI_5;
#endif
}

/*
 * hal_platform_get_board_name - Get board name
 */
const char *hal_platform_get_board_name(void)
{
#if defined(BOARD_RPI_CM5_IO)
    return "Raspberry Pi CM5 IO Board";
#else
    return "Raspberry Pi 5";
#endif
}

/*
 * hal_platform_get_soc_name - Get SoC name
 */
const char *hal_platform_get_soc_name(void)
{
    return "BCM2712";
}

/*
 * hal_platform_get_info - Get platform information
 */
hal_error_t hal_platform_get_info(hal_platform_info_t *info)
{
    if (!info) {
        return HAL_ERROR_NULL_PTR;
    }

    info->platform_id = hal_platform_get_id();
    info->arch = HAL_ARCH_ARM64;
    info->board_name = hal_platform_get_board_name();
    info->soc_name = hal_platform_get_soc_name();

    uint32_t revision = 0;
    bcm_mailbox_get_board_revision(&revision);
    info->board_revision = revision;

    uint64_t serial = 0;
    bcm_mailbox_get_board_serial(&serial);
    info->serial_number = serial;

    return HAL_SUCCESS;
}

/*
 * hal_platform_get_memory_info - Get memory information
 */
hal_error_t hal_platform_get_memory_info(hal_memory_info_t *info)
{
    if (!info) {
        return HAL_ERROR_NULL_PTR;
    }

    info->arm_base = (uintptr_t)detected_ram_base;
    info->arm_size = (size_t)detected_ram_size;

    uint32_t vc_base = 0, vc_size = 0;
    bcm_mailbox_get_vc_memory(&vc_base, &vc_size);
    info->gpu_base = vc_base;
    info->gpu_size = vc_size;

    info->peripheral_base = BCM2712_PERI_BASE;

    return HAL_SUCCESS;
}

/*
 * hal_platform_get_arm_memory - Get ARM memory size
 */
size_t hal_platform_get_arm_memory(void)
{
    return (size_t)detected_ram_size;
}

/*
 * hal_platform_get_total_memory - Get total memory
 */
size_t hal_platform_get_total_memory(void)
{
    return (size_t)detected_ram_size;
}

/*
 * hal_platform_get_clock_info - Get clock information
 */
hal_error_t hal_platform_get_clock_info(hal_clock_info_t *info)
{
    if (!info) {
        return HAL_ERROR_NULL_PTR;
    }

    info->arm_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_ARM);
    info->core_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_CORE);
    info->uart_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_UART);
    info->emmc_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_EMMC);

    return HAL_SUCCESS;
}

/*
 * hal_platform_get_arm_freq - Get ARM frequency
 */
uint32_t hal_platform_get_arm_freq(void)
{
    return hal_platform_get_clock_rate(HAL_CLOCK_ARM);
}

/*
 * hal_platform_get_clock_rate - Get specific clock rate
 */
uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id)
{
    uint32_t rate = 0;
    uint32_t mbox_clock_id;

    switch (clock_id) {
        case HAL_CLOCK_ARM:   mbox_clock_id = CLOCK_ID_ARM;  break;
        case HAL_CLOCK_CORE:  mbox_clock_id = CLOCK_ID_CORE; break;
        case HAL_CLOCK_UART:  mbox_clock_id = CLOCK_ID_UART; break;
        case HAL_CLOCK_EMMC:  mbox_clock_id = 1; break;  /* EMMC clock ID */
        case HAL_CLOCK_PWM:   mbox_clock_id = 10; break; /* PWM clock ID */
        default: return 0;
    }

    if (bcm_mailbox_get_clock_rate(mbox_clock_id, &rate)) {
        return rate;
    }
    return 0;
}

/*
 * hal_platform_get_temperature - Get CPU temperature
 */
hal_error_t hal_platform_get_temperature(int32_t *temp_mc)
{
    if (!temp_mc) {
        return HAL_ERROR_NULL_PTR;
    }

    uint32_t temp = 0;
    if (bcm_mailbox_get_temperature(&temp)) {
        *temp_mc = (int32_t)temp;
        return HAL_SUCCESS;
    }
    return HAL_ERROR_HARDWARE;
}

/*
 * hal_platform_get_max_temperature - Get max temperature
 */
hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc)
{
    if (!temp_mc) {
        return HAL_ERROR_NULL_PTR;
    }

    uint32_t temp = 0;
    if (bcm_mailbox_get_max_temperature(&temp)) {
        *temp_mc = (int32_t)temp;
        return HAL_SUCCESS;
    }

    /* Fallback default */
    *temp_mc = 85000;
    return HAL_SUCCESS;
}

/*
 * hal_platform_get_throttle_status - Get throttle status
 */
hal_error_t hal_platform_get_throttle_status(uint32_t *status)
{
    if (!status) {
        return HAL_ERROR_NULL_PTR;
    }

    if (bcm_mailbox_get_throttled(status)) {
        return HAL_SUCCESS;
    }

    *status = 0;
    return HAL_SUCCESS;
}

/*
 * hal_platform_is_throttled - Check if throttled
 */
bool hal_platform_is_throttled(void)
{
    uint32_t status = 0;
    hal_platform_get_throttle_status(&status);
    return (status & 0x0F) != 0;
}

/*
 * hal_platform_set_power - Set device power state
 */
hal_error_t hal_platform_set_power(hal_device_id_t device, bool on)
{
    uint32_t dev_id;

    switch (device) {
        case HAL_DEVICE_SD_CARD: dev_id = BCM_POWER_SD; break;
        case HAL_DEVICE_UART0:   dev_id = BCM_POWER_UART0; break;
        case HAL_DEVICE_UART1:   dev_id = BCM_POWER_UART1; break;
        case HAL_DEVICE_USB:     dev_id = BCM_POWER_USB; break;
        case HAL_DEVICE_I2C0:    dev_id = BCM_POWER_I2C0; break;
        case HAL_DEVICE_I2C1:    dev_id = BCM_POWER_I2C1; break;
        case HAL_DEVICE_I2C2:    dev_id = BCM_POWER_I2C2; break;
        case HAL_DEVICE_SPI:     dev_id = BCM_POWER_SPI; break;
        default: return HAL_ERROR_INVALID_ARG;
    }

    if (bcm_mailbox_set_power_state(dev_id, on)) {
        return HAL_SUCCESS;
    }
    return HAL_ERROR_HARDWARE;
}

/*
 * hal_platform_get_power - Get device power state
 */
hal_error_t hal_platform_get_power(hal_device_id_t device, bool *on)
{
    if (!on) {
        return HAL_ERROR_NULL_PTR;
    }

    uint32_t dev_id;

    switch (device) {
        case HAL_DEVICE_SD_CARD: dev_id = BCM_POWER_SD; break;
        case HAL_DEVICE_UART0:   dev_id = BCM_POWER_UART0; break;
        case HAL_DEVICE_UART1:   dev_id = BCM_POWER_UART1; break;
        case HAL_DEVICE_USB:     dev_id = BCM_POWER_USB; break;
        case HAL_DEVICE_I2C0:    dev_id = BCM_POWER_I2C0; break;
        case HAL_DEVICE_I2C1:    dev_id = BCM_POWER_I2C1; break;
        case HAL_DEVICE_I2C2:    dev_id = BCM_POWER_I2C2; break;
        case HAL_DEVICE_SPI:     dev_id = BCM_POWER_SPI; break;
        default: return HAL_ERROR_INVALID_ARG;
    }

    if (bcm_mailbox_get_power_state(dev_id, on)) {
        return HAL_SUCCESS;
    }

    *on = true;  /* Assume on if query fails */
    return HAL_SUCCESS;
}

/*
 * hal_platform_reboot - Reboot system
 */
hal_error_t hal_platform_reboot(void)
{
    return HAL_ERROR_NOT_SUPPORTED;
}

/*
 * hal_platform_shutdown - Shutdown system
 */
hal_error_t hal_platform_shutdown(void)
{
    return HAL_ERROR_NOT_SUPPORTED;
}

/*
 * hal_panic - Halt with error
 */
HAL_NORETURN void hal_panic(const char *message)
{
    UNUSED(message);
    while (1) {
        HAL_WFI();
    }
}

/*
 * hal_debug_putc - Write character to debug console
 */
void hal_debug_putc(char c)
{
    UNUSED(c);
}

/*
 * hal_debug_puts - Write string to debug console
 */
void hal_debug_puts(const char *s)
{
    while (*s) {
        hal_debug_putc(*s++);
    }
}

/*
 * hal_debug_printf - Formatted debug output
 */
void hal_debug_printf(const char *fmt, ...)
{
    UNUSED(fmt);
}