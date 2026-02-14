/*
 * hal_cpu.h — CPU Operations Contract for Tutorial-OS
 * =====================================================
 *
 * This header defines the interface that EVERY platform must provide for
 * low-level CPU operations. It's the contract between portable kernel code
 * and architecture-specific implementations.
 *
 * WHAT THIS FILE IS:
 *   A specification. It documents every operation the kernel needs from the
 *   CPU, explains when and why you'd use each one, and provides the portable
 *   subset (MMIO read/write) that works on all architectures.
 *
 * WHAT THIS FILE IS NOT:
 *   An implementation. Architecture-specific operations (barriers, cache,
 *   timing, CPU hints) are NOT defined here — they're in the platform headers
 *   that include this file.
 *
 * HOW IT FITS TOGETHER:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                    hal/hal_cpu.h                        │
 *   │  • Documents the full contract                         │
 *   │  • Provides portable: mmio_read(), mmio_write()        │
 *   │  • Declares what platforms must implement               │
 *   └─────────────┬───────────────────────────┬───────────────┘
 *                 │                           │
 *   ┌─────────────▼───────────┐ ┌─────────────▼───────────────┐
 *   │    common/mmio.h        │ │   soc/kyx1/kyx1_cpu.h       │
 *   │  (ARM64 / BCM2710)      │ │  (RISC-V / Ky X1)           │
 *   │                         │ │                              │
 *   │  #include "hal_cpu.h"   │ │  #include "hal_cpu.h"        │
 *   │  + ARM64 barriers       │ │  + RISC-V fence barriers     │
 *   │  + ARM64 cache ops      │ │  + Zicbom cache ops          │
 *   │  + BCM system timer     │ │  + rdtime @ 24 MHz           │
 *   │  + wfe/wfi/sev          │ │  + wfi                       │
 *   │  + BCM addresses        │ │  + Ky X1 addresses           │
 *   └─────────────────────────┘ └──────────────────────────────┘
 *
 * ADDING A NEW PLATFORM:
 *   1. Create your platform header (e.g., soc/myboard/myboard_cpu.h)
 *   2. #include "hal/hal_cpu.h" at the top
 *   3. Implement every operation listed in the CONTRACT sections below
 *   4. Your drivers #include "myboard_cpu.h" instead of "mmio.h"
 *
 * WHY NOT USE #ifdef SWITCHING IN ONE FILE?
 *   We considered having one header with #ifdef ARM64 / #ifdef RISCV64
 *   blocks. But that approach:
 *     - Makes the file hard to read (interleaved architectures)
 *     - Hides platform-specific details that are educational
 *     - Doesn't scale well to 5+ platforms
 *
 *   The "contract + implementations" pattern is how real OS kernels work
 *   (Linux has asm/barrier.h per architecture, all implementing the same API).
 *   It's also how Tutorial-OS's UI system already works (canvas vtable).
 */

#ifndef HAL_CPU_H
#define HAL_CPU_H

#include "types.h"

/* =============================================================================
 * PORTABLE: MEMORY-MAPPED I/O
 * =============================================================================
 *
 * These work identically on every architecture. Hardware registers appear at
 * specific memory addresses, and we read/write them using volatile pointers
 * to prevent the compiler from optimizing away the access.
 *
 * The `volatile` keyword tells the compiler: "this memory location can change
 * at any time (hardware is writing to it) and every read/write matters (the
 * hardware is watching)." Without volatile, the compiler might:
 *   - Cache a register value in a CPU register and never re-read it
 *   - Eliminate a "redundant" write (but the hardware needs both writes!)
 *   - Reorder accesses (hardware registers are often order-sensitive)
 *
 * These are defined here in the contract because they're truly portable —
 * volatile pointer access works on ARM64, RISC-V, x86, MIPS, everything.
 */

/*
 * mmio_write — Write a 32-bit value to a hardware register
 *
 * @param addr   Physical address of the register
 * @param value  Value to write
 *
 * Example: mmio_write(GPIO_BASE + GPSET0, 1 << pin);
 */
