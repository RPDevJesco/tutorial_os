/*
 * hal/hal_platform.h - Platform Initialization and Information
 *
 * Tutorial-OS: HAL Interface Definitions
 *
 * This header provides:
 *   - Platform initialization sequence
 *   - Hardware information queries (temperature, clocks, memory)
 *   - Board identification
 *
 * This HAL abstracts those queries for platforms that don't have mailbox
 * (Rockchip, Amlogic, Allwinner use different mechanisms).
 *
 */

#ifndef HAL_PLATFORM_H
#define HAL_PLATFORM_H

#include "hal_types.h"

/* =============================================================================
 * PLATFORM IDENTIFICATION
 * =============================================================================
 */

/*
 * Platform information structure
 */
typedef struct {
    hal_platform_id_t platform_id;      /* Platform identifier */
    hal_arch_t arch;                    /* Architecture (ARM64, RISC-V, etc.) */
    const char *board_name;             /* Human-readable board name */
    const char *soc_name;               /* SoC name (BCM2710, RK3528A, etc.) */
    uint32_t board_revision;            /* Board revision code */
    uint64_t serial_number;             /* Board serial number (if available) */
} hal_platform_info_t;

/*
 * Memory region information
 */
typedef struct {
    uintptr_t arm_base;                 /* ARM accessible RAM base */
    size_t arm_size;                    /* ARM accessible RAM size */
    uintptr_t gpu_base;                 /* GPU memory base (if applicable) */
    size_t gpu_size;                    /* GPU memory size */
    uintptr_t peripheral_base;          /* Peripheral register base */
} hal_memory_info_t;

/*
 * Clock information
 */
typedef struct {
    uint32_t arm_freq_hz;               /* ARM CPU frequency */
    uint32_t core_freq_hz;              /* Core/GPU frequency */
    uint32_t uart_freq_hz;              /* UART clock frequency */
    uint32_t emmc_freq_hz;              /* EMMC/SD clock frequency */
} hal_clock_info_t;

/* =============================================================================
 * PLATFORM INITIALIZATION
 * =============================================================================
 */

/*
 * Early platform initialization
 *
 * Called very early in boot, before most other subsystems.
 * Sets up MMIO base addresses and basic hardware state.
 *
 * @return  HAL_SUCCESS or error code
 */
hal_error_t hal_platform_early_init(void);

/*
 * Full platform initialization
 *
 * Called after early init. Initializes all HAL subsystems:
 *   - Timer
 *   - GPIO
 *   - UART (for debug output)
 *
 * @return  HAL_SUCCESS or error code
 */
hal_error_t hal_platform_init(void);

/*
 * Check if platform is initialized
 *
 * @return  true if hal_platform_init() has completed successfully
 */
bool hal_platform_is_initialized(void);

/* =============================================================================
 * PLATFORM INFORMATION
 * =============================================================================
 */

/*
 * Get platform information
 *
 * @param info  Output: platform info structure
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_platform_get_info(hal_platform_info_t *info);

/*
 * Get platform ID
 *
 * @return  Platform identifier
 */
hal_platform_id_t hal_platform_get_id(void);

/*
 * Get board name string
 *
 * @return  Human-readable board name (e.g., "Raspberry Pi Zero 2W")
 */
const char *hal_platform_get_board_name(void);

/*
 * Get SoC name string
 *
 * @return  SoC name (e.g., "BCM2710")
 */
const char *hal_platform_get_soc_name(void);

/* =============================================================================
 * MEMORY INFORMATION
 * =============================================================================
 */

/*
 * Get memory information
 *
 * On BCM platforms, queries via mailbox.
 * On other platforms, reads from device tree or fixed values.
 *
 * @param info  Output: memory info structure
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_platform_get_memory_info(hal_memory_info_t *info);

/*
 * Get ARM memory size
 *
 * @return  ARM accessible RAM in bytes
 */
size_t hal_platform_get_arm_memory(void);

/*
 * Get total RAM size
 *
 * @return  Total RAM in bytes (ARM + GPU on Pi)
 */
size_t hal_platform_get_total_memory(void);

/* =============================================================================
 * CLOCK INFORMATION
 * =============================================================================
 * Maps to mailbox_get_clock_rate() function.
 */

/*
 * Get clock information
 *
 * @param info  Output: clock info structure
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_platform_get_clock_info(hal_clock_info_t *info);

/*
 * Get ARM CPU frequency
 *
 * @return Frequency in Hz, or 0 on error
 */
uint32_t hal_platform_get_arm_freq(void);

/*
 * Get measured ARM CPU frequency
 *
 * On BCM platforms, can differ from max freq if throttled.
 *
 * @return  Measured frequency in Hz
 */
uint32_t hal_platform_get_arm_freq_measured(void);

/*
 * Clock IDs for detailed queries
 */
typedef enum {
    HAL_CLOCK_ARM       = 0,
    HAL_CLOCK_CORE      = 1,
    HAL_CLOCK_UART      = 2,
    HAL_CLOCK_EMMC      = 3,
    HAL_CLOCK_PWM       = 4,
    HAL_CLOCK_PIXEL     = 5,
} hal_clock_id_t;

/*
 * Get specific clock rate
 *
 * @param clock_id  Clock to query
 * @return          Frequency in Hz, or 0 on error
 */
uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id);

/* =============================================================================
 * TEMPERATURE MONITORING
 * =============================================================================
 * Maps to mailbox_get_temperature() function.
 */

