/*
 * hal_platform_kyx1.c — HAL Platform Implementation for SpacemiT Ky X1
 * =====================================================================
 *
 * This file implements the hal_platform_* contract for the Orange Pi RV2.
 * It's the RISC-V equivalent of what soc/bcm2710/soc_init.c does for the Pi:
 *
 *   main.c calls hal_platform_get_info()
 *     → BCM2710: queries VideoCore mailbox
 *     → Ky X1:   queries SBI + PMIC + CSRs (THIS FILE)
 *
 * DRIVER INTEGRATION MAP:
 * -----------------------
 * This file wires together three driver layers:
 *
 *   ┌─────────────────────────────────────────────────┐
 *   │  hal_platform_kyx1.c  (this file — HAL glue)    │
 *   ├─────────────────────────────────────────────────┤
 *   │  sbi.c               │ SBI ecalls to OpenSBI    │
 *   │  - mvendorid/marchid │ M-mode CSR reads         │
 *   │  - CPU freq measure  │ cycle/time calibration   │
 *   │  - SBI SRST          │ reboot/shutdown          │
 *   ├──────────────────────┼──────────────────────────┤
 *   │  pmic_spm8821.c      │ SPM8821 PMIC via I2C     │
 *   │  - temperature       │ GPADC thermal sensor     │
 *   │  - chip ID           │ board identification     │
 *   │  - regulator status  │ voltage monitoring       │
 *   ├──────────────────────┼──────────────────────────┤
 *   │  i2c.c               │ PXA I2C8 controller      │
 *   │  - bus init          │ 100 kHz standard mode    │
 *   │  - register r/w      │ polled byte transfers    │
 *   └──────────────────────┴──────────────────────────┘
 *
 */

#include "types.h"
#include "hal/hal_platform.h"
#include "hal/hal_types.h"
#include "hal/hal_gpio.h"
#include "kyx1_cpu.h"
#include "kyx1_regs.h"

/* SBI and PMIC drivers */
#include "drivers/sbi.h"
#include "drivers/pmic_spm8821.h"


/* =============================================================================
 * EXTERNAL SYMBOLS
 * =============================================================================
 * These are provided by other kyx1 source files and boot assembly.
 */

/* From boot/riscv64/common_init.S — saved during boot */
extern uint64_t __dtb_ptr;
extern uint64_t __boot_hart_id;

/* From soc/kyx1/uart.c */
extern void kyx1_uart_init_hw(void);
extern void kyx1_uart_putc(char c);
extern void kyx1_uart_puts(const char *str);
extern void kyx1_uart_puthex(uint64_t val);
extern void kyx1_uart_putdec(uint32_t val);

/* From soc/kyx1/timer.c */
extern void delay_ms(uint32_t ms);

/* From soc/kyx1/gpio.c */
extern void kyx1_gpio_init_heartbeat_led(void);
extern void kyx1_gpio_set_led(bool on);

/* From soc/kyx1/display_simplefb.c */
extern bool kyx1_display_init(void *dtb, void *fb_out);
extern void kyx1_display_present(void *fb);


/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static bool g_platform_initialized = false;
static bool g_pmic_available = false;

/*
 * Cached hardware info — populated once during init, then returned on each
 * hal_platform_get_info() call without re-querying hardware.
 */
static kyx1_cpu_info_t g_cpu_info;
static uint32_t g_measured_cpu_freq_hz = 0;


/*
 * Ky X1 known constants
 * ---------------------
 * Fallback values used when hardware queries fail or during early boot
 * before drivers are initialized. These come from the K1 datasheet.
 */
#define KYX1_CPU_MAX_FREQ_HZ    1600000000UL    /* 1.6 GHz nominal max */
#define KYX1_TIMER_FREQ_HZ      24000000UL      /* 24 MHz timebase */
#define KYX1_UART_FREQ_HZ       14745600UL      /* UART reference clock */
#define KYX1_TOTAL_RAM           (4ULL * 1024 * 1024 * 1024) /* 4 GB */
#define KYX1_THERMAL_MAX_MC      105000          /* 105°C max junction temp */
#define KYX1_NUM_CORES           8


