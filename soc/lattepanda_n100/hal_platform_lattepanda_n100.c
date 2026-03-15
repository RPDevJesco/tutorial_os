/*
 * soc/lattepanda_n100/hal_platform_lattepanda_n100.c — HAL Platform Bridge
 *
 * Tutorial-OS: LattePanda N100 (x86_64 / UEFI) HAL Implementation
 *
 * This file is the x86_64 equivalent of hal_platform_jh7110.c and
 * hal_platform_kyx1.c. It implements the unified hal_platform_* API
 * so that kernel/main.c runs without a single #ifdef regardless of
 * whether it is running on BCM2712, JH7110, or Intel N100.
 *
 * PLATFORM INFO SOURCES (x86_64):
 *   - CPU info:     CPUID instruction (leaves 0x00, 0x01, 0x16)
 *   - Temperature:  IA32_THERM_STATUS MSR (0x19C) — per-core
 *                   IA32_PACKAGE_THERM_STATUS MSR (0x1B1) — package
 *   - Clock rates:  CPUID leaf 0x16 (base/max/bus MHz) or MSRs
 *   - Power:        Peripherals are always-on post-UEFI; report present
 *   - Memory:       No GPU split on x86_64 — all RAM is CPU-accessible
 *
 * WHAT THIS FILE DOES NOT DO:
 *   - ACPI table parsing (would require significant runtime infrastructure)
 *   - HPET or APIC setup (not needed for Tutorial-OS educational demo)
 *   - Runtime MSR access (requires ring 0, which we are in, but kept minimal)
 *
 * The same hal_platform.h contract is satisfied here with x86_64-specific
 * implementations. Readers see identical API calls in main.c regardless
 * of platform — that is the HAL's entire purpose.
 */

#include "hal/hal_types.h"
#include "hal/hal_platform.h"
#include "hal/hal_timer.h"
#include "hal/hal_gpio.h"
#include "drivers/framebuffer/framebuffer.h"

/* ============================================================
 * External declarations
 * ============================================================ */

/* uart_8250.c */
extern void n100_uart_putc(char c);
extern void n100_uart_puts(const char *s);

/* display_gop.c */
extern void n100_display_present(framebuffer_t *fb);
extern uint32_t n100_display_get_pixel_format(void);
extern uint64_t n100_display_get_fb_base(void);

/* ============================================================
 * Module state
 * ============================================================ */

static bool g_platform_initialized = false;

/* Cached from CPUID at init time */
static uint32_t g_cpuid_max_leaf  = 0;
static uint32_t g_cpu_family      = 0;
static uint32_t g_cpu_model       = 0;
static uint32_t g_cpu_stepping    = 0;
static uint32_t g_cpu_base_mhz   = 0;  /* From CPUID leaf 0x16 */
static uint32_t g_cpu_max_mhz    = 0;

/* N100 confirmed constants (AlderLake ULX, CPUID 0xB06E0) */
#define N100_NUM_CORES          4
#define N100_NUM_THREADS        4   /* E-cores: 1 thread each, no HT */
#define N100_THERMAL_MAX_MC     105000  /* 105 °C Tj-max in milli-Celsius */
#define N100_TIMER_FREQ_HZ      1000000UL  /* TSC-based 1 MHz reference */

/* ============================================================
 * CPUID helpers
 * ============================================================ */

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

static inline void cpuid_ex(uint32_t leaf, uint32_t subleaf,
                             uint32_t *eax, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

/*
 * rdmsr — Read a Model Specific Register.
 *
 * MSR access requires ring 0 (CPL=0). We are executing in ring 0 here
 * (UEFI runs with CPL=0, ExitBootServices does not change privilege level).
 * This is safe as long as the MSR address is valid for this CPU.
 */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile (
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(msr)
    );
    return ((uint64_t)hi << 32) | lo;
}

/* ============================================================
 * Platform initialization
 * ============================================================ */

