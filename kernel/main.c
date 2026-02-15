/*
 * main.c — Hardware Inspector (Platform-Portable)
 * =================================================
 *
 * A bare-metal OS that introspects and displays detailed hardware
 * information about the device it runs on. Works across all
 * Tutorial-OS supported platforms:
 *
 *   - Raspberry Pi Zero 2W  (BCM2710, ARM64, DPI/HDMI)
 *   - Raspberry Pi CM4       (BCM2711, ARM64, HDMI)
 *   - Raspberry Pi CM5       (BCM2712, ARM64, HDMI)
 *   - Radxa Rock 2A          (RK3528A, ARM64, HDMI)
 *   - Orange Pi RV2          (Ky X1,   RISC-V64, HDMI)
 *
 * PORTABILITY:
 * ------------
 * This file uses ONLY HAL interfaces — no platform-specific includes.
 * All hardware queries go through hal_platform_get_*() functions.
 * Platform-specific strings (CPU core name, boot chain, display
 * interface) are derived from the hal_platform_id_t enum returned
 * by the HAL, NOT from #ifdefs.
 *
 * RESOLUTION SCALING:
 * -------------------
 * The layout was originally designed for 640x480 (DPI on GPi Case).
 * It now scales proportionally to any resolution by computing
 * horizontal and vertical scale factors from the framebuffer
 * dimensions. At 1920x1080, panels are ~3x wider and ~2x taller,
 * with proportional spacing. The 8x16 bitmap font is fixed-size
 * but the increased panel spacing keeps everything readable.
 *
 * OVERFLOW SAFETY:
 * ----------------
 * All arithmetic that could exceed uint32_t (clock rates, memory
 * sizes) is performed at full width (size_t / uint64_t) and only
 * narrowed AFTER division reduces the value to a safe range.
 * Percentages are computed in MHz or MB, never in Hz or bytes.
 */

#include "types.h"
#include "framebuffer.h"

/* HAL Interface Headers */
#include "mmio.h"
#include "hal/hal.h"

/* UI System */
#include "ui_types.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "memory/allocator.h"


/* =============================================================================
 * GLOBALS
 * =============================================================================
 */

static allocator_t alloc;
extern char __heap_start;


/* =============================================================================
 * ARCHITECTURE-PORTABLE IDLE
 * =============================================================================
 *
 * ARM64 uses WFE (wait for event), RISC-V uses WFI (wait for interrupt).
 * Both put the core into a low-power state until an interrupt or event.
 * We define a portable wrapper so main.c doesn't need #ifdef __riscv.
 */

static inline void cpu_idle(void)
{
#if defined(__riscv)
    __asm__ volatile("wfi");
#else
    __asm__ volatile("wfe");
#endif
}


/* =============================================================================
 * NUMBER FORMATTING UTILITIES
 * =============================================================================
 *
 * Bare-metal has no printf/sprintf. These convert integers to strings
 * using a caller-provided buffer. u64_to_dec() writes from the END
 * of the buffer backwards and returns a pointer to the first digit.
 */

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

