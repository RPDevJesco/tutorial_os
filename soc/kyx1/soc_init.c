/*
 * soc_init.c — Ky X1 SoC Initialization for Tutorial-OS
 * ========================================================
 *
 * This is the top-level SoC initialization file that orchestrates all the
 * Ky X1-specific drivers into a working system. It's called from kernel_main
 * and is the equivalent of what the BCM2710 platform does with mailbox_init,
 * fb_init, and gpio_configure_for_dpi.
 *
 * On the BCM2710 (Pi Zero 2W), kernel_main does:
 *   1. mailbox_get_arm_memory()     — query RAM size from GPU
 *   2. fb_init()                    — allocate framebuffer via mailbox
 *   3. gpio_configure_for_dpi()     — set up DPI display pins
 *   4. ... draw the Hardware Inspector UI ...
 *
 * On the Ky X1 (Orange Pi RV2), the flow is:
 *   1. kyx1_soc_init()             — UART, timer are ready (no init needed!)
 *   2. kyx1_display_init(fb, dtb)  — parse SimpleFB from DTB
 *   3. kyx1_gpio_init_heartbeat_led() — configure board LED
 *   4. ... draw the Hardware Inspector UI — SAME CODE as Pi! ...
 *
 * The magic of the HAL: steps 1-3 are different per platform, but step 4
 * is IDENTICAL. The UI widgets, themes, canvas — all portable.
 *
 * What this file does NOT need to do (compared to BCM2710):
 *   - No mailbox init (no VideoCore GPU)
 *   - No RAM size query (DTB has memory info)
 *   - No clock configuration (U-Boot + OpenSBI handled it)
 *   - No power domain setup (already done)
 *   - No DPI pin muxing (no DPI — we use HDMI via SimpleFB)
 */

#include "kyx1_regs.h"
#include "types.h"
#include "framebuffer.h"

/* =============================================================================
 * EXTERNAL DECLARATIONS
 * =============================================================================
 * These are implemented in the other soc/kyx1/ files.
 */

/* uart.c */
extern void kyx1_uart_puts(const char *str);
extern void kyx1_uart_puthex(uint64_t val);
extern void kyx1_uart_putdec(uint32_t val);
extern void kyx1_uart_putc(char c);
extern void kyx1_uart_init_hw(void);

/* timer.c */
extern uint32_t micros(void);
extern uint64_t micros64(void);
extern void delay_us(uint32_t us);
extern void delay_ms(uint32_t ms);

/* gpio.c */
extern void kyx1_gpio_init_heartbeat_led(void);
extern void kyx1_gpio_set_led(bool on);

/* display_simplefb.c */
extern bool kyx1_display_init(framebuffer_t *fb, const void *dtb);
extern void kyx1_display_present(framebuffer_t *fb);

/* boot/riscv64/common_init.S — boot parameters stored during init */
extern uint64_t __dtb_ptr;
extern uint64_t __boot_hart_id;

/* =============================================================================
 * SOC INFORMATION
 * =============================================================================
 * Static info about the Ky X1 for the Hardware Inspector UI.
 */

static const char *kyx1_soc_name     = "SpacemiT Ky X1 (K1-derivative)";
static const char *kyx1_board_name   = "Orange Pi RV2";
static const char *kyx1_cpu_name     = "X60 (RV64GCVB) x8";
static const char *kyx1_boot_method  = "FSBL -> OpenSBI -> U-Boot -> kernel";

/*
 * kyx1_get_soc_name — Return the SoC name for display
 */
const char *kyx1_get_soc_name(void)
{
    return kyx1_soc_name;
}

/*
 * kyx1_get_board_name — Return the board name for display
 */
const char *kyx1_get_board_name(void)
{
    return kyx1_board_name;
}

/*
 * kyx1_get_cpu_name — Return the CPU core name for display
 */
const char *kyx1_get_cpu_name(void)
{
    return kyx1_cpu_name;
}

/*
 * kyx1_get_boot_method — Return boot chain description
 */
const char *kyx1_get_boot_method(void)
{
    return kyx1_boot_method;
}

