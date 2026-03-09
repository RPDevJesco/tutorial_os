/*
 * hal_platform_jh7110.c — HAL Platform Implementation for StarFive JH-7110
 * ==========================================================================
 *
 * This file implements the hal_platform_* contract so kernel/main.c runs
 * unchanged on the Milk-V Mars — the same main.c that runs on BCM2710 and
 * Ky X1 works here without a single #ifdef.
 *
 *   main.c calls hal_platform_get_info()
 *     → BCM2710: queries VideoCore mailbox
 *     → Ky X1:   SBI CSR reads + PMIC SPM8821
 *     → JH7110:  SBI CSR reads + PMIC AXP15060   (THIS FILE)
 *
 * DRIVER INTEGRATION MAP:
 * -----------------------
 *
 *   ┌─────────────────────────────────────────────────┐
 *   │  hal_platform_jh7110.c (this file — HAL glue)   │
 *   ├─────────────────────────────────────────────────┤
 *   │  drivers/sbi.c          │ CPU CSR + SBI calls   │
 *   │  - mvendorid=0x489      │ SiFive JEDEC ID       │
 *   │  - marchid=U74          │ Core identification   │
 *   │  - CPU freq measure     │ rdcycle/rdtime ratio  │
 *   ├─────────────────────────┼───────────────────────┤
 *   │  drivers/pmic_axp15060.c│ AXP15060 via I2C6    │
 *   │  - chip_id = 0x50       │ Board identification  │
 *   │  - NTC temp via GPADC   │ Temperature sensor    │
 *   │  - DCDC voltage status  │ Power monitoring      │
 *   ├─────────────────────────┼───────────────────────┤
 *   │  drivers/i2c.c          │ DW I2C6 controller   │
 *   │  - 100 kHz standard     │ Polled byte transfers │
 *   └─────────────────────────┴───────────────────────┘
 *
 * COMPARED TO hal_platform_kyx1.c:
 *   - hal_platform_get_info():  platform_id → HAL_PLATFORM_MILKV_MARS
 *   - hal_platform_get_temperature(): uses axp15060_get_temperature()
 *                                     instead of spm8821_get_temperature()
 *   - hal_platform_get_memory_info(): no DPU reserved region to report
 *   - hal_platform_get_arm_freq():    1.5 GHz nominal (vs 1.6 GHz)
 *   - Everything else: structurally identical
 */

#include "types.h"
#include "hal/hal_platform.h"
#include "hal/hal_types.h"
#include "hal/hal_gpio.h"
#include "jh7110_regs.h"
#include "drivers/sbi.h"
#include "drivers/pmic_axp15060.h"

/* boot/riscv64/common_init.S */
extern uint64_t __dtb_ptr;
extern uint64_t __boot_hart_id;

/* uart.c */
extern void jh7110_uart_init_hw(void);
extern void jh7110_uart_putc(char c);
extern void jh7110_uart_puts(const char *str);
extern void jh7110_uart_puthex(uint64_t val);
extern void jh7110_uart_putdec(uint32_t val);

/* timer.c */
extern void delay_ms(uint32_t ms);
extern uint32_t jh7110_measure_cpu_freq(uint32_t calibration_ms);

/* gpio.c */
extern void jh7110_gpio_init_heartbeat_led(void);
extern void jh7110_gpio_set_led(bool on);

/* display_simplefb.c */
extern bool jh7110_display_init(void *fb, const void *dtb);
extern void jh7110_display_present(void *fb);

/* soc_init.c */
extern bool jh7110_soc_init(void *fb);

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static bool g_platform_initialized = false;
static jh7110_cpu_info_t g_cpu_info;
static uint32_t g_measured_cpu_freq_hz = 0;

/* Known constants */
#define JH7110_TIMER_FREQ_HZ_CONST  24000000UL
#define JH7110_THERMAL_MAX_MC_CONST 105000
#define JH7110_NUM_CORES_CONST      4

/* =============================================================================
 * PLATFORM INITIALIZATION
 * =============================================================================
 */

hal_error_t hal_platform_early_init(void)
{
    /*
     * Early init handled by common_init.S (shared with kyx1):
     *   - Stack setup
     *   - BSS zero
     *   - FP enable (fsrm/fssr)
     *   - Trap vector (stvec = trap_vector)
     *   - DTB pointer saved to __dtb_ptr
     * Nothing to do here.
     */
    return HAL_SUCCESS;
}

