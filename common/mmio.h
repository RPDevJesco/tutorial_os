/*
 * mmio.h - Memory-Mapped I/O and System Primitives
 * =================================================
 *
 * This header provides low-level hardware access primitives used by all
 * drivers. It centralizes MMIO operations, memory barriers, timing functions,
 * and cache management.
 *
 * Previously these were scattered inline across drivers. Centralizing them:
 *   - Ensures consistent implementation
 *   - Makes it easier to port to different platforms
 *   - Provides a single place to add debugging/tracing
 *
 * MEMORY-MAPPED I/O:
 * ------------------
 * On ARM, hardware registers appear at specific memory addresses. Reading
 * and writing these addresses controls the hardware. We use 'volatile' to
 * prevent the compiler from optimizing away these accesses.
 *
 * MEMORY BARRIERS:
 * ----------------
 * ARM processors can reorder memory operations for performance. When talking
 * to hardware, we need barriers to ensure operations happen in order:
 *   - DMB (Data Memory Barrier): Ensures all memory accesses before complete
 *     before any after
 *   - DSB (Data Synchronization Barrier): Ensures all memory accesses complete
 *     before the next instruction executes
 *   - ISB (Instruction Synchronization Barrier): Flushes the pipeline
 *
 * TIMING:
 * -------
 * We use the BCM283x system timer (1MHz) for accurate microsecond timing.
 * This is much more accurate than spin loops.
 *
 * CACHE MANAGEMENT:
 * -----------------
 * When sharing memory with the GPU or DMA, we need to manage caches:
 *   - Clean: Write dirty cache lines to RAM (CPU wrote, GPU needs to see)
 *   - Invalidate: Discard cache contents (GPU wrote, CPU needs to see fresh)
 *   - Flush: Clean then invalidate
 */

#ifndef MMIO_H
#define MMIO_H

#include "types.h"

/* =============================================================================
 * HARDWARE BASE ADDRESSES
 * =============================================================================
 */

/*
 * BCM2837 peripheral base address (Pi Zero 2W / Pi 3)
 *
 * All peripherals are memory-mapped starting at this address.
 * Add the peripheral's offset to get its register addresses.
 */
#define PERIPHERAL_BASE     0x3F000000

/*
 * ARM Local peripherals base
 *
 * Used for multicore control, local timers, and mailboxes between cores.
 */
#define ARM_LOCAL_BASE      0x40000000

/*
 * System Timer base address
 *
 * The system timer runs at 1MHz and is used for accurate timing.
 */
#define SYSTIMER_BASE       (PERIPHERAL_BASE + 0x00003000)

/*
 * System Timer Counter Low register
 *
 * 32-bit counter incrementing at 1MHz. Wraps every ~71 minutes.
 */
#define SYSTIMER_CLO        (SYSTIMER_BASE + 0x04)

/*
 * System Timer Counter High register
 *
 * Upper 32 bits of the 64-bit counter.
 */
#define SYSTIMER_CHI        (SYSTIMER_BASE + 0x08)

/*
 * ARM Cortex-A53 cache line size
 *
 * Used for cache maintenance operations.
 */
#define CACHE_LINE_SIZE     64


/* =============================================================================
 * MMIO ACCESS FUNCTIONS
 * =============================================================================
 *
 * These functions read/write hardware registers with proper volatile semantics.
 */

/*
 * mmio_write() - Write a 32-bit value to a hardware register
 *
 * @param addr   Register address
 * @param value  Value to write
 */