/*
 * Get CPU temperature
 *
 * @param temp_mc   Output: temperature in millicelsius
 * @return          HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_platform_get_temperature(int32_t *temp_mc);

/*
 * Get CPU temperature (convenience)
 *
 * @return  Temperature in degrees Celsius, or -1 on error
 */
int32_t hal_platform_get_temp_celsius(void);

/*
 * Get max temperature before throttling
 *
 * @param temp_mc   Output: max temperature in millicelsius
 * @return          HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc);

/* =============================================================================
 * THROTTLING STATUS
 * =============================================================================
 * Maps to mailbox_get_throttled() function.
 */

/*
 * Throttle status flags
 */
#define HAL_THROTTLE_UNDERVOLT_NOW      BIT(0)
#define HAL_THROTTLE_ARM_FREQ_CAPPED    BIT(1)
#define HAL_THROTTLE_THROTTLED_NOW      BIT(2)
#define HAL_THROTTLE_SOFT_TEMP_LIMIT    BIT(3)
#define HAL_THROTTLE_UNDERVOLT_OCCURRED BIT(16)
#define HAL_THROTTLE_FREQ_CAP_OCCURRED  BIT(17)
#define HAL_THROTTLE_THROTTLE_OCCURRED  BIT(18)
#define HAL_THROTTLE_SOFT_TEMP_OCCURRED BIT(19)

/*
 * Get throttle status
 *
 * @param status    Output: throttle status flags
 * @return          HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_platform_get_throttle_status(uint32_t *status);

/*
 * Check if currently throttled
 *
 * @return  true if any throttling is active
 */
bool hal_platform_is_throttled(void);

/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 * Maps to mailbox_set_power_state() function.
 */

/*
 * Device IDs for power management
 */
typedef enum {
    HAL_DEVICE_SD_CARD  = 0,
    HAL_DEVICE_UART0    = 1,
    HAL_DEVICE_UART1    = 2,
    HAL_DEVICE_USB      = 3,
    HAL_DEVICE_I2C0     = 4,
    HAL_DEVICE_I2C1     = 5,
    HAL_DEVICE_I2C2     = 6,
    HAL_DEVICE_SPI      = 7,
    HAL_DEVICE_PWM      = 8,
} hal_device_id_t;

/*
 * Set device power state
 *
 * @param device    Device ID
 * @param on        true = power on, false = power off
 * @return          HAL_SUCCESS or error code
 */
hal_error_t hal_platform_set_power(hal_device_id_t device, bool on);

/*
 * Get device power state
 *
 * @param device    Device ID
 * @param on        Output: current power state
 * @return          HAL_SUCCESS or error code
 */
hal_error_t hal_platform_get_power(hal_device_id_t device, bool *on);

/* =============================================================================
 * SYSTEM CONTROL
 * =============================================================================
 */

/*
 * Reboot the system
 *
 * Does not return on success.
 *
 * @return  HAL_ERROR_NOT_SUPPORTED if reboot not available
 */
hal_error_t hal_platform_reboot(void);

/*
 * Halt/shutdown the system
 *
 * Does not return on success.
 *
 * @return  HAL_ERROR_NOT_SUPPORTED if shutdown not available
 */
hal_error_t hal_platform_shutdown(void);

/*
 * Panic - halt with error
 *
 * Displays error message (if possible) and halts.
 * Does not return.
 *
 * @param message   Error message
 */
HAL_NORETURN void hal_panic(const char *message);

/* =============================================================================
 * DEBUG OUTPUT
 * =============================================================================
 */

/*
 * Write character to debug console
 *
 * Uses UART or other available debug output.
 *
 * @param c     Character to write
 */
void hal_debug_putc(char c);

/*
 * Write string to debug console
 *
 * @param s     String to write
 */
void hal_debug_puts(const char *s);

/*
 * Write formatted string to debug console
 *
 * Limited printf implementation (no floating point).
 *
 * @param fmt   Format string
 * @param ...   Arguments
 */
void hal_debug_printf(const char *fmt, ...);

/* =============================================================================
 * PLATFORM-SPECIFIC NOTES
 * =============================================================================
 *
 * BCM2710/BCM2711 (Pi Zero 2W, Pi 4, CM4):
 *   - Hardware info via VideoCore mailbox
 *   - Temperature from mailbox tag
 *   - Power management via mailbox
 *
 * bcm2712 (Pi 5, CM5):
 *   - Similar mailbox interface
 *   - Some info from RP1 registers
 *
 * RK3528A (Rock 2A):
 *   - Info from device tree or fixed values
 *   - Temperature from thermal sensor registers
 *   - Power via PMU (Power Management Unit)
 *
 * S905X (Le Potato):
 *   - Info from device tree
 *   - Temperature from thermal sensor
 *   - Power via PMIC
 *
 * H618 (KICKPI K2B):
 *   - Info from device tree
 *   - Temperature from thermal sensor
 *   - Power via AXP PMU
 *
 * K1 RISC-V (Orange Pi RV2):
 *   - Info from device tree or SBI
 *   - May need different query mechanisms
 *
 * Intel N150 (LattePanda IOTA):
 *   - Info from CPUID, MSRs, ACPI tables
 *   - Temperature from IA32_THERM_STATUS MSR
 *   - Clock rates from CPUID leaf 0x16 or MSRs
 *   - Power management via ACPI
 *   - No GPU memory split (integrated, shared RAM)
 *   - Throttle status from IA32_PACKAGE_THERM_STATUS
 */


#endif /* HAL_PLATFORM_H */
