/*
 * common/mmio.h — Memory-Mapped I/O and System Primitives (Multi-Architecture)
 * ==============================================================================
 *
 * This header provides low-level hardware access primitives used by all
 * drivers across all architectures. It's the single file that makes
 * "portable" drivers like framebuffer.c compile correctly on ARM64, RISC-V,
 * and x86_64.
 *
 * HOW IT WORKS
 * ============
 * Every section detects the target architecture at compile time:
 *
 *   #ifdef __aarch64__   → ARM64 (Raspberry Pi, Radxa Rock, etc.)
 *   #ifdef __riscv       → RISC-V (Milk-V Mars, Orange Pi RV2, etc.)
 *   #ifdef __x86_64__    → x86_64 (LattePanda MU, LattePanda IOTA)
 *
 * For ARM64 and RISC-V, "hardware access" always means MMIO: a hardware
 * register lives at a physical address, and we read/write it with a
 * volatile pointer. The mmio_read/mmio_write functions below handle this.
 *
 * For x86_64, MMIO still exists (GOP framebuffer, PCI BARs), but the
 * legacy PC UART (8250/16550) uses PORT I/O — a completely separate address
 * space accessed via the IN/OUT instruction family, not memory loads/stores.
 * The LattePanda MU's COM1 UART at 0x3F8 is port I/O, not MMIO.
 *
 * WHAT'S IN THIS FILE
 * ===================
 *   - Portable MMIO read/write (via hal_cpu.h / hal_types.h)
 *   - Memory barriers (dmb, dsb, isb — wrappers around HAL_* macros)
 *   - CPU hints (wfe, wfi, sev, cpu_relax)
 *   - Timing (micros, delay_us, delay_ms)
 *   - Cache management (ARM64 / RISC-V)
 *   - Platform-specific hardware base addresses (BCM, etc.)
 *   - x86_64 port I/O (outb, inb, outw, inw, outl, inl)
 *
 * WHAT IS NOT IN THIS FILE
 * ========================
 *   - PCI configuration space access (future)
 *   - MSI/MSI-X interrupt setup (future)
 *   - ACPI table parsing (future)
 */

#ifndef MMIO_H
#define MMIO_H

/*
 * Include the HAL CPU contract.
 * Provides: hal_mmio_read/write (all widths), HAL_DMB/DSB/ISB/WFI/WFE/SEV/NOP.
 */
#include "hal_types.h"


/* =============================================================================
 * MEMORY BARRIER WRAPPERS
 * =============================================================================
 *
 * These inline functions wrap the HAL_* macros from hal_types.h, providing
 * a lowercase API that matches the existing codebase's calling convention
 * (dsb(), dmb(), isb() rather than HAL_DSB(), etc.).
 *
 * Architecture-specific semantics are documented in hal_types.h. The short
 * version:
 *
 *   dmb() — Data Memory Barrier: ordering only.
 *            ARM64: dmb sy | RISC-V: fence iorw,iorw | x86: mfence
 *
 *   dsb() — Data Synchronization Barrier: ordering + completion.
 *            ARM64: dsb sy | RISC-V: fence iorw,iorw | x86: mfence
 *
 *   isb() — Instruction Synchronization Barrier: pipeline flush.
 *            ARM64: isb   | RISC-V: fence.i          | x86: compiler barrier
 *
 * GUARD: kyx1_cpu.h and jh7110_cpu.h already define dmb/dsb/isb/wfe/wfi/sev/
 * cpu_relax for RISC-V. Those headers are included by RISC-V SoC files before
 * framebuffer.h pulls in this header. Without the guard, every RISC-V
 * compilation unit gets a "redefinition" error. We detect their include guards
 * and skip our definitions when they've already been provided.
 *
 * ARM64 code does NOT include kyx1_cpu.h or jh7110_cpu.h, so ARM64 always
 * enters this block and gets the definitions from the HAL_* macros.
 * x86_64 code does not include either of those headers, so it also enters.
 */
#if !defined(KYX1_CPU_H) && !defined(JH7110_CPU_H)