static inline void mmio_write(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

/*
 * mmio_read() - Read a 32-bit value from a hardware register
 *
 * @param addr  Register address
 *
 * Returns: The register value
 */
static inline uint32_t mmio_read(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}


/* =============================================================================
 * MEMORY BARRIERS
 * =============================================================================
 *
 * ARM processors can reorder memory operations. Barriers ensure ordering.
 */

/*
 * dmb() - Data Memory Barrier
 *
 * Ensures all explicit memory accesses before this instruction complete
 * before any explicit memory accesses after it.
 *
 * Use when: You need memory operations to be visible in order, but don't
 * need to wait for completion before continuing.
 */
static inline void dmb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

/*
 * dsb() - Data Synchronization Barrier
 *
 * Ensures all explicit memory accesses complete before the next instruction.
 * Stronger than DMB - actually waits for completion.
 *
 * Use when: You need to ensure writes are visible before continuing,
 * e.g., before enabling something that depends on the write.
 */
static inline void dsb(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

/*
 * isb() - Instruction Synchronization Barrier
 *
 * Flushes the pipeline and ensures all previous instructions complete
 * before fetching new instructions.
 *
 * Use when: After modifying system registers or memory that affects
 * instruction execution (e.g., enabling MMU, modifying page tables).
 */
static inline void isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}

/*
 * sev() - Send Event
 *
 * Sends an event to all cores, waking any that are in WFE sleep.
 *
 * Use when: Signaling other cores that work is available.
 */
static inline void sev(void)
{
    __asm__ volatile("sev" ::: "memory");
}

/*
 * wfe() - Wait For Event
 *
 * Puts the core into low-power sleep until an event occurs.
 * Events include: SEV from another core, interrupts, etc.
 *
 * Use when: Waiting for something with low power consumption.
 */
static inline void wfe(void)
{
    __asm__ volatile("wfe" ::: "memory");
}

/*
 * wfi() - Wait For Interrupt
 *
 * Puts the core into low-power sleep until an interrupt occurs.
 *
 * Use when: Idle loop waiting for interrupts.
 */
static inline void wfi(void)
{
    __asm__ volatile("wfi" ::: "memory");
}


/* =============================================================================
 * TIMING FUNCTIONS
 * =============================================================================
 *
 * Accurate timing using the BCM283x system timer (1MHz).
 */

/*
 * micros() - Get current time in microseconds
 *
 * Returns the low 32 bits of the system timer counter.
 * Wraps every ~71 minutes (2^32 microseconds).
 *
 * Returns: Current time in microseconds
 */
static inline uint32_t micros(void)
{
    return mmio_read(SYSTIMER_CLO);
}

/*
 * micros64() - Get current time in microseconds (64-bit)
 *
 * Returns the full 64-bit system timer counter.
 * Note: Reading the two halves is not atomic, so there's a small
 * chance of error at the 32-bit boundary. For most uses, micros() is fine.
 *
 * Returns: Current time in microseconds (64-bit)
 */
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

/*
 * delay_us() - Delay for a number of microseconds
 *
 * Uses the system timer for accurate delays. Handles wrap-around correctly.
 *
 * @param us  Number of microseconds to delay
 */
static inline void delay_us(uint32_t us)
{
    uint32_t start = micros();
    while ((micros() - start) < us) {
        /* Hint to CPU that we're spinning */
        __asm__ volatile("yield");
    }
}

/*
 * delay_ms() - Delay for a number of milliseconds
 *
 * @param ms  Number of milliseconds to delay
 */
static inline void delay_ms(uint32_t ms)
{
    delay_us(ms * 1000);
}


/* =============================================================================
 * CACHE MANAGEMENT
 * =============================================================================
 *
 * ARM caches can hold stale data when sharing memory with GPU/DMA.
 * These functions ensure coherency.
 */

/*
 * clean_dcache_range() - Clean data cache for a memory range
 *
 * Writes any dirty cache lines back to RAM. Use when the CPU has written
 * data that the GPU or DMA needs to see.
 *
 * @param start  Start address (will be aligned down to cache line)
 * @param size   Number of bytes
 */
static inline void clean_dcache_range(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~63;  /* Align to cache line (64 bytes) */
    uintptr_t end = start + size;
    while (addr < end) {
        __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
        addr += 64;
    }
}

/*
 * invalidate_dcache_range() - Invalidate data cache for a memory range
 *
 * Discards cached data, forcing next read to come from RAM.
 * Use when the GPU or DMA has written data that the CPU needs to see.
 *
 * WARNING: Any dirty data in the cache will be lost!
 *
 * @param start  Start address (will be aligned down to cache line)
 * @param size   Number of bytes
 */
static inline void invalidate_dcache_range(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~63;
    uintptr_t end = start + size;
    while (addr < end) {
        __asm__ volatile("dc ivac, %0" : : "r"(addr) : "memory");
        addr += 64;
    }
}

/*
 * flush_dcache_range() - Clean and invalidate data cache for a memory range
 *
 * Writes dirty data back to RAM, then invalidates. Use for bidirectional
 * shared memory where both CPU and GPU/DMA read and write.
 *
 * @param start  Start address (will be aligned down to cache line)
 * @param size   Number of bytes
 */
static inline void flush_dcache_range(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = start + size;

    while (addr < end) {
        /* DC CIVAC - Clean and Invalidate by VA to Point of Coherency */
        __asm__ volatile("dc civac, %0" :: "r"(addr));
        addr += CACHE_LINE_SIZE;
    }

    dsb();
}

#endif /* MMIO_H */