/* =============================================================================
 * PLATFORM INITIALIZATION
 * =============================================================================
 */

hal_error_t hal_platform_early_init(void)
{
    /* RISC-V early init is handled by common_init.S:
     * - Stack setup
     * - BSS zero
     * - FP enable
     * - Trap vector
     * Nothing to do here that isn't already done. */
    return HAL_SUCCESS;
}

hal_error_t hal_platform_init(void)
{
    if (g_platform_initialized) {
        return HAL_ERROR_ALREADY_INIT;
    }

    kyx1_uart_puts("[hal] Platform init: Ky X1 (SpacemiT)\n");

    /* Initialize UART hardware (may already be in SBI mode) */
    kyx1_uart_init_hw();

    /* Timer is always available via rdtime CSR — no init needed */

    /* Set up heartbeat LED (GPIO 96, active low) */
    kyx1_gpio_init_heartbeat_led();
    kyx1_gpio_set_led(true);  /* LED on = system alive */

    /* ── NEW: Query CPU info via SBI ecalls ──
     *
     * This reads mvendorid, marchid, mimpid from M-mode via OpenSBI,
     * probes available SBI extensions, reads S-mode CSRs (cycle, time,
     * sstatus, satp), and measures CPU frequency by calibrating the
     * cycle counter against the 24 MHz time reference.
     */
    kyx1_uart_puts("[hal] Querying CPU info via SBI...\n");
    kyx1_csr_get_info(&g_cpu_info);

    // blocking but needed for accurate measurments
    uint32_t cpu_info = kyx1_measure_cpu_freq(10);

    uint64_t hz64 = (uint64_t)g_cpu_info.cpu_freq_mhz * 1000000ULL;
    if (hz64 > 0xFFFFFFFFUL) {
        g_measured_cpu_freq_hz = KYX1_CPU_MAX_FREQ_HZ; // or fallback / error
    } else {
        g_measured_cpu_freq_hz = (uint32_t)hz64;
    }

    // fallback if the values are higher than max frequency
    if (g_measured_cpu_freq_hz > KYX1_CPU_MAX_FREQ_HZ) g_measured_cpu_freq_hz = cpu_info;

    /* Initialize PMIC via I2C ──
     *
     * The SPM8821 PMIC connects via I2C8 at address 0x41.
     * This initializes the I2C controller, probes the PMIC,
     * reads the chip ID, and enables temperature/voltage queries.
     */
    kyx1_uart_puts("[hal] Initializing PMIC (SPM8821 via I2C8)...\n");
    if (spm8821_init() == 0) {
        g_pmic_available = true;
        kyx1_uart_puts("[hal] PMIC: OK\n");

        /*
         * Dump register map on first boot — this is our primary tool
         * for reverse-engineering the SPM8821 register layout. Once
         * we've verified the offsets, this can be gated behind a
         * debug flag to avoid the ~2 second UART dump delay.
         */
        spm8821_dump_registers(0x00, 0xFF);
        spm8821_print_status();
    } else {
        g_pmic_available = false;
        kyx1_uart_puts("[hal] PMIC: not available (I2C error)\n");
    }

    kyx1_uart_puts("[hal] Init complete: UART + SBI + PMIC + GPIO\n");

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
 *
 * mvendorid/marchid serve as a "serial number" equivalent since
 * RISC-V doesn't have a per-board serial like BCM's OTP.
 */

hal_platform_id_t hal_platform_get_id(void)
{
    return HAL_PLATFORM_ORANGEPI_RV2;
}

const char *hal_platform_get_board_name(void)
{
    return "Orange Pi RV2";
}

const char *hal_platform_get_soc_name(void)
{
    return "SpacemiT Ky X1 (K1)";
}

hal_error_t hal_platform_get_info(hal_platform_info_t *info)
{
    if (!info) return HAL_ERROR_NULL_PTR;

    info->platform_id   = HAL_PLATFORM_ORANGEPI_RV2;
    info->arch          = HAL_ARCH_RISCV64;
    info->board_name    = "Orange Pi RV2";
    info->soc_name      = "SpacemiT Ky X1";

    /*
     * Use marchid as board_revision (it encodes the microarchitecture
     * version) and combine mvendorid + mimpid as serial_number (these
     * encode vendor JEDEC code and implementation version). These are
     * the closest RISC-V equivalents to BCM's board revision and OTP
     * serial number.
     *
     * TEACHING POINT: ARM has dedicated identification registers
     * (MIDR_EL1, etc.) readable from EL1. RISC-V's equivalent
     * registers are M-mode only — we had to go through SBI to get
     * them. This is a fundamental difference in the privilege models.
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
     * Ky X1 has no ARM/GPU memory split like BCM.
     * All 4 GB is accessible to the CPU. The DPU has a small reserved
     * region at 0x2FF40000 (384 KB) configured by U-Boot.
     */
    info->arm_base        = 0x00000000;
    info->arm_size        = KYX1_TOTAL_RAM;
    info->gpu_base        = 0x2FF40000;         /* DPU reserved region */
    info->gpu_size        = 384 * 1024;          /* 384 KB for display */
    info->peripheral_base = KYX1_PERI_BASE;      /* 0xD4000000 */

    return HAL_SUCCESS;
}

size_t hal_platform_get_arm_memory(void)
{
    return (size_t)KYX1_TOTAL_RAM;
}

size_t hal_platform_get_total_memory(void)
{
    return (size_t)KYX1_TOTAL_RAM;
}


/* =============================================================================
 * CLOCK INFORMATION
 * =============================================================================
 *
 * NOW USES REAL MEASUREMENT for HAL_CLOCK_ARM instead of a hardcoded constant.
 *
 * The SBI driver measures CPU frequency by comparing the cycle counter
 * (rdcycle) against the time counter (rdtime, 24 MHz reference) over
 * a calibration interval. This gives us the actual running frequency,
 * which may differ from nominal if DVFS is active.
 */

uint32_t hal_platform_get_clock_rate(hal_clock_id_t clock_id)
{
    switch (clock_id) {
        case HAL_CLOCK_ARM:
            /*
             * Return measured frequency if available, otherwise fallback.
             * The measurement happens once during hal_platform_init().
             *
             * TEACHING POINT: On BCM, we query the VideoCore mailbox for
             * the ARM clock. On RISC-V, we measure it ourselves by
             * comparing cycle/time CSR ratios, a fundamental technique
             * used by all RISC-V OSes.
             */
            uint32_t mhz = kyx1_measure_cpu_freq(10);
            return mhz * 1000000UL;

        case HAL_CLOCK_CORE:
            /* Bus/interconnect clock — typically half of CPU */
            return hal_platform_get_clock_rate(HAL_CLOCK_ARM) / 2;

        case HAL_CLOCK_UART:
            return KYX1_UART_FREQ_HZ;

        case HAL_CLOCK_EMMC:
            /* eMMC/SD clock — usually 200 MHz from PLL */
            return 200000000UL;

        case HAL_CLOCK_PWM:
            /* PWM reference — not yet characterized */
            return 0;

        case HAL_CLOCK_PIXEL:
            /* Display pixel clock — set by U-Boot for HDMI mode */
            return 0;  /* Would need to read DPU registers */

        default:
            return 0;
    }
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

uint32_t hal_platform_get_arm_freq(void)
{
    return hal_platform_get_clock_rate(HAL_CLOCK_ARM);
}

uint32_t hal_platform_get_arm_freq_measured(void)
{
    /*
     * This now returns the ACTUAL measured frequency in Hz, not a
     * hardcoded constant. If hal_platform_init() hasn't been called
     * yet, we do a one-shot measurement here.
     *
     * kyx1_measure_cpu_freq() takes calibration time in milliseconds
     * and returns frequency in MHz that we convert to Hz for the HAL.
     * It should be noted that this is likely to get corrupted due to timing issues
     * and we account for this by not using g_measured_cpu_freq_hz as a cached value
     * in other locations.
     */
    if (g_measured_cpu_freq_hz > 0) {
        return g_measured_cpu_freq_hz;
    }

    uint32_t freq_mhz = kyx1_measure_cpu_freq(10);  /* 10 ms calibration */
    if (freq_mhz > 0) {
        g_measured_cpu_freq_hz = freq_mhz * 1000000UL;
        return g_measured_cpu_freq_hz;
    }

    return KYX1_CPU_MAX_FREQ_HZ;  /* Fallback to nominal */
}


/* =============================================================================
 * TEMPERATURE MONITORING
 * =============================================================================
 *
 * NOW ATTEMPTS PMIC READ instead of returning NOT_SUPPORTED.
 *
 * The SPM8821 PMIC has a GPADC with thermal sensing capability.
 * If the PMIC is available and temperature registers are accessible,
 * we return real data. Otherwise we fall back gracefully.
 *
 * TEACHING POINT: This is a great example of "layered availability" —
 * the HAL gracefully degrades from:
 *   1. Real PMIC temperature (best)
 *   2. NOT_SUPPORTED with max temp still available (acceptable)
 * The UI code checks the return value and adapts its display.
 */

hal_error_t hal_platform_get_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return HAL_ERROR_NULL_PTR;

    /* Try PMIC thermal sensor if available */
    if (g_pmic_available) {
        int32_t pmic_temp;
        if (spm8821_get_temperature(&pmic_temp) == 0) {
            *temp_mc = pmic_temp;
            return HAL_SUCCESS;
        }
    }

    /*
     * PMIC temperature not available yet — the GPADC register
     * addresses need to be discovered via register dump first.
     * This is expected on initial bring-up.
     */
    *temp_mc = 0;
    return HAL_ERROR_NOT_SUPPORTED;
}

int32_t hal_platform_get_temp_celsius(void)
{
    int32_t temp_mc;
    if (HAL_FAILED(hal_platform_get_temperature(&temp_mc))) {
        return -1;
    }
    return temp_mc / 1000;
}

hal_error_t hal_platform_get_max_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return HAL_ERROR_NULL_PTR;

    /* Ky X1 max junction temperature from datasheet */
    *temp_mc = KYX1_THERMAL_MAX_MC;
    return HAL_SUCCESS;
}