/*
 * n100_hal_platform_init — Cache CPUID data for later HAL queries.
 *
 * Called from soc_init.c AFTER ExitBootServices. No UEFI calls here.
 */
void n100_hal_platform_init(void)
{
    uint32_t eax, ebx, ecx, edx;

    /* Leaf 0: max basic leaf */
    cpuid(0x00, &g_cpuid_max_leaf, &ebx, &ecx, &edx);

    /* Leaf 1: family/model/stepping */
    if (g_cpuid_max_leaf >= 1) {
        cpuid(0x01, &eax, &ebx, &ecx, &edx);

        uint32_t family   = (eax >> 8)  & 0xF;
        uint32_t ext_fam  = (eax >> 20) & 0xFF;
        uint32_t model    = (eax >> 4)  & 0xF;
        uint32_t ext_mod  = (eax >> 16) & 0xF;

        if (family == 0xF) family += ext_fam;
        if (family == 0x6 || family == 0xF) model |= (ext_mod << 4);

        g_cpu_family   = family;
        g_cpu_model    = model;
        g_cpu_stepping = eax & 0xF;
    }

    /*
     * Leaf 0x16: CPU frequency information
     *   EAX[15:0] = Base clock in MHz
     *   EBX[15:0] = Maximum clock in MHz
     *   ECX[15:0] = Bus/reference clock in MHz
     *
     * Available on Skylake and later (Intel N100 is AlderLake — supported).
     * Returns 0 if firmware didn't populate it (uncommon on modern UEFI).
     */
    if (g_cpuid_max_leaf >= 0x16) {
        cpuid(0x16, &eax, &ebx, &ecx, &edx);
        g_cpu_base_mhz = eax & 0xFFFF;
        g_cpu_max_mhz  = ebx & 0xFFFF;
    }

    /*
     * Fallback: if CPUID 0x16 returns 0, use known N100 frequency.
     * The Intel N100 base clock is 800 MHz, max is 3.4 GHz.
     */
    if (g_cpu_base_mhz == 0) g_cpu_base_mhz = 800;
    if (g_cpu_max_mhz  == 0) g_cpu_max_mhz  = 3400;

    g_platform_initialized = true;

    n100_uart_puts("HAL: platform init done  family=0x");
    /* Inline hex — can't use puthex32 without pulling it in here */
    static const char h[] = "0123456789ABCDEF";
    n100_uart_putc(h[(g_cpu_family >> 4) & 0xF]);
    n100_uart_putc(h[g_cpu_family & 0xF]);
    n100_uart_puts("  model=0x");
    n100_uart_putc(h[(g_cpu_model >> 4) & 0xF]);
    n100_uart_putc(h[g_cpu_model & 0xF]);
    n100_uart_puts("\r\n");
}

/* ============================================================
 * hal_platform.h implementation
 * ============================================================ */

hal_error_t hal_platform_early_init(void)
{
    /*
     * On x86_64/UEFI, early init is done before efi_main() is called.
     * The firmware configured GDT, IDT, paging, and memory.
     * Nothing to do here.
     */
    return HAL_SUCCESS;
}

hal_error_t hal_platform_init(void)
{
    if (!g_platform_initialized) {
        n100_hal_platform_init();
    }
    return HAL_SUCCESS;
}

bool hal_platform_is_initialized(void)
{
    return g_platform_initialized;
}

hal_error_t hal_platform_get_info(hal_platform_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    info->platform_id   = HAL_PLATFORM_LATTEPANDA_MU;
    info->arch          = HAL_ARCH_X86_64;
    info->board_name    = "LattePanda MU";
    info->soc_name      = "Intel Alder Lake-N (N100)";
    info->board_revision = 0;
    info->serial_number  = 0;

    return HAL_SUCCESS;
}

hal_platform_id_t hal_platform_get_id(void)
{
    return HAL_PLATFORM_LATTEPANDA_MU;
}

const char *hal_platform_get_board_name(void)
{
    return "LattePanda MU";
}

