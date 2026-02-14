/*
 * mmio.h — Memory-Mapped I/O and System Primitives (Multi-Architecture)
 * ======================================================================
 *
 * This header provides low-level hardware access primitives used by all
 * drivers across all architectures. It's the single file that makes
 * "portable" drivers like framebuffer.c compile on both ARM64 and RISC-V.
 *
 * HOW IT WORKS:
 *   Every function detects the target architecture at compile time:
 *     #ifdef __aarch64__  → ARM64 inline assembly (original code, unchanged)
 *     #ifdef __riscv      → RISC-V inline assembly or extern calls
 *
 *   For ARM64, everything is exactly as it was — no behavior changes.
 *
 *   For RISC-V:
 *     - Barriers:   fence instructions (inline)
 *     - CPU hints:  wfi (inline), compiler barriers where no equivalent exists
 *     - Timing:     extern to soc/kyx1/timer.c (uses rdtime @ 24 MHz)
 *     - Cache ops:  extern to boot/riscv64/cache.S (uses Zicbom instructions)
 *                   Cache ops are extern because Zicbom needs a special -march
 *                   flag that only cache.S is compiled with.
 *     - BCM addrs:  only defined on ARM64 (RISC-V has kyx1_regs.h instead)
 *
 * WHAT'S IN THIS FILE:
 *   - Portable MMIO read/write (shared across all platforms via hal_cpu.h)
 *   - Memory barriers (dmb, dsb, isb)
 *   - CPU power hints (wfe, wfi, sev, yield/cpu_relax)
 *   - Timing (micros, delay_us, delay_ms)
 *   - Cache management (clean, invalidate, flush)
 *   - Platform-specific hardware base addresses
 */

#ifndef MMIO_H
#define MMIO_H

/*
 * Include the HAL CPU contract.
 * This gives us the portable mmio_read() and mmio_write() functions
 * that work on every architecture. We don't redefine them here.
 */
#include "hal_cpu.h"


/* =============================================================================
 * HARDWARE BASE ADDRESSES
 * =============================================================================
 *
 * BCM addresses are only relevant on ARM64 Pi platforms.
 * RISC-V platforms define their addresses in soc/kyx1/kyx1_regs.h.
 */

#ifdef __aarch64__

#define PERIPHERAL_BASE     0x3F000000
#define ARM_LOCAL_BASE      0x40000000
#define SYSTIMER_BASE       (PERIPHERAL_BASE + 0x00003000)
#define SYSTIMER_CLO        (SYSTIMER_BASE + 0x04)
#define SYSTIMER_CHI        (SYSTIMER_BASE + 0x08)

#endif /* __aarch64__ */

/*
 * Cache line size — same on Cortex-A53 and X60 cores (64 bytes)
 */
#define CACHE_LINE_SIZE     64


/* =============================================================================
 * MEMORY BARRIERS
 * =============================================================================
 *
 * ARM64: dmb sy, dsb sy, isb
 * RISC-V: fence (covers both dmb and dsb semantics), fence.i
 *
 * RISC-V's fence instruction is a combined ordering + completion barrier.
 * There's no separate "ordering only" vs "completion" distinction like
 * ARM64's dmb vs dsb. We use the same fence for both, which is correct
 * (slightly conservative for dmb, but safe and simple).
 */

#if defined(__aarch64__)

static inline void dmb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline void dsb(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

static inline void isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}

#elif defined(__riscv)

