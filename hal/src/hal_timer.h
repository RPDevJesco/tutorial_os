/*
 * hal/hal_timer.h - Timer and Delay Functions
 *
 * Tutorial-OS: HAL Interface Definitions
 *
 * This header abstracts timing operations. Different platforms have
 * different timer hardware:
 *
 *   BCM2710/BCM2711/bcm2712: System Timer (1MHz)
 *   RK3528A:                 ARM Generic Timer + Rockchip timers
 *   S905X:                   ARM Generic Timer
 *   H618:                    Allwinner Timer
 *   K1 (RISC-V):             CLINT timer (via SBI)
 *
 */

#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include "hal_types.h"

/* =============================================================================
 * TIMER INITIALIZATION
 * =============================================================================
 */

/*
 * Initialize the timer subsystem
 *
 * Called during hal_platform_init(). Most platforms don't need
 * explicit timer initialization, but some (like RISC-V) do.
 *
 * @return  HAL_SUCCESS or error code
 */
hal_error_t hal_timer_init(void);

/* =============================================================================
 * TIME RETRIEVAL
 * =============================================================================
 */

/*
 * Get current time in microseconds
 *
 * Returns a monotonically increasing counter. On BCM2710, this reads
 * from the 1MHz system timer. Wraps after ~4295 seconds (71 minutes)
 * for the 32-bit version, or ~584,942 years for 64-bit.
 *
 * @return  Current time in microseconds
 */
uint64_t hal_timer_get_ticks(void);

/*
 * Get current time in microseconds (32-bit for compatibility)
 *
 * Matches existing usage from SYSTIMER_CLO reads.
 * Wraps every ~71 minutes.
 *
 * @return  Current time in microseconds (lower 32 bits)
 */
uint32_t hal_timer_get_ticks32(void);

/*
 * Get current time in milliseconds
 *
 * Convenience function. Less precision but easier to work with.
 *
 * @return  Current time in milliseconds
 */
uint64_t hal_timer_get_ms(void);

/* =============================================================================
 * DELAY FUNCTIONS
 * =============================================================================
 * These match existing delay_us() function from mmio.h.
 */

/*
 * Delay for specified microseconds
 *
 * Busy-waits using the hardware timer. This is accurate to within
 * a few microseconds on most platforms.
 *
 * @param us    Microseconds to delay
 */
void hal_delay_us(uint32_t us);

/*
 * Delay for specified milliseconds
 *
 * Convenience function. Calls hal_delay_us() internally.
 *
 * @param ms    Milliseconds to delay
 */
void hal_delay_ms(uint32_t ms);

/*
 * Delay for specified seconds
 *
 * @param s     Seconds to delay
 */
void hal_delay_s(uint32_t s);

/* =============================================================================
 * TIMEOUT HELPERS
 * =============================================================================
 * Useful for polling hardware with timeouts.
 */

/*
 * Check if timeout has elapsed
 *
 * Usage:
 *     uint32_t start = hal_timer_get_ticks32();
 *     while (!condition) {
 *         if (hal_timer_timeout(start, 1000)) {  // 1ms timeout
 *             return HAL_ERROR_TIMEOUT;
 *         }
 *     }
 *
 * @param start_us      Start time from hal_timer_get_ticks32()
 * @param timeout_us    Timeout duration in microseconds
 * @return              true if timeout has elapsed
 */
HAL_INLINE bool hal_timer_timeout(uint32_t start_us, uint32_t timeout_us)
{
    return (hal_timer_get_ticks32() - start_us) >= timeout_us;
}

/*
 * Calculate elapsed time
 *
 * @param start_us      Start time from hal_timer_get_ticks32()
 * @return              Elapsed microseconds
 */
HAL_INLINE uint32_t hal_timer_elapsed(uint32_t start_us)
{
    return hal_timer_get_ticks32() - start_us;
}

/* =============================================================================
 * STOPWATCH UTILITY
 * =============================================================================
 * For measuring code execution time.
 */

typedef struct {
    uint64_t start_ticks;
    uint64_t stop_ticks;
    bool running;
} hal_stopwatch_t;

/*
 * Start stopwatch
 */
HAL_INLINE void hal_stopwatch_start(hal_stopwatch_t *sw)
{
    sw->start_ticks = hal_timer_get_ticks();
    sw->running = true;
}

/*
 * Stop stopwatch
 */
HAL_INLINE void hal_stopwatch_stop(hal_stopwatch_t *sw)
{
    sw->stop_ticks = hal_timer_get_ticks();
    sw->running = false;
}

/*
 * Get elapsed time in microseconds
 */
HAL_INLINE uint64_t hal_stopwatch_elapsed_us(hal_stopwatch_t *sw)
{
    if (sw->running) {
        return hal_timer_get_ticks() - sw->start_ticks;
    }
    return sw->stop_ticks - sw->start_ticks;
}

/*
 * Get elapsed time in milliseconds
 */
HAL_INLINE uint64_t hal_stopwatch_elapsed_ms(hal_stopwatch_t *sw)
{
    return hal_stopwatch_elapsed_us(sw) / 1000;
}

/* =============================================================================
 * PLATFORM-SPECIFIC NOTES
 * =============================================================================
 *
 * BCM2710/BCM2711 (Pi Zero 2W, Pi 3, Pi 4, CM4):
 *   - System Timer at PERIPHERAL_BASE + 0x3000
 *   - 1MHz counter (1 tick = 1 microsecond)
 *   - The existing delay_us() reads SYSTIMER_CLO directly
 *
 * bcm2712 (Pi 5, CM5):
 *   - Similar system timer, but different base address
 *   - Also has ARM Generic Timer available
 *
 * RK3528A (Rock 2A):
 *   - ARM Generic Timer (CNTPCT_EL0)
 *   - Frequency from CNTFRQ_EL0 (usually 24MHz)
 *   - Also has Rockchip-specific timers
 *
 * S905X (Le Potato):
 *   - ARM Generic Timer
 *   - Frequency varies by board config
 *
 * H618 (KICKPI K2B):
 *   - ARM Generic Timer
 *   - Also has Allwinner Timer/Watchdog
 *
 * K1 RISC-V (Orange Pi RV2):
 *   - CLINT (Core Local Interruptor) provides mtime
 *   - Accessed via SBI calls in S-mode
 *   - Frequency from device tree
 */

#endif /* HAL_TIMER_H */
