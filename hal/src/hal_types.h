/*
 * hal/hal_types.h - Hardware Abstraction Layer Types
 *
 * Tutorial-OS: HAL Interface Definitions
 *
 * This header defines fundamental types used throughout the HAL.
 * It is designed to be compatible with existing common/types.h
 * while adding HAL-specific types and utilities.
 *
 */

#ifndef HAL_TYPES_H
#define HAL_TYPES_H

/* =============================================================================
 * FIXED-WIDTH INTEGER TYPES
 * =============================================================================
 * Matches existing types.h definitions.
 */
#if defined(SOC_RP2350) || defined(__STDC_HOSTED__)
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifndef NULL
#define NULL ((void *)0)
#endif
#else
typedef signed char         int8_t;
typedef unsigned char       uint8_t;
typedef signed short        int16_t;
typedef unsigned short      uint16_t;
typedef signed int          int32_t;
typedef unsigned int        uint32_t;
typedef __INT64_TYPE__      int64_t;
typedef __UINT64_TYPE__     uint64_t;

typedef __UINTPTR_TYPE__    uintptr_t;
typedef __INTPTR_TYPE__     intptr_t;
typedef __SIZE_TYPE__       size_t;

typedef _Bool bool;
#define true  1
#define false 0

#ifndef NULL
#define NULL ((void *)0)
#endif
#endif

/* =============================================================================
 * HAL ERROR CODES
 * =============================================================================
 * All HAL functions return hal_error_t to indicate success or failure.
 * Error codes are grouped by subsystem (0xNNxx where NN = subsystem).
 */

typedef enum {
    /* Success */
    HAL_SUCCESS             = 0x0000,

    /* General errors (0x00xx) */
    HAL_ERROR               = 0x0001,   /* Generic error */
    HAL_ERROR_NOT_INIT      = 0x0002,   /* Subsystem not initialized */
    HAL_ERROR_ALREADY_INIT  = 0x0003,   /* Already initialized */
    HAL_ERROR_INVALID_ARG   = 0x0004,   /* Invalid argument */
    HAL_ERROR_NULL_PTR      = 0x0005,   /* NULL pointer passed */
    HAL_ERROR_TIMEOUT       = 0x0006,   /* Operation timed out */
    HAL_ERROR_BUSY          = 0x0007,   /* Resource busy */
    HAL_ERROR_NOT_SUPPORTED = 0x0008,   /* Feature not supported on this platform */
    HAL_ERROR_NO_MEMORY     = 0x0009,   /* Out of memory */
    HAL_ERROR_HARDWARE      = 0x000A,   /* Hardware failure */

    /* Display errors (0x01xx) */
    HAL_ERROR_DISPLAY_INIT      = 0x0100,   /* Display init failed */
    HAL_ERROR_DISPLAY_MODE      = 0x0101,   /* Invalid display mode */
    HAL_ERROR_DISPLAY_NO_FB     = 0x0102,   /* No framebuffer allocated */
    HAL_ERROR_DISPLAY_MAILBOX   = 0x0103,   /* Mailbox communication failed */

    /* GPIO errors (0x02xx) */
    HAL_ERROR_GPIO_INVALID_PIN  = 0x0200,   /* Pin number out of range */
    HAL_ERROR_GPIO_INVALID_MODE = 0x0201,   /* Invalid pin mode */

    /* Timer errors (0x03xx) */
    HAL_ERROR_TIMER_INVALID     = 0x0300,   /* Invalid timer */

    /* UART errors (0x04xx) */
    HAL_ERROR_UART_INVALID      = 0x0400,   /* Invalid UART number */
    HAL_ERROR_UART_BAUD         = 0x0401,   /* Invalid baud rate */
    HAL_ERROR_UART_OVERFLOW     = 0x0402,   /* Buffer overflow */

    /* USB errors (0x05xx) */
    HAL_ERROR_USB_INIT          = 0x0500,   /* USB init failed */
    HAL_ERROR_USB_NO_DEVICE     = 0x0501,   /* No device connected */
    HAL_ERROR_USB_ENUM          = 0x0502,   /* Enumeration failed */
    HAL_ERROR_USB_TRANSFER      = 0x0503,   /* Transfer error */
    HAL_ERROR_USB_STALL         = 0x0504,   /* Endpoint stalled */

    /* Storage errors (0x06xx) */
    HAL_ERROR_STORAGE_INIT      = 0x0600,   /* Storage init failed */
    HAL_ERROR_STORAGE_NO_CARD   = 0x0601,   /* No card inserted */
    HAL_ERROR_STORAGE_READ      = 0x0602,   /* Read error */
    HAL_ERROR_STORAGE_WRITE     = 0x0603,   /* Write error */
    HAL_ERROR_STORAGE_CRC       = 0x0604,   /* CRC error */

    /* Audio errors (0x07xx) */
    HAL_ERROR_AUDIO_INIT        = 0x0700,   /* Audio init failed */
    HAL_ERROR_AUDIO_FORMAT      = 0x0701,   /* Unsupported format */

} hal_error_t;