static inline void dmb(void)
{
    /*
     * fence iorw, iorw — orders all memory operations.
     * Closest equivalent to ARM64's dmb sy.
     *
     * 'i' = device input, 'o' = device output,
     * 'r' = memory reads, 'w' = memory writes
     */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline void dsb(void)
{
    /*
     * RISC-V fence is a completion barrier (like dsb, not just ordering).
     * Same instruction as dmb — RISC-V doesn't distinguish the two.
     */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline void isb(void)
{
    /*
     * fence.i — instruction fetch barrier.
     * Ensures that stores to instruction memory are visible to
     * subsequent instruction fetches. Used after modifying code
     * or trap vectors.
     */
    __asm__ volatile("fence.i" ::: "memory");
}

#else
#error "Unsupported architecture — need barrier implementations"
#endif


/* =============================================================================
 * CPU POWER HINTS
 * =============================================================================
 *
 * ARM64: sev, wfe, wfi, yield
 * RISC-V: wfi only — no wfe/sev/yield equivalents
 *
 * For functions without a RISC-V equivalent, we use a compiler memory
 * barrier to prevent the optimizer from removing spin loops.
 */

#if defined(__aarch64__)

static inline void sev(void)
{
    __asm__ volatile("sev" ::: "memory");
}

static inline void wfe(void)
{
    __asm__ volatile("wfe" ::: "memory");
}

static inline void wfi(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

static inline void cpu_relax(void)
{
    __asm__ volatile("yield" ::: "memory");
}

#elif defined(__riscv)

static inline void sev(void)
{
    /* No RISC-V equivalent — multi-core wakeup uses IPIs instead */
    __asm__ volatile("" ::: "memory");
}

static inline void wfe(void)
{
    /* RISC-V has no wfe — use wfi as the closest equivalent.
     * Both put the core to sleep until an interrupt/event. */
    __asm__ volatile("wfi" ::: "memory");
}

static inline void wfi(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

static inline void cpu_relax(void)
{
    /* No yield instruction on RISC-V — compiler barrier prevents
     * the optimizer from removing spin loops. */
    __asm__ volatile("" ::: "memory");
}

#endif


/* =============================================================================
 * TIMING
 * =============================================================================
 *
 * ARM64/BCM: Uses BCM283x system timer @ 1 MHz (inline, register reads)
 * RISC-V:    Uses rdtime CSR @ 24 MHz, provided by soc/kyx1/timer.c (extern)
 *
 * Both provide the same API: micros(), micros64(), delay_us(), delay_ms()
 * with microsecond-resolution timing.
 */

#if defined(__aarch64__)

static inline uint32_t micros(void)
{
    return mmio_read(SYSTIMER_CLO);
}

static inline uint64_t micros64(void)
{
    uint32_t hi = mmio_read(SYSTIMER_CHI);
    uint32_t lo = mmio_read(SYSTIMER_CLO);
    /* Re-read high if it changed (handles wrap during read) */
    if (mmio_read(SYSTIMER_CHI) != hi) {
        hi = mmio_read(SYSTIMER_CHI);
        lo = mmio_read(SYSTIMER_CLO);
    }
    return ((uint64_t)hi << 32) | lo;
}

static inline void delay_us(uint32_t us)
{
    uint32_t start = micros();
    while ((micros() - start) < us) {
        __asm__ volatile("yield");
    }
}

static inline void delay_ms(uint32_t ms)
{
    delay_us(ms * 1000);
}

#elif defined(__riscv)

/*
 * On RISC-V, timing is provided by soc/kyx1/timer.c which reads
 * the rdtime CSR at 24 MHz and converts to microseconds.
 * These are declared extern here so portable drivers can call them.
 */
extern uint32_t micros(void);
extern uint64_t micros64(void);
extern void     delay_us(uint32_t us);
extern void     delay_ms(uint32_t ms);

#endif


/* =============================================================================
 * CACHE MANAGEMENT
 * =============================================================================
 *
 * ARM64: dc cvac/ivac/civac instructions (inline)
 * RISC-V: Zicbom cbo.clean/inval/flush instructions (extern from cache.S)
 *
 * WHY RISC-V CACHE OPS ARE EXTERN:
 *   The cbo.* instructions require -march=rv64gc_zicbom which is only
 *   enabled for cache.S (via CACHE_ASFLAGS in board.mk). If we put
 *   them inline here, every .c file would need that flag, which would
 *   fail on toolchains that don't support it or pollute the flag space.
 *   Calling out to cache.S keeps the special flag isolated.
 *
 * Same API, same semantics:
 *   clean_dcache_range()       — write dirty lines to RAM
 *   invalidate_dcache_range()  — discard cached data
 *   flush_dcache_range()       — clean + invalidate
 */

#if defined(__aarch64__)

static inline void clean_dcache_range(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = start + size;
    while (addr < end) {
        __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

static inline void invalidate_dcache_range(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = start + size;
    while (addr < end) {
        __asm__ volatile("dc ivac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

static inline void flush_dcache_range(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = start + size;
    while (addr < end) {
        __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

#elif defined(__riscv)

/*
 * On RISC-V, cache management is provided by boot/riscv64/cache.S
 * which is compiled with -march=rv64gc_zicbom_zicboz.
 *
 * Same function names, same signatures, same semantics as ARM64.
 */
extern void clean_dcache_range(uintptr_t start, size_t size);
extern void invalidate_dcache_range(uintptr_t start, size_t size);
extern void flush_dcache_range(uintptr_t start, size_t size);

#endif


#endif /* MMIO_H */