static uint32_t gcd(uint32_t a, uint32_t b)
{
    while (b) {
        uint32_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/*
 * Format aspect ratio as "W:H" string (e.g. "16:9")
 * Returns pointer to static buffer — not reentrant.
 */
static const char *format_ratio(uint32_t w, uint32_t h)
{
    static char rbuf[16];
    uint32_t g = gcd(w, h);
    if (g == 0) g = 1;
    uint32_t rw = w / g, rh = h / g;

    char *p = rbuf;
    /* Write ratio width */
    char tmp[12];
    char *s = u64_to_dec(rw, tmp);
    while (*s) *p++ = *s++;
    *p++ = ':';
    /* Write ratio height */
    s = u64_to_dec(rh, tmp);
    while (*s) *p++ = *s++;
    *p = '\0';
    return rbuf;
}


/* =============================================================================
 * PLATFORM-SPECIFIC STRING HELPERS
 * =============================================================================
 *
 * These functions return display strings based on the platform ID
 * from the HAL. This keeps all platform awareness in one place
 * rather than scattering #ifdefs through the drawing code.
 *
 * WHY NOT PUT THESE IN THE HAL?
 * These are purely cosmetic/display strings. The HAL provides raw
 * data (platform_id, arch enum); the UI layer decides how to
 * present it to humans. Adding "get_cpu_core_display_name()" to
 * the HAL would violate separation of concerns.
 */

static const char *get_cpu_core_name(hal_platform_id_t id)
{
    switch (id) {
        case HAL_PLATFORM_RPI_ZERO2W:
        case HAL_PLATFORM_RPI_3B:
        case HAL_PLATFORM_RPI_3BP:
            return "Cortex-A53";

        case HAL_PLATFORM_RPI_4B:
        case HAL_PLATFORM_RPI_CM4:
            return "Cortex-A72";

        case HAL_PLATFORM_RPI_5:
        case HAL_PLATFORM_RPI_CM5:
            return "Cortex-A76";

        case HAL_PLATFORM_RADXA_ROCK2A:
            return "Cortex-A53";

        case HAL_PLATFORM_ORANGEPI_RV2:
            return "X60 (RV64GC)";

        default:
            return "CPU";
    }
}

static const char *get_arch_name(hal_arch_t arch)
{
    switch (arch) {
        case HAL_ARCH_ARM64:    return "ARM64";
        case HAL_ARCH_RISCV64:  return "RISC-V64";
        default:                return "Unknown";
    }
}

static const char *get_arch_isa(hal_platform_id_t id)
{
    switch (id) {
        case HAL_PLATFORM_RPI_ZERO2W:
        case HAL_PLATFORM_RPI_3B:
        case HAL_PLATFORM_RPI_3BP:
        case HAL_PLATFORM_RPI_4B:
        case HAL_PLATFORM_RPI_CM4:
        case HAL_PLATFORM_RPI_5:
        case HAL_PLATFORM_RPI_CM5:
        case HAL_PLATFORM_RADXA_ROCK2A:
            return "ARMv8-A";

        case HAL_PLATFORM_ORANGEPI_RV2:
            return "RV64GCV";

        default:
            return "";
    }
}

static const char *get_display_interface(hal_platform_id_t id, uint32_t width)
{
    /*
     * Heuristic: Pi Zero 2W at 640x480 is almost certainly the GPi Case
     * with DPI parallel RGB. Everything else uses HDMI.
     */
    if ((id == HAL_PLATFORM_RPI_ZERO2W) && (width <= 640)) {
        return "DPI RGB666 Parallel";
    }
    if (id == HAL_PLATFORM_RADXA_ROCK2A) {
        return "HDMI (VOP2)";
    }
    if (id == HAL_PLATFORM_ORANGEPI_RV2) {
        return "HDMI (SimpleFB)";
    }
    return "HDMI";
}

/*
 * Boot chain stages — NULL-terminated arrays of stage names.
 * Drawn with arrows between them in the BOOT SEQUENCE panel.
 */
static const char *get_boot_stage(hal_platform_id_t id, int stage)
{
    switch (id) {
        case HAL_PLATFORM_RADXA_ROCK2A:
            switch (stage) {
                case 0: return "BootROM";
                case 1: return "TPL";
                case 2: return "U-Boot";
                case 3: return "Image";
                default: return NULL;
            }
        case HAL_PLATFORM_ORANGEPI_RV2:
            switch (stage) {
                case 0: return "BROM";
                case 1: return "FSBL";
                case 2: return "OpenSBI";
                case 3: return "U-Boot";
                case 4: return "Image";
                default: return NULL;
            }
        default: /* BCM / Raspberry Pi */
            switch (stage) {
                case 0: return "GPU ROM";
                case 1: return "bootcode.bin";
                case 2: return "start.elf";
                case 3: return "kernel8.img";
                default: return NULL;
            }
    }
}


/* =============================================================================
 * LAYOUT COMPUTATION
 * =============================================================================
 *
 * The original layout was designed for 640x480. We compute scale factors
 * to adapt to any resolution:
 *
 *   sx = fb.width  / 640   (horizontal scale, e.g. 3 at 1920)
 *   sy = fb.height / 480   (vertical scale,   e.g. 2 at 1080)
 *
 * Panel positions and sizes are multiplied by these factors. Text
 * itself stays at fixed bitmap font size (8x16 px) — the extra
 * panel space becomes comfortable whitespace.
 *
 * Why separate sx/sy? Because 640x480 is 4:3 but 1920x1080 is 16:9.
 * Using a single scale would either waste horizontal space or
 * overflow vertically.
 */

typedef struct {
    /* Scale factors */
    uint32_t sx, sy;

    /* Outer layout */
    uint32_t margin;        /* Outer margin from screen edge              */
    uint32_t col_gap;       /* Gap between left and right columns         */
    uint32_t full_w;        /* Full content width (both columns + gap)    */
    uint32_t col_w;         /* Single column width                        */
    uint32_t left_x;        /* Left column X position                     */
    uint32_t right_x;       /* Right column X position                    */

    /* Inner panel layout */
    uint32_t pad;           /* Inner padding within panels                */
    uint32_t row_h;         /* Vertical spacing between text rows         */
    uint32_t hdr_h;         /* Panel header height (title + divider)      */
    uint32_t panel_gap;     /* Vertical gap between panels                */

    /* Widget sizes */
    uint32_t bar_w;         /* Progress bar width                         */
    uint32_t bar_h;         /* Progress bar height                        */
    uint32_t badge_h;       /* Badge height                               */
    uint32_t toast_w;       /* Toast notification width                   */
    uint32_t toast_h;       /* Toast notification height                  */
} layout_t;

static layout_t compute_layout(uint32_t fb_w, uint32_t fb_h)
{
    layout_t L;

    L.sx = fb_w / 640;
    if (L.sx < 1) L.sx = 1;
    if (L.sx > 4) L.sx = 4;

    L.sy = fb_h / 480;
    if (L.sy < 1) L.sy = 1;
    if (L.sy > 4) L.sy = 4;

    L.margin    = 10 * L.sx;
    L.col_gap   = 10 * L.sx;
    L.pad       = 8  * L.sx;
    L.row_h     = 14 * L.sy;
    L.hdr_h     = 18 * L.sy;
    L.panel_gap = 6  * L.sy;
    L.bar_w     = 80 * L.sx;
    L.bar_h     = 10 * L.sy;
    L.badge_h   = 14 * L.sy;
    L.toast_w   = 60 * L.sx;
    L.toast_h   = 14 * L.sy;

    L.full_w  = fb_w - 2 * L.margin;
    L.col_w   = (L.full_w - L.col_gap) / 2;
    L.left_x  = L.margin;
    L.right_x = L.margin + L.col_w + L.col_gap;

    return L;
}


/* =============================================================================
 * DRAWING HELPERS
 * =============================================================================
 *
 * Small wrappers to reduce repetition in the main drawing code.
 * These handle the common patterns: draw a clock row with label,
 * progress bar, and MHz value; draw a panel header; etc.
 */

/*
 * Draw a panel section header: title text + horizontal divider below.
 * Returns the Y coordinate where content should start drawing.
 */
static uint32_t draw_panel_header(framebuffer_t *fb, const layout_t *L,
                                  const ui_theme_t *theme,
                                  uint32_t px, uint32_t py, uint32_t pw,
                                  const char *title)
{
    fb_draw_string_transparent(fb, px + L->pad, py + L->pad / 2,
                               title, theme->colors.text_secondary);
    ui_draw_divider_h(fb, px + L->pad, py + L->hdr_h - 2,
                      pw - L->pad * 2, theme);
    return py + L->hdr_h;
}

/*
 * Draw a clock frequency row: label, progress bar, "NNN / MMM MHz".
 * All percentage math done in MHz to avoid uint32_t overflow.
 */
static void draw_clock_row(framebuffer_t *fb, const layout_t *L,
                           const ui_theme_t *theme,
                           uint32_t px, uint32_t y,
                           const char *label, uint32_t freq_hz,
                           uint32_t max_hz, ui_color_t bar_color,
                           char *buf)
{
    uint32_t mhz = freq_hz / 1000000;
    uint32_t max_mhz = max_hz / 1000000;
    if (max_mhz == 0) max_mhz = mhz;

    /* Percentage computed in MHz — safe from overflow */
    uint32_t pct = (max_mhz > 0) ? (mhz * 100) / max_mhz : 0;
    if (pct > 100) pct = 100;

    /* Label */
    fb_draw_string_transparent(fb, px + L->pad, y, label,
                               theme->colors.text_secondary);

    /* Progress bar */
    uint32_t bar_x = px + L->pad + 5 * 8;  /* After ~5 char label */
    ui_rect_t bar = ui_rect(bar_x, y - 1, L->bar_w, L->bar_h);
    ui_draw_progress_bar(fb, bar, theme, pct, bar_color);

    /* Value text: "NNN / MMM MHz" or just "NNN MHz" */
    uint32_t val_x = bar_x + L->bar_w + L->pad / 2;
    fb_draw_string_transparent(fb, val_x, y,
                               u64_to_dec(mhz, buf), theme->colors.text_primary);

    if (max_hz != freq_hz) {
        uint32_t sep_x = val_x + 5 * 8;
        fb_draw_string_transparent(fb, sep_x, y, "/",
                                   theme->colors.text_secondary);
        fb_draw_string_transparent(fb, sep_x + 2 * 8, y,
                                   u64_to_dec(max_mhz, buf),
                                   theme->colors.text_secondary);
        fb_draw_string_transparent(fb, sep_x + 7 * 8, y, "MHz",
                                   theme->colors.text_secondary);
    } else {
        fb_draw_string_transparent(fb, val_x + 5 * 8, y, "MHz",
                                   theme->colors.text_secondary);
    }
}


/* =============================================================================
 * KERNEL MAIN — HARDWARE INSPECTOR
 * =============================================================================
 */

void kernel_main(uintptr_t dtb_ptr, uintptr_t ram_base, uintptr_t ram_size)
{
    /* Store DTB pointer for SimpleFB display init (RISC-V / U-Boot) */
    extern uint64_t __dtb_ptr;
    __dtb_ptr = dtb_ptr;

    /* Initialize heap allocator */
    allocator_init_from_ram(&alloc, ram_base, ram_size,
                            (uintptr_t)&__heap_start,
                            NULL, NULL, NULL, NULL);

    /* Configure GPIO for DPI display (no-op on HDMI platforms) */
    hal_gpio_configure_dpi();

    /* Initialize framebuffer */
    framebuffer_t fb;
    if (!fb_init(&fb)) {
        while (1) { cpu_idle(); }
    }

    fb_present(&fb);

    /* =========================================================================
     * SETUP: theme, layout, platform data
     * =========================================================================
     */

    ui_theme_t theme = ui_theme_for_width(fb.width, UI_PALETTE_DARK);
    fb_clear(&fb, theme.colors.bg_primary);

    layout_t L = compute_layout(fb.width, fb.height);
    char buf[64];

    /* Query platform information from HAL */
    hal_platform_info_t pinfo;
    hal_platform_get_info(&pinfo);

    hal_memory_info_t mem_info;
    hal_platform_get_memory_info(&mem_info);

    /* Derive display strings from platform ID */
    const char *cpu_core    = get_cpu_core_name(pinfo.platform_id);
    const char *arch_name   = get_arch_name(pinfo.arch);
    const char *arch_isa    = get_arch_isa(pinfo.platform_id);
    const char *disp_iface  = get_display_interface(pinfo.platform_id, fb.width);

    /*
     * Panel height computation:
     *   header (hdr_h) + n_rows * row_h + bottom_padding (pad/2)
     */
    uint32_t panel_3row = L.hdr_h + 3 * L.row_h + L.pad;
    uint32_t panel_4row = L.hdr_h + 4 * L.row_h + L.pad;
    uint32_t panel_2row = L.hdr_h + 2 * L.row_h + L.pad;

    /* Current Y position — tracks vertical layout */
    uint32_t cur_y = L.pad;


    /* =========================================================================
     * HEADER — Full width, centered title + subtitle
     * =========================================================================
     */
    uint32_t hdr_h = 40 * L.sy;
    ui_rect_t header_panel = ui_rect(L.left_x, cur_y, L.full_w, hdr_h);
    ui_draw_panel(&fb, header_panel, &theme, UI_PANEL_ELEVATED);

    const char *title = "HARDWARE INSPECTOR";
    uint32_t title_w = fb_text_width_large(title);
    uint32_t title_x = L.left_x + (L.full_w - title_w) / 2;
    fb_draw_string_large_transparent(&fb, title_x, cur_y + L.pad,
                                     title, theme.colors.accent_bright);

    const char *subtitle = pinfo.board_name ? pinfo.board_name : "Bare Metal System";
    uint32_t sub_w = fb_text_width(subtitle);
    uint32_t sub_x = L.left_x + (L.full_w - sub_w) / 2;
    fb_draw_string_transparent(&fb, sub_x, cur_y + hdr_h - L.row_h,
                               subtitle, theme.colors.text_secondary);

    cur_y += hdr_h + L.panel_gap;


    /* =========================================================================
     * ROW 1: BOARD (left) + DISPLAY (right)
     * =========================================================================
     */
    uint32_t row1_h = panel_3row;

    /* --- BOARD PANEL --- */
    {
        uint32_t px = L.left_x, pw = L.col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, row1_h);
        ui_draw_panel(&fb, panel, &theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(&fb, &L, &theme, px, cur_y, pw, "BOARD");

        /* Model + CPU core badge */
        fb_draw_string_transparent(&fb, px + L.pad, y, "Model:",
                                   theme.colors.text_secondary);
        const char *soc = pinfo.soc_name ? pinfo.soc_name : "Unknown";
        fb_draw_string_transparent(&fb, px + L.pad + 7 * 8, y, soc,
                                   theme.colors.text_primary);
        ui_draw_badge(&fb, px + L.pad + 7 * 8 + (uint32_t)fb_text_width(soc) + 8,
                      y - 2, &theme, cpu_core);

        y += L.row_h;

        /* Serial number */
        fb_draw_string_transparent(&fb, px + L.pad, y, "Serial:",
                                   theme.colors.text_secondary);
        if (pinfo.serial_number != 0) {
            fb_draw_string_transparent(&fb, px + L.pad + 7 * 8, y,
                                       u64_to_hex(pinfo.serial_number, buf),
                                       theme.colors.text_primary);
        } else {
            fb_draw_string_transparent(&fb, px + L.pad + 7 * 8, y,
                                       "N/A", theme.colors.text_secondary);
        }

        y += L.row_h;

        /* Architecture badges */
        fb_draw_string_transparent(&fb, px + L.pad, y, "Arch:",
                                   theme.colors.text_secondary);
        ui_draw_badge(&fb, px + L.pad + 6 * 8, y - 2, &theme, arch_name);
        ui_draw_badge(&fb, px + L.pad + 6 * 8 + (uint32_t)fb_text_width(arch_name) + 16,
                      y - 2, &theme, arch_isa);
    }

    /* --- DISPLAY PANEL --- */
    {
        uint32_t px = L.right_x, pw = L.col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, row1_h);
        ui_draw_panel(&fb, panel, &theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(&fb, &L, &theme, px, cur_y, pw, "DISPLAY");

        /* Resolution + aspect ratio badge */
        fb_draw_string_transparent(&fb, px + L.pad, y, "Resolution:",
                                   theme.colors.text_secondary);
        uint32_t res_x = px + L.pad + 11 * 8;
        fb_draw_string_transparent(&fb, res_x, y,
                                   u64_to_dec(fb.width, buf), theme.colors.text_primary);
        res_x += (uint32_t)fb_text_width(u64_to_dec(fb.width, buf));
        fb_draw_string_transparent(&fb, res_x, y, "x", theme.colors.text_secondary);
        res_x += 8;
        fb_draw_string_transparent(&fb, res_x, y,
                                   u64_to_dec(fb.height, buf), theme.colors.text_primary);
        res_x += (uint32_t)fb_text_width(u64_to_dec(fb.height, buf)) + 8;
        ui_draw_badge(&fb, res_x, y - 2, &theme, format_ratio(fb.width, fb.height));

        y += L.row_h;

        /* Pixel format */
        fb_draw_string_transparent(&fb, px + L.pad, y, "Format:",
                                   theme.colors.text_secondary);
        ui_draw_badge(&fb, px + L.pad + 8 * 8, y - 2, &theme, "ARGB8888");
        fb_draw_string_transparent(&fb, px + L.pad + 8 * 8 + 80, y,
                                   "32-bit", theme.colors.text_secondary);

        y += L.row_h;

        /* Display interface */
        fb_draw_string_transparent(&fb, px + L.pad, y, "Interface:",
                                   theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, px + L.pad + 10 * 8, y,
                                   disp_iface, theme.colors.text_primary);
    }

    cur_y += row1_h + L.panel_gap;


    /* =========================================================================
     * ROW 2: PROCESSOR (left) + PERIPHERALS (right)
     * =========================================================================
     */
    uint32_t row2_h = panel_4row;

    /* --- PROCESSOR PANEL --- */
    {
        uint32_t px = L.left_x, pw = L.col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, row2_h);
        ui_draw_panel(&fb, panel, &theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(&fb, &L, &theme, px, cur_y, pw, "PROCESSOR");

        /* ARM / CPU clock */
        uint32_t arm_clock = hal_platform_get_clock_rate(HAL_CLOCK_ARM);
        uint32_t arm_max   = hal_platform_get_arm_freq();
        if (arm_max == 0) arm_max = arm_clock;
        if (arm_clock > 0) {
            draw_clock_row(&fb, &L, &theme, px, y, "CPU", arm_clock, arm_max,
                           theme.colors.accent, buf);
        }

        y += L.row_h;

        /* Core / bus clock */
        uint32_t core_clock = hal_platform_get_clock_rate(HAL_CLOCK_CORE);
        if (core_clock > 0) {
            draw_clock_row(&fb, &L, &theme, px, y, "Core", core_clock, core_clock,
                           theme.colors.info, buf);
        }

        y += L.row_h;

        /* eMMC / SD clock */
        uint32_t emmc_clock = hal_platform_get_clock_rate(HAL_CLOCK_EMMC);
        if (emmc_clock > 0) {
            draw_clock_row(&fb, &L, &theme, px, y, "eMMC", emmc_clock, emmc_clock,
                           theme.colors.warning, buf);
        }

        y += L.row_h;

        /* PWM clock (if available) */
        uint32_t pwm_clock = hal_platform_get_clock_rate(HAL_CLOCK_PWM);
        if (pwm_clock > 0) {
            draw_clock_row(&fb, &L, &theme, px, y, "PWM", pwm_clock, pwm_clock,
                           theme.colors.success, buf);
        }
    }

    /* --- PERIPHERALS PANEL --- */
    {
        uint32_t px = L.right_x, pw = L.col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, row2_h);
        ui_draw_panel(&fb, panel, &theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(&fb, &L, &theme, px, cur_y, pw, "PERIPHERALS");

        /* USB Host */
        bool usb_on = false;
        if (HAL_OK(hal_platform_get_power(HAL_DEVICE_USB, &usb_on))) {
            fb_draw_string_transparent(&fb, px + L.pad, y, "USB Host:",
                                       theme.colors.text_secondary);
            ui_rect_t toast = ui_rect(px + L.pad + 10 * 8, y - 3, L.toast_w, L.toast_h);
            ui_draw_toast(&fb, toast, &theme, usb_on ? "Active" : "Off",
                          usb_on ? UI_TOAST_SUCCESS : UI_TOAST_ERROR);
        }

        y += L.row_h + 4 * L.sy;

        /* SD Card */
        bool sd_on = false;
        if (HAL_OK(hal_platform_get_power(HAL_DEVICE_SD_CARD, &sd_on))) {
            fb_draw_string_transparent(&fb, px + L.pad, y, "SD Card:",
                                       theme.colors.text_secondary);
            ui_rect_t toast = ui_rect(px + L.pad + 10 * 8, y - 3, L.toast_w, L.toast_h);
            ui_draw_toast(&fb, toast, &theme, sd_on ? "Active" : "Off",
                          sd_on ? UI_TOAST_SUCCESS : UI_TOAST_ERROR);
        }

        y += L.row_h + 4 * L.sy;

        /* I2C and SPI — query HAL power state if available */
        bool i2c_on = false;
        bool have_i2c = HAL_OK(hal_platform_get_power(HAL_DEVICE_I2C0, &i2c_on));
        fb_draw_string_transparent(&fb, px + L.pad, y, "I2C:",
                                   theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, px + L.pad + 5 * 8, y,
                                   have_i2c ? (i2c_on ? "Active" : "Off") : "Unknown",
                                   have_i2c ? (i2c_on ? theme.colors.success :
                                                        theme.colors.error) :
                                              theme.colors.text_secondary);

        bool spi_on = false;
        bool have_spi = HAL_OK(hal_platform_get_power(HAL_DEVICE_SPI, &spi_on));
        fb_draw_string_transparent(&fb, px + pw / 2, y, "SPI:",
                                   theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, px + pw / 2 + 5 * 8, y,
                                   have_spi ? (spi_on ? "Active" : "Off") : "Unknown",
                                   have_spi ? (spi_on ? theme.colors.success :
                                                        theme.colors.error) :
                                              theme.colors.text_secondary);
    }

    cur_y += row2_h + L.panel_gap;


    /* =========================================================================
     * ROW 3: MEMORY (left) + SYSTEM STATUS (right)
     * =========================================================================
     */
    uint32_t row3_h = panel_3row;

    /* --- MEMORY PANEL --- */
    {
        uint32_t px = L.left_x, pw = L.col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, row3_h);
        ui_draw_panel(&fb, panel, &theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(&fb, &L, &theme, px, cur_y, pw, "MEMORY");

        /*
         * OVERFLOW-SAFE MEMORY MATH:
         * Division to MB happens at full width (size_t = 64-bit),
         * then we narrow to uint32_t. At 4 GB, that's 4096 MB — fits.
         */
        uint32_t arm_mb   = (uint32_t)(mem_info.arm_size / (1024 * 1024));
        uint32_t gpu_mb   = (uint32_t)(mem_info.gpu_size / (1024 * 1024));
        uint32_t total_mb = arm_mb + gpu_mb;

        if (total_mb > 0) {
            uint32_t arm_pct = (arm_mb * 100) / total_mb;

            /* Total + split bar */
            fb_draw_string_transparent(&fb, px + L.pad, y, "Total:",
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 7 * 8, y,
                                       u64_to_dec(total_mb, buf), theme.colors.text_primary);
            fb_draw_string_transparent(&fb, px + L.pad + 12 * 8, y, "MB",
                                       theme.colors.text_secondary);

            /* Stacked bar: ARM/CPU portion (accent) + GPU portion (warning) */
            uint32_t bar_x = px + L.pad + 15 * 8;
            uint32_t split_bar_w = L.bar_w + 40 * L.sx;
            fb_fill_rounded_rect(&fb, bar_x, y - 1, split_bar_w, L.bar_h, 3,
                                 theme.colors.accent);
            if (gpu_mb > 0) {
                uint32_t gpu_w = split_bar_w - (arm_pct * split_bar_w / 100);
                fb_fill_rounded_rect(&fb, bar_x + split_bar_w - gpu_w, y - 1,
                                     gpu_w, L.bar_h, 3, theme.colors.warning);
                fb_draw_string_transparent(&fb, bar_x + split_bar_w + 8, y,
                                           "CPU/GPU", theme.colors.text_secondary);
            }

            y += L.row_h;

            /* Detail row — only show GPU split if GPU memory is reported */
            if (gpu_mb > 0) {
                fb_draw_string_transparent(&fb, px + L.pad, y, "ARM:",
                                           theme.colors.text_secondary);
                fb_draw_string_transparent(&fb, px + L.pad + 5 * 8, y,
                                           u64_to_dec(arm_mb, buf), theme.colors.text_primary);
                fb_draw_string_transparent(&fb, px + L.pad + 10 * 8, y, "MB",
                                           theme.colors.text_secondary);

                fb_draw_string_transparent(&fb, px + L.pad + 14 * 8, y, "GPU:",
                                           theme.colors.text_secondary);
                fb_draw_string_transparent(&fb, px + L.pad + 19 * 8, y,
                                           u64_to_dec(gpu_mb, buf), theme.colors.text_primary);
                fb_draw_string_transparent(&fb, px + L.pad + 24 * 8, y, "MB",
                                           theme.colors.text_secondary);
            } else {
                /* Unified memory (no ARM/GPU split) — e.g. RISC-V, Rockchip */
                fb_draw_string_transparent(&fb, px + L.pad, y, "RAM:",
                                           theme.colors.text_secondary);
                fb_draw_string_transparent(&fb, px + L.pad + 5 * 8, y,
                                           u64_to_dec(arm_mb, buf), theme.colors.text_primary);
                fb_draw_string_transparent(&fb, px + L.pad + 10 * 8, y, "MB",
                                           theme.colors.text_secondary);
                fb_draw_string_transparent(&fb, px + L.pad + 14 * 8, y, "(unified)",
                                           theme.colors.text_secondary);
            }
        } else {
            /* HAL returned 0 for everything — can't determine memory */
            fb_draw_string_transparent(&fb, px + L.pad, y, "Total:",
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 7 * 8, y,
                                       "Unknown", theme.colors.text_secondary);
            y += L.row_h;
        }

        y += L.row_h;

        fb_draw_string_transparent(&fb, px + L.pad, y, "Heap:",
                                   theme.colors.text_secondary);
        fb_draw_string_transparent(&fb, px + L.pad + 6 * 8, y,
                                   u64_to_hex((uintptr_t)&__heap_start, buf),
                                   theme.colors.text_primary);
    }

    /* --- SYSTEM STATUS PANEL --- */
    {
        uint32_t px = L.right_x, pw = L.col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, row3_h);
        ui_draw_panel(&fb, panel, &theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(&fb, &L, &theme, px, cur_y, pw, "SYSTEM STATUS");

        /* Temperature */
        int32_t temp_mc = 0;
        int32_t temp_max_mc = 0;
        bool have_temp = HAL_OK(hal_platform_get_temperature(&temp_mc)) &&
                         HAL_OK(hal_platform_get_max_temperature(&temp_max_mc));

        if (have_temp && temp_mc > 0) {
            uint32_t temp_c = temp_mc / 1000;
            /* Safe percentage: both values already in millicelsius (< 200000) */
            uint32_t temp_pct = (temp_max_mc > 0) ?
                ((uint32_t)temp_mc * 100) / (uint32_t)temp_max_mc : 0;
            if (temp_pct > 100) temp_pct = 100;

            fb_draw_string_transparent(&fb, px + L.pad, y, "Temp:",
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 6 * 8, y,
                                       u64_to_dec(temp_c, buf), theme.colors.text_primary);
            fb_draw_string_transparent(&fb, px + L.pad + 9 * 8, y, "C",
                                       theme.colors.text_primary);

            ui_rect_t temp_bar = ui_rect(px + L.pad + 11 * 8, y - 1,
                                         L.bar_w, L.bar_h);
            ui_color_t tc = theme.colors.success;
            if (temp_pct > 80) tc = theme.colors.error;
            else if (temp_pct > 60) tc = theme.colors.warning;
            ui_draw_progress_bar(&fb, temp_bar, &theme, temp_pct, tc);

            fb_draw_string_transparent(&fb, px + L.pad + 11 * 8 + L.bar_w + 8, y,
                                       "max", theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 15 * 8 + L.bar_w + 8, y,
                                       u64_to_dec(temp_max_mc / 1000, buf),
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 18 * 8 + L.bar_w + 8, y,
                                       "C", theme.colors.text_secondary);
        } else {
            /* No temperature sensor — show placeholder */
            fb_draw_string_transparent(&fb, px + L.pad, y, "Temp:",
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 6 * 8, y,
                                       "(no sensor)", theme.colors.text_secondary);
        }

        y += L.row_h;

        /* Throttle status — only meaningful on platforms that report it */
        uint32_t throttle = 0;
        hal_error_t throttle_err = hal_platform_get_throttle_status(&throttle);
        if (HAL_OK(throttle_err)) {
            bool undervolt    = throttle & HAL_THROTTLE_UNDERVOLT_NOW;
            bool is_throttled = throttle & HAL_THROTTLE_THROTTLED_NOW;
            bool freq_cap     = throttle & HAL_THROTTLE_ARM_FREQ_CAPPED;

            fb_draw_string_transparent(&fb, px + L.pad, y, "Voltage:",
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 8 * 8, y,
                                       undervolt ? "LOW" : "OK",
                                       undervolt ? theme.colors.error :
                                                   theme.colors.success);

            fb_draw_string_transparent(&fb, px + L.pad + 14 * 8, y, "Throttle:",
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 23 * 8, y,
                                       is_throttled ? "YES" : "NO",
                                       is_throttled ? theme.colors.error :
                                                      theme.colors.success);

            fb_draw_string_transparent(&fb, px + L.pad + 28 * 8, y, "Cap:",
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 32 * 8, y,
                                       freq_cap ? "YES" : "NO",
                                       freq_cap ? theme.colors.error :
                                                  theme.colors.success);
        } else {
            /* Platform doesn't support throttle reporting */
            fb_draw_string_transparent(&fb, px + L.pad, y, "Throttle:",
                                       theme.colors.text_secondary);
            fb_draw_string_transparent(&fb, px + L.pad + 10 * 8, y,
                                       "N/A", theme.colors.text_secondary);
        }
    }

    cur_y += row3_h + L.panel_gap;


    /* =========================================================================
     * BOOT SEQUENCE — Full width
     * =========================================================================
     */
    {
        uint32_t boot_h = panel_2row;
        ui_rect_t panel = ui_rect(L.left_x, cur_y, L.full_w, boot_h);
        ui_draw_panel(&fb, panel, &theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(&fb, &L, &theme, L.left_x, cur_y,
                                       L.full_w, "BOOT SEQUENCE");

        /* Draw boot stages with arrows */
		uint32_t bx = L.left_x + L.pad;
		for (int i = 0; ; i++) {
 			const char *stage = get_boot_stage(pinfo.platform_id, i);
    		if (!stage) break;

    		if (i > 0) {
        		fb_draw_string_transparent(&fb, bx, y, "->", theme.colors.accent);
        		bx += 3 * 8;
    		}

   		 	bool is_last = (get_boot_stage(pinfo.platform_id, i + 1) == NULL);
    		fb_draw_string_transparent(&fb, bx, y, stage,
                               is_last ? theme.colors.accent_bright :
                                         theme.colors.text_primary);
    		bx += (uint32_t)fb_text_width(stage) + 8;
		}

        /* "(this code!)" annotation after the last stage */
        fb_draw_string_transparent(&fb, bx + 4, y, "(this code!)",
                                   theme.colors.text_secondary);

        cur_y += boot_h + L.panel_gap;
    }


    /* =========================================================================
     * FOOTER — Full width
     * =========================================================================
     */
    {
        uint32_t ftr_h = 28 * L.sy;
        ui_rect_t footer = ui_rect(L.left_x, cur_y, L.full_w, ftr_h);
        ui_draw_panel(&fb, footer, &theme, UI_PANEL_ELEVATED);

        uint32_t badge_y = cur_y + (ftr_h - 14) / 2;

        /* Left side badges */
        uint32_t bx = L.left_x + L.pad;
        ui_draw_badge(&fb, bx, badge_y, &theme, "BARE METAL");
        bx += (uint32_t)fb_text_width("BARE METAL") + 20;
        ui_draw_badge(&fb, bx, badge_y, &theme, "NO OS");
        bx += (uint32_t)fb_text_width("NO OS") + 20;
        ui_draw_badge(&fb, bx, badge_y, &theme, "TUTORIAL-OS");

        /* Right side "System Ready" toast */
        uint32_t ready_w = 120 * L.sx;
        uint32_t ready_h = 20 * L.sy;
        ui_rect_t ready = ui_rect(L.left_x + L.full_w - ready_w - L.pad,
                                  cur_y + (ftr_h - ready_h) / 2,
                                  ready_w, ready_h);
        ui_draw_toast(&fb, ready, &theme, "System Ready", UI_TOAST_SUCCESS);
    }


    /* =========================================================================
     * DECORATIVE ELEMENTS
     * =========================================================================
     */

    /* Accent line at very top of screen */
    fb_fill_rect(&fb, 0, 0, fb.width, 3 * L.sy, theme.colors.accent);

    /* Present the completed frame */
    fb_present(&fb);

    /* Idle — wait for interrupts forever */
    while (1) {
        cpu_idle();
    }
}


/* =============================================================================
 * EXCEPTION HANDLERS
 * =============================================================================
 *
 * These are called by the architecture-specific vector table:
 *   ARM64:   vectors.S dispatches based on ESR_EL1
 *   RISC-V:  vectors.S dispatches based on scause
 *
 * For now we just halt. A real OS would log the fault, dump
 * registers, and potentially recover.
 */

void handle_sync_exception(void *context, uint64_t esr, uint64_t far)
{
    (void)context;
    (void)esr;
    (void)far;
    while (1) cpu_idle();
}

void handle_irq(void *context)
{
    (void)context;
    while (1) cpu_idle();
}

void handle_unhandled_exception(void *context, uint64_t esr)
{
    (void)context;
    (void)esr;
    while (1) cpu_idle();
}