/* =============================================================================
 * TEMPERATURE READOUT
 * =============================================================================
 *
 * The Ky X1 has 5 BJT thermal sensors at 0xD4018000 (`ky,x1-tsensor`).
 * For now, we use SBI to read temperature via the SBI HSM extension,
 * or we can read the sensor registers directly.
 *
 * BCM2710 comparison: Temperature comes from mailbox TAG_GET_TEMPERATURE.
 * On Ky X1, it's MMIO register reads from the thermal sensor block.
 *
 */
int32_t kyx1_get_temperature(void)
{
    /*
     * The sensor has 5 zones (BJT sensors) — we'd read the CPU zone.
     * For now return 0 to indicate "not yet implemented".
     */
    return KYX1_THERMAL_BASE;
}

/* =============================================================================
 * CPU FREQUENCY
 * =============================================================================
 *
 * The Ky X1 X60 cores run at up to 1.6 GHz. The actual frequency depends
 * on DVFS (Dynamic Voltage and Frequency Scaling) controlled by the PMIC.
 *
 * BCM2710: CPU frequency comes from mailbox TAG_GET_CLOCK_RATE.
 * Ky X1: Would need to read clock controller registers at 0xD4050000.
 *
 */
uint32_t kyx1_get_cpu_freq_mhz(void)
{
    return KYX1_CLOCK_BASE; /* 1.6 GHz max from DTS cpu@0 operating-points */
}

/* =============================================================================
 * MEMORY INFORMATION
 * =============================================================================
 * DRAM size is known from the hardware reference and DTS.
 */

uint64_t kyx1_get_total_ram(void)
{
    return KYX1_TOTAL_DRAM; /* 4 GB (2 banks × 2 GB) */
}

uint64_t kyx1_get_usable_ram(void)
{
    /*
     * Usable RAM from our kernel load point to the DPU reserved boundary.
     * 0x11000000 to 0x2FF40000 = ~497 MB for the kernel
     * Plus we can potentially reclaim the CMA pool (768 MB at 0x40000000)
     * and DRAM bank 1 (2 GB at 0x100000000).
     *
     * For initial reporting, return the conservative usable range.
     */
    return 0x2FF40000 - 0x11000000; /* ~497 MB */
}

/* =============================================================================
 * SOC INITIALIZATION
 * =============================================================================
 */

/*
 * kyx1_soc_init — Master initialization function
 *
 * Called from kernel_main() with the DTB pointer from boot.
 * Initializes all SoC subsystems and returns a ready framebuffer.
 *
 * @param fb   Framebuffer struct to populate
 *
 * Returns: true if initialization succeeded, false otherwise
 *
 * After this returns true, the caller can immediately start drawing
 * with fb_fill_rect(), ui_draw_panel(), etc.
 */
bool kyx1_soc_init(framebuffer_t *fb)
{
    const void *dtb = (const void *)(uintptr_t)__dtb_ptr;

    /* =====================================================================
     * Phase 1: Console
     * =====================================================================
     * UART output via SBI is already working (entry.S printed 'T').
     * Optionally switch to direct UART for faster output.
     */
    kyx1_uart_putdec((uint32_t)__boot_hart_id);
    kyx1_uart_puthex((uint64_t)(uintptr_t)dtb);

    /* Optionally switch to direct UART (faster than SBI ecalls) */
    kyx1_uart_init_hw();

    /* =====================================================================
     * Phase 2: CPU Frequency Measurement
     * =====================================================================
     * Measure actual CPU clock by comparing rdcycle against rdtime.
     * This gives us the real running frequency, which may differ from
     * the nominal 1.6 GHz if DVFS is active.
     */
    uint32_t cpu_mhz = kyx1_measure_cpu_freq(10);

    /* =====================================================================
     * Phase 3: GPIO — Heartbeat LED
     * =====================================================================
     */
    kyx1_gpio_init_heartbeat_led();
    kyx1_gpio_set_led(true);  /* LED on — we're alive! */

    /* =====================================================================
     * Phase 4: Display — SimpleFB from DTB
     * =====================================================================
     * This is the critical step. If this succeeds, we have pixels.
     */
    kyx1_display_present(fb);

    return true;
}
