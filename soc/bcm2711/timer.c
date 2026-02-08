/*
 * soc/bcm2711/timer.c - BCM2711 Timer Implementation
 *
 * Tutorial-OS: BCM2711 HAL Implementation
 *
 * Identical to BCM2710 - the system timer interface is the same,
 * only the base address differs (handled by bcm2711_regs.h).
 */

#include "hal/hal_timer.h"
#include "bcm2711_regs.h"

hal_error_t hal_timer_init(void)
{
    /* System timer runs automatically */
    return HAL_SUCCESS;
}

uint64_t hal_timer_get_ticks(void)
{
    uint32_t hi1, lo, hi2;

    do {
        hi1 = hal_mmio_read32(BCM_SYSTIMER_CHI);
        lo  = hal_mmio_read32(BCM_SYSTIMER_CLO);
        hi2 = hal_mmio_read32(BCM_SYSTIMER_CHI);
    } while (hi1 != hi2);

    return ((uint64_t)hi1 << 32) | lo;
}

uint32_t hal_timer_get_ticks32(void)
{
    return hal_mmio_read32(BCM_SYSTIMER_CLO);
}

uint64_t hal_timer_get_ms(void)
{
    return hal_timer_get_ticks() / 1000;
}

void hal_delay_us(uint32_t us)
{
    uint32_t start = hal_mmio_read32(BCM_SYSTIMER_CLO);

    while ((hal_mmio_read32(BCM_SYSTIMER_CLO) - start) < us) {
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
