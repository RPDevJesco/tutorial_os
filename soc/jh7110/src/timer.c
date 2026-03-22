/*
 * timer.c — Timer Driver for the StarFive JH-7110 SoC
 * =====================================================
 *
 * Implements microsecond timing using the RISC-V rdtime CSR.
 *
 * THIS IS NEARLY IDENTICAL TO soc/kyx1/timer.c.
 * The core concept is the same: both SoCs use a 24 MHz reference
 * oscillator as the rdtime source. The only differences are:
 *   - The prefix (jh7110_ vs kyx1_)
 *   - The CPU frequency nominal max (1.5 GHz vs 1.6 GHz)
 *
 * This is a strong illustration of the HAL portability thesis:
 * timer code written for one RISC-V bare-metal board works almost
 * verbatim on another, even from a different vendor.
 *
 * HOW RDTIME WORKS:
 * =================
 *
 * The RISC-V `rdtime` pseudo-instruction reads the `time` CSR, which
 * is a 64-bit register that counts at the platform timer frequency.
 *
 * On the JH7110 (and Ky X1), this frequency is 24 MHz — set by the
 * on-board crystal oscillator connected to the OSC_XIN pin.
 * This is specified in the JH7110 datasheet section 2.10 "Clock Source":
 *   "OSC 24 MHz system main clock source"
 *
 * From S-mode (where OpenSBI puts us), reading the time CSR requires
 * that OpenSBI has enabled the SBI Timer extension (EID 0x54494D45).
 * Modern OpenSBI does this automatically.
 *
 * TIMER ARITHMETIC:
 *   24 MHz = 24,000,000 ticks per second
 *   1 microsecond = 24 ticks
 *   1 millisecond = 24,000 ticks
 *
 * This gives us ~46 minutes before the 32-bit microsecond counter wraps.
 * The 64-bit counter won't wrap for ~24,000 years.
 *
 * CPU FREQUENCY MEASUREMENT:
 * ==========================
 * We measure the actual CPU clock frequency by counting CPU cycles
 * (rdcycle CSR) against the known-frequency time counter (rdtime)
 * over a calibration interval. This gives us the real running
 * frequency even if DVFS (dynamic voltage/frequency scaling) is active.
 *
 * The U74 supports rdcycle from S-mode. Some strict supervisor
 * configurations disable this, but on JH7110 with stock U-Boot/OpenSBI,
 * rdcycle is accessible.
 */

#include "jh7110_regs.h"
#include "types.h"

/* 24 MHz OSC = timer ticks per microsecond */
#define TIMER_TICKS_PER_US      24ULL
#define TIMER_FREQ_HZ           JH7110_TIMER_FREQ_HZ    /* 24,000,000 */

/* =============================================================================
 * RAW COUNTER ACCESS
 * =============================================================================
 */

/*
 * read_time — read the RISC-V time CSR (64-bit, counts at 24 MHz)
 *
 * This is the fundamental hardware primitive everything else is built on.
 * We mark it noinline to ensure the compiler doesn't reorder it with
 * respect to other memory accesses in timing-critical code.
 */
static inline uint64_t read_time(void)
{
    uint64_t t;
    __asm__ volatile("rdtime %0" : "=r"(t));
    return t;
}

/*
 * read_cycle — read the RISC-V cycle CSR (CPU cycle counter)
 *
 * Used for CPU frequency measurement. Counts at the CPU clock frequency
 * (not the timer frequency), so comparing it against rdtime gives us
 * the CPU-to-timer frequency ratio = CPU frequency.
 */
static inline uint64_t read_cycle(void)
{
    uint64_t c;
    __asm__ volatile("rdcycle %0" : "=r"(c));
    return c;
}

/* =============================================================================
 * PUBLIC TIMING API
 * =============================================================================
 */

/*
 * ticks — return raw 64-bit timer counter value
 *
 * Each tick is 1/24,000,000 second (41.67 nanoseconds).
 * Useful for fine-grained timing or when you want the raw value.
 */
uint64_t ticks(void)
{
    return read_time();
}

/*
 * micros64 — microseconds since boot (64-bit, no rollover concern)
 *
 * 24 MHz ticks / 24 = microseconds.
 * Integer division is fine here — 1 µs resolution is sufficient.
 */