const char *hal_platform_get_soc_name(void)
{
    return "Intel Alder Lake-N (N100)";
}

/* ============================================================
 * Memory information
 * ============================================================ */

/*
 * __ram_base/__ram_size are set by soc_init.c:populate_ram_globals()
 * from the EFI memory map after ExitBootServices.
 */
extern uint64_t __ram_base;
extern uint64_t __ram_size;

hal_error_t hal_platform_get_memory_info(hal_memory_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    info->arm_base        = (uintptr_t)__ram_base;
    info->arm_size        = (size_t)__ram_size;
    info->gpu_base        = 0;   /* No GPU split on x86_64 */
    info->gpu_size        = 0;
    info->peripheral_base = 0;   /* No single peripheral base — MMIO via ACPI */

    return HAL_SUCCESS;
}

size_t hal_platform_get_arm_memory(void)  { return (size_t)__ram_size; }
size_t hal_platform_get_total_memory(void){ return (size_t)__ram_size; }

/* ============================================================
 * Clock rates
 * ============================================================ */

uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id)
{
    switch (clock_id) {
        case HAL_CLOCK_ARM:
        case HAL_CLOCK_CORE:
            /* Return base clock in Hz */
            return g_cpu_base_mhz * 1000000UL;

        case HAL_CLOCK_UART:
            /* 8250 UART uses 1.8432 MHz reference */
            return 1843200UL;

        case HAL_CLOCK_EMMC:
            /* SD card via PCIe SD host — UEFI initialized */
            return 100000000UL;  /* 100 MHz typical */

        case HAL_CLOCK_PIXEL:
            /* Display pixel clock — we don't know without EDID/ACPI */
            return 0;

        default:
            return 0;
    }
}

uint32_t hal_platform_get_arm_freq(void)
{
    return g_cpu_base_mhz * 1000000UL;
}

uint32_t hal_platform_get_arm_freq_measured(void)
{
    /* x86_64 doesn't have a cheap "current freq" oracle without ring-0 MSRs.
     * Return base clock — good enough for the tutorial display. */
    return hal_platform_get_arm_freq();
}

hal_error_t hal_platform_get_clock_info(hal_clock_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;
    info->arm_freq_hz  = hal_platform_get_clock_rate(HAL_CLOCK_ARM);
    info->core_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_CORE);
    info->uart_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_UART);
    info->emmc_freq_hz = hal_platform_get_clock_rate(HAL_CLOCK_EMMC);
    return HAL_SUCCESS;
}

/* ============================================================
 * Temperature
 * ============================================================ */

hal_error_t hal_platform_get_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return HAL_ERROR_NULL_PTR;

	/* IA32_PACKAGE_THERM_STATUS */
    uint64_t msr_val = rdmsr(0x1B1);
    uint32_t readout = (uint32_t)((msr_val >> 16) & 0x7F);
    int32_t  temp_c  = (int32_t)105 - (int32_t)readout;

    *temp_mc = temp_c * 1000;
    return HAL_SUCCESS;
}

int32_t hal_platform_get_temp_celsius(void)
{
    int32_t mc;
    if (HAL_OK(hal_platform_get_temperature(&mc))) return mc / 1000;
    return -1;
}

hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return HAL_ERROR_NULL_PTR;
    *temp_mc = N100_THERMAL_MAX_MC;
    return HAL_SUCCESS;
}

/* ============================================================
 * Power management
 * ============================================================ */

/*
 * hal_platform_get_power — Report peripheral power state.
 *
 * After ExitBootServices, UEFI has already initialized and powered all
 * peripherals. We report them as active. A production kernel would
 * query ACPI power namespaces, but that's beyond Tutorial-OS scope.
 */
