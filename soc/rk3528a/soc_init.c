/*
 * soc/rk3528a/soc_init.c - RK3528A Platform Initialization
 *
 * Tutorial-OS: RK3528A HAL Implementation
 *
 * NO MAILBOX on Rockchip! Hardware info comes from:
 *   - Fixed values based on SoC documentation
 *   - Device tree (if U-Boot passes it)
 *   - Direct register reads
 *
 * Temperature is read from thermal sensor registers.
 * Clock info from CRU (Clock and Reset Unit).
 */

#include "hal/hal_platform.h"
#include "hal/hal_timer.h"
#include "hal/hal_gpio.h"
#include "rk3528a_regs.h"

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static bool g_platform_initialized = false;

/* Fixed values for RK3528A */
static const char *g_board_name = "Radxa Rock 2A";
static const char *g_soc_name = "RK3528A";

/* =============================================================================
 * THERMAL SENSOR (Temperature)
 * =============================================================================
 */

#define RK_TSADC_BASE           (RK_PERIPHERAL_BASE + 0x02280000)
#define RK_TSADC_DATA0          (RK_TSADC_BASE + 0x0020)
#define RK_TSADC_COMP0_INT      (RK_TSADC_BASE + 0x0030)

/*
 * Convert TSADC value to temperature (millicelsius)
 * This is an approximation - actual formula depends on calibration
 */
static int32_t tsadc_to_temp(uint32_t code)
{
    /*
     * RK3528A TSADC formula (approximate):
     * temp = (code - offset) * slope
     * Typical: offset ~402, slope ~-0.226
     *
     * This is simplified - real driver uses calibration data.
     */
    if (code < 100 || code > 700) {
        return -1000;  /* Invalid reading */
    }

    /* Approximate linear conversion */
    int32_t temp_mc = ((int32_t)code - 402) * (-226);
    temp_mc += 40000;  /* Offset to reasonable range */

    return temp_mc;
}

/* =============================================================================
 * CRU (Clock and Reset Unit)
 * =============================================================================
 */

/*
 * Read ARM core frequency from CRU
 * Simplified - actual implementation needs PLL calculation
 */
static uint32_t read_arm_freq(void)
{
    /* RK3528A typically runs at 1.5-2.0 GHz */
    /* TODO: Read actual frequency from CRU registers */
    return 1800000000;  /* 1.8 GHz placeholder */
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

    /* Initialize timer (ARM Generic Timer) */
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

    info->platform_id = HAL_PLATFORM_RADXA_ROCK2A;
    info->arch = HAL_ARCH_ARM64;
    info->board_name = g_board_name;
    info->soc_name = g_soc_name;
    info->board_revision = 0x20240001;  /* Placeholder */
    info->serial_number = 0;            /* Not available without EEPROM */

    return HAL_SUCCESS;
}

hal_platform_id_t hal_platform_get_id(void)
{
    return HAL_PLATFORM_RADXA_ROCK2A;
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
     * Rock 2A has 1GB/2GB/4GB variants.
     * Using placeholder values.
     */
    info->arm_base = RK_DRAM_BASE;
    info->arm_size = 2UL * 1024 * 1024 * 1024;  /* 2GB placeholder */
    info->gpu_base = 0;  /* No separate GPU memory on RK */
    info->gpu_size = 0;
    info->peripheral_base = RK_PERIPHERAL_BASE;

    return HAL_SUCCESS;
}

size_t hal_platform_get_arm_memory(void)
{
    /* TODO: Detect actual RAM size */
    return 2UL * 1024 * 1024 * 1024;  /* 2GB */
}

size_t hal_platform_get_total_memory(void)
{
    return hal_platform_get_arm_memory();
}

/* =============================================================================
 * CLOCK INFORMATION
 * =============================================================================
 */

