/*
 * soc/rk3528a/timer.c - RK3528A Timer Implementation
 *
 * Tutorial-OS: RK3528A HAL Implementation
 *
 * COMPLETELY DIFFERENT FROM BCM!
 *
 * BCM uses a memory-mapped System Timer (1MHz).
 * Rockchip uses the ARM Generic Timer accessed via system registers.
 *
 * The ARM Generic Timer:
 *   - Counter frequency from CNTFRQ_EL0 (typically 24MHz on RK)
 *   - Current count from CNTPCT_EL0 (physical) or CNTVCT_EL0 (virtual)
 *   - No MMIO access needed!
 *
 * This demonstrates how the HAL abstracts fundamentally different
 * timer implementations behind the same interface.
 */

#include "hal/hal_timer.h"
#include "rk3528a_regs.h"

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static uint64_t g_timer_freq = 0;           /* Counter frequency in Hz */
static uint64_t g_ticks_per_us = 0;         /* Ticks per microsecond */

/* =============================================================================
 * INITIALIZATION
 * =============================================================================
 */

hal_error_t hal_timer_init(void)
{
    /*
     * Read the counter frequency from CNTFRQ_EL0.
     * This is set by firmware (U-Boot) during boot.
     * Typical value: 24,000,000 Hz (24 MHz)
     */
    g_timer_freq = RK_READ_CNTFRQ();

    if (g_timer_freq == 0) {
        /* Firmware didn't set it - assume 24 MHz */
        g_timer_freq = RK_TIMER_FREQ_HZ;
    }

    g_ticks_per_us = g_timer_freq / 1000000;
    if (g_ticks_per_us == 0) {
        g_ticks_per_us = 1;  /* Prevent division by zero */
    }

    return HAL_SUCCESS;
}

/* =============================================================================
 * TIME RETRIEVAL
 * =============================================================================
 */

uint64_t hal_timer_get_ticks(void)
{
    /*
     * Read the physical counter value from CNTPCT_EL0.
     * This is a 64-bit free-running counter.
     *
     * Unlike BCM which returns microseconds directly (1MHz timer),
     * this returns raw ticks. We convert to microseconds below.
     */
    uint64_t ticks = RK_READ_CNTPCT();

    /* Convert to microseconds for consistency with BCM HAL */
    if (g_ticks_per_us > 0) {
        return ticks / g_ticks_per_us;
    }
    return ticks;
}

uint32_t hal_timer_get_ticks32(void)
{
    return (uint32_t)hal_timer_get_ticks();
}

uint64_t hal_timer_get_ms(void)
{
    return hal_timer_get_ticks() / 1000;
}

/* =============================================================================
 * DELAY FUNCTIONS
 * =============================================================================
 */

void hal_delay_us(uint32_t us)
{
    /*
     * Busy-wait using the ARM Generic Timer.
     *
     * We read the counter, calculate the target value,
     * then poll until we reach it.
     */
    uint64_t start = RK_READ_CNTPCT();
    uint64_t target = start + ((uint64_t)us * g_ticks_per_us);

    while (RK_READ_CNTPCT() < target) {
        HAL_NOP();
    }
}

void hal_delay_ms(uint32_t ms)
{
    while (ms > 1000) {
        hal_delay_us(1000000);
        ms -= 1000;
    }
    hal_delay_us(ms * 1000);
}

void hal_delay_s(uint32_t s)
{
    while (s--) {
        hal_delay_us(1000000);
    }
}

/* =============================================================================
 * ROCKCHIP-SPECIFIC
 * =============================================================================
 */

/*
 * Get raw timer frequency (useful for debug)
 */
uint64_t rk_timer_get_frequency(void)
{
    return g_timer_freq;
}

/*
 * Get raw counter value (without conversion)
 */
uint64_t rk_timer_get_raw_ticks(void)
{
    return RK_READ_CNTPCT();
}