hal_error_t hal_platform_get_power(hal_device_id_t device, bool *enabled)
{
    if (!enabled) return HAL_ERROR_NULL_PTR;

    switch (device) {
        case HAL_DEVICE_UART0:
        case HAL_DEVICE_USB:
        case HAL_DEVICE_SD_CARD:
            *enabled = true;
            return HAL_SUCCESS;

        case HAL_DEVICE_I2C0:
        case HAL_DEVICE_I2C1:
        case HAL_DEVICE_I2C2:
        case HAL_DEVICE_SPI:
        case HAL_DEVICE_PWM:
        default:
            *enabled = false;
            return HAL_SUCCESS;
    }
}

hal_error_t hal_platform_set_power(hal_device_id_t device, bool enabled)
{
    (void)device;
    (void)enabled;
    /* Power management via ACPI not implemented — stubs for HAL contract */
    return HAL_ERROR_NOT_SUPPORTED;
}

/* ============================================================
 * Throttle status
 * ============================================================ */

/*
 * hal_platform_get_throttle_status — Read thermal throttle state via MSR.
 *
 * IA32_PACKAGE_THERM_STATUS (MSR 0x1B1):
 *   Bit 0  = thermal status (1 = throttling active)
 *   Bit 10 = PROCHOT log
 *
 * Maps to the same HAL_THROTTLE_* flag convention as BCM mailbox returns,
 * so kernel/main.c throttle rendering works identically on all platforms.
 */
hal_error_t hal_platform_get_throttle_status(uint32_t *status)
{
    if (!status) return HAL_ERROR_NULL_PTR;

    uint64_t msr_val = rdmsr(0x1B1);
    uint32_t flags = 0;

    if (msr_val & (1U << 0))  flags |= HAL_THROTTLE_THROTTLED_NOW;
    if (msr_val & (1U << 10)) flags |= HAL_THROTTLE_THROTTLE_OCCURRED;

    *status = flags;
    return HAL_SUCCESS;
}

bool hal_platform_is_throttled(void)
{
    uint32_t status = 0;
    hal_platform_get_throttle_status(&status);
    return (status & HAL_THROTTLE_THROTTLED_NOW) != 0;
}

/* ============================================================
 * System control
 * ============================================================ */

hal_error_t hal_platform_reboot(void)
{
    /*
     * x86_64 reset via port 0xCF9:
     *   Write 0x06 to trigger a full cold reset.
     * This is the standard ACPI-independent reset path, works on all
     * x86_64 hardware. The UEFI Runtime Service ResetSystem would also
     * work but requires the Runtime Services table to still be valid.
     */
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"((uint8_t)0x06), "Nd"((uint16_t)0xCF9)
    );
    while (1) __asm__ volatile ("hlt");
    return HAL_SUCCESS;
}

hal_error_t hal_platform_shutdown(void)
{
    /*
     * x86_64 shutdown via ACPI SLP_TYP + SLP_EN writes to PM1a_CNT.
     * The correct port depends on the FADT ACPI table which we haven't
     * parsed. Use halt as a safe fallback — this is an educational OS.
     */
    n100_uart_puts("HAL: shutdown requested — halting\r\n");
    while (1) __asm__ volatile ("hlt");
    return HAL_SUCCESS;
}

void hal_panic(const char *msg)
{
    n100_uart_puts("\r\n[PANIC] ");
    n100_uart_puts(msg ? msg : "(null)");
    n100_uart_puts("\r\n[PANIC] System halted\r\n");
    while (1) __asm__ volatile ("hlt");
}

/* ============================================================
 * Debug output
 * ============================================================ */

void hal_debug_putc(char c) { n100_uart_putc(c); }
void hal_debug_puts(const char *s) { n100_uart_puts(s); }

void hal_printf(const char *fmt, ...)
{
    /* Minimal stub — not needed for Tutorial-OS display demo */
    (void)fmt;
}

/* ============================================================
 * Timer / delay
 * ============================================================ */

/*
 * delay_ms — busy-wait using TSC.
 *
 * We don't have a calibrated timer before ACPI/HPET setup, so we use
 * CPUID leaf 0x16 base frequency (cached at init) to estimate cycles.
 * Good enough for Tutorial-OS boot delays; not a precision timer.
 */