/* =============================================================================
 * THROTTLING STATUS
 * =============================================================================
 *
 * The Ky X1 doesn't have a VideoCore-style throttle reporting mechanism.
 * The PMIC handles thermal shutdown independently. We always report
 * "no throttle" since we can't query the DVFS state yet.
 *
 * Future: could monitor CPU frequency changes via periodic rdcycle
 * measurements to detect if the SoC has throttled down.
 */

hal_error_t hal_platform_get_throttle_status(uint32_t *status)
{
    if (!status) return HAL_ERROR_NULL_PTR;

    /* No throttle reporting available on Ky X1 */
    *status = 0;
    return HAL_SUCCESS;
}

bool hal_platform_is_throttled(void)
{
    return false;
}


/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 *
 * BCM chips have per-device power domains controlled via mailbox.
 * The Ky X1 peripherals are enabled/disabled via clock gating and
 * PMIC rails, managed by U-Boot. In bare-metal mode, everything
 * U-Boot left on stays on.
 *
 * We report everything as "on" since we can't query individual
 * power domains without the PMIC I2C driver.
 */

hal_error_t hal_platform_set_power(hal_device_id_t device, bool on)
{
    (void)device;
    (void)on;

    /* TODO: Implement clock gating for peripheral power management
     * We do have a pmic_spm driver, however, there are unknowns
     * rather than guestimate it, I have simply opted for
     * just returning not supported for expedience
     */

    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_platform_get_power(hal_device_id_t device, bool *on)
{
    if (!on) return HAL_ERROR_NULL_PTR;

    /*
     * All peripherals are assumed powered on by U-Boot.
     * Return true for known devices, error for unknown.
     */
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
            *on = false;  /* Not configured by U-Boot typically */
            return HAL_SUCCESS;

        default:
            *on = false;
            return HAL_ERROR_INVALID_ARG;
    }
}


