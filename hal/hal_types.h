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

/* Boolean - matches types.h */
typedef _Bool bool;
#define true  1
#define false 0

/* NULL pointer */
#ifndef NULL
#define NULL ((void *)0)
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
    HAL_ERROR_USB_ENUM_FAILED   = 0x0502,   /* Enumeration failed */
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

/* Error checking macros */
#define HAL_OK(x)       ((x) == HAL_SUCCESS)
#define HAL_FAILED(x)   ((x) != HAL_SUCCESS)

/* =============================================================================
 * PLATFORM IDENTIFICATION
 * =============================================================================
 */

typedef enum {
    /* Raspberry Pi family */
    HAL_PLATFORM_RPI_ZERO2W     = 0x0001,   /* BCM2710 */
    HAL_PLATFORM_RPI_3B         = 0x0002,   /* BCM2710 */
    HAL_PLATFORM_RPI_3BP        = 0x0003,   /* BCM2710 */
    HAL_PLATFORM_RPI_4B         = 0x0010,   /* BCM2711 */
    HAL_PLATFORM_RPI_CM4        = 0x0011,   /* BCM2711 */
    HAL_PLATFORM_RPI_5          = 0x0020,   /* bcm2712 */
    HAL_PLATFORM_RPI_CM5        = 0x0021,   /* bcm2712 */

    /* Other ARM64 boards */
    HAL_PLATFORM_LIBRE_POTATO   = 0x0100,   /* S905X */
    HAL_PLATFORM_RADXA_ROCK2A   = 0x0200,   /* RK3528A */
    HAL_PLATFORM_KICKPI_K2B     = 0x0300,   /* H618 */

    /* RISC-V boards */
    HAL_PLATFORM_ORANGEPI_RV2   = 0x1000,   /* SpacemiT K1 */

    HAL_PLATFORM_UNKNOWN        = 0xFFFF,
} hal_platform_id_t;

typedef enum {
    HAL_ARCH_ARM64      = 0x01,
    HAL_ARCH_ARM32      = 0x02,
    HAL_ARCH_RISCV64    = 0x03,
    HAL_ARCH_UNKNOWN    = 0xFF,
} hal_arch_t;

/* =============================================================================
 * UTILITY MACROS
 * =============================================================================
 * These match/extend existing types.h macros.
 */

/* Array size - matches ARRAY_SIZE */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#endif

/* Unused parameter - matches UNUSED */
#ifndef UNUSED
#define UNUSED(x)           ((void)(x))
#endif

/* Min/Max - matches MIN/MAX */
#ifndef MIN
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#endif

/* Clamp value to range */
#define CLAMP(x, lo, hi)    MIN(MAX(x, lo), hi)

/* Bit operations - matches BIT */
#ifndef BIT
#define BIT(n)              (1UL << (n))
#endif

#define BIT_SET(x, n)       ((x) |= BIT(n))
#define BIT_CLEAR(x, n)     ((x) &= ~BIT(n))
#define BIT_TEST(x, n)      (((x) & BIT(n)) != 0)
#define BIT_TOGGLE(x, n)    ((x) ^= BIT(n))

/* Alignment - matches ALIGN_UP */
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
 * MEMORY BARRIERS
 * =============================================================================
 * These match existing mmio.h barrier definitions.
 * Architecture-specific implementations.
 */

#if defined(__aarch64__)
    /* ARM64 barriers */
    #define HAL_DMB()   __asm__ volatile("dmb sy" ::: "memory")
    #define HAL_DSB()   __asm__ volatile("dsb sy" ::: "memory")
    #define HAL_ISB()   __asm__ volatile("isb" ::: "memory")
    #define HAL_WFE()   __asm__ volatile("wfe")
    #define HAL_WFI()   __asm__ volatile("wfi")
    #define HAL_SEV()   __asm__ volatile("sev")
    #define HAL_NOP()   __asm__ volatile("nop")
#elif defined(__riscv)
    /* RISC-V barriers */
    #define HAL_DMB()   __asm__ volatile("fence rw, rw" ::: "memory")
    #define HAL_DSB()   __asm__ volatile("fence rw, rw" ::: "memory")
    #define HAL_ISB()   __asm__ volatile("fence.i" ::: "memory")
    #define HAL_WFE()   __asm__ volatile("wfi")
    #define HAL_WFI()   __asm__ volatile("wfi")
    #define HAL_SEV()   /* No SEV equivalent on RISC-V */
    #define HAL_NOP()   __asm__ volatile("nop")
#else
    /* Fallback - compiler barrier only */
    #define HAL_DMB()   __asm__ volatile("" ::: "memory")
    #define HAL_DSB()   __asm__ volatile("" ::: "memory")
    #define HAL_ISB()   __asm__ volatile("" ::: "memory")
    #define HAL_WFE()
    #define HAL_WFI()
    #define HAL_SEV()
    #define HAL_NOP()
#endif

/* =============================================================================
 * MMIO ACCESS
 * =============================================================================
 * Volatile read/write for hardware registers.
 * These match existing mmio_read/mmio_write from common/mmio.h
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

/* Write with memory barrier (ensures ordering) */
HAL_INLINE void hal_mmio_write32_mb(uintptr_t addr, uint32_t value)
{
    HAL_DMB();
    hal_mmio_write32(addr, value);
    HAL_DMB();
}

/* Read with memory barrier */
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
 * For framebuffer and shared memory access.
 * Matches existing READ_VOLATILE/WRITE_VOLATILE pattern.
 */

#define HAL_READ_VOLATILE(ptr)          (*(volatile typeof(*(ptr)) *)(ptr))
#define HAL_WRITE_VOLATILE(ptr, val)    (*(volatile typeof(*(ptr)) *)(ptr) = (val))

#endif /* HAL_TYPES_H */