hal_error_t hal_platform_init(void)
{
    if (g_platform_initialized) return HAL_SUCCESS;

    /* Initialize UART (SBI fallback already works from entry.S) */
    jh7110_uart_init_hw();
    jh7110_uart_puts("[hal] JH7110 platform init\n");

    /* Read CPU identification via SBI */
    jh7110_sbi_get_cpu_info(&g_cpu_info);

    jh7110_uart_puts("[hal] mvendorid=0x");
    jh7110_uart_puthex(g_cpu_info.mvendorid);
    jh7110_uart_puts(" marchid=0x");
    jh7110_uart_puthex(g_cpu_info.marchid);
    jh7110_uart_puts(" → ");
    jh7110_uart_puts(g_cpu_info.core_name);
    jh7110_uart_putc('\n');

    /* Measure CPU frequency */
    uint32_t freq_mhz = jh7110_measure_cpu_freq(10);
    g_measured_cpu_freq_hz = freq_mhz * 1000000UL;
    jh7110_uart_puts("[hal] CPU freq: ");
    jh7110_uart_putdec(freq_mhz);
    jh7110_uart_puts(" MHz\n");

    /* Initialize PMIC */
    axp15060_init();

    /* Initialize heartbeat LED (no-op on Mars, but API is uniform) */
    jh7110_gpio_init_heartbeat_led();
    jh7110_gpio_set_led(true);

    g_platform_initialized = true;
    return HAL_SUCCESS;
}

bool hal_platform_is_initialized(void)
{
    return g_platform_initialized;
}

/* =============================================================================
 * PLATFORM INFORMATION
 * =============================================================================
 */

hal_platform_id_t hal_platform_get_id(void)
{
    return HAL_PLATFORM_MILKV_MARS;
}

const char *hal_platform_get_board_name(void)
{
    return "Milk-V Mars";
}

const char *hal_platform_get_soc_name(void)
{
    return "StarFive JH7110";
}

hal_error_t hal_platform_get_info(hal_platform_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    info->platform_id    = HAL_PLATFORM_MILKV_MARS;
    info->arch           = HAL_ARCH_RISCV64;
    info->board_name     = "Milk-V Mars";
    info->soc_name       = "StarFive JH7110";

    /*
     * Use marchid as board_revision and combine mvendorid+mimpid as
     * serial_number — same approach as kyx1. RISC-V M-mode CSRs are
     * the closest analog to ARM's board revision registers.
     */
    info->board_revision = (uint32_t)g_cpu_info.marchid;
    info->serial_number  = ((uint64_t)g_cpu_info.mvendorid << 32) |
                            g_cpu_info.mimpid;

    return HAL_SUCCESS;
}

/* =============================================================================
 * MEMORY INFORMATION
 * =============================================================================
 */

hal_error_t hal_platform_get_memory_info(hal_memory_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    /*
     * JH7110 has no ARM/GPU memory split. All 8 GB is CPU-accessible.
     * The display framebuffer is dynamically allocated by U-Boot at
     * a high address and reported via DTB — we don't reserve a fixed
     * region like the Ky X1 does for its DPU at 0x2FF40000.
     */
    info->arm_base        = 0x40000000UL;        /* DRAM starts here */
    info->arm_size        = JH7110_TOTAL_RAM;    /* 8 GB */
    info->gpu_base        = 0;                   /* No dedicated GPU memory */
    info->gpu_size        = 0;
    info->peripheral_base = JH7110_PERI_BASE;    /* 0x10000000 */

    return HAL_SUCCESS;
}

size_t hal_platform_get_arm_memory(void)
{
    return (size_t)JH7110_TOTAL_RAM;
}

size_t hal_platform_get_total_memory(void)
{
    return (size_t)JH7110_TOTAL_RAM;
}

/* =============================================================================
 * CLOCK INFORMATION
 * =============================================================================
 */

hal_error_t hal_platform_get_clock_info(hal_clock_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    info->arm_freq_hz  = g_measured_cpu_freq_hz > 0
                         ? g_measured_cpu_freq_hz
                         : JH7110_CPU_FREQ_HZ;
    info->core_freq_hz = 0;                    /* No separate "core" clock */
    info->uart_freq_hz = JH7110_TIMER_FREQ_HZ; /* UART ref = 24 MHz OSC */
    info->emmc_freq_hz = 0;                    /* Not yet queried */

    return HAL_SUCCESS;
}

uint32_t hal_platform_get_arm_freq(void)
{
    return g_measured_cpu_freq_hz > 0
           ? g_measured_cpu_freq_hz
           : JH7110_CPU_FREQ_HZ;
}

uint32_t hal_platform_get_arm_freq_measured(void)
{
    if (g_measured_cpu_freq_hz > 0) return g_measured_cpu_freq_hz;

    uint32_t freq_mhz = jh7110_measure_cpu_freq(10);
    if (freq_mhz > 0) {
        g_measured_cpu_freq_hz = freq_mhz * 1000000UL;
        return g_measured_cpu_freq_hz;
    }
    return JH7110_CPU_FREQ_HZ;
}