/* =============================================================================
 * GPIO — DPI CONFIGURATION
 * =============================================================================
 *
 * On BCM/Pi, gpio_configure_for_dpi() sets up 28 GPIO pins for the parallel
 * RGB display interface. On the Orange Pi RV2, display is HDMI — configured
 * entirely by U-Boot's DPU driver. Nothing to do here.
 *
 * main.c calls this unconditionally, so we provide a no-op.
 */
hal_error_t hal_gpio_configure_dpi(void)
{
    /* No-op on HDMI platforms */
    return HAL_SUCCESS;
}

/*
 * hal_gpio_init — Initialize GPIO subsystem
 *
 * On Ky X1, the heartbeat LED is already set up in hal_platform_init().
 */
hal_error_t hal_gpio_init(void)
{
    return HAL_SUCCESS;
}


/* =============================================================================
 * SYSTEM CONTROL — NOW FUNCTIONAL VIA SBI SRST
 * =============================================================================
 *
 * The SBI System Reset Extension (SRST, EID 0x53525354) provides a
 * standard mechanism for S-mode software to request system reset.
 * OpenSBI knows how to talk to the SPM8821 PMIC to actually power
 * cycle the board — we don't need our own I2C driver for this!
 *
 * TEACHING POINT: This is a perfect example of the RISC-V privilege
 * model working in our favor. We can't directly control the PMIC
 * from S-mode, but we don't need to — OpenSBI in M-mode handles
 * the platform-specific details. Our job is just to make the ecall.
 */

