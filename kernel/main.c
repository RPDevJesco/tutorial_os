/*
 * main.c - Hardware Inspector for Raspberry Pi Zero 2W
 * =====================================================
 *
 * A bare-metal OS that introspects and displays detailed hardware
 * information about the device it runs on.
 */

#include "types.h"
#include "framebuffer.h"

/* HAL Interface Headers */
#include "hal/hal.h"

/* UI System */
#include "ui_types.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "memory/allocator.h"

static allocator_t alloc;
extern char __heap_start;

static char *u64_to_hex(uint64_t val, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';

    for (int i = 0; i < 16; i++) {
        buf[2 + i] = hex[(val >> (60 - i * 4)) & 0xF];
    }
    buf[18] = '\0';
    return buf;
}

static char *u64_to_dec(uint64_t val, char *buf)
{
    char *p = buf + 31;
    *p = '\0';

    if (val == 0) {
        *--p = '0';
        return p;
    }

    while (val) {
        *--p = '0' + (val % 10);
        val /= 10;
    }
    return p;
}

void kernel_main(uintptr_t dtb_ptr, uintptr_t ram_base, uintptr_t ram_size)
{
    (void)dtb_ptr;

    allocator_init_from_ram(&alloc, ram_base, ram_size,
                            (uintptr_t)&__heap_start,
                            NULL, NULL, NULL, NULL);

    /* Configure GPIO for DPI display */
    hal_gpio_configure_dpi();

    /* Initialize framebuffer */
    framebuffer_t fb;
    if (!fb_init(&fb)) {
        while (1) __asm__ volatile("wfe");
    }

    /* Create theme */
    ui_theme_t theme = ui_theme_for_width(640, UI_PALETTE_DARK);

    /* Clear to theme background */
    fb_clear(&fb, theme.colors.bg_primary);

    char buf[64];
    uint32_t y;

    /* Layout constants */
    uint32_t left_x = 10;
    uint32_t left_w = 300;
    uint32_t right_x = 320;
    uint32_t right_w = 310;
    uint32_t pad = 8;

    /* Get platform information */
    hal_platform_info_t platform_info;
    hal_platform_get_info(&platform_info);

    /* Get memory information */
    hal_memory_info_t mem_info;
    hal_platform_get_memory_info(&mem_info);

    /* =========================================================================
     * HEADER
     * =========================================================================
     */
    ui_rect_t header_panel = ui_rect(left_x, 8, 620, 40);
    ui_draw_panel(&fb, header_panel, &theme, UI_PANEL_ELEVATED);

    const char *title = "HARDWARE INSPECTOR";
    uint32_t title_w = fb_text_width_large(title);
    uint32_t title_x = left_x + (620 - title_w) / 2;
    fb_draw_string_large_transparent(&fb, title_x, 18, title, theme.colors.accent_bright);

    /* Subtitle - use HAL board name */
    const char *subtitle = platform_info.board_name ? platform_info.board_name : "Bare Metal System";
    uint32_t sub_w = fb_text_width(subtitle);
    uint32_t sub_x = left_x + (620 - sub_w) / 2;
    fb_draw_string_transparent(&fb, sub_x, 36, subtitle, theme.colors.text_secondary);

    /* =========================================================================
     * LEFT COLUMN
     * =========================================================================
     */

    /* --- BOARD PANEL --- */
    ui_rect_t board_panel = ui_rect(left_x, 56, left_w, 70);
    ui_draw_panel(&fb, board_panel, &theme, UI_PANEL_ELEVATED);

    fb_draw_string_transparent(&fb, left_x + pad, 62, "BOARD", theme.colors.text_secondary);
    ui_draw_divider_h(&fb, left_x + pad, 74, left_w - pad * 2, &theme);

    y = 80;
    fb_draw_string_transparent(&fb, left_x + pad, y, "Model:", theme.colors.text_secondary);
    const char *soc_name = platform_info.soc_name ? platform_info.soc_name : "Unknown";
    fb_draw_string_transparent(&fb, left_x + 55, y, soc_name, theme.colors.text_primary);
    ui_draw_badge(&fb, left_x + 130, y - 2, &theme, "Cortex-A53");

    y += 14;
    fb_draw_string_transparent(&fb, left_x + pad, y, "Serial:", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, left_x + 55, y, u64_to_hex(platform_info.serial_number, buf), theme.colors.text_primary);

    y += 14;
    fb_draw_string_transparent(&fb, left_x + pad, y, "Arch:", theme.colors.text_secondary);
    ui_draw_badge(&fb, left_x + 55, y - 2, &theme, "ARM64");
    ui_draw_badge(&fb, left_x + 105, y - 2, &theme, "ARMv8-A");

    /* --- PROCESSOR PANEL --- */
    ui_rect_t cpu_panel = ui_rect(left_x, 132, left_w, 80);
    ui_draw_panel(&fb, cpu_panel, &theme, UI_PANEL_ELEVATED);

    fb_draw_string_transparent(&fb, left_x + pad, 138, "PROCESSOR", theme.colors.text_secondary);
    ui_draw_divider_h(&fb, left_x + pad, 150, left_w - pad * 2, &theme);

    y = 156;
    uint32_t arm_clock = hal_platform_get_clock_rate(HAL_CLOCK_ARM);
    if (arm_clock > 0) {
        uint32_t mhz = arm_clock / 1000000;
        fb_draw_string_transparent(&fb, left_x + pad, y, "ARM", theme.colors.text_secondary);

        /* Progress bar - estimate max as 1000 MHz for BCM2710 */
        uint32_t arm_max = 1000000000;  /* 1 GHz typical max for Pi Zero 2W */
        uint32_t pct = (arm_clock * 100) / arm_max;
        if (pct > 100) pct = 100;
        ui_rect_t clk_bar = ui_rect(left_x + 40, y - 1, 80, 10);
        ui_draw_progress_bar(&fb, clk_bar, &theme, pct, theme.colors.accent);

        fb_draw_string_transparent(&fb, left_x + 125, y, u64_to_dec(mhz, buf), theme.colors.text_primary);
        fb_draw_string_transparent(&fb, left_x + 160, y, "/", theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, left_x + 170, y, u64_to_dec(arm_max / 1000000, buf), theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, left_x + 205, y, "MHz", theme.colors.text_secondary);
    }

    y += 14;
    uint32_t core_clock = hal_platform_get_clock_rate(HAL_CLOCK_CORE);
    if (core_clock > 0) {
        uint32_t mhz = core_clock / 1000000;
        fb_draw_string_transparent(&fb, left_x + pad, y, "Core", theme.colors.text_secondary);
        ui_rect_t core_bar = ui_rect(left_x + 40, y - 1, 80, 10);
        ui_draw_progress_bar(&fb, core_bar, &theme, 80, theme.colors.info);
        fb_draw_string_transparent(&fb, left_x + 125, y, u64_to_dec(mhz, buf), theme.colors.text_primary);
        fb_draw_string_transparent(&fb, left_x + 160, y, "MHz", theme.colors.text_secondary);
    }

    y += 14;
    /* Note: HAL doesn't have SDRAM clock, show EMMC instead */
    uint32_t emmc_clock = hal_platform_get_clock_rate(HAL_CLOCK_EMMC);
    if (emmc_clock > 0) {
        uint32_t mhz = emmc_clock / 1000000;
        fb_draw_string_transparent(&fb, left_x + pad, y, "eMMC", theme.colors.text_secondary);
        ui_rect_t emmc_bar = ui_rect(left_x + 40, y - 1, 80, 10);
        ui_draw_progress_bar(&fb, emmc_bar, &theme, 40, theme.colors.warning);
        fb_draw_string_transparent(&fb, left_x + 125, y, u64_to_dec(mhz, buf), theme.colors.text_primary);
        fb_draw_string_transparent(&fb, left_x + 160, y, "MHz", theme.colors.text_secondary);
    }

    y += 14;
    uint32_t pwm_clock = hal_platform_get_clock_rate(HAL_CLOCK_PWM);
    if (pwm_clock > 0) {
        uint32_t mhz = pwm_clock / 1000000;
        fb_draw_string_transparent(&fb, left_x + pad, y, "PWM", theme.colors.text_secondary);
        ui_rect_t pwm_bar = ui_rect(left_x + 40, y - 1, 80, 10);
        ui_draw_progress_bar(&fb, pwm_bar, &theme, 20, theme.colors.success);
        fb_draw_string_transparent(&fb, left_x + 125, y, u64_to_dec(mhz, buf), theme.colors.text_primary);
        fb_draw_string_transparent(&fb, left_x + 160, y, "MHz", theme.colors.text_secondary);
    }

    /* --- MEMORY PANEL --- */
    ui_rect_t mem_panel = ui_rect(left_x, 218, left_w, 70);
    ui_draw_panel(&fb, mem_panel, &theme, UI_PANEL_ELEVATED);

    fb_draw_string_transparent(&fb, left_x + pad, 224, "MEMORY", theme.colors.text_secondary);
    ui_draw_divider_h(&fb, left_x + pad, 236, left_w - pad * 2, &theme);

    y = 242;
    uint32_t arm_size = (uint32_t)mem_info.arm_size;
    uint32_t gpu_size = (uint32_t)mem_info.gpu_size;
    uint32_t total_mb = (arm_size + gpu_size) / (1024 * 1024);
    uint32_t arm_mb = arm_size / (1024 * 1024);
    uint32_t gpu_mb = gpu_size / (1024 * 1024);
    uint32_t arm_pct = (arm_size + gpu_size > 0) ? (arm_size * 100) / (arm_size + gpu_size) : 0;

    fb_draw_string_transparent(&fb, left_x + pad, y, "Total:", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, left_x + 50, y, u64_to_dec(total_mb, buf), theme.colors.text_primary);
    fb_draw_string_transparent(&fb, left_x + 80, y, "MB", theme.colors.text_secondary);

    /* Stacked bar showing ARM vs GPU split */
    fb_fill_rounded_rect(&fb, left_x + 110, y - 1, 120, 10, 3, theme.colors.accent);
    fb_fill_rounded_rect(&fb, left_x + 110 + (arm_pct * 120 / 100), y - 1, 120 - (arm_pct * 120 / 100), 10, 3, theme.colors.warning);

    fb_draw_string_transparent(&fb, left_x + 240, y, "ARM/GPU", theme.colors.text_secondary);

    y += 14;
    fb_draw_string_transparent(&fb, left_x + pad, y, "ARM:", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, left_x + 45, y, u64_to_dec(arm_mb, buf), theme.colors.text_primary);
    fb_draw_string_transparent(&fb, left_x + 80, y, "MB", theme.colors.text_secondary);

    fb_draw_string_transparent(&fb, left_x + 110, y, "GPU:", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, left_x + 145, y, u64_to_dec(gpu_mb, buf), theme.colors.text_primary);
    fb_draw_string_transparent(&fb, left_x + 170, y, "MB", theme.colors.text_secondary);

    fb_draw_string_transparent(&fb, left_x + 200, y, "Heap:", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, left_x + 240, y, u64_to_dec((uintptr_t)&__heap_start, buf), theme.colors.text_primary);

    /* =========================================================================
     * RIGHT COLUMN
     * =========================================================================
     */

    /* --- DISPLAY PANEL --- */
    ui_rect_t disp_panel = ui_rect(right_x, 56, right_w, 70);
    ui_draw_panel(&fb, disp_panel, &theme, UI_PANEL_ELEVATED);

    fb_draw_string_transparent(&fb, right_x + pad, 62, "DISPLAY", theme.colors.text_secondary);
    ui_draw_divider_h(&fb, right_x + pad, 74, right_w - pad * 2, &theme);

    y = 80;
    fb_draw_string_transparent(&fb, right_x + pad, y, "Resolution:", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, right_x + 90, y, u64_to_dec(fb.width, buf), theme.colors.text_primary);
    fb_draw_string_transparent(&fb, right_x + 120, y, "x", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, right_x + 130, y, u64_to_dec(fb.height, buf), theme.colors.text_primary);
    ui_draw_badge(&fb, right_x + 170, y - 2, &theme, "4:3");

    y += 14;
    fb_draw_string_transparent(&fb, right_x + pad, y, "Format:", theme.colors.text_secondary);
    ui_draw_badge(&fb, right_x + 60, y - 2, &theme, "ARGB8888");
    fb_draw_string_transparent(&fb, right_x + 140, y, "32-bit", theme.colors.text_secondary);

    y += 14;
    fb_draw_string_transparent(&fb, right_x + pad, y, "Interface:", theme.colors.text_secondary);
    ui_draw_badge(&fb, right_x + 80, y - 2, &theme, "DPI");
    fb_draw_string_transparent(&fb, right_x + 120, y, "RGB666 Parallel", theme.colors.text_secondary);

    /* --- PERIPHERALS PANEL --- */
    ui_rect_t periph_panel = ui_rect(right_x, 132, right_w, 80);
    ui_draw_panel(&fb, periph_panel, &theme, UI_PANEL_ELEVATED);

    fb_draw_string_transparent(&fb, right_x + pad, 138, "PERIPHERALS", theme.colors.text_secondary);
    ui_draw_divider_h(&fb, right_x + pad, 150, right_w - pad * 2, &theme);

    y = 156;
    bool usb_on = false;
    if (HAL_OK(hal_platform_get_power(HAL_DEVICE_USB, &usb_on))) {
        fb_draw_string_transparent(&fb, right_x + pad, y, "USB Host:", theme.colors.text_secondary);
        ui_rect_t usb_toast = ui_rect(right_x + 80, y - 3, 60, 14);
        ui_draw_toast(&fb, usb_toast, &theme, usb_on ? "Active" : "Off",
                      usb_on ? UI_TOAST_SUCCESS : UI_TOAST_ERROR);
    }

    y += 18;
    bool sd_on = false;
    if (HAL_OK(hal_platform_get_power(HAL_DEVICE_SD_CARD, &sd_on))) {
        fb_draw_string_transparent(&fb, right_x + pad, y, "SD Card:", theme.colors.text_secondary);
        ui_rect_t sd_toast = ui_rect(right_x + 80, y - 3, 60, 14);
        ui_draw_toast(&fb, sd_toast, &theme, sd_on ? "Active" : "Off",
                      sd_on ? UI_TOAST_SUCCESS : UI_TOAST_ERROR);
    }

    y += 18;
    fb_draw_string_transparent(&fb, right_x + pad, y, "I2C:", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, right_x + 50, y, "Available", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, right_x + 130, y, "SPI:", theme.colors.text_secondary);
    fb_draw_string_transparent(&fb, right_x + 170, y, "Available", theme.colors.text_secondary);

    /* --- SYSTEM STATUS PANEL --- */
    ui_rect_t status_panel = ui_rect(right_x, 218, right_w, 70);
    ui_draw_panel(&fb, status_panel, &theme, UI_PANEL_ELEVATED);

    fb_draw_string_transparent(&fb, right_x + pad, 224, "SYSTEM STATUS", theme.colors.text_secondary);
    ui_draw_divider_h(&fb, right_x + pad, 236, right_w - pad * 2, &theme);

    /* Temperature with visual bar */
    y = 244;
    int32_t temp_mc = 0;
    int32_t temp_max_mc = 0;
    if (HAL_OK(hal_platform_get_temperature(&temp_mc)) &&
        HAL_OK(hal_platform_get_max_temperature(&temp_max_mc))) {
        uint32_t temp_c = temp_mc / 1000;
        uint32_t temp_pct = (temp_max_mc > 0) ? ((uint32_t)temp_mc * 100) / (uint32_t)temp_max_mc : 0;
        if (temp_pct > 100) temp_pct = 100;

        fb_draw_string_transparent(&fb, right_x + pad, y, "Temp:", theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, right_x + 50, y, u64_to_dec(temp_c, buf), theme.colors.text_primary);
        fb_draw_string_transparent(&fb, right_x + 70, y, "C", theme.colors.text_primary);

        ui_rect_t temp_bar = ui_rect(right_x + 90, y - 1, 100, 10);
        ui_color_t temp_color = theme.colors.success;
        if (temp_pct > 80) temp_color = theme.colors.error;
        else if (temp_pct > 60) temp_color = theme.colors.warning;
        ui_draw_progress_bar(&fb, temp_bar, &theme, temp_pct, temp_color);

        fb_draw_string_transparent(&fb, right_x + 200, y, "max", theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, right_x + 230, y, u64_to_dec(temp_max_mc / 1000, buf), theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, right_x + 250, y, "C", theme.colors.text_secondary);
    }

    /* Throttle status */
    y += 16;
    uint32_t throttle = 0;
    if (HAL_OK(hal_platform_get_throttle_status(&throttle))) {
        bool undervolt = throttle & HAL_THROTTLE_UNDERVOLT_NOW;
        bool is_throttled = throttle & HAL_THROTTLE_THROTTLED_NOW;
        bool freq_cap = throttle & HAL_THROTTLE_ARM_FREQ_CAPPED;

        fb_draw_string_transparent(&fb, right_x + pad, y, "Voltage:", theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, right_x + 65, y, undervolt ? "LOW" : "OK",
                                   undervolt ? theme.colors.error : theme.colors.success);

        fb_draw_string_transparent(&fb, right_x + 100, y, "Throttle:", theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, right_x + 165, y, is_throttled ? "YES" : "NO",
                                   is_throttled ? theme.colors.error : theme.colors.success);

        fb_draw_string_transparent(&fb, right_x + 200, y, "Cap:", theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, right_x + 235, y, freq_cap ? "YES" : "NO",
                                   freq_cap ? theme.colors.error : theme.colors.success);
    }

    /* =========================================================================
     * BOOT SEQUENCE PANEL
     * =========================================================================
     */
    ui_rect_t boot_panel = ui_rect(left_x, 294, 620, 50);
    ui_draw_panel(&fb, boot_panel, &theme, UI_PANEL_FLAT);

    fb_draw_string_transparent(&fb, left_x + pad, 300, "BOOT SEQUENCE", theme.colors.text_secondary);
    ui_draw_divider_h(&fb, left_x + pad, 312, 604, &theme);

    y = 320;
    /* Boot stages with arrows */
    fb_draw_string_transparent(&fb, left_x + pad, y, "GPU ROM", theme.colors.text_primary);
    fb_draw_string_transparent(&fb, left_x + 70, y, "->", theme.colors.accent);
    fb_draw_string_transparent(&fb, left_x + 90, y, "bootcode.bin", theme.colors.text_primary);
    fb_draw_string_transparent(&fb, left_x + 185, y, "->", theme.colors.accent);
    fb_draw_string_transparent(&fb, left_x + 205, y, "start.elf", theme.colors.text_primary);
    fb_draw_string_transparent(&fb, left_x + 280, y, "->", theme.colors.accent);
    fb_draw_string_transparent(&fb, left_x + 300, y, "kernel8.img", theme.colors.accent_bright);
    fb_draw_string_transparent(&fb, left_x + 390, y, "(this code!)", theme.colors.text_secondary);

    /* =========================================================================
     * FOOTER
     * =========================================================================
     */
    ui_rect_t footer = ui_rect(left_x, 350, 620, 28);
    ui_draw_panel(&fb, footer, &theme, UI_PANEL_ELEVATED);

    /* Left side badges */
    ui_draw_badge(&fb, left_x + pad, 357, &theme, "BARE METAL");
    ui_draw_badge(&fb, left_x + 95, 357, &theme, "NO OS");
    ui_draw_badge(&fb, left_x + 150, 357, &theme, "TUTORIAL-OS");

    /* Right side status */
    ui_rect_t ready_toast = ui_rect(right_x + 180, 354, 120, 20);
    ui_draw_toast(&fb, ready_toast, &theme, "System Ready", UI_TOAST_SUCCESS);

    /* =========================================================================
     * DECORATIVE ELEMENTS
     * =========================================================================
     */

    /* Accent line at very top */
    fb_fill_rect(&fb, 0, 0, 640, 3, theme.colors.accent);

    /* Present the frame */
    fb_present(&fb);

    /* Idle */
    while (1) {
        __asm__ volatile("wfe");
    }
}


/* =============================================================================
 * EXCEPTION HANDLERS
 * =============================================================================
 */

void handle_sync_exception(void *context, uint64_t esr, uint64_t far)
{
    (void)context;
    (void)esr;
    (void)far;
    while (1) __asm__ volatile("wfe");
}

void handle_irq(void *context)
{
    (void)context;
    while (1) __asm__ volatile("wfe");
}

void handle_unhandled_exception(void *context, uint64_t esr)
{
    (void)context;
    (void)esr;
    while (1) __asm__ volatile("wfe");
}