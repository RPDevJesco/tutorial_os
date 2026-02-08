/*
 * soc/bcm2710/timer.c - BCM2710 Timer Implementation
 *
 * Tutorial-OS: BCM2710 HAL Implementation
 *
 * Implements hal_timer.h using the BCM2710 System Timer.
 * The system timer is a 1MHz free-running counter - perfect for
 * microsecond-accurate timing.
 *
 */

#include "hal/hal_timer.h"
#include "bcm2710_regs.h"

/* =============================================================================
 * INITIALIZATION
 * =============================================================================
 */

hal_error_t hal_timer_init(void)
{
    /*
     * BCM2710 system timer runs automatically from boot.
     * No initialization needed!
     */
    return HAL_SUCCESS;
}

/* =============================================================================
 * TIME RETRIEVAL
 * =============================================================================
 */

uint64_t hal_timer_get_ticks(void)
{
    /*
     * Read the 64-bit counter atomically.
     * Must read CHI, then CLO, then CHI again to detect wrap.
     */
    uint32_t hi1, lo, hi2;

    do {
        hi1 = hal_mmio_read32(BCM_SYSTIMER_CHI);
        lo  = hal_mmio_read32(BCM_SYSTIMER_CLO);
        hi2 = hal_mmio_read32(BCM_SYSTIMER_CHI);
    } while (hi1 != hi2);  /* Retry if wrapped during read */

    return ((uint64_t)hi1 << 32) | lo;
}

uint32_t hal_timer_get_ticks32(void)
{
    /*
     * Just read the low 32 bits - sufficient for most delays.
     * Wraps every ~71 minutes at 1MHz.
     */
    return hal_mmio_read32(BCM_SYSTIMER_CLO);
}

uint64_t hal_timer_get_ms(void)
{
    return hal_timer_get_ticks() / 1000;
}

/* =============================================================================
 * DELAY FUNCTIONS
 * =============================================================================
 * These match existing delay_us() implementation from mmio.h.
 */

void hal_delay_us(uint32_t us)
{
    /*
     * Busy-wait using the 1MHz system timer.
     * Since the timer runs at 1MHz, 1 tick = 1 microsecond.
     *
     * This is the existing delay_us() implementation:
     *   uint32_t start = mmio_read(SYSTIMER_CLO);
     *   while ((mmio_read(SYSTIMER_CLO) - start) < us) {
     *       __asm__ volatile("yield");
     *   }
     */
    uint32_t start = hal_mmio_read32(BCM_SYSTIMER_CLO);

    while ((hal_mmio_read32(BCM_SYSTIMER_CLO) - start) < us) {
        /* Yield to other threads/cores if in a multicore context */
        HAL_NOP();
    }
}

void hal_delay_ms(uint32_t ms)
{
    /* Convert to microseconds, being careful about overflow */
    while (ms > 1000) {
        hal_delay_us(1000000);  /* 1 second at a time */
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