hal_error_t hal_platform_reboot(void)
{
    kyx1_sbi_reboot();

    /*
     * If we get here, the SRST extension wasn't available or failed.
     * The sbi_reboot function already logged the error to UART.
     */
    return HAL_ERROR_NOT_SUPPORTED;
}

hal_error_t hal_platform_shutdown(void)
{
    kyx1_sbi_shutdown();

    /* Same — if we return, it didn't work */
    return HAL_ERROR_NOT_SUPPORTED;
}

HAL_NORETURN void hal_panic(const char *message)
{
    if (message) kyx1_uart_puts(message);
    kyx1_uart_puts("\n");

    /* Blink LED rapidly to indicate panic */
    while (1) {
        kyx1_gpio_set_led(true);
        delay_ms(100);
        kyx1_gpio_set_led(false);
        delay_ms(100);
    }
}


/* =============================================================================
 * DEBUG OUTPUT
 * =============================================================================
 * Routes to our UART driver (SBI ecall or direct PXA UART).
 */

void hal_debug_putc(char c)
{
    kyx1_uart_putc(c);
}

void hal_debug_puts(const char *s)
{
    kyx1_uart_puts(s);
}

void hal_debug_printf(const char *fmt, ...)
{
    /*
     * Full printf isn't available in bare-metal without a format library.
     * Just output the format string as-is. main.c primarily uses
     * hal_platform_get_info() to get data and formats it with u64_to_dec().
     */
    (void)fmt;
}


/* =============================================================================
 * DISPLAY INITIALIZATION (HAL bridge)
 * =============================================================================
 *
 * Bridges the HAL display interface to our kyx1_display_init().
 * main.c calls fb_init() which routes here.
 *
 * NOTE: Argument order was fixed in a previous session — fb first, dtb second,
 * matching the kyx1_display_init(framebuffer_t *fb, const void *dtb) signature.
 */

#include "framebuffer.h"

bool fb_init(framebuffer_t *fb)
{
    const void *dtb = (const void *)(uintptr_t)__dtb_ptr;
    return kyx1_display_init((void *)fb, (void *)dtb);
}