static inline void dmb(void) { HAL_DMB(); }
static inline void dsb(void) { HAL_DSB(); }
static inline void isb(void) { HAL_ISB(); }

static inline void wfe(void) { HAL_WFE(); }
static inline void wfi(void) { HAL_WFI(); }
static inline void sev(void) { HAL_SEV(); }

/*
 * cpu_relax — Spin-loop hint.
 *
 *   ARM64:   yield  — hints the scheduler
 *   RISC-V:  nop    — no dedicated spin hint in standard RV64GC
 *   x86_64:  pause  — reduces power; prevents machine clears on SMT exit
 */
static inline void cpu_relax(void)
{
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__riscv)
    __asm__ volatile("nop"   ::: "memory");
#elif defined(__x86_64__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

#endif /* !KYX1_CPU_H && !JH7110_CPU_H */


/* =============================================================================
 * PORTABLE MMIO READ/WRITE
 * =============================================================================
 *
 * These are thin wrappers around hal_mmio_* from hal_types.h. They preserve
 * the original mmio_read / mmio_write naming that existing drivers use.
 *
 * These work identically on ARM64, RISC-V, and x86_64 MMIO ranges.
 * For x86_64 legacy UART access, use the port I/O section below.
 */

/*
 * GUARD: hal_cpu.h (included by kyx1_cpu.h and jh7110_cpu.h) already defines
 * mmio_write/read. Skip our definitions to avoid redefinition errors in RISC-V
 * compilation units that include both a platform CPU header and framebuffer.h.
 */
#ifndef HAL_CPU_H
static inline void     mmio_write(uintptr_t addr, uint32_t v) { hal_mmio_write32(addr, v); }
static inline uint32_t mmio_read (uintptr_t addr)             { return hal_mmio_read32(addr); }
static inline void     mmio_write8(uintptr_t addr, uint8_t v) { hal_mmio_write8(addr, v); }
static inline uint8_t  mmio_read8 (uintptr_t addr)            { return hal_mmio_read8(addr); }
#endif /* !HAL_CPU_H */


/* =============================================================================
 * HARDWARE BASE ADDRESSES (ARM64 BCM)
 * =============================================================================
 *
 * BCM addresses are only relevant on ARM64 Pi platforms.
 * RISC-V platforms define their addresses in soc/kyx1/kyx1_regs.h and
 * soc/jh7110/jh7110_regs.h.
 * x86_64 I/O port addresses are constants — see the port I/O section below.
 * x86_64 MMIO (GOP framebuffer) addresses are runtime values from UEFI.
 */

#ifdef __aarch64__

#define PERIPHERAL_BASE     0x3F000000UL
#define ARM_LOCAL_BASE      0x40000000UL
#define SYSTIMER_BASE       (PERIPHERAL_BASE + 0x00003000UL)
#define SYSTIMER_CLO        (SYSTIMER_BASE + 0x04UL)
#define SYSTIMER_CHI        (SYSTIMER_BASE + 0x08UL)

#endif /* __aarch64__ */

/* Cache line size — 64 bytes on Cortex-A53, X60, SiFive U74, and Intel N100 */
#define CACHE_LINE_SIZE     64


/* =============================================================================
 * TIMING (ARM64)
 * =============================================================================
 *
 * ARM64: reads the BCM system timer register (1 MHz, 1 tick = 1 µs).
 * RISC-V: micros() is provided by soc/<soc>/timer.c using rdtime.
 * x86_64: micros() will be provided by soc/n100/timer.c
 *         using HPET or the TSC (TBD during bring-up).
 *
 * All platforms expose the same API: micros(), delay_us(), delay_ms().
 */

#ifdef __aarch64__

static inline uint32_t micros(void)
{
    return mmio_read(SYSTIMER_CLO);
}

static inline uint64_t micros64(void)
{
    uint32_t hi = mmio_read(SYSTIMER_CHI);
    uint32_t lo = mmio_read(SYSTIMER_CLO);
    /* Re-read hi if it rolled over during the lo read */
    if (mmio_read(SYSTIMER_CHI) != hi) {
        hi = mmio_read(SYSTIMER_CHI);
        lo = mmio_read(SYSTIMER_CLO);
    }
    return ((uint64_t)hi << 32) | lo;
}

