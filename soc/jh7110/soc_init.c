/*
 * soc_init.c — StarFive JH-7110 SoC Initialization for Tutorial-OS
 * ==================================================================
 *
 * This is the top-level orchestration file for the JH7110 (Milk-V Mars).
 * It's the direct equivalent of soc/kyx1/soc_init.c — same structure,
 * same phase ordering, same naming convention.
 *
 * COMPARISON WITH KYX1:
 * =====================
 *
 * On the Ky X1 (Orange Pi RV2), soc_init.c does:
 *   Phase 1: UART (SBI fallback + direct PXA UART)
 *   Phase 2: CPU frequency measurement (rdcycle/rdtime calibration)
 *   Phase 3: GPIO — heartbeat LED on GPIO96
 *   Phase 4: Display — SimpleFB from DTB
 *
 * On the JH7110 (Milk-V Mars), soc_init.c does:
 *   Phase 1: UART (SBI fallback + direct DW 8250 UART)
 *   Phase 2: CPU frequency measurement (same algorithm, same 24 MHz ref)
 *   Phase 3: GPIO — no user LED, note in UART instead
 *   Phase 4: PMIC — AXP15060 via I2C6 (NEW vs kyx1)
 *   Phase 5: Display — SimpleFB from DTB (same strategy as kyx1)
 *
 * The addition of a discrete PMIC phase is because:
 *   - The AXP15060 is well-documented and worth demonstrating
 *   - Temperature reading via AXP15060 is straightforward
 *   - It illustrates the PMIC variance between boards with same strategy
 *
 * WHAT STAYS IDENTICAL VS KYX1:
 *   ✓ Entry point signature: kyx1_soc_init() / jh7110_soc_init()
 *   ✓ DTB pointer from __dtb_ptr (set by common_init.S)
 *   ✓ CPU freq measurement using rdcycle/rdtime ratio
 *   ✓ Display via SimpleFB — no DC8200 driver needed
 *   ✗ UART IP differs (PXA vs DW 8250)
 *   ✗ GPIO controller differs (MMP banks vs per-pin dout/doen)
 *   ✗ PMIC differs (SPM8821 vs AXP15060)
 *   ✗ No cache.S (U74 = no Zicbom)
 *
 * This "same structure, different silicon" comparison is exactly what the
 * book chapter needs: two RISC-V boards, one HAL strategy, clearly visible
 * differences isolated to their respective SoC layers.
 */

#include "jh7110_regs.h"
#include "types.h"
#include "framebuffer.h"
#include "drivers/sbi.h"
#include "drivers/i2c.h"
#include "drivers/pmic_axp15060.h"

/* =============================================================================
 * EXTERNAL DECLARATIONS
 * =============================================================================
 */

/* uart.c */
extern void jh7110_uart_puts(const char *str);
extern void jh7110_uart_puthex(uint64_t val);
extern void jh7110_uart_putdec(uint32_t val);
extern void jh7110_uart_putc(char c);
extern void jh7110_uart_init_hw(void);

/* cache coherency resolution page table mmu.S */
extern void jh7110_mmu_init(void);

/* timer.c */
extern uint32_t micros(void);
extern uint64_t micros64(void);
extern void delay_us(uint32_t us);
extern void delay_ms(uint32_t ms);
extern uint32_t jh7110_measure_cpu_freq(uint32_t calibration_ms);

/* gpio.c */
extern void jh7110_gpio_init_heartbeat_led(void);
extern void jh7110_gpio_set_led(bool on);

/* display_simplefb.c */
extern bool jh7110_display_init(framebuffer_t *fb, const void *dtb);
extern void jh7110_display_present(framebuffer_t *fb);

/* boot/riscv64/common_init.S — set during boot */
extern uint64_t __dtb_ptr;
extern uint64_t __boot_hart_id;

/* =============================================================================
 * SoC INFORMATION
 * =============================================================================
 */

uint64_t jh7110_get_total_ram(void)
{
    return JH7110_TOTAL_RAM;  /* 8 GB on Milk-V Mars 8GB */
}