hal_error_t hal_platform_get_clock_info(hal_clock_info_t *info)
{
    if (info == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    info->arm_freq_hz = read_arm_freq();
    info->core_freq_hz = 500000000;     /* 500 MHz GPU */
    info->uart_freq_hz = 24000000;      /* 24 MHz */
    info->emmc_freq_hz = 200000000;     /* 200 MHz */

    return HAL_SUCCESS;
}

uint32_t hal_platform_get_arm_freq(void)
{
    return read_arm_freq();
}

uint32_t hal_platform_get_arm_freq_measured(void)
{
    /* TODO: Measure actual frequency */
    return read_arm_freq();
}

uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id)
{
    switch (clock_id) {
        case HAL_CLOCK_ARM:     return read_arm_freq();
        case HAL_CLOCK_CORE:    return 500000000;
        case HAL_CLOCK_UART:    return 24000000;
        case HAL_CLOCK_EMMC:    return 200000000;
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
     * Read from TSADC (Thermal Sensor ADC)
     * Note: TSADC must be enabled first (done by U-Boot typically)
     */
    uint32_t code = hal_mmio_read32(RK_TSADC_DATA0) & 0xFFF;
    *temp_mc = tsadc_to_temp(code);

    return HAL_SUCCESS;
}

int32_t hal_platform_get_temp_celsius(void)
{
    int32_t temp_mc;
    if (HAL_OK(hal_platform_get_temperature(&temp_mc))) {
        return temp_mc / 1000;
    }
    return -1;
}

hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc)
{
    if (temp_mc == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    /* RK3528A max junction temp is typically 125°C */
    *temp_mc = 85000;  /* 85°C recommended max */
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
     * Rockchip doesn't have a single throttle status register like BCM.
     * Would need to check TSADC interrupts and DVFS state.
     */
    *status = 0;
    return HAL_SUCCESS;
}

bool hal_platform_is_throttled(void)
{
    /* Check if temperature is above threshold */
    int32_t temp = hal_platform_get_temp_celsius();
    return (temp > 80);  /* Throttle above 80°C */
}

/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 */

hal_error_t hal_platform_set_power(hal_device_id_t device, bool on)
{
    /*
     * Rockchip uses CRU (Clock and Reset Unit) for device power.
     * Each device has clock gates and reset controls.
     * TODO: Implement device-specific power control.
     */
    (void)device;
    (void)on;
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_platform_get_power(hal_device_id_t device, bool *on)
{
    if (on == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    /* TODO: Check CRU clock gate status */
    (void)device;
    *on = true;  /* Assume always on */
    return HAL_SUCCESS;
}

/* =============================================================================
 * SYSTEM CONTROL
 * =============================================================================
 */

hal_error_t hal_platform_reboot(void)
{
    /*
     * RK3528A uses CRU for reset control.
     * Writing to GLB_SRST_FST triggers global reset.
     */
    #define RK_CRU_GLB_SRST_FST     (RK_CRU_BASE + 0x0208)
    hal_mmio_write32(RK_CRU_GLB_SRST_FST, 0xFDB9);

    while (1) {
        HAL_WFI();
    }

    return HAL_SUCCESS;  /* Never reached */
}

hal_error_t hal_platform_shutdown(void)
{
    /* Requires PMIC control - not supported in basic HAL */
    return HAL_ERROR_NOT_SUPPORTED;
}

void hal_panic(const char *message)
{
    (void)message;
    while (1) {
        HAL_WFI();
    }
}

/* =============================================================================
 * DEBUG OUTPUT
 * =============================================================================
 */

void hal_debug_putc(char c)
{
    /* UART2 is typically debug console on Rock 2A */
    #define RK_DEBUG_UART RK_UART2_BASE

    /* Wait for TX FIFO not full */
    while (!(hal_mmio_read32(RK_DEBUG_UART + RK_UART_LSR) & (1 << 5))) {
        HAL_NOP();
    }

    hal_mmio_write32(RK_DEBUG_UART + RK_UART_THR, c);
}

void hal_debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            hal_debug_putc('\r');
        }
        hal_debug_putc(*s++);
    }
}

void hal_debug_printf(const char *fmt, ...)
{
    /* Minimal implementation - just puts */
    (void)fmt;
}
