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

    L.sy = fb_h / 360;
    if (L.sy < 1) L.sy = 1;

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
    framebuffer_t fb = {0};  // zero-initialize before fb_init
    if (!fb_init(&fb)) {
        while (1) { cpu_idle(); }
    }

 	ui_theme_t theme = ui_theme_for_width(fb.width, UI_PALETTE_DARK);

	/* =========================================================================
 	* DIAGNOSTIC TEST — Replace everything after fb_clear with this block.
 	* Each row tests one primitive. Label each with a plain fb_fill_rect
 	* strip on the left so you can identify which row is which even if
 	* text is broken.
 	* =========================================================================
 	*/

	fb_clear(&fb, theme.colors.bg_primary);

	uint32_t y = 20;
	uint32_t stripe_w = 16;  /* colored ID stripe on left edge */

	/* --- ROW 1: fb_fill_rect (known working baseline) --- */
	fb_fill_rect(&fb, 0,        y, stripe_w, 30, 0xFFFFFFFF);  /* white ID */
	fb_fill_rect(&fb, 40,       y, 400, 30, 0xFFFF0000);       /* red box */
	fb_fill_rect(&fb, 460,      y, 400, 30, 0xFF00FF00);       /* green box */
	fb_fill_rect(&fb, 880,      y, 400, 30, 0xFF0000FF);       /* blue box */
	y += 50;

	/* --- ROW 2: fb_fill_rounded_rect, radius=0 (should == fb_fill_rect) --- */
	fb_fill_rect(&fb, 0,   y, stripe_w, 30, 0xFFFF0000);
	fb_fill_rounded_rect(&fb, 40,  y, 400, 30, 0, 0xFFFF0000);
	fb_fill_rounded_rect(&fb, 460, y, 400, 30, 0, 0xFF00FF00);
	y += 50;

	/* --- ROW 3: fb_fill_rounded_rect, radius=8 --- */
	fb_fill_rect(&fb, 0,   y, stripe_w, 30, 0xFF00FF00);
	fb_fill_rounded_rect(&fb, 40,  y, 400, 30, 8, 0xFFFF0000);
	fb_fill_rounded_rect(&fb, 460, y, 400, 30, 8, 0xFF00FF00);
	y += 50;

    /* --- ROW 4: fb_draw_line (pixel-level test) --- */
    fb_fill_rect(&fb, 0, y, stripe_w, 30, 0xFF0000FF);
    fb_draw_line(&fb, 40, y + 15, 440, y + 15, 0xFFFFFFFF);
    y += 50;

	/* --- ROW 5: fb_draw_string_transparent --- */
	fb_fill_rect(&fb, 0, y, stripe_w, 30, 0xFFFFFF00);
	fb_draw_string_transparent(&fb, 40, y, "HELLO WORLD - fb_draw_string_transparent", 0xFFFFFFFF);
	y += 50;

	/* --- ROW 6: fb_draw_string_large_transparent --- */
	fb_fill_rect(&fb, 0, y, stripe_w, 30, 0xFFFF00FF);
	fb_draw_string_large_transparent(&fb, 40, y, "LARGE TEXT TEST", 0xFFFFFFFF);
	y += 50;

	/* --- ROW 7: ui_draw_panel --- */
	fb_fill_rect(&fb, 0, y, stripe_w, 50, 0xFF00FFFF);
	ui_rect_t test_panel = ui_rect(40, y, 600, 50);
	ui_draw_panel(&fb, test_panel, &theme, UI_PANEL_ELEVATED);
	y += 70;

	/* --- ROW 8: ui_draw_badge --- */
	fb_fill_rect(&fb, 0, y, stripe_w, 30, 0xFF8080FF);
	ui_draw_badge(&fb, 40, y, &theme, "BADGE TEST");
	ui_draw_badge(&fb, 200, y, &theme, "ANOTHER");
	y += 50;

	/* --- ROW 9: ui_draw_progress_bar --- */
	fb_fill_rect(&fb, 0, y, stripe_w, 20, 0xFFFF8080);
	ui_rect_t pbar = ui_rect(40, y, 300, 16);
	ui_draw_progress_bar(&fb, pbar, &theme, 66, theme.colors.accent);
	y += 40;

	/* --- ROW 10: ui_draw_toast --- */
	fb_fill_rect(&fb, 0, y, stripe_w, 30, 0xFF80FF80);
	ui_rect_t toast = ui_rect(40, y, 120, 24);
	ui_draw_toast(&fb, toast, &theme, "OK", UI_TOAST_SUCCESS);
	ui_rect_t toast2 = ui_rect(200, y, 120, 24);
	ui_draw_toast(&fb, toast2, &theme, "ERROR", UI_TOAST_ERROR);
	y += 50;

	/* Accent bar so we know the frame was presented */
	fb_fill_rect(&fb, 0, 0, fb.width, 6, theme.colors.accent);

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