/* Success/failure check macros */
#define HAL_SUCCEEDED(e)    ((e) == HAL_SUCCESS)
#define HAL_FAILED(e)       ((e) != HAL_SUCCESS)
/* Alias used by older soc_init.c files */
#define HAL_OK(x)           ((x) == HAL_SUCCESS)


/* =============================================================================
 * PLATFORM IDENTIFICATION
 * =============================================================================
 *
 * hal_platform_id_t identifies which board we're running on.
 * hal_arch_t identifies the CPU architecture.
 *
 * Values are stable — SoC-specific code uses them as switch cases, and the
 * Rust implementation mirrors them with matching repr(u16)/repr(u8) enums
 * for cross-language debugging compatibility.
 */

typedef enum {
    /* Raspberry Pi family (BCM SoCs) */
    HAL_PLATFORM_RPI_ZERO2W     = 0x0001,   /* BCM2710 — rpi-zero2w-gpi */
    HAL_PLATFORM_RPI_3B         = 0x0002,   /* BCM2710 */
    HAL_PLATFORM_RPI_3BP        = 0x0003,   /* BCM2710 */
    HAL_PLATFORM_RPI_4B         = 0x0010,   /* BCM2711 */
    HAL_PLATFORM_RPI_CM4        = 0x0011,   /* BCM2711 — rpi-cm4-io */
    HAL_PLATFORM_RPI_5          = 0x0020,   /* BCM2712 */
    HAL_PLATFORM_RPI_CM5        = 0x0021,   /* BCM2712 — rpi-cm5-io */

    /* Other ARM64 boards */
    HAL_PLATFORM_RADXA_ROCK2A   = 0x0200,   /* RK3528A */

    /* ARM32 boards */
    HAL_PLATFORM_PICO2_LAFVIN = 0x3000,  /* RP2350A — pico2-lafvin */

    /* RISC-V boards */
    HAL_PLATFORM_ORANGEPI_RV2   = 0x1000,   /* SpacemiT K1 / Ky X1 — orangepi-rv2 */
    HAL_PLATFORM_MILKV_MARS     = 0x1001,   /* StarFive JH7110 — milkv-mars */

    /* x86_64 boards */
    HAL_PLATFORM_LATTEPANDA_MU  = 0x2000,   /* Intel N100 — lattepanda-mu */
    HAL_PLATFORM_LATTEPANDA_IOTA = 0x2001,  /* Intel N100 — lattepanda-iota */

    HAL_PLATFORM_UNKNOWN        = 0xFFFF,
} hal_platform_id_t;

typedef enum {
    HAL_ARCH_ARM64      = 0x01,
    HAL_ARCH_ARM32      = 0x02,
    HAL_ARCH_RISCV64    = 0x03,
    HAL_ARCH_X86_64     = 0x04,
    HAL_ARCH_UNKNOWN    = 0xFF,
} hal_arch_t;


/* =============================================================================
 * UTILITY MACROS
 * =============================================================================
 */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef UNUSED
#define UNUSED(x)           ((void)(x))
#endif

#ifndef MIN
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#endif

#define CLAMP(x, lo, hi)    MIN(MAX(x, lo), hi)

#ifndef BIT
#define BIT(n)              (1UL << (n))
#endif

#define BIT_SET(x, n)       ((x) |= BIT(n))
#define BIT_CLEAR(x, n)     ((x) &= ~BIT(n))
#define BIT_TEST(x, n)      (((x) & BIT(n)) != 0)
#define BIT_TOGGLE(x, n)    ((x) ^= BIT(n))

#ifndef ALIGN_UP
#define ALIGN_UP(x, align)  (((x) + (align) - 1) & ~((align) - 1))
#endif

#define ALIGN_DOWN(x, align)    ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align)    (((x) & ((align) - 1)) == 0)

/* Compiler attributes */
#define HAL_UNUSED              __attribute__((unused))
#define HAL_PACKED              __attribute__((packed))
#define HAL_ALIGNED(n)          __attribute__((aligned(n)))
#define HAL_WEAK                __attribute__((weak))
#define HAL_NORETURN            __attribute__((noreturn))
#define HAL_SECTION(s)          __attribute__((section(s)))

/* Force inline */
#define HAL_INLINE              static inline __attribute__((always_inline))


