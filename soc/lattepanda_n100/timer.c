/*
 * soc/lattepanda_n100/timer.c — TSC-Based Timer Implementation
 *
 * Tutorial-OS: LattePanda N100 (x86_64 / UEFI) Timer Implementation
 *
 * x86_64 timing is fundamentally different from ARM64/RISC-V:
 *   - ARM64:   MMIO System Timer (BCM) or ARM Generic Timer (rdcntpct)
 *   - RISC-V:  rdtime CSR (memory-mapped CLINT counter)
 *   - x86_64:  TSC (Time Stamp Counter) via rdtsc instruction
 *
 * The TSC counts CPU cycles from an arbitrary start point. To convert
 * TSC ticks to real time, we need the TSC frequency. On the Intel N100
 * (AlderLake-N), the TSC is invariant — it runs at a constant rate
 * regardless of CPU frequency scaling (C-states, P-states, turbo).
 * The invariant TSC frequency equals the CPU's base clock * a fixed ratio
 * available from CPUID leaf 0x15 (TSC/CCC ratio) or approximated from
 * leaf 0x16 (base MHz).
 *
 * For Tutorial-OS, we use a practical approach:
 *   1. Try CPUID 0x15 (TSC frequency directly) — available on Skylake+
 *   2. Fall back to CPUID 0x16 base MHz if 0x15 returns zero
 *   3. Fall back to a known constant for the N100 if CPUID lies
 *
 * The spin-based delays from the bring-up test (main.c) used `pause`
 * loops with magic counts, which is fragile across CPU speeds. This
 * implementation uses rdtsc for accurate, calibrated delays.
 *
 * UEFI TIMING NOTE:
 *   UEFI Boot Services provides Stall() for delays, but we call
 *   hal_delay_us() after ExitBootServices when Stall() is gone.
 *   The TSC is the only option post-EBS without setting up HPET/APIC.
 */

#include "hal/hal_types.h"
#include "hal/hal_timer.h"

/* ============================================================
 * TSC frequency (set once at init, used for all conversions)
 * ============================================================ */

static uint64_t g_tsc_freq_hz = 0;     /* TSC ticks per second */
static bool     g_timer_initialized = false;

/* Intel N100 base clock: 800 MHz, but TSC typically runs at max non-turbo.
 * CPUID 0x16 EBX gives max non-turbo MHz = 3400 for N100.
 * If CPUID 0x15 is available it gives the exact TSC freq.
 * Fallback constant: 800 MHz (base clock) — conservative but safe. */
#define N100_TSC_FREQ_FALLBACK_HZ   800000000ULL

/* ============================================================
 * CPUID helpers (shared pattern with hal_platform_lattepanda_n100.c)
 * ============================================================ */

static inline void cpuid(uint32_t leaf,
                          uint32_t *eax, uint32_t *ebx,
                          uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

/* ============================================================
 * rdtsc — Read the Time Stamp Counter
 * ============================================================ */

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    /*
     * rdtsc: loads TSC into EDX:EAX.
     * The "=a"/"=d" constraints capture EAX and EDX.
     * memory clobber prevents the compiler from reordering this.
     */
    __asm__ volatile (
        "rdtsc"
        : "=a"(lo), "=d"(hi)
        :
        : "memory"
    );
    return ((uint64_t)hi << 32) | lo;
}

/* ============================================================
 * TSC frequency detection via CPUID
 * ============================================================ */

/*
 * tsc_freq_from_cpuid — Attempt to read TSC frequency from CPUID.
 *
 * CPUID leaf 0x15: TSC / Core Crystal Clock ratio
 *   EAX = ratio denominator
 *   EBX = ratio numerator
 *   ECX = core crystal clock frequency in Hz (may be 0)
 *
 *   TSC freq = ECX * EBX / EAX  (when ECX != 0)
 *
 * CPUID leaf 0x16: Processor frequency info
 *   EAX[15:0] = base clock in MHz
 *   EBX[15:0] = max non-turbo clock in MHz
 *
 * Intel N100 (AlderLake-N): leaf 0x15 is available but ECX may be 0.
 * In that case, the crystal clock is 38.4 MHz (AlderLake platform).
 * We hardcode this well-known value for the AlderLake platform.
 */