void delay_ms(uint32_t ms)
{
    if (g_cpu_base_mhz == 0) {
        /* Pre-init fallback: spin a fixed count */
        volatile uint64_t i;
        for (i = 0; i < (uint64_t)ms * 100000ULL; i++) {
            __asm__ volatile ("pause");
        }
        return;
    }

    uint64_t cycles_per_ms = (uint64_t)g_cpu_base_mhz * 1000ULL;
    uint64_t start, now, delta;

    __asm__ volatile ("rdtsc; shl $32, %%rdx; or %%rdx, %%rax"
                      : "=a"(start) : : "rdx");

    do {
        __asm__ volatile ("pause");
        __asm__ volatile ("rdtsc; shl $32, %%rdx; or %%rdx, %%rax"
                          : "=a"(now) : : "rdx");
        delta = now - start;
    } while (delta < cycles_per_ms * (uint64_t)ms);
}

void delay_us(uint32_t us)
{
    delay_ms(1);   /* Minimum granularity; fine for boot-time use */
    (void)us;
}

/* ============================================================
 * GPIO / DPI stubs — x86_64 has no DPI parallel display bus
 * ============================================================ */

/*
 * hal_gpio_configure_dpi — stub for x86_64.
 *
 * On BCM2710/2711, DPI is a parallel RGB interface driven through GPIO
 * pins. kernel/main.c calls this to set up the display GPIO mux.
 * On x86_64/UEFI, the display is a GOP framebuffer — no GPIO involved.
 * This stub satisfies the linker and returns success.
 */
hal_error_t hal_gpio_configure_dpi(void)
{
    return HAL_SUCCESS;
}

hal_error_t hal_gpio_init(void)     { return HAL_SUCCESS; }
hal_error_t hal_gpio_deinit(void)   { return HAL_SUCCESS; }

hal_error_t hal_timer_init(void)
{
    /* TSC is always running on N100 — nothing to initialize */
    return HAL_SUCCESS;
}

uint64_t hal_timer_get_ticks(void)
{
    uint64_t tsc;
    __asm__ volatile ("rdtsc; shl $32, %%rdx; or %%rdx, %%rax"
                      : "=a"(tsc) : : "rdx");
    return tsc;
}

uint64_t hal_timer_get_freq(void)
{
    return (uint64_t)g_cpu_base_mhz * 1000000ULL;
}

uint32_t hal_timer_get_us(void)
{
    if (g_cpu_base_mhz == 0) return 0;
    return (uint32_t)(hal_timer_get_ticks() / (uint64_t)g_cpu_base_mhz);
}

/*
 * fb_init — HAL display init entry point called from kernel/main.c.
 *
 * On x86_64/UEFI, the framebuffer is already initialized by the time
 * kernel_main() is called — n100_display_init() ran in efi_main() before
 * ExitBootServices. The *fb pointer was populated then and passed through.
 *
 * This function is a no-op: just return success since the framebuffer is
 * already live. Contrast with BCM (mailbox request) and JH7110 (DTB parse)
 * where fb_init() does the real work.
 */
bool fb_init(framebuffer_t *fb)
{
    /* fb was populated in soc_init.c:efi_main() — already valid */
    return (fb != NULL && fb->initialized);
}

bool fb_init_with_size(framebuffer_t *fb, uint32_t width, uint32_t height)
{
    /* x86_64: dimensions come from GOP, ignore hints */
    (void)width;
    (void)height;
    return fb_init(fb);
}

hal_error_t hal_display_init(framebuffer_t **fb_out)
{
    /* fb was set up in soc_init.c — nothing to do */
    (void)fb_out;
    return HAL_SUCCESS;
}

hal_error_t hal_display_present(framebuffer_t *fb)
{
    n100_display_present(fb);
    return HAL_SUCCESS;
}