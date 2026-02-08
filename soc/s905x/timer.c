/*
 * soc/s905x/timer.c - Amlogic S905X Timer Implementation
 *
 * Tutorial-OS: S905X HAL Implementation
 *
 * SAME APPROACH AS RK3528A (and different from BCM):
 *
 * BCM uses a memory-mapped System Timer (1MHz, accessed via MMIO).
 * Both S905X and RK3528A use the ARM Generic Timer via system registers.
 *
 * If you've already read soc/rk3528a/timer.c, this will look almost
 * identical! That's the beauty of the ARM Generic Timer — it's a
 * standard ARM feature that works the same across different SoC vendors.
 *
 * The only potential difference is the counter frequency:
 *   - RK3528A: 24 MHz (set by Rockchip firmware)
 *   - S905X:   24 MHz (set by Amlogic firmware)
 *   - Both happen to use 24 MHz, but we read it at runtime to be safe.
 *
 * This is a great example of how ARM standards reduce porting effort.
 * The timer code is vendor-agnostic once you're using Generic Timer.
 */

#include "hal/hal_timer.h"
#include "s905x_regs.h"

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
     * This is set by firmware (BL31/U-Boot) during boot.
     * Typical value: 24,000,000 Hz (24 MHz)
     *
     * Same register, same approach as Rockchip.
     */
    g_timer_freq = AML_READ_CNTFRQ();

    if (g_timer_freq == 0) {
        /* Firmware didn't set it - assume 24 MHz */
        g_timer_freq = AML_TIMER_FREQ_HZ;
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
     * this returns raw ticks. We convert to microseconds for
     * consistency with the HAL interface.
     */
    uint64_t ticks = AML_READ_CNTPCT();

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
     * Identical approach to Rockchip.
     */
    uint64_t start = AML_READ_CNTPCT();
    uint64_t target = (uint64_t)us * g_ticks_per_us;

    while ((AML_READ_CNTPCT() - start) < target) {
        /* Spin */
    }
}

void hal_delay_ms(uint32_t ms)
{
    hal_delay_us(ms * 1000);
}

/* =============================================================================
 * TIMER FREQUENCY
 * =============================================================================
 */

uint64_t hal_timer_get_frequency(void)
{
    return g_timer_freq;
}
