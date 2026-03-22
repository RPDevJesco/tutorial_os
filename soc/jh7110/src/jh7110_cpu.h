/*
 * jh7110_cpu.h — CPU Operations for the JH-7110 SoC (SiFive U74)
 * ================================================================
 *
 * This is the JH7110 implementation of the hal/hal_cpu.h contract.
 * It's the direct equivalent of soc/kyx1/kyx1_cpu.h, adapted for
 * the SiFive U74 core.
 *
 * RISC-V drivers include this instead of mmio.h (ARM64):
 *   ARM64 code:   #include "mmio.h"         (gets ARM barriers, BCM timer)
 *   JH7110 code:  #include "jh7110_cpu.h"   (gets RISC-V barriers, rdtime)
 *
 * KEY DIFFERENCE FROM KYX1_CPU.H:
 * ================================
 * The Ky X1 (SpacemiT X60) supports several non-standard extensions:
 *   - Zicbom: cache block operations (cbo.clean, cbo.inval, cbo.flush)
 *   - Zicboz: cache block zero (cbo.zero)
 *   - V extension: vector instructions
 *   - B extension: bit manipulation
 *
 * The SiFive U74 implements RV64GC ONLY:
 *   - G = IMAFDZicsr_Zifencei (base + multiply + atomics + float + Zifencei)
 *   - C = compressed instructions
 *   - NO Zicbom — do NOT use cache block operations on U74
 *   - NO V extension
 *   - NO B extension
 *
 * Therefore, this file:
 *   - KEEPS all fence instructions (standard RV64GC)
 *   - KEEPS wfi, wfe polyfill, cpu_relax (standard RV64GC)
 *   - REMOVES Zicbom cache flush functions (not in kyx1_cpu.h anyway,
 *     those are in cache.S which we also exclude from soc.mk)
 *   - The ISA restriction is enforced by board.mk: -march=rv64gc
 *     (not rv64gcv like KyX1 might use)
 *
 * CACHE COHERENCY NOTE:
 *   Without Zicbom, how do we ensure cache coherency?
 *   The U74 has a hardware-coherent cache (the shared 2MB L2 cache
 *   manages coherency automatically for CPU-visible memory). For
 *   framebuffer writes (device memory), we use fence instructions to
 *   ensure ordering. This is sufficient for our bare-metal use case
 *   since we run with the MMU off (satp=0), treating all memory as
 *   strongly-ordered device memory.
 */

#ifndef JH7110_CPU_H
#define JH7110_CPU_H

#include "hal/hal_cpu.h"       /* Portable mmio_read/mmio_write contract */
#include "jh7110_regs.h"       /* JH7110 hardware base addresses */

/* =============================================================================
 * MEMORY BARRIERS
 * =============================================================================
 *
 * Identical to kyx1_cpu.h — RISC-V fence instructions are standard RV64GC.
 * The commentary below explains why these map to what they do, which is
 * valuable teaching material for the book's RISC-V vs ARM64 chapter.
 *
 * ARM64 has three barrier instructions:
 *   dmb sy  — Data Memory Barrier: order all accesses, don't wait
 *   dsb sy  — Data Synchronization Barrier: wait for all to complete
 *   isb     — Instruction Synchronization Barrier: flush pipeline
 *
 * RISC-V has two:
 *   fence iorw, iorw  — equivalent to dsb (orders + waits, device+memory)
 *   fence.i           — equivalent to isb (sync instruction cache)
 *
 * There's no "order only without wait" fence in standard RISC-V, so
 * dmb() and dsb() both compile to the same instruction. This is slightly
 * conservative but always correct.
 */

static inline void dmb(void)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline void dsb(void)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/*
 * isb — Instruction Synchronization Barrier
 *
 * fence.i is from Zifencei, which is part of the base G extension (RV64GC).
 * So this works correctly on the U74.
 */
static inline void isb(void)
{
    __asm__ volatile("fence.i" ::: "memory");
}

/* =============================================================================
 * CPU POWER HINTS
 * =============================================================================
 */

static inline void wfi(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

/*
 * wfe — polyfill as wfi (RISC-V has no wfe/sev pair)
 * Same as kyx1_cpu.h — the RISC-V architecture simply doesn't have this
 * concept. See kyx1_cpu.h for the full explanation.
 */
static inline void wfe(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

static inline void sev(void)
{
    /* No equivalent on RISC-V. Send IPI via CLINT for SMP wake. */
}

static inline void cpu_relax(void)
{
    /* RISC-V has no YIELD equivalent. nop is fine for spin loops. */
    __asm__ volatile("nop" ::: "memory");
}

/* =============================================================================
 * CACHE LINE SIZE
 * =============================================================================
 *
 * SiFive U74 L1 cache line size: 64 bytes (same as most ARMv8 cores).
 * U74 L2 cache (2 MB shared): also 64-byte cache lines.
 *
 * Without Zicbom, we can't flush individual cache lines. This constant
 * is still useful for alignment calculations.
 */
#define CACHE_LINE_SIZE 64

/* =============================================================================
 * TIMING (declaration — implemented in timer.c)
 * =============================================================================
 *
 * The U74 implements rdtime as a CSR read that returns the system timer
 * value. The timer runs at the OSC frequency (24 MHz on JH7110).
 *
 * This is identical to the Ky X1 approach. Both SoCs use a 24 MHz
 * reference oscillator as the rdtime source. The CPU frequency may
 * differ (1.5 GHz U74 vs 1.6 GHz X60), but the timer math is the same.
 *
 * rdtime is always accessible from S-mode — no SBI call needed.
 * OpenSBI maps the CLINT mtime counter to the S-mode time CSR.
 */
extern uint32_t micros(void);
extern uint64_t micros64(void);
extern void delay_us(uint32_t us);
extern void delay_ms(uint32_t ms);
extern uint64_t ticks(void);
extern uint32_t jh7110_measure_cpu_freq(uint32_t calibration_ms);

/* cpu_idle — enter low-power state until next event */
static inline void cpu_idle(void)
{
    wfi();
}

#endif /* JH7110_CPU_H */