static inline void delay_us(uint32_t us)
{
    uint32_t start = micros();
    while ((micros() - start) < us);
}

static inline void delay_ms(uint32_t ms)
{
    delay_us(ms * 1000);
}

#else /* RISC-V and x86_64 — provided by SoC-specific timer.c */

extern uint32_t micros(void);
extern uint64_t micros64(void);
extern void     delay_us(uint32_t us);
extern void     delay_ms(uint32_t ms);

#endif /* __aarch64__ */


/* =============================================================================
 * CACHE MANAGEMENT (ARM64 / RISC-V)
 * =============================================================================
 *
 * x86_64 does not require explicit cache maintenance. The x86 memory model
 * guarantees cache coherency between cores and between the CPU and DMA-capable
 * devices (PCIe, USB, etc.) using hardware snooping. For GOP framebuffer
 * writes, an mfence (via dsb()) is sufficient — no cache flush is needed.
 *
 * On ARM64 the cache routines are inline (ARM v8 cache instructions).
 * On RISC-V the cache routines are extern to cache.S (Zicbom instructions
 * require a separate -march flag that only cache.S is compiled with).
 * On x86_64 these functions are defined as no-ops — calling code can
 * safely call clean_dcache_range() without an #ifdef at every call site.
 */

#if defined(__aarch64__)