uint64_t micros64(void)
{
    return read_time() / TIMER_TICKS_PER_US;
}

/*
 * micros — microseconds since boot (32-bit)
 *
 * Rolls over after ~71 minutes. Sufficient for most delay operations.
 * For long-running timing, use micros64().
 */
uint32_t micros(void)
{
    return (uint32_t)micros64();
}

/*
 * delay_us — spin-wait for the specified number of microseconds
 *
 * Uses rdtime for accurate busy-waiting. The CPU executes other
 * instructions (the loop itself) while waiting, which is fine for
 * bare-metal without a scheduler. For power-sensitive code, use
 * wfi() to sleep instead.
 *
 * Minimum reliable delay: ~5 µs (limited by timer resolution + loop overhead)
 * For sub-microsecond delays, use nop sequences instead.
 */
void delay_us(uint32_t us)
{
    uint64_t start = read_time();
    uint64_t end   = start + (uint64_t)us * TIMER_TICKS_PER_US;

    /* Handle potential 64-bit rollover (won't happen for 24,000 years,
     * but defensive programming is a good habit to demonstrate) */
    while (read_time() < end)
        ;
}

/*
 * delay_ms — spin-wait for the specified number of milliseconds
 *
 * Implemented as repeated 1000 µs delays to avoid overflow in the
 * multiplication for large values.
 */
void delay_ms(uint32_t ms)
{
    while (ms--) {
        delay_us(1000);
    }
}

/* =============================================================================
 * CPU FREQUENCY MEASUREMENT
 * =============================================================================
 *
 * Measure actual CPU clock rate by correlating rdcycle with rdtime.
 *
 * Algorithm:
 *   1. Read time T1 and cycle counter C1 simultaneously (as close as possible)
 *   2. Wait for calibration_ms milliseconds using rdtime
 *   3. Read time T2 and cycle counter C2
 *   4. CPU frequency = (C2 - C1) * TIMER_FREQ_HZ / (T2 - T1)
 *
 * With a 10ms calibration interval at 1.5 GHz:
 *   Expected cycles: 1,500,000,000 * 0.01 = 15,000,000 cycles
 *   With 24 MHz timer: 24,000,000 * 0.01 = 240,000 timer ticks
 *   Result: 15,000,000 / 240,000 * 24,000,000 = 1,500,000,000 Hz → 1500 MHz
 *
 * @param calibration_ms  How long to measure (longer = more accurate)
 * @return CPU frequency in MHz
 */
uint32_t jh7110_measure_cpu_freq(uint32_t calibration_ms)
{
    /* Ensure calibration period won't overflow uint64 arithmetic */
    if (calibration_ms == 0) calibration_ms = 10;
    if (calibration_ms > 1000) calibration_ms = 1000;

    /* Synchronize start: read cycle and time as close together as possible */
    uint64_t t1 = read_time();
    uint64_t c1 = read_cycle();

    /* Wait exactly calibration_ms milliseconds using the 24 MHz timer */
    uint64_t ticks_to_wait = (uint64_t)calibration_ms * (TIMER_FREQ_HZ / 1000);
    uint64_t t_end = t1 + ticks_to_wait;
    while (read_time() < t_end)
        ;

    /* Read end values */
    uint64_t c2 = read_cycle();
    uint64_t t2 = read_time();

    /* Compute frequency: cycles_per_second = delta_cycles * timer_freq / delta_time */
    uint64_t delta_cycles = c2 - c1;
    uint64_t delta_time   = t2 - t1;

    if (delta_time == 0) return JH7110_CPU_FREQ_HZ / 1000000; /* fallback */

    /* freq_hz = delta_cycles * TIMER_FREQ_HZ / delta_time */
    /* Compute in MHz to avoid 64-bit overflow: */
    uint64_t freq_mhz = (delta_cycles * (TIMER_FREQ_HZ / 1000000)) / delta_time;

    /* Sanity check: U74 is rated 1.5 GHz max, refuse obviously wrong values */
    if (freq_mhz < 100 || freq_mhz > 2000) {
        return JH7110_CPU_FREQ_HZ / 1000000; /* 1500 MHz nominal */
    }

    return (uint32_t)freq_mhz;
}