/* =============================================================================
 * MEMORY BARRIERS AND CPU HINTS
 * =============================================================================
 *
 * These macros provide portable access to CPU barrier and power-management
 * instructions across all supported architectures. Each architecture maps
 * to its native instructions; there is no generic fallback that silently
 * does nothing.
 *
 * ARM64
 * -----
 *   dmb sy   — Data Memory Barrier (ordering only; CPU waits for ordering,
 *              not necessarily completion).
 *   dsb sy   — Data Synchronization Barrier (completion; no subsequent
 *              instruction executes until all prior memory accesses complete).
 *   isb      — Instruction Synchronization Barrier (flushes pipeline;
 *              required after writing to system registers like SCTLR_EL1).
 *   wfe      — Wait For Event (low-power sleep until event/interrupt).
 *   wfi      — Wait For Interrupt (deeper sleep until interrupt).
 *   sev      — Send Event (wakes other cores sleeping on wfe).
 *   nop      — No-operation.
 *
 * RISC-V
 * ------
 *   RISC-V does not distinguish between ordering-only (dmb) and
 *   completion (dsb) barriers the way ARM64 does. The fence instruction
 *   provides both ordering and completion semantics simultaneously, so
 *   HAL_DMB() and HAL_DSB() compile to the same instruction. This is
 *   correct and matches what real RISC-V kernels do.
 *
 *   fence iorw, iorw — Orders ALL prior device I/O and memory accesses
 *                      before ALL subsequent ones. Covers both dmb and dsb.
 *   fence.i          — Synchronizes instruction cache with data writes.
 *                      Required after writing executable code to memory.
 *   wfi              — Wait For Interrupt (standard RV64GC).
 *   wfe              — No RISC-V equivalent; mapped to wfi (conservative).
 *   nop              — No-operation.
 *
 * x86_64
 * ------
 *   x86 uses Total Store Order (TSO), a much stronger memory model than
 *   ARM64 or RISC-V. Stores are never reordered with other stores, and
 *   loads are never reordered with other loads. In practice, the compiler
 *   memory barrier (::: "memory") is sufficient for most ordering needs.
 *
 *   mfence   — Full memory fence; orders loads and stores across the
 *              barrier in both directions. Equivalent to both dmb and dsb
 *              on ARM64. Relatively expensive on x86 — only use when
 *              you genuinely need cross-CPU ordering (e.g., before signaling
 *              another core or after writing to device-mapped memory).
 *
 *   isb equivalent — x86 has no direct ISB. Pipeline serialization is
 *              achieved with cpuid (reliable but very expensive) or implicitly
 *              via far jumps and interrupt returns. For our bare-metal use,
 *              a compiler barrier suffices — we are not JIT-compiling code
 *              or reloading segment registers.
 *
 *   hlt      — Halt until next interrupt. This is the exact semantic
 *              equivalent of ARM64's wfi. Used in the idle loop.
 *
 *   wfe/sev  — x86 has no architectural equivalent to ARM64's
 *              event register mechanism. HAL_WFE() maps to hlt (same
 *              effect: sleep until interrupt) and HAL_SEV() is a no-op.
 *              On x86, inter-core signaling uses locked memory operations
 *              and IPI interrupts, not the ARM event register pattern.
 *
 *   pause    — Spin-loop hint. Tells the CPU's branch predictor that
 *              this is a spin-wait loop, reducing power and improving
 *              performance when another hyperthread holds the resource.
 *              Equivalent to ARM64 yield and RISC-V nop-in-a-loop.
 */

#if defined(__aarch64__)

    #define HAL_DMB()   __asm__ volatile("dmb sy"   ::: "memory")
    #define HAL_DSB()   __asm__ volatile("dsb sy"   ::: "memory")
    #define HAL_ISB()   __asm__ volatile("isb"      ::: "memory")
    #define HAL_WFE()   __asm__ volatile("wfe")
    #define HAL_WFI()   __asm__ volatile("wfi")
    #define HAL_SEV()   __asm__ volatile("sev")
    #define HAL_NOP()   __asm__ volatile("nop")

#elif defined(__riscv)

    /*
     * fence iorw, iorw orders device I/O (i/o) and memory (r/w) in both
     * directions. This covers both dmb (ordering) and dsb (completion)
     * semantics — RISC-V makes no distinction between the two.
     */
    #define HAL_DMB()   __asm__ volatile("fence iorw, iorw" ::: "memory")
    #define HAL_DSB()   __asm__ volatile("fence iorw, iorw" ::: "memory")
    #define HAL_ISB()   __asm__ volatile("fence.i"          ::: "memory")
    #define HAL_WFE()   __asm__ volatile("wfi")   /* No WFE on RISC-V; WFI is closest */
    #define HAL_WFI()   __asm__ volatile("wfi")
    #define HAL_SEV()   /* No RISC-V equivalent — inter-core wake via IPI */
    #define HAL_NOP()   __asm__ volatile("nop")