static inline void clean_dcache_range(uintptr_t start, size_t len)
{
    uintptr_t addr = start & ~((uintptr_t)(CACHE_LINE_SIZE - 1));
    uintptr_t end  = start + len;
    while (addr < end) {
        __asm__ volatile("dc cvac, %0" :: "r"(addr));
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

static inline void invalidate_dcache_range(uintptr_t start, size_t len)
{
    uintptr_t addr = start & ~((uintptr_t)(CACHE_LINE_SIZE - 1));
    uintptr_t end  = start + len;
    while (addr < end) {
        __asm__ volatile("dc ivac, %0" :: "r"(addr));
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

static inline void flush_dcache_range(uintptr_t start, size_t len)
{
    uintptr_t addr = start & ~((uintptr_t)(CACHE_LINE_SIZE - 1));
    uintptr_t end  = start + len;
    while (addr < end) {
        __asm__ volatile("dc civac, %0" :: "r"(addr));
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

#elif defined(__riscv)

/* Defined in boot/riscv64/cache.S (requires Zicbom -march flag) */
extern void clean_dcache_range(uintptr_t start, size_t len);
extern void invalidate_dcache_range(uintptr_t start, size_t len);
extern void flush_dcache_range(uintptr_t start, size_t len);

#elif defined(__x86_64__)

/*
 * x86_64: cache maintenance is handled by hardware. These are intentional
 * no-ops. Drivers that call clean_dcache_range() before presenting a
 * framebuffer will compile and run correctly on all three architectures
 * without any #ifdef at the call site.
 *
 * If future work adds non-coherent DMA (e.g., a custom PCIe DMA engine),
 * clflush/clwb instructions would go here.
 */
static inline void clean_dcache_range(uintptr_t start, size_t len)
{
    (void)start; (void)len;
}
static inline void invalidate_dcache_range(uintptr_t start, size_t len)
{
    (void)start; (void)len;
}
static inline void flush_dcache_range(uintptr_t start, size_t len)
{
    (void)start; (void)len;
}

#endif /* cache management */


/* =============================================================================
 * x86_64 PORT I/O
 * =============================================================================
 *
 * WHAT IS PORT I/O?
 * -----------------
 * x86 processors have two distinct address spaces:
 *
 *   Memory space — The normal address space. Every address is accessed with
 *     ordinary load/store instructions (MOV, MOVZX, etc.). MMIO lives here:
 *     the GOP framebuffer, PCI BARs, APIC registers.
 *
 *   I/O space — A separate, 16-bit address space with 65536 ports.
 *     Accessed exclusively via the IN/OUT instruction family. The CPU decodes
 *     these differently from memory accesses; they go to the chipset's legacy
 *     I/O decoder, not the memory bus. The "address" is called a port number,
 *     not an address.
 *
 * WHY DOES THIS MATTER FOR TUTORIAL-OS?
 * --------------------------------------
 * The 8250/16550 UART (COM1, COM2, etc.) is a legacy PC device that lives
 * in I/O space, not memory space. COM1 is at port 0x3F8. You cannot access
 * it with a volatile pointer and mmio_read/mmio_write — those issue memory
 * load/store instructions, which go to the wrong address space entirely.
 *
 * The LattePanda MU's UART (confirmed by UART log: "COM1 0x3F8") is exactly
 * this device. soc/n100/uart_8250.c uses outb/inb exclusively.
 *
 * ARM64 AND RISC-V DO NOT HAVE PORT I/O
 * ---------------------------------------
 * These architectures have only one address space. Everything is MMIO.
 * PL011 UART (ARM64), NS16550 UART (RISC-V) — all MMIO registers at
 * physical memory addresses. The IN/OUT instructions do not exist on those
 * ISAs.
 *
 * This section is compiled only when targeting x86_64.
 *
 * PORT WIDTHS
 * -----------
 *   outb / inb  — 8-bit (byte)    — UART registers, legacy RTC, etc.
 *   outw / inw  — 16-bit (word)   — PCI config space index port (0xCF8)
 *   outl / inl  — 32-bit (dword)  — PCI config space data port (0xCFC)
 *
 * The "N" constraint on the port operand tells the assembler to use the
 * short-form encoding when the port fits in 8 bits (constant 0-255).
 * For variable port numbers it falls back to the DX register form.
 * "Nd" accepts both.
 */

#ifdef __x86_64__

/*
 * outb — Write a byte to an I/O port.
 *
 * @param port   I/O port number (0x0000–0xFFFF)
 * @param val    Byte to write
 *
 * Example: outb(0x3F8, 'A');  // send 'A' to COM1 UART data register
 */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1"
                     :
                     : "a"(val), "Nd"(port)
                     : "memory");
}

/*
 * inb — Read a byte from an I/O port.
 *
 * @param port   I/O port number (0x0000–0xFFFF)
 * @return       Byte read from the port
 *
 * Example: uint8_t lsr = inb(0x3F8 + 5);  // read COM1 line status register
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0"
                     : "=a"(val)
                     : "Nd"(port)
                     : "memory");
    return val;
}

/*
 * outw — Write a 16-bit word to an I/O port.
 *
 * Used for PCI config space index writes and some legacy devices.
 */
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1"
                     :
                     : "a"(val), "Nd"(port)
                     : "memory");
}

/*
 * inw — Read a 16-bit word from an I/O port.
 */
static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile("inw %1, %0"
                     : "=a"(val)
                     : "Nd"(port)
                     : "memory");
    return val;
}

/*
 * outl — Write a 32-bit dword to an I/O port.
 *
 * Primary use: PCI config space data register at port 0xCFC.
 */
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1"
                     :
                     : "a"(val), "Nd"(port)
                     : "memory");
}

/*
 * inl — Read a 32-bit dword from an I/O port.
 *
 * Primary use: PCI config space data register at port 0xCFC.
 */
static inline uint32_t inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile("inl %1, %0"
                     : "=a"(val)
                     : "Nd"(port)
                     : "memory");
    return val;
}

/*
 * io_delay — Insert a short delay between back-to-back port I/O accesses.
 *
 * Legacy PC devices (particularly the 8250 UART at 115200 baud) can be
 * slower than the CPU. Writing to port 0x80 (a POST code port, unused
 * during normal operation) causes the chipset to complete one I/O bus cycle,
 * which provides enough settling time between back-to-back port accesses.
 *
 * This is only needed for very tight polling loops or initialization
 * sequences that bang registers rapidly. Normal UART tx/rx doesn't need it.
 */
static inline void io_delay(void)
{
    outb(0x80, 0x00);
}

#endif /* __x86_64__ */


#endif /* MMIO_H */