#define ALDERLAKE_CRYSTAL_HZ    38400000ULL   /* 38.4 MHz XTAL */

static uint64_t tsc_freq_from_cpuid(void)
{
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_leaf;

    cpuid(0x00, &max_leaf, &ebx, &ecx, &edx);

    /* Try leaf 0x15 first (most accurate) */
    if (max_leaf >= 0x15) {
        cpuid(0x15, &eax, &ebx, &ecx, &edx);

        if (eax != 0 && ebx != 0) {
            uint64_t crystal_hz = (ecx != 0) ? (uint64_t)ecx
                                              : ALDERLAKE_CRYSTAL_HZ;
            return (crystal_hz * ebx) / eax;
        }
    }

    /* Fall back to leaf 0x16 base clock */
    if (max_leaf >= 0x16) {
        cpuid(0x16, &eax, &ebx, &ecx, &edx);
        uint32_t base_mhz = eax & 0xFFFF;
        if (base_mhz > 0) {
            return (uint64_t)base_mhz * 1000000ULL;
        }
    }

    /* Last resort: known N100 base clock */
    return N100_TSC_FREQ_FALLBACK_HZ;
}

/* ============================================================
 * hal_timer.h implementation
 * ============================================================ */

hal_error_t hal_timer_init(void)
{
    if (g_timer_initialized) return HAL_SUCCESS;

    g_tsc_freq_hz = tsc_freq_from_cpuid();

    /* Sanity check: reject obviously wrong values */
    if (g_tsc_freq_hz < 100000000ULL || g_tsc_freq_hz > 10000000000ULL) {
        g_tsc_freq_hz = N100_TSC_FREQ_FALLBACK_HZ;
    }

    g_timer_initialized = true;
    return HAL_SUCCESS;
}

/*
 * hal_timer_get_ticks — Return TSC value as microsecond-equivalent ticks.
 *
 * The contract in hal_timer.h says ticks = microseconds on BCM (1 MHz timer).
 * On x86_64 we return raw TSC ticks here, callers that need microseconds
 * should use hal_timer_get_ms() or hal_delay_us(). The raw TSC value is
 * still useful for profiling and relative timing.
 *
 * NOTE: a follow-up chapter could normalize this to microseconds by dividing by (g_tsc_freq_hz / 1e6).
 * Left as raw TSC here to preserve the direct hardware feel.
 */
uint64_t hal_timer_get_ticks(void)
{
    return rdtsc();
}

uint32_t hal_timer_get_ticks32(void)
{
    return (uint32_t)rdtsc();
}

uint64_t hal_timer_get_ms(void)
{
    if (g_tsc_freq_hz == 0) return 0;
    return rdtsc() / (g_tsc_freq_hz / 1000);
}

/*
 * hal_delay_us — Busy-wait for the specified number of microseconds.
 *
 * Converts microseconds to TSC ticks using the cached frequency, then
 * spins until the TSC advances by that many ticks. This replaces the
 * magic-count `pause` loops in the bring-up test with calibrated delays.
 *
 * Accuracy: ±1–2 µs typical. The `pause` instruction reduces power
 * consumption and prevents pipeline hazards in the spin loop.
 */
void hal_delay_us(uint32_t us)
{
    if (g_tsc_freq_hz == 0) {
        /* Timer not initialized — fall back to coarse pause-loop */
        volatile uint32_t n = us * 400;   /* ~400 pause/µs at ~800 MHz */
        while (n--) __asm__ volatile ("pause");
        return;
    }

    uint64_t ticks_to_wait = ((uint64_t)us * g_tsc_freq_hz) / 1000000ULL;
    uint64_t start = rdtsc();

    while ((rdtsc() - start) < ticks_to_wait) {
        __asm__ volatile ("pause");
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
