/*
 * timer.c — Timer Driver for the Ky X1 SoC
 * ============================================
 *
 * Provides microsecond-accurate timing using the RISC-V `rdtime` instruction.
 * This is the Ky X1 equivalent of the BCM2710 system timer in common/mmio.h.
 *
 * How it works:
 *   The `rdtime` instruction reads the `time` CSR, which is a 64-bit counter
 *   incremented at the timebase frequency. On the Ky X1, that's 24 MHz
 *   (from the device tree: timebase-frequency = <24000000>).
 *
 *   To convert ticks to microseconds: us = ticks / 24
 *   To convert microseconds to ticks: ticks = us * 24
 *
 * BCM2710 comparison:
 *   BCM: System timer at 0x3F003004, runs at 1 MHz, read via MMIO
 *   Ky X1: rdtime instruction, runs at 24 MHz, read via CSR (no MMIO needed!)
 *
 *   The 24x higher frequency gives us much finer timing resolution.
 *   1 BCM tick = 1 µs. 1 Ky X1 tick = 41.67 ns.
 *
 * Why rdtime instead of the hardware timer at 0xD4014000?
 *   - rdtime is a single instruction (no MMIO bus latency)
 *   - It's 64 bits (never wraps in practice — overflows after 24,000 years)
 *   - It's the standard RISC-V way to read time
 *   - OpenSBI ensures it works (traps to M-mode if needed)
 *
 * This file provides the SAME function signatures as the BCM timer code,
 * so the rest of Tutorial-OS (drivers, UI, etc.) works unchanged.
 */

#include "kyx1_regs.h"
#include "types.h"

/* =============================================================================
 * CORE TIMER ACCESS
 * =============================================================================
 */

/*
 * kyx1_rdtime — Read the 64-bit time counter
 *
 * This is the fundamental building block. Everything else derives from this.
 * On the Ky X1, rdtime reads at 24 MHz — each tick is ~41.67 nanoseconds.
 *
 * On ARM64, the equivalent is reading SYSTIMER_CLO (32-bit, 1 MHz).
 * The RISC-V version is better: 64-bit (no wrap handling) and higher resolution.
 */
static inline uint64_t kyx1_rdtime(void)
{
    uint64_t t;
    __asm__ volatile("rdtime %0" : "=r"(t));
    return t;
}

/* =============================================================================
 * PUBLIC API — Drop-in replacements for BCM timer functions
 * =============================================================================
 * These match the signatures in common/mmio.h so the rest of the codebase
 * (drivers, UI, audio timing, USB polling) works without changes.
 */

/*
 * micros — Get current time in microseconds
 *
 * Returns the lower 32 bits for compatibility with the BCM API.
 * Wraps every ~71 minutes (same as BCM version).
 *
 * Internal: rdtime ticks / 24 = microseconds
 */
uint32_t micros(void)
{
    return (uint32_t)(kyx1_rdtime() / (KYX1_TIMEBASE_FREQ / 1000000));
}

/*
 * micros64 — Get current time in microseconds (64-bit)
 *
 * Never wraps in practice (overflow after ~768,000 years at 24 MHz).
 * The BCM version had to carefully handle 32-bit high/low reads to avoid
 * tearing. The RISC-V version is atomic (single 64-bit read instruction).
 */
uint64_t micros64(void)
{
    return kyx1_rdtime() / (KYX1_TIMEBASE_FREQ / 1000000);
}

/*
 * delay_us — Delay for a specified number of microseconds
 *
 * Uses rdtime for accurate busy-wait delays. Handles wrap-around correctly
 * (though with 64-bit counters, wrap-around is effectively impossible).
 *
 * BCM version: reads SYSTIMER_CLO in a loop (MMIO every iteration)
 * Ky X1 version: reads rdtime in a loop (CSR read, much faster)
 *
 * @param us  Number of microseconds to delay
 */
void delay_us(uint32_t us)
{
    /*
     * Convert microseconds to rdtime ticks.
     * ticks = us * 24 (since timebase = 24 MHz = 24 ticks per µs)
     *
     * Use 64-bit math to avoid overflow for large delays.
     * Max safe delay with 32-bit us: ~178 seconds (2^32 / 24M ticks)
     */
    uint64_t ticks = (uint64_t)us * (KYX1_TIMEBASE_FREQ / 1000000);
    uint64_t start = kyx1_rdtime();

    while ((kyx1_rdtime() - start) < ticks)
        ; /* Busy wait */
}

/*
 * delay_ms — Delay for a specified number of milliseconds
 *
 * Convenience wrapper around delay_us.
 */
void delay_ms(uint32_t ms)
{
    delay_us(ms * 1000);
}

/*
 * ticks — Get raw timer ticks (platform-specific resolution)
 *
 * Returns the raw rdtime value at 24 MHz. Useful for high-resolution
 * timing measurements where microsecond granularity isn't enough.
 *
 * To convert to nanoseconds: ns = ticks * 1000 / 24
 */
uint64_t ticks(void)
{
    return kyx1_rdtime();
}

/*
 * ticks_to_us — Convert raw ticks to microseconds
 */
uint64_t ticks_to_us(uint64_t t)
{
    return t / (KYX1_TIMEBASE_FREQ / 1000000);
}

/*
 * us_to_ticks — Convert microseconds to raw ticks
 */
uint64_t us_to_ticks(uint64_t us)
{
    return us * (KYX1_TIMEBASE_FREQ / 1000000);
}
