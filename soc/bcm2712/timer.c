/*
* soc/bcm2712/timer.c - BCM2712 System Timer
 * ==========================================
 */

#include "bcm2712_regs.h"
#include "hal/hal_timer.h"

/* System timer registers */
#define TIMER_CS    ((volatile uint32_t *)(BCM2712_TIMER_BASE + 0x00))
#define TIMER_CLO   ((volatile uint32_t *)(BCM2712_TIMER_BASE + 0x04))
#define TIMER_CHI   ((volatile uint32_t *)(BCM2712_TIMER_BASE + 0x08))

/*
 * hal_timer_init - Initialize the system timer
 */
hal_error_t hal_timer_init(void)
{
    /* System timer runs automatically, no init needed */
    return HAL_SUCCESS;
}

/*
 * hal_timer_get_ticks - Get current timer value (1MHz)
 */
uint64_t hal_timer_get_ticks(void)
{
    uint32_t hi, lo;

    /* Read high, low, high again to handle rollover */
    do {
        hi = *TIMER_CHI;
        lo = *TIMER_CLO;
    } while (hi != *TIMER_CHI);

    return ((uint64_t)hi << 32) | lo;
}

/*
 * hal_timer_get_ticks32 - Get current timer value (32-bit)
 */
uint32_t hal_timer_get_ticks32(void)
{
    return *TIMER_CLO;
}

/*
 * hal_timer_get_ms - Get milliseconds since boot
 */
uint64_t hal_timer_get_ms(void)
{
    return hal_timer_get_ticks() / 1000;
}

/*
 * hal_delay_us - Delay for specified microseconds
 */
void hal_delay_us(uint32_t us)
{
    uint64_t start = hal_timer_get_ticks();
    while ((hal_timer_get_ticks() - start) < us) {
        /* Spin */
    }
}

/*
 * hal_delay_ms - Delay for specified milliseconds
 */
void hal_delay_ms(uint32_t ms)
{
    hal_delay_us(ms * 1000);
}

/*
 * hal_delay_s - Delay for specified seconds
 */
void hal_delay_s(uint32_t s)
{
    hal_delay_ms(s * 1000);
}