uint64_t jh7110_get_usable_ram(void)
{
    /*
     * Conservative estimate: from kernel load address to first reserved region.
     * We load at 0x40200000 and have ~2GB of clean space before potential
     * peripheral-mapped or special regions.
     *
     * The DTB memory node will give us the full picture at boot time.
     */
    return 0x80000000UL - 0x40200000UL;  /* ~1 GB initial working range */
}

/* =============================================================================
 * SOC INITIALIZATION
 * =============================================================================
 */

bool jh7110_soc_init(framebuffer_t *fb)
{
    const void *dtb = (const void *)(uintptr_t)__dtb_ptr;

    /* =====================================================================
     * Phase 1: Console
     * =====================================================================
     * SBI console (legacy putchar) is already working — entry.S printed 'T'.
     * Switch to direct DW 8250 UART for faster output without SBI overhead.
     */

    /* Phase 0: Init the mmu */
    jh7110_mmu_init();

    /* Phase 1: Init hardware UART first, THEN print */
    jh7110_uart_init_hw();

    /* Print boot hart and DTB address for diagnosis */
    jh7110_uart_puts("\n[jh7110] Boot hart: ");
    jh7110_uart_putdec((uint32_t)__boot_hart_id);
    jh7110_uart_puts("  DTB @ ");
    jh7110_uart_puthex((uint64_t)(uintptr_t)dtb);
    jh7110_uart_putc('\n');

    /* =====================================================================
     * Phase 2: CPU Frequency Measurement
     * =====================================================================
     * Identical to kyx1 — rdcycle / rdtime calibration at 24 MHz reference.
     * The result differs (U74 @ 1.5 GHz vs X60 @ 1.6 GHz) but the code is
     * structurally identical. Same algorithm, different chip, same result.
     */
    jh7110_uart_puts("[jh7110] Measuring CPU frequency...\n");
    uint32_t cpu_mhz = jh7110_measure_cpu_freq(10);
    jh7110_uart_puts("[jh7110] CPU: ~");
    jh7110_uart_putdec(cpu_mhz);
    jh7110_uart_puts(" MHz\n");

    /* =====================================================================
     * Phase 3: GPIO — Heartbeat LED
     * =====================================================================
     * The Mars has no standardized user LED. jh7110_gpio_init_heartbeat_led()
     * is a no-op that prints a note. We call it anyway to maintain the
     * same API shape as kyx1 — demonstrates graceful capability absence.
     */
    jh7110_gpio_init_heartbeat_led();

    /* =====================================================================
     * Phase 4: PMIC (AXP15060)
     * =====================================================================
     * This phase has no equivalent on the Ky X1 / Orange Pi RV2.
     * The JH7110 Mars uses the X-Powers AXP15060 PMIC, which unlike
     * the Ky X1's SPM8821 (poorly documented, I2C bus shared with many
     * devices), is well-documented and straightforward to initialize.
     *
     * We initialize the PMIC here so hal_platform_get_temperature()
     * can return real temperature data.
     */
    jh7110_uart_puts("[jh7110] Initializing PMIC (AXP15060 @ I2C6/0x36)...\n");
    if (axp15060_init() == 0) {
        jh7110_uart_puts("[jh7110] PMIC OK\n");
        axp15060_print_status();
    } else {
        jh7110_uart_puts("[jh7110] PMIC init failed — temperature N/A\n");
    }

    /* =====================================================================
     * Phase 5: Display — SimpleFB from DTB
     * =====================================================================
     * Same strategy as kyx1. U-Boot initializes the DC8200 + HDMI TX
     * and puts a simple-framebuffer node in the DTB. We parse the DTB
     * to find the framebuffer address, then hand it to the UI system.
     *
     * If you want to verify this works before display: comment out everything
     * below here and check that UART output above is correct first.
     */
    jh7110_uart_puts("[jh7110] Initializing display (SimpleFB from DTB)...\n");
    if (!jh7110_display_init(fb, dtb)) {
        jh7110_uart_puts("[jh7110] ERROR: Display init failed\n");
        return false;
    }

    jh7110_uart_puts("[jh7110] Display OK — ");
    jh7110_uart_putdec(fb->width);
    jh7110_uart_puts("x");
    jh7110_uart_putdec(fb->height);
    jh7110_uart_puts("\n[jh7110] SoC init complete\n");

    return true;
}