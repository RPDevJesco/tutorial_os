/*
 * kyx1_cpu.h — CPU Operations for the Ky X1 SoC (RISC-V 64-bit)
 * =================================================================
 *
 * This is the RISC-V implementation of the hal/hal_cpu.h contract.
 * It's the Ky X1 equivalent of common/mmio.h (ARM64 / BCM2710).
 *
 * RISC-V drivers include this instead of mmio.h:
 *   ARM64 code:   #include "mmio.h"       (gets ARM barriers, BCM timer, etc.)
 *   RISC-V code:  #include "kyx1_cpu.h"   (gets RISC-V barriers, rdtime, etc.)
 *
 * Both headers fulfill the same hal_cpu.h contract, so any code that uses
 * only the contract functions (dmb, dsb, micros, delay_us, etc.) compiles
 * unchanged on either architecture.
 *
 * WHAT'S IN THIS FILE (inline, available to all RISC-V code):
 *   - mmio_read/mmio_write (from hal_cpu.h — portable)
 *   - Memory barriers (fence instructions)
 *   - CPU hints (wfi, wfe polyfill, cpu_relax)
 *   - CACHE_LINE_SIZE constant
 *
 * WHAT'S IN SEPARATE FILES (declared here, defined in .c/.S):
 *   - Timing: micros(), delay_us(), etc.        → soc/kyx1/timer.c
 *   - Cache ops: clean_dcache_range(), etc.      → boot/riscv64/cache.S
 *
 * This split is intentional. On ARM64, mmio.h puts timing inline because
 * it's just an MMIO read of the system timer register. On RISC-V, timing
 * uses rdtime (a CSR read + division) which is better as a real function
 * to avoid code bloat from inlining division everywhere. Cache ops use
 * Zicbom instructions that benefit from assembly implementation.
 */

#ifndef KYX1_CPU_H
#define KYX1_CPU_H

/* Include the HAL contract — gives us portable mmio_read/mmio_write */
#include "hal_cpu.h"

/* Include Ky X1 register definitions for hardware base addresses */
#include "kyx1_regs.h"

/* =============================================================================
 * MEMORY BARRIERS
 * =============================================================================
 *
 * RISC-V uses `fence` instructions where ARM64 uses `dmb`, `dsb`, and `isb`.
 *
 * The fence instruction takes two arguments specifying which operations to
 * order: i=device input, o=device output, r=memory read, w=memory write.
 *   fence iorw, iorw  = order ALL prior ops before ALL subsequent ops
 *   fence.i           = sync instruction cache with data writes
 *
 * KEY DIFFERENCE FROM ARM64:
 *   ARM64 distinguishes between dmb (ordering only) and dsb (wait for
 *   completion). RISC-V's `fence iorw, iorw` provides dsb-level semantics —
 *   it both orders AND ensures completion. There's no lighter "ordering only"
 *   fence in standard RISC-V (the Zifencei extension only adds fence.i).
 *
 *   This means on RISC-V, dmb() and dsb() compile to the same instruction.
 *   That's fine — it's correct (just potentially slightly conservative).
 *   Real RISC-V kernels do the same thing.
 */

/*
 * dmb — Data Memory Barrier
 *
 * ARM64: dmb sy (ordering barrier — doesn't wait for completion)
 * RISC-V: fence iorw, iorw (orders AND waits — slightly stronger than needed,
 *         but correct and the standard RISC-V approach)
 */
static inline void dmb(void)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/*
 * dsb — Data Synchronization Barrier
 *
 * ARM64: dsb sy (wait for all prior memory ops to complete)
 * RISC-V: fence iorw, iorw (same instruction — RISC-V fence is naturally dsb-strength)
 */
static inline void dsb(void)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/*
 * isb — Instruction Synchronization Barrier
 *
 * ARM64: isb (flush pipeline, re-fetch instructions)
 * RISC-V: fence.i (synchronize instruction cache with data cache/memory)
 *
 * fence.i is from the Zifencei extension, which the Ky X1 supports.
 */
static inline void isb(void)
{
    __asm__ volatile("fence.i" ::: "memory");
}


/* =============================================================================
 * CPU POWER HINTS
 * =============================================================================
 */

/*
 * wfi — Wait For Interrupt
 *
 * Identical concept on both ARM64 and RISC-V (even the mnemonic is the same!).
 * CPU enters low-power state until an interrupt arrives.
 */
static inline void wfi(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

/*
 * wfe — Wait For Event (polyfill)
 *
 * ARM64 has a dedicated wfe instruction with matching sev (send event).
 * RISC-V has NO wfe/sev equivalent. We implement wfe as wfi, which is
 * slightly different (wakes on interrupts, not events) but correct for
 * our use cases (idle loops, halt conditions).
 *
 * For proper spinlock-style wake-up on RISC-V, you'd use atomic operations
 * with IPIs (inter-processor interrupts) instead of the wfe/sev pattern.
 * That's a future SMP concern — for single-core use, wfi is perfect.
 */
static inline void wfe(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

/*
 * sev — Send Event (polyfill)
 *
 * ARM64's sev wakes all cores sleeping in wfe. RISC-V doesn't have this
 * concept. On RISC-V, you'd send an IPI via the CLINT to wake specific
 * cores. For now, this is a no-op — it compiles but does nothing.
 */
static inline void sev(void)
{
    /* No direct equivalent on RISC-V. Future: send IPI via CLINT */
}

/*
 * cpu_relax — Spin-loop hint
 *
 * ARM64: yield (hint to the CPU that we're spinning, may allow other
 *        threads on the same core to run)
 * RISC-V: nop for now. The Zihintpause extension defines a `pause`
 *         instruction for this purpose, but it's optional and we don't
 *         know if the X60 cores support it yet.
 *
 * Either way, the compiler memory barrier ("memory" clobber) prevents the
 * optimizer from turning the spin loop into an infinite tight loop.
 */
static inline void cpu_relax(void)
{
    __asm__ volatile("nop" ::: "memory");
}


/* =============================================================================
 * CACHE CONSTANT
 * =============================================================================
 *
 * From the Ky X1 device tree:
 *   d-cache-size = <0x8000>;         (32 KB)
 *   d-cache-block-size = <0x40>;     (64 bytes)
 *   d-cache-sets = <0x80>;           (128 sets)
 *
 * Same as ARM Cortex-A53 — both use 64-byte cache lines.
 */

#define CACHE_LINE_SIZE     64


/* =============================================================================
 * TIMING — Declarations (implemented in soc/kyx1/timer.c)
 * =============================================================================
 *
 * On ARM64/BCM, these are inline functions that read an MMIO register.
 * On RISC-V, they use `rdtime` (a CSR read) plus division by 24 to convert
 * from 24 MHz ticks to microseconds. The division makes inlining wasteful,
 * so they're real functions in timer.c.
 *
 * Same signatures, same behavior, different implementation.
 */

uint32_t micros(void);
uint64_t micros64(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

/* Extended timing API (not in BCM version — bonus RISC-V functionality) */
uint64_t ticks(void);
uint64_t ticks_to_us(uint64_t t);
uint64_t us_to_ticks(uint64_t us);


/* =============================================================================
 * CACHE MANAGEMENT — Declarations (implemented in boot/riscv64/cache.S)
 * =============================================================================
 *
 * On ARM64/BCM, these are inline functions using dc cvac/ivac/civac.
 * On RISC-V, they use Zicbom instructions (cbo.clean/inval/flush) which
 * are implemented in assembly for clarity and to handle the instruction
 * encoding reliably.
 *
 * NOTE: The RISC-V versions take (void *start, uint64_t size) while the
 * ARM64 versions take (uintptr_t start, size_t size). Both work — the
 * types are compatible. We declare with the same types as ARM64 for
 * consistent calling convention across architectures.
 */

void clean_dcache_range(uintptr_t start, size_t size);
void invalidate_dcache_range(uintptr_t start, size_t size);
void flush_dcache_range(uintptr_t start, size_t size);

/* Additional RISC-V cache ops (not in ARM64 version) */
void zero_dcache_range(uintptr_t start, size_t size);
void sync_icache(void);


/* =============================================================================
 * RISC-V SPECIFIC: CSR ACCESS
 * =============================================================================
 *
 * Control and Status Registers (CSRs) are RISC-V's equivalent of ARM64's
 * system registers (mrs/msr). They control trap handling, interrupt state,
 * memory management, and performance counters.
 *
 * ARM64: mrs x0, sctlr_el1      RISC-V: csrr t0, sstatus
 * ARM64: msr vbar_el1, x0       RISC-V: csrw stvec, t0
 *
 * These have no ARM64 equivalent in mmio.h (ARM system register access
 * is done directly in assembly in boot.S). We provide them here as
 * inline C wrappers for convenience.
 */

/*
 * csrr_sstatus — Read the supervisor status register
 *
 * Contains: interrupt enable (SIE), previous interrupt enable (SPIE),
 * previous privilege mode (SPP), FP state (FS), etc.
 */
static inline uint64_t csrr_sstatus(void)
{
    uint64_t val;
    __asm__ volatile("csrr %0, sstatus" : "=r"(val));
    return val;
}

/*
 * csrr_scause — Read the supervisor cause register
 *
 * After a trap: bit 63 = interrupt flag, bits 62:0 = cause code.
 */
static inline uint64_t csrr_scause(void)
{
    uint64_t val;
    __asm__ volatile("csrr %0, scause" : "=r"(val));
    return val;
}

/*
 * csrr_stval — Read the supervisor trap value
 *
 * After a trap: faulting virtual address (for page faults) or the
 * faulting instruction bits (for illegal instruction exceptions).
 */
static inline uint64_t csrr_stval(void)
{
    uint64_t val;
    __asm__ volatile("csrr %0, stval" : "=r"(val));
    return val;
}

/*
 * csrr_time — Read the time CSR directly
 *
 * This is what `rdtime` actually does. Returns the platform timer count
 * at the timebase frequency (24 MHz on Ky X1).
 */
static inline uint64_t csrr_time(void)
{
    uint64_t val;
    __asm__ volatile("rdtime %0" : "=r"(val));
    return val;
}

/*
 * enable_interrupts / disable_interrupts
 *
 * Control the SIE (Supervisor Interrupt Enable) bit in sstatus.
 * ARM64 equivalent: msr daifclr, #2 / msr daifset, #2
 */
static inline void enable_interrupts(void)
{
    __asm__ volatile("csrsi sstatus, 0x2" ::: "memory"); /* Set SIE (bit 1) */
}

static inline void disable_interrupts(void)
{
    __asm__ volatile("csrci sstatus, 0x2" ::: "memory"); /* Clear SIE (bit 1) */
}

#endif /* KYX1_CPU_H */