static inline void mmio_write(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

/*
 * mmio_read — Read a 32-bit value from a hardware register
 *
 * @param addr  Physical address of the register
 * @return      The current register value
 *
 * Example: uint32_t status = mmio_read(UART_BASE + LSR);
 */
static inline uint32_t mmio_read(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

/*
 * mmio_write8 / mmio_read8 — 8-bit variants
 *
 * Some peripherals (especially UARTs) have 8-bit registers.
 * Not all platforms need these, but they're portable.
 */
static inline void mmio_write8(uintptr_t addr, uint8_t value)
{
    *(volatile uint8_t *)addr = value;
}

static inline uint8_t mmio_read8(uintptr_t addr)
{
    return *(volatile uint8_t *)addr;
}

/* =============================================================================
 * CONTRACT: MEMORY BARRIERS
 * =============================================================================
 *
 * Modern CPUs can reorder memory operations for performance. When talking to
 * hardware or communicating between CPU cores, we need barriers to enforce
 * ordering. Every platform MUST provide these three barriers.
 *
 * REQUIRED IMPLEMENTATIONS:
 *
 *   void dmb(void);
 *     Data Memory Barrier — ensures all memory accesses BEFORE this point
 *     are visible to other observers BEFORE any memory accesses AFTER it.
 *     Does NOT wait for completion — just establishes ordering.
 *
 *     ARM64: dmb sy        RISC-V: fence iorw, iorw
 *
 *     Use when: Between writing data and writing a "doorbell" register that
 *     tells hardware to read the data. Ensures hardware sees the data before
 *     the doorbell.
 *
 *   void dsb(void);
 *     Data Synchronization Barrier — like DMB but WAITS for all prior
 *     memory accesses to actually complete before continuing.
 *     Stronger (and slower) than DMB.
 *
 *     ARM64: dsb sy        RISC-V: fence iorw, iorw
 *     (On RISC-V, fence iorw,iorw covers both dmb and dsb semantics)
 *
 *     Use when: After writing to a control register that changes system
 *     behavior (e.g., enabling the MMU), where you need to be CERTAIN
 *     the write completed before executing the next instruction.
 *
 *   void isb(void);
 *     Instruction Synchronization Barrier — flushes the CPU instruction
 *     pipeline. Ensures all instructions after the ISB are fetched fresh.
 *
 *     ARM64: isb           RISC-V: fence.i
 *
 *     Use when: After modifying code in memory (self-modifying code,
 *     loading a new module), or after changing system registers that
 *     affect instruction execution (enabling MMU, changing exception level).
 */

/* =============================================================================
 * CONTRACT: CPU POWER HINTS
 * =============================================================================
 *
 * These put the CPU into low-power states while waiting for something to
 * happen. Every platform MUST provide at least wfi().
 *
 * REQUIRED IMPLEMENTATIONS:
 *
 *   void wfi(void);
 *     Wait For Interrupt — CPU sleeps until an interrupt arrives.
 *     Available on both ARM64 and RISC-V (same mnemonic!).
 *     Use in: Idle loops, halt conditions, parked secondary cores.
 *
 *   void wfe(void);
 *     Wait For Event — CPU sleeps until an event occurs (not just interrupts).
 *     ARM64-specific. On RISC-V, this can be implemented as wfi() or nop.
 *     Use in: Spinlocks (with matching sev() from the unlocker).
 *
 *   void sev(void);
 *     Send Event — wakes all cores sleeping in wfe().
 *     ARM64-specific. On RISC-V, this is a no-op (use IPI instead).
 *     Use in: After releasing a spinlock, to wake waiters.
 *
 *   void cpu_relax(void);
 *     Spin-loop hint — tells the CPU "I'm busy-waiting, don't waste power."
 *     ARM64: yield         RISC-V: nop (or pause if Zihintpause supported)
 *     Use in: Any busy-wait loop (polling a register, waiting for a flag).
 */

/* =============================================================================
 * CONTRACT: TIMING
 * =============================================================================
 *
 * Microsecond-accurate timing is essential for hardware drivers (UART baud
 * rates, USB timing, debouncing, display refresh). Every platform MUST
 * provide these timing functions.
 *
 * REQUIRED IMPLEMENTATIONS:
 *
 *   uint32_t micros(void);
 *     Current time in microseconds (32-bit, wraps every ~71 minutes).
 *     BCM2710: Reads system timer at 1 MHz.
 *     Ky X1: Reads rdtime at 24 MHz, divides by 24.
 *
 *   uint64_t micros64(void);
 *     Current time in microseconds (64-bit, effectively never wraps).
 *     Prefer this for long-duration timing. Use micros() for tight loops
 *     where the 64-bit division overhead matters.
 *
 *   void delay_us(uint32_t us);
 *     Busy-wait for the specified number of microseconds.
 *     MUST be accurate (not a cycle-counting spin loop).
 *
 *   void delay_ms(uint32_t ms);
 *     Busy-wait for the specified number of milliseconds.
 *     Convenience wrapper: delay_us(ms * 1000).
 */

/* =============================================================================
 * CONTRACT: CACHE MANAGEMENT
 * =============================================================================
 *
 * When the CPU writes data that a hardware device (display controller, DMA,
 * USB controller) needs to read from DRAM, cache coherency becomes critical.
 * The CPU cache may hold dirty data that hasn't been written to DRAM yet.
 *
 * REQUIRED IMPLEMENTATIONS:
 *
 *   void clean_dcache_range(uintptr_t start, size_t size);
 *     Write dirty cache lines to DRAM. The cache lines remain valid.
 *     ARM64: dc cvac loop      RISC-V: cbo.clean loop (Zicbom)
 *     Use when: CPU wrote data that a device needs to read.
 *     Example: After writing pixels to framebuffer, before DPU scans them.
 *
 *   void invalidate_dcache_range(uintptr_t start, size_t size);
 *     Discard cached data (WITHOUT writing back). Force next read from DRAM.
 *     ARM64: dc ivac loop      RISC-V: cbo.inval loop (Zicbom)
 *     WARNING: Dirty data in cache will be LOST!
 *     Use when: Device wrote data to DRAM that CPU needs to read fresh.
 *     Example: After DMA transfer completes into a receive buffer.
 *
 *   void flush_dcache_range(uintptr_t start, size_t size);
 *     Clean THEN invalidate — write back dirty data, then discard cache line.
 *     ARM64: dc civac loop     RISC-V: cbo.flush loop (Zicbom)
 *     Use when: Bidirectional shared buffer (both CPU and device read/write).
 *
 * ALSO REQUIRED:
 *
 *   #define CACHE_LINE_SIZE  <bytes>
 *     The CPU's data cache line size. Used for alignment calculations.
 *     Both ARM Cortex-A53 and Ky X1 X60 use 64-byte cache lines.
 */

#endif /* HAL_CPU_H */