#elif defined(__arm__) || defined(__thumb__)

    /* ARM32 Cortex-M: DMB, DSB, ISB are native Thumb-2 instructions */
    #define HAL_DMB()   __asm__ volatile("dmb sy" ::: "memory")
    #define HAL_DSB()   __asm__ volatile("dsb sy" ::: "memory")
    #define HAL_ISB()   __asm__ volatile("isb sy" ::: "memory")
    #define HAL_NOP()   __asm__ volatile("nop")
    #define HAL_WFI()   __asm__ volatile("wfi")
    #define HAL_WFE()   __asm__ volatile("wfe")
    #define HAL_SEV()   __asm__ volatile("sev")

#elif defined(__x86_64__)

    /*
     * x86 TSO makes explicit barriers rare. The compiler barrier (::: "memory")
     * prevents reordering at the compiler level; mfence adds a CPU-level
     * barrier for cases where the hardware might reorder (device MMIO,
     * cross-core communication). See the architecture note above for when
     * each is appropriate.
     */
    #define HAL_DMB()   __asm__ volatile("mfence" ::: "memory")
    #define HAL_DSB()   __asm__ volatile("mfence" ::: "memory")
    #define HAL_ISB()   __asm__ volatile(""       ::: "memory")  /* compiler barrier */
    #define HAL_WFE()   __asm__ volatile("hlt")   /* No WFE; HLT sleeps until interrupt */
    #define HAL_WFI()   __asm__ volatile("hlt")
    #define HAL_SEV()   /* No x86 equivalent — inter-core wake via APIC IPI */
    #define HAL_NOP()   __asm__ volatile("nop")

#else
    #error "Unsupported architecture: add barrier definitions for this target."
    /*
     * We explicitly error here rather than silently falling through to
     * empty macros. A silent fallback would compile successfully but
     * produce code with missing barriers that is subtly wrong on any
     * new architecture. Forcing an error makes porting explicit.
     */
#endif


/* =============================================================================
 * MMIO ACCESS
 * =============================================================================
 *
 * Volatile read/write for memory-mapped hardware registers.
 *
 * These are correct for ARM64, RISC-V, and x86_64 MMIO (GOP framebuffer,
 * MMIO UARTs, etc.). x86 port I/O (outb/inb) is NOT MMIO — those
 * primitives live in common/mmio.h under the x86_64 section.
 *
 * The `volatile` qualifier tells the compiler that this memory location
 * can change without the compiler's knowledge (hardware is writing to it)
 * and that every access matters (the hardware is watching). Without it:
 *   - A read result might be cached in a register across a loop
 *   - A "redundant" write might be eliminated (but the hardware saw both!)
 *   - Accesses might be reordered (hardware registers are order-sensitive)
 */

HAL_INLINE void hal_mmio_write32(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

HAL_INLINE uint32_t hal_mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

HAL_INLINE void hal_mmio_write64(uintptr_t addr, uint64_t value)
{
    *(volatile uint64_t *)addr = value;
}

HAL_INLINE uint64_t hal_mmio_read64(uintptr_t addr)
{
    return *(volatile uint64_t *)addr;
}

HAL_INLINE void hal_mmio_write16(uintptr_t addr, uint16_t value)
{
    *(volatile uint16_t *)addr = value;
}

HAL_INLINE uint16_t hal_mmio_read16(uintptr_t addr)
{
    return *(volatile uint16_t *)addr;
}

HAL_INLINE void hal_mmio_write8(uintptr_t addr, uint8_t value)
{
    *(volatile uint8_t *)addr = value;
}

HAL_INLINE uint8_t hal_mmio_read8(uintptr_t addr)
{
    return *(volatile uint8_t *)addr;
}

/* Write/read with surrounding memory barriers */
HAL_INLINE void hal_mmio_write32_mb(uintptr_t addr, uint32_t value)
{
    HAL_DMB();
    hal_mmio_write32(addr, value);
    HAL_DMB();
}

HAL_INLINE uint32_t hal_mmio_read32_mb(uintptr_t addr)
{
    HAL_DMB();
    uint32_t val = hal_mmio_read32(addr);
    HAL_DMB();
    return val;
}


/* =============================================================================
 * VOLATILE MEMORY ACCESS
 * =============================================================================
 * For framebuffer and shared memory access where volatile semantics
 * are needed without a uintptr_t cast at the call site.
 */

#define READ_VOLATILE(ptr)          (*(volatile typeof(*(ptr)) *)(ptr))
#define WRITE_VOLATILE(ptr, val)    (*(volatile typeof(*(ptr)) *)(ptr) = (val))


#endif /* HAL_TYPES_H */