/* =============================================================================
 * TEMPERATURE MONITORING
 * =============================================================================
 *
 * Uses the AXP15060 PMIC NTC thermistor for temperature readings.
 * This is cleaner than the Ky X1 situation (SPM8821 is poorly documented).
 * The AXP15060 has a proper GPADC channel for the NTC measurement.
 *
 * TEACHING POINT:
 *   Both kyx1 and jh7110 have hal_platform_get_temperature().
 *   Both ultimately read from a PMIC over I2C.
 *   The PMIC models differ, but the HAL contract is identical:
 *     - Returns millicelsius on success
 *     - Returns HAL_ERROR_NOT_SUPPORTED if PMIC isn't ready
 *   This is the HAL doing its job: isolating the difference.
 */

hal_error_t hal_platform_get_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return HAL_ERROR_NULL_PTR;

    if (!axp15060_is_available()) {
        *temp_mc = 0;
        return HAL_ERROR_NOT_SUPPORTED;
    }

    int result = axp15060_get_temperature(temp_mc);
    if (result != 0) {
        *temp_mc = 0;
        return HAL_ERROR_HARDWARE;
    }

    return HAL_SUCCESS;
}

int32_t hal_platform_get_temp_celsius(void)
{
    int32_t temp_mc;
    if (HAL_FAILED(hal_platform_get_temperature(&temp_mc))) return -1;
    return temp_mc / 1000;
}

hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return HAL_ERROR_NULL_PTR;
    *temp_mc = JH7110_THERMAL_MAX_MC;
    return HAL_SUCCESS;
}

/* =============================================================================
 * THROTTLING STATUS
 * =============================================================================
 */

hal_error_t hal_platform_get_throttle_status(uint32_t *flags)
{
    if (!flags) return HAL_ERROR_NULL_PTR;
    /*
     * JH7110 has no VideoCore-style throttle reporting.
     * The AXP15060 handles thermal shutdown independently.
     * Report no throttle until drivers/pmic is used.
     */
    *flags = 0;
    return HAL_SUCCESS;
}

bool hal_platform_is_throttled(void)
{
    return false;
}

/* =============================================================================
 * DISPLAY INITIALIZATION (HAL bridge)
 * =============================================================================
 */

#include "framebuffer.h"

bool fb_init(framebuffer_t *fb)
{
    const void *dtb = (const void *)(uintptr_t)__dtb_ptr;

    /* Print DTB ptr nibble by nibble via SBI — works before UART init */
    uint64_t d = (uint64_t)(uintptr_t)dtb;
    register long a7 asm("a7") = 1;
    register long a0 asm("a0") = 'D'; asm volatile("ecall"::"r"(a7),"r"(a0));
    for (int i = 60; i >= 0; i -= 4) {
        int nib = (d >> i) & 0xF;
        a0 = nib < 10 ? '0' + nib : 'a' + nib - 10;
        asm volatile("ecall"::"r"(a7),"r"(a0));
    }
    a0 = '\n'; asm volatile("ecall"::"r"(a7),"r"(a0));

    return jh7110_display_init((void *)fb, dtb);
}

bool fb_init_with_size(framebuffer_t *fb, uint32_t width, uint32_t height)
{
    /* JH7110: dimensions come from DTB, ignore the hints */
    (void)width;
    (void)height;
    return fb_init(fb);
}

/* =============================================================================
 * PANIC
 * =============================================================================
 */

hal_error_t hal_platform_reboot(void)
{
    sbi_reboot();
    return HAL_SUCCESS;
}

hal_error_t hal_platform_shutdown(void)
{
    sbi_shutdown();
    return HAL_SUCCESS;
}

void hal_debug_putc(char c) { jh7110_uart_putc(c); }
void hal_debug_puts(const char *s) { jh7110_uart_puts(s); }

void hal_panic(const char *msg)
{
    jh7110_uart_puts("\n[PANIC] ");
    jh7110_uart_puts(msg ? msg : "(null)");
    jh7110_uart_puts("\n[PANIC] System halted\n");
    while (1) __asm__ volatile("wfi");
}

void hal_printf(const char *fmt, ...)
{
    (void)fmt;
}

/* =============================================================================
 * CLOCK RATES
 * =============================================================================
 *
 * On BCM, clock rates come from the VideoCore mailbox.
 * On JH7110, we measure the CPU via rdcycle/rdtime and use known constants
 * for the rest. Same HAL contract, different implementation.
 */

uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id)
{
    switch (clock_id) {
        case HAL_CLOCK_ARM:
            /* Return measured frequency; re-measure if not yet done */
            if (g_measured_cpu_freq_hz > 0)
                return g_measured_cpu_freq_hz;
            return (uint32_t)(jh7110_measure_cpu_freq(10) * 1000000UL);

        case HAL_CLOCK_CORE:
            /* APB/bus clock — SYS_CRG typically runs at ~100 MHz */
            return 100000000UL;

        case HAL_CLOCK_UART:
            /* UART ref clock = 24 MHz crystal oscillator */
            return JH7110_TIMER_FREQ_HZ;

        case HAL_CLOCK_EMMC:
            /* SD/eMMC clock — set by U-Boot, typically 100–200 MHz */
            return 100000000UL;

        case HAL_CLOCK_PWM:
        case HAL_CLOCK_PIXEL:
        default:
            return 0;
    }
}

/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 *
 * The JH7110 peripherals are powered and clocked by U-Boot before handoff.
 * We report the expected state rather than querying hardware, matching the
 * same approach used by hal_platform_kyx1.c.
 */

hal_error_t hal_platform_set_power(hal_device_id_t device, bool on)
{
    (void)device;
    (void)on;
    /* Clock gating via SYS_CRG is possible but not yet implemented */
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_platform_get_power(hal_device_id_t device, bool *on)
{
    if (!on) return HAL_ERROR_NULL_PTR;

    /* U-Boot initializes all standard peripherals before handoff */
    switch (device) {
        case HAL_DEVICE_SD_CARD:
        case HAL_DEVICE_UART0:
        case HAL_DEVICE_USB:
        case HAL_DEVICE_I2C0:
        case HAL_DEVICE_I2C1:
        case HAL_DEVICE_SPI:
            *on = true;
            return HAL_SUCCESS;

        case HAL_DEVICE_UART1:
        case HAL_DEVICE_I2C2:
        case HAL_DEVICE_PWM:
            *on = false;
            return HAL_SUCCESS;

        default:
            *on = false;
            return HAL_ERROR_INVALID_ARG;
    }
}

/* =============================================================================
 * CACHE MANAGEMENT
 * =============================================================================
 *
 * The SiFive U74 is RV64GC — it does NOT have the Zicbom extension, so
 * there are no cbo.clean / cbo.flush / cbo.inval instructions.
 *
 * The U74 does have hardware cache coherency for DMA-capable masters via
 * the TileLink fabric. The DC8200 display controller is a separate bus
 * master that reads directly from DRAM. A full fence ensures our stores
 * reach the point of coherency before the display controller scans them.
 *
 * This is why cache.S is NOT compiled for the JH7110 target — there are
 * no cache block operations to implement. The fence iorw,iorw instruction
 * orders all memory operations and is sufficient for the framebuffer case.
 *
 * Contrast with the Ky X1 (SpacemiT X60, RV64GCV + Zicbom): that platform
 * compiles cache.S with actual cbo.clean loops.
 */

void clean_dcache_range(uintptr_t start, size_t size)
{
    jh7110_l2_flush_range(start, size);
}

void handle_exception(void *frame, unsigned long scause, unsigned long stval)
{
    (void)frame;
    jh7110_uart_puts("\n[TRAP] scause=");
    jh7110_uart_puthex(scause);
    jh7110_uart_puts(" stval=");
    jh7110_uart_puthex(stval);
    jh7110_uart_puts(" sepc=");

    uint64_t sepc;
    __asm__ volatile("csrr %0, sepc" : "=r"(sepc));
    jh7110_uart_puthex(sepc);
    jh7110_uart_putc('\n');

    /* Decode common causes */
    switch (scause) {
    case 0:  jh7110_uart_puts("[TRAP] Instruction address misaligned\n"); break;
    case 1:  jh7110_uart_puts("[TRAP] Instruction access fault\n"); break;
    case 2:  jh7110_uart_puts("[TRAP] Illegal instruction\n"); break;
    case 4:  jh7110_uart_puts("[TRAP] Load address misaligned\n"); break;
    case 5:  jh7110_uart_puts("[TRAP] Load access fault\n"); break;
    case 6:  jh7110_uart_puts("[TRAP] Store address misaligned\n"); break;
    case 7:  jh7110_uart_puts("[TRAP] Store access fault\n"); break;
    case 12: jh7110_uart_puts("[TRAP] Instruction page fault\n"); break;
    case 13: jh7110_uart_puts("[TRAP] Load page fault\n"); break;
    case 15: jh7110_uart_puts("[TRAP] Store page fault\n"); break;
    default: jh7110_uart_puts("[TRAP] Unknown cause\n"); break;
    }

    while (1) __asm__ volatile("wfi");
}
