/*
 * kernel/main.c — Hardware Inspector (Platform-Portable)
 * =======================================================
 *
 * A bare-metal OS that introspects and displays detailed hardware
 * information about the device it runs on. Works across all
 * Tutorial-OS supported platforms:
 *
 *   - Raspberry Pi Zero 2W   (BCM2710,     ARM64,   DPI/HDMI)
 *   - Raspberry Pi CM4       (BCM2711,     ARM64,   HDMI)
 *   - Raspberry Pi CM5       (BCM2712,     ARM64,   HDMI)
 *   - Milk-V Mars            (JH7110,      RISC-V,  HDMI)
 *   - Orange Pi RV2          (KyX1,        RISC-V,  HDMI)
 *   - LattePanda IOTA        (N100,        x86_64,  HDMI/DP via GOP)
 *   - LattePanda MU          (N100/N305,   x86_64,  HDMI/DP via GOP)
 *
 * LIVE UPDATE ARCHITECTURE:
 * -------------------------
 * The display is split into two categories of content:
 *
 *   STATIC  — Board identity, serial number, architecture, memory layout,
 *             display info, peripherals, boot sequence.  Drawn ONCE at
 *             startup, never touched again.
 *
 *   DYNAMIC — CPU/Core/eMMC clock frequencies, CPU temperature,
 *             throttle/undervoltage status.  Polled and redrawn every
 *             UPDATE_INTERVAL_MS milliseconds.
 *
 * Only the two dynamic panels (PROCESSOR, SYSTEM STATUS) are erased and
 * redrawn on each update cycle.  Everything else stays in DRAM untouched,
 * which minimises the per-frame DMA flush cost on SimpleFB platforms.
 *
 * LAYOUT (three rows of panels):
 *   Row 1: BOARD (left)      + DISPLAY (right)       — static
 *   Row 2: PROCESSOR (left)  + PERIPHERALS (right)   — PROCESSOR dynamic
 *   Row 3: MEMORY (left)     + SYSTEM STATUS (right) — SYSTEM STATUS dynamic
 *   Full:  BOOT SEQUENCE                             — static
 *
 * PORTABILITY:
 * ------------
 * This file uses ONLY HAL interfaces — no platform-specific includes.
 * All hardware queries go through hal_platform_get_*() functions.
 * Platform-specific strings are derived from hal_platform_id_t, NOT #ifdefs.
 *
 * RESOLUTION SCALING:
 * -------------------
 * The layout was originally designed for 640×480 (DPI on GPi Case 2W).
 * It now scales proportionally to any resolution by computing horizontal
 * and vertical scale factors from the framebuffer dimensions.
 *
 * FONT SCALING:
 * -------------
 * Panel geometry scales with sx/sy. Text scales independently via
 * font_scale = sx/2 (clamped to [1,3]).  All text drawing goes through
 * mstr()/mtw() wrappers; all character-position offsets use L->char_w
 * (= 8 * font_scale) instead of the literal constant 8.
 *
 *   Resolution   sx  font_scale  char_w
 *   1920×1080     3      1         8px   (unchanged behaviour)
 *   2560×1440     4      2        16px
 *   3840×2160     6      3        24px
 *
 * OVERFLOW SAFETY:
 * ----------------
 * All arithmetic that could exceed uint32_t (clock rates, memory sizes)
 * is performed at full width (size_t / uint64_t) and only narrowed AFTER
 * division reduces the value to a safe range.
 *
 * KERNEL_MAIN SIGNATURE:
 * ----------------------
 *   void kernel_main(framebuffer_t *boot_fb)
 *
 *   boot_fb is non-NULL only on x86_64 (UEFI), where the GOP framebuffer
 *   must be captured before ExitBootServices() and cannot be re-initialized
 *   afterward. On ARM64 and RISC-V, boot_fb is NULL and we call fb_init().
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
 * CONFIGURATION
 * =============================================================================
 */

#define UPDATE_INTERVAL_MS  1000U


/* =============================================================================
 * GLOBALS
 * =============================================================================
 */

static allocator_t alloc;
extern char __heap_start;

extern uint64_t __ram_base;
extern uint64_t __ram_size;


/* =============================================================================
 * ARCHITECTURE-PORTABLE IDLE
 * =============================================================================
 */

static inline void cpu_idle(void)
{
#if defined(__riscv)
    __asm__ volatile("wfi");
#elif defined(__x86_64__)
    __asm__ volatile("pause");
#else
    __asm__ volatile("wfe");
#endif
}


/* =============================================================================
 * NUMBER FORMATTING UTILITIES
 * =============================================================================
 *
 * Bare-metal has no printf/sprintf. These convert integers to strings
 * using a caller-provided buffer. u64_to_dec() writes from the END of
 * the buffer backwards and returns a pointer to the first digit.
 */

static char *u64_to_dec(uint64_t val, char *buf)
{
    buf[19] = '\0';
    int i = 18;
    if (val == 0) { buf[i--] = '0'; }
    else {
        while (val > 0 && i >= 0) {
            buf[i--] = '0' + (int)(val % 10);
            val /= 10;
        }
    }
    return &buf[i + 1];
}

static char *u64_to_hex(uint64_t val, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[18] = '\0';
    int i = 17;
    if (val == 0) { buf[i--] = '0'; }
    else {
        while (val > 0 && i >= 1) {
            buf[i--] = hex[val & 0xF];
            val >>= 4;
        }
    }
    buf[i--] = 'x';
    buf[i]   = '0';
    return &buf[i];
}

static uint32_t gcd(uint32_t a, uint32_t b)
{
    while (b) { uint32_t t = b; b = a % b; a = t; }
    return a;
}

static const char *format_ratio(uint32_t w, uint32_t h)
{
    static char buf[16];
    uint32_t g = gcd(w, h);
    uint32_t rw = w / g, rh = h / g;

    int i = 0;
    uint32_t tmp = rw;
    char tmp_buf[8]; int ti = 7; tmp_buf[ti] = '\0';
    if (tmp == 0) { tmp_buf[--ti] = '0'; }
    else { while (tmp) { tmp_buf[--ti] = '0' + (tmp % 10); tmp /= 10; } }
    while (tmp_buf[ti]) buf[i++] = tmp_buf[ti++];
    buf[i++] = ':';
    ti = 7; tmp_buf[ti] = '\0'; tmp = rh;
    if (tmp == 0) { tmp_buf[--ti] = '0'; }
    else { while (tmp) { tmp_buf[--ti] = '0' + (tmp % 10); tmp /= 10; } }
    while (tmp_buf[ti]) buf[i++] = tmp_buf[ti++];
    buf[i] = '\0';
    return buf;
}


/* =============================================================================
 * LAYOUT
 * =============================================================================
 *
 * The original design targeted 640×480 (DPI on GPi Case 2W). We compute
 * scale factors to adapt to any resolution:
 *
 *   sx = fb.width  / 640   (horizontal scale, e.g. 3 at 1920, 6 at 3840)
 *   sy = fb.height / 360   (vertical scale,   e.g. 3 at 1080, 6 at 2160)
 *
 * Using 360 instead of 480 as the sy divisor ensures panels fill the screen
 * at all standard 16:9 resolutions (720p, 1080p, 1440p, 4K).
 *
 * Panel positions and sizes are multiplied by these factors.
 *
 * Font scales separately via font_scale = sx/2 (clamped [1,3]).
 * All character-position offsets use L->char_w (= 8 * font_scale)
 * so they stay aligned with the drawn text at any resolution.
 *
 * Why separate sx/sy? Because 640×480 is 4:3 but 1920×1080 is 16:9.
 * Using a single scale would either waste horizontal space or overflow
 * vertically.
 */
typedef struct {
    /* Scale factors */
    uint32_t sx, sy;
    uint32_t font_scale;   /* Text scale factor: sx/2, clamped [1,3]     */
    uint32_t char_w;       /* Pixels per character: 8 * font_scale       */

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

    /* Font scale: half of sx, clamped to [1, 3].
     * At 1080p (sx=3): font_scale=1 — preserves existing 8px behaviour.
     * At 4K    (sx=6): font_scale=3 — 24px chars fill the larger panels. */
    L.font_scale = L.sx / 2;
    if (L.font_scale < 1) L.font_scale = 1;
    if (L.font_scale > 3) L.font_scale = 3;
    L.char_w = 8 * L.font_scale;

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

/*
 * panel_height() — height of a panel containing n text rows.
 *
 * Includes the header row and bottom padding. Used identically in both
 * draw_static_panels and draw_dynamic_panels so Y positions always match.
 */
static inline uint32_t panel_height(const layout_t *L, int rows)
{
    return L->hdr_h + (uint32_t)rows * L->row_h + L->pad;
}


/* =============================================================================
 * STATE STRUCTURES
 * =============================================================================
 */

/*
 * static_state_t — hardware data that never changes while the system runs.
 *
 * Collected once in static_state_init() during boot. All expensive
 * mailbox / DTB / I2C queries happen here so the render loop never pays
 * for them.
 *
 * Adding a field here is the correct place to put any new "query once"
 * data. Do NOT add HAL calls to draw_static_panels() — that function
 * only reads from this struct.
 */
typedef struct {
    hal_platform_info_t pinfo;
    hal_memory_info_t   minfo;

    /* Pre-computed max values used as progress-bar denominators */
    uint32_t arm_max_hz;        /* Max ARM/CPU freq at boot (denominator) */
    int32_t  temp_max_mc;       /* Max junction temp in millicelsius      */
    bool     have_temp_max;     /* Platform reports a max temperature     */

    /* Pre-resolved display strings — avoids per-frame switch statements */
    const char *cpu_core;       /* e.g. "Cortex-A53", "U74", "Alder Lake E" */
    const char *arch_name;      /* e.g. "ARM64", "RISC-V", "x86_64"      */
    const char *arch_isa;       /* e.g. "AArch64", "RV64GC", "AMD64"     */
    const char *disp_iface;     /* e.g. "DPI RGB666", "HDMI", "UEFI GOP" */
} static_state_t;

/*
 * dynamic_state_t — hardware data that changes at runtime.
 *
 * Polled on every frame by dynamic_state_poll(). Only HAL calls that
 * return time-varying values should appear in that function.
 *
 * Each field has a matching boolean that indicates whether the HAL
 * successfully returned a value. Drawing code checks these before
 * attempting to render — platforms without a feature gracefully show
 * "N/A" rather than garbage or zero.
 */
typedef struct {
    /* Clock frequencies (Hz) */
    uint32_t arm_hz;            /* Current (possibly throttled) CPU freq  */
    uint32_t core_hz;           /* Core/GPU clock                         */
    uint32_t emmc_hz;           /* eMMC / SD controller clock             */
    uint32_t pwm_hz;            /* PWM clock (may be 0 = not available)   */

    /* Temperature */
    int32_t  temp_mc;           /* Current temperature in millicelsius    */
    bool     have_temp;         /* true if temp read succeeded            */

    /* Throttle / undervoltage */
    uint32_t throttle;          /* HAL_THROTTLE_* bitmask                 */
    bool     have_throttle;     /* true if throttle read succeeded        */
} dynamic_state_t;


/* =============================================================================
 * PLATFORM STRING HELPERS
 * =============================================================================
 *
 * These derive display strings from the platform ID returned by the HAL.
 * NO #ifdefs — the platform_id value IS the branch. This means a binary
 * built for one platform will still compile correctly on all others.
 */

static const char *get_cpu_core(hal_platform_id_t id)
{
    switch (id) {
        case HAL_PLATFORM_RPI_ZERO2W:
        case HAL_PLATFORM_RPI_3B:
        case HAL_PLATFORM_RPI_3BP:   return "Cortex-A53";
        case HAL_PLATFORM_RPI_4B:
        case HAL_PLATFORM_RPI_CM4:   return "Cortex-A72";
        case HAL_PLATFORM_RPI_5:
        case HAL_PLATFORM_RPI_CM5:   return "Cortex-A76";
        case HAL_PLATFORM_MILKV_MARS: return "SiFive U74";
        case HAL_PLATFORM_ORANGEPI_RV2: return "XuanTie C908";
        case HAL_PLATFORM_LATTEPANDA_MU:
        case HAL_PLATFORM_LATTEPANDA_IOTA: return "Alder Lake-N E";
        default:                     return "Unknown";
    }
}

static const char *get_arch_name(hal_platform_id_t id)
{
    switch (id) {
        case HAL_PLATFORM_RPI_ZERO2W:
        case HAL_PLATFORM_RPI_3B:
        case HAL_PLATFORM_RPI_3BP:
        case HAL_PLATFORM_RPI_4B:
        case HAL_PLATFORM_RPI_CM4:
        case HAL_PLATFORM_RPI_5:
        case HAL_PLATFORM_RPI_CM5:   return "ARM64";
        case HAL_PLATFORM_MILKV_MARS:
        case HAL_PLATFORM_ORANGEPI_RV2: return "RISC-V";
        case HAL_PLATFORM_LATTEPANDA_MU:
        case HAL_PLATFORM_LATTEPANDA_IOTA: return "x86_64";
        default:                     return "Unknown";
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
        case HAL_PLATFORM_RPI_CM5:   return "AArch64";
        case HAL_PLATFORM_MILKV_MARS:
        case HAL_PLATFORM_ORANGEPI_RV2: return "RV64GC";
        case HAL_PLATFORM_LATTEPANDA_MU:
        case HAL_PLATFORM_LATTEPANDA_IOTA: return "AMD64";
        default:                     return "Unknown";
    }
}

static const char *get_display_interface(hal_platform_id_t id, uint32_t fb_width)
{
    switch (id) {
        case HAL_PLATFORM_RPI_ZERO2W:
        case HAL_PLATFORM_RPI_3B:
        case HAL_PLATFORM_RPI_3BP:
            return (fb_width <= 640) ? "DPI RGB666 Parallel" : "HDMI";
        case HAL_PLATFORM_RPI_4B:
        case HAL_PLATFORM_RPI_CM4:   return "HDMI";
        case HAL_PLATFORM_RPI_5:
        case HAL_PLATFORM_RPI_CM5:   return "HDMI";
        case HAL_PLATFORM_MILKV_MARS: return "HDMI (SimpleFB)";
        case HAL_PLATFORM_ORANGEPI_RV2: return "HDMI (SimpleFB)";
        case HAL_PLATFORM_LATTEPANDA_MU:
        case HAL_PLATFORM_LATTEPANDA_IOTA: return "UEFI GOP";
        default:                     return "Unknown";
    }
}

static const char *get_boot_stage(hal_platform_id_t id, int index)
{
    switch (id) {
        case HAL_PLATFORM_RPI_ZERO2W:
        case HAL_PLATFORM_RPI_3B:
        case HAL_PLATFORM_RPI_3BP:
        case HAL_PLATFORM_RPI_4B:
        case HAL_PLATFORM_RPI_CM4:
        case HAL_PLATFORM_RPI_5:
        case HAL_PLATFORM_RPI_CM5: {
            static const char *bcm[] = {
                "GPU FW", "ATF (BL31)", "U-Boot SPL", "U-Boot", "Kernel", NULL
            };
            return bcm[index];
        }
        case HAL_PLATFORM_MILKV_MARS:
        case HAL_PLATFORM_ORANGEPI_RV2: {
            static const char *rv[] = {
                "SPL", "OpenSBI", "U-Boot", "Kernel", NULL
            };
            return rv[index];
        }
        case HAL_PLATFORM_LATTEPANDA_IOTA:
        case HAL_PLATFORM_LATTEPANDA_MU: {
            static const char *x86[] = {
                "UEFI", "BOOTX64.EFI", NULL
            };
            return x86[index];
        }
        default:
            return (index == 0) ? "Kernel" : NULL;
    }
}


/* =============================================================================
 * FONT-SCALE-AWARE TEXT WRAPPERS
 * =============================================================================
 *
 * Every text draw call in main.c goes through mstr()/mtw() so that
 * character size scales automatically with the layout.
 *
 * mstr() — draw a string with transparent background at current font_scale.
 * mtw()  — measure a string's pixel width at current font_scale.
 *
 * At font_scale=1 (≤1080p): delegates to fb_draw_string_transparent.
 *   → Identical to previous behaviour on Milk-V Mars, Raspberry Pi, etc.
 *
 * At font_scale>1 (1440p, 4K): delegates to fb_draw_string_scaled_transparent.
 *   → Requires fb_draw_string_scaled_transparent() in framebuffer.c.
 *     See framebuffer_addition.c for the implementation to paste in.
 *
 * TEACHING NOTE:
 * The wrapper pattern is a classical "single point of change" design.
 * Adding font scaling to a codebase with 50+ text draw calls would
 * normally require touching every call site. By routing all calls through
 * these two functions, the scaling logic lives in exactly one place.
 * At font_scale=1: delegates to fb_draw_string_transparent (no overhead).
 * Widgets use the same pattern via wstr()/wtw() in ui_widgets.c, reading
 * font_scale from theme instead of layout — same value, same scale.
 */

static void mstr(framebuffer_t *fb, const layout_t *L,
                 uint32_t x, uint32_t y, const char *s, uint32_t color)
{
    if (L->font_scale <= 1)
        fb_draw_string_transparent(fb, x, y, s, color);
    else
        fb_draw_string_scaled_transparent(fb, x, y, s, color, L->font_scale);
}

static uint32_t mtw(const layout_t *L, const char *s)
{
    return fb_text_width(s) * L->font_scale;
}


/* =============================================================================
 * DRAWING HELPERS
 * =============================================================================
 */

/*
 * draw_panel_header() — title text + horizontal divider.
 * Returns the Y coordinate where panel content should begin.
 */
static uint32_t draw_panel_header(framebuffer_t *fb,
                                   const layout_t *L,
                                   const ui_theme_t *theme,
                                   uint32_t px, uint32_t py,
                                   uint32_t pw, const char *title)
{
    mstr(fb, L, px + L->pad, py + L->pad / 2, title, theme->colors.accent);
    uint32_t divider_y = py + L->hdr_h - 2;
    fb_draw_hline(fb, px + L->pad, divider_y, pw - 2 * L->pad,
                  theme->colors.border);
    return divider_y + L->pad / 2;
}

/*
 * draw_clock_row() — label + progress bar + "NNN / MMM MHz"
 *
 * All percentage math done in MHz to avoid uint32_t overflow.
 * Character positions use L->char_w so they scale with font_scale.
 */
static void draw_clock_row(framebuffer_t *fb, const layout_t *L,
                            const ui_theme_t *theme,
                            uint32_t px, uint32_t y,
                            const char *label, uint32_t freq_hz,
                            uint32_t max_hz, ui_color_t bar_color,
                            char *buf)
{
    uint32_t mhz     = freq_hz / 1000000;
    uint32_t max_mhz = max_hz  / 1000000;
    if (max_mhz == 0) max_mhz = mhz;

    uint32_t pct = (max_mhz > 0) ? (mhz * 100) / max_mhz : 0;
    if (pct > 100) pct = 100;

    mstr(fb, L, px + L->pad, y, label, theme->colors.text_secondary);

    uint32_t bar_x = px + L->pad + 5 * L->char_w;
    ui_rect_t bar  = ui_rect(bar_x, y - 1, L->bar_w, L->bar_h);
    ui_draw_progress_bar(fb, bar, theme, pct, bar_color);

    uint32_t val_x = bar_x + L->bar_w + L->pad / 2;
    mstr(fb, L, val_x, y, u64_to_dec(mhz, buf), theme->colors.text_primary);

    if (max_hz != freq_hz) {
        uint32_t sep_x = val_x + 5 * L->char_w;
        mstr(fb, L, sep_x,                        y, "/",
             theme->colors.text_secondary);
        mstr(fb, L, sep_x + 2 * L->char_w,        y,
             u64_to_dec(max_mhz, buf), theme->colors.text_secondary);
        mstr(fb, L, sep_x + 7 * L->char_w,        y, "MHz",
             theme->colors.text_secondary);
    } else {
        mstr(fb, L, val_x + 5 * L->char_w, y, "MHz",
             theme->colors.text_secondary);
    }
}


/* =============================================================================
 * STATIC STATE INIT
 * =============================================================================
 */

static void static_state_init(static_state_t *s, uint32_t fb_width)
{
    /* Platform identity */
    hal_platform_get_info(&s->pinfo);
    hal_platform_get_memory_info(&s->minfo);

    /* Max CPU frequency — fixed at boot, used as progress-bar ceiling */
    s->arm_max_hz = hal_platform_get_arm_freq();

    /* Max temperature — fixed property of the silicon */
    s->have_temp_max = HAL_OK(hal_platform_get_max_temperature(&s->temp_max_mc));

    /* Pre-resolve display strings once */
    s->cpu_core   = get_cpu_core(s->pinfo.platform_id);
    s->arch_name  = get_arch_name(s->pinfo.platform_id);
    s->arch_isa   = get_arch_isa(s->pinfo.platform_id);
    s->disp_iface = get_display_interface(s->pinfo.platform_id, fb_width);
}


/* =============================================================================
 * DYNAMIC STATE POLL
 * =============================================================================
 */

static void dynamic_state_poll(dynamic_state_t *d)
{
    /* Clock frequencies — all can change between frames */
    d->arm_hz  = hal_platform_get_arm_freq();
    d->core_hz = hal_platform_get_clock_rate(HAL_CLOCK_CORE);
    d->emmc_hz = hal_platform_get_clock_rate(HAL_CLOCK_EMMC);
    d->pwm_hz  = hal_platform_get_clock_rate(HAL_CLOCK_PWM);

    /* Temperature */
    d->have_temp = HAL_OK(hal_platform_get_temperature(&d->temp_mc));

    /* Throttle / undervoltage flags */
    d->have_throttle = HAL_OK(hal_platform_get_throttle_status(&d->throttle));
}


/* =============================================================================
 * DRAW STATIC PANELS
 * =============================================================================
 *
 * Called ONCE at startup. Draws everything that never changes:
 *   - Title / subtitle header          (full width)
 *   - Row 1: BOARD (left)              + DISPLAY (right)
 *   - Row 2: [PROCESSOR placeholder]   + PERIPHERALS (right)
 *   - Row 3: MEMORY (left)             + [SYSTEM STATUS placeholder]
 *   - BOOT SEQUENCE                    (full width)
 *
 * The PROCESSOR and SYSTEM STATUS panels are drawn by draw_dynamic_panels.
 * We advance cur_y past row 2 and draw PERIPHERALS at the correct Y so
 * that all subsequent static panels land in the right place.
 *
 * TEACHING NOTE: This function reads ONLY from static_state_t and the
 * framebuffer struct — it makes zero HAL calls. That constraint is the
 * whole point of the static/dynamic split.
 */
static void draw_static_panels(framebuffer_t *fb,
                                const layout_t *L,
                                const ui_theme_t *theme,
                                const static_state_t *s)
{
    char buf[20];
    uint32_t cur_y = L->margin;

    /* Panel height constants */
    uint32_t panel_2row = panel_height(L, 2);
    uint32_t panel_3row = panel_height(L, 3);
    uint32_t panel_4row = panel_height(L, 4);

    /* -------------------------------------------------------------------------
     * TITLE HEADER — centred, full width
     * -------------------------------------------------------------------------
     */
    {
        ui_rect_t panel = ui_rect(L->left_x, cur_y, L->full_w, panel_2row);
        ui_draw_panel(fb, panel, theme, UI_PANEL_ELEVATED);

        const char *title = "Tutorial-OS: Hardware Inspector";
        uint32_t title_w  = mtw(L, title);
        uint32_t title_x  = L->left_x + (L->full_w - title_w) / 2;
        mstr(fb, L, title_x, cur_y + L->pad, title, theme->colors.text_primary);

        const char *subtitle = s->pinfo.board_name
                             ? s->pinfo.board_name : "Bare Metal System";
        uint32_t sub_w = mtw(L, subtitle);
        uint32_t sub_x = L->left_x + (L->full_w - sub_w) / 2;
        mstr(fb, L, sub_x, cur_y + panel_2row - L->row_h,
             subtitle, theme->colors.text_secondary);

        cur_y += panel_2row + L->panel_gap;
    }

    /* -------------------------------------------------------------------------
     * ROW 1 LEFT: BOARD
     * -------------------------------------------------------------------------
     */
    {
        uint32_t px = L->left_x, pw = L->col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, panel_3row);
        ui_draw_panel(fb, panel, theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(fb, L, theme, px, cur_y, pw, "BOARD");

        /* Model + CPU core badge */
        mstr(fb, L, px + L->pad, y, "Model:", theme->colors.text_secondary);
        const char *soc = s->pinfo.soc_name ? s->pinfo.soc_name : "Unknown";
        mstr(fb, L, px + L->pad + 7 * L->char_w, y, soc,
             theme->colors.text_primary);
        ui_draw_badge(fb,
                      px + L->pad + 7 * L->char_w + mtw(L, soc) + L->char_w,
                      y - 2, theme, s->cpu_core);
        y += L->row_h;

        /* Serial number */
        mstr(fb, L, px + L->pad, y, "Serial:", theme->colors.text_secondary);
        if (s->pinfo.serial_number != 0) {
            mstr(fb, L, px + L->pad + 8 * L->char_w, y,
                 u64_to_hex(s->pinfo.serial_number, buf),
                 theme->colors.text_primary);
        } else {
            mstr(fb, L, px + L->pad + 8 * L->char_w, y,
                 "N/A", theme->colors.text_secondary);
        }
        y += L->row_h;

        /* Architecture badges */
        mstr(fb, L, px + L->pad, y, "Arch:", theme->colors.text_secondary);
        ui_draw_badge(fb, px + L->pad + 6 * L->char_w, y - 2,
                      theme, s->arch_name);
        ui_draw_badge(fb,
                      px + L->pad + 6 * L->char_w + mtw(L, s->arch_name) + 2 * L->char_w,
                      y - 2, theme, s->arch_isa);
    }

    /* -------------------------------------------------------------------------
     * ROW 1 RIGHT: DISPLAY
     * -------------------------------------------------------------------------
     */
    {
        uint32_t px = L->right_x, pw = L->col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, panel_3row);
        ui_draw_panel(fb, panel, theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(fb, L, theme, px, cur_y, pw, "DISPLAY");

        /* Resolution + aspect ratio badge */
        mstr(fb, L, px + L->pad, y, "Resolution:",
             theme->colors.text_secondary);
        uint32_t res_x = px + L->pad + 12 * L->char_w;
        const char *ws = u64_to_dec(fb->width, buf);
        mstr(fb, L, res_x, y, ws, theme->colors.text_primary);
        res_x += mtw(L, ws);
        mstr(fb, L, res_x, y, "x", theme->colors.text_secondary);
        res_x += L->char_w;
        const char *hs = u64_to_dec(fb->height, buf);
        mstr(fb, L, res_x, y, hs, theme->colors.text_primary);
        res_x += mtw(L, hs);
        ui_draw_badge(fb, res_x + L->char_w, y - 2, theme,
                      format_ratio(fb->width, fb->height));
        y += L->row_h;

        /* Pixel format */
        mstr(fb, L, px + L->pad, y, "Format:",
             theme->colors.text_secondary);
        ui_draw_badge(fb, px + L->pad + 8 * L->char_w, y - 2, theme, "ARGB8888");
        mstr(fb, L, px + L->pad + 18 * L->char_w, y,
             "32-bit", theme->colors.text_secondary);
        y += L->row_h;

        /* Display interface */
        mstr(fb, L, px + L->pad, y, "Interface:",
             theme->colors.text_secondary);
        mstr(fb, L, px + L->pad + 11 * L->char_w, y,
             s->disp_iface, theme->colors.text_primary);
    }

    cur_y += panel_3row + L->panel_gap;

    /* -------------------------------------------------------------------------
     * ROW 2 LEFT: PROCESSOR placeholder — drawn by draw_dynamic_panels.
     * ROW 2 RIGHT: PERIPHERALS
     *
     * We don't draw the PROCESSOR panel here — draw_dynamic_panels owns it.
     * We DO advance cur_y by panel_4row so ROW 3 lands at the correct Y.
     * -------------------------------------------------------------------------
     */
    {
        uint32_t px = L->right_x, pw = L->col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, panel_4row);
        ui_draw_panel(fb, panel, theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(fb, L, theme, px, cur_y, pw, "PERIPHERALS");

        /* USB Host */
        bool usb_on = false;
        if (HAL_OK(hal_platform_get_power(HAL_DEVICE_USB, &usb_on))) {
            mstr(fb, L, px + L->pad, y, "USB Host:",
                 theme->colors.text_secondary);
            ui_rect_t toast = ui_rect(px + L->pad + 10 * L->char_w, y - 3,
                                      L->toast_w, L->toast_h);
            ui_draw_toast(fb, toast, theme, usb_on ? "Active" : "Off",
                          usb_on ? UI_TOAST_SUCCESS : UI_TOAST_ERROR);
        }
        y += L->row_h + 4 * L->sy;

        /* SD Card */
        bool sd_on = false;
        if (HAL_OK(hal_platform_get_power(HAL_DEVICE_SD_CARD, &sd_on))) {
            mstr(fb, L, px + L->pad, y, "SD Card:",
                 theme->colors.text_secondary);
            ui_rect_t toast = ui_rect(px + L->pad + 10 * L->char_w, y - 3,
                                      L->toast_w, L->toast_h);
            ui_draw_toast(fb, toast, theme, sd_on ? "Active" : "Off",
                          sd_on ? UI_TOAST_SUCCESS : UI_TOAST_ERROR);
        }
        y += L->row_h + 4 * L->sy;

        /* I2C */
        bool i2c_on = false;
        if (HAL_OK(hal_platform_get_power(HAL_DEVICE_I2C0, &i2c_on))) {
            mstr(fb, L, px + L->pad, y, "I2C:",
                 theme->colors.text_secondary);
            ui_rect_t toast = ui_rect(px + L->pad + 10 * L->char_w, y - 3,
                                      L->toast_w, L->toast_h);
            ui_draw_toast(fb, toast, theme, i2c_on ? "Active" : "Off",
                          i2c_on ? UI_TOAST_SUCCESS : UI_TOAST_ERROR);
        }
    }

    cur_y += panel_4row + L->panel_gap;

    /* -------------------------------------------------------------------------
     * ROW 3 LEFT: MEMORY
     * ROW 3 RIGHT: SYSTEM STATUS placeholder — drawn by draw_dynamic_panels.
     * -------------------------------------------------------------------------
     */
    {
        uint32_t px = L->left_x, pw = L->col_w;
        ui_rect_t panel = ui_rect(px, cur_y, pw, panel_3row);
        ui_draw_panel(fb, panel, theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(fb, L, theme, px, cur_y, pw, "MEMORY");

        size_t arm   = s->minfo.arm_size;
        size_t gpu   = s->minfo.gpu_size;
        size_t total = arm + gpu;

        if (total > 0 && gpu > 0) {
            /* Split memory (BCM): ARM accessible + GPU reserved */
            uint32_t arm_mb  = (uint32_t)(arm  >> 20);
            uint32_t gpu_mb  = (uint32_t)(gpu  >> 20);
            uint32_t tot_mb  = (uint32_t)(total >> 20);
            uint32_t arm_pct = (tot_mb > 0) ? (arm_mb * 100) / tot_mb : 0;

            mstr(fb, L, px + L->pad, y, "Total:",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 7 * L->char_w, y,
                 u64_to_dec(tot_mb, buf), theme->colors.text_primary);
            mstr(fb, L, px + L->pad + 12 * L->char_w, y, "MB",
                 theme->colors.text_secondary);
            ui_rect_t mem_bar = ui_rect(px + L->pad + 15 * L->char_w, y - 1,
                                        L->bar_w, L->bar_h);
            ui_draw_progress_bar(fb, mem_bar, theme, arm_pct,
                                 theme->colors.info);
            y += L->row_h;

            mstr(fb, L, px + L->pad, y, "RAM:",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 5 * L->char_w, y,
                 u64_to_dec(arm_mb, buf), theme->colors.text_primary);
            mstr(fb, L, px + L->pad + 10 * L->char_w, y, "MB",
                 theme->colors.text_secondary);
            y += L->row_h;

            mstr(fb, L, px + L->pad, y, "GPU:",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 5 * L->char_w, y,
                 u64_to_dec(gpu_mb, buf), theme->colors.text_primary);
            mstr(fb, L, px + L->pad + 10 * L->char_w, y, "MB",
                 theme->colors.text_secondary);
        } else if (arm > 0) {
            /* Unified memory (RISC-V, x86_64) */
            uint32_t arm_mb = (uint32_t)(arm >> 20);

            mstr(fb, L, px + L->pad, y, "Total:",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 7 * L->char_w, y,
                 u64_to_dec(arm_mb, buf), theme->colors.text_primary);
            mstr(fb, L, px + L->pad + 12 * L->char_w, y, "MB",
                 theme->colors.text_secondary);
            ui_rect_t mem_bar = ui_rect(px + L->pad + 15 * L->char_w, y - 1,
                                        L->bar_w, L->bar_h);
            ui_draw_progress_bar(fb, mem_bar, theme, 100,
                                 theme->colors.info);
            y += L->row_h;

            mstr(fb, L, px + L->pad, y, "RAM:",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 5 * L->char_w, y,
                 u64_to_dec(arm_mb, buf), theme->colors.text_primary);
            mstr(fb, L, px + L->pad + 10 * L->char_w, y, "MB",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 14 * L->char_w, y,
                 "(unified)", theme->colors.text_secondary);
            y += L->row_h;
        } else {
            mstr(fb, L, px + L->pad, y, "Total:",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 7 * L->char_w, y,
                 "Unknown", theme->colors.text_secondary);
            y += L->row_h;
            y += L->row_h;
        }

        mstr(fb, L, px + L->pad, y, "Heap:",
             theme->colors.text_secondary);
        mstr(fb, L, px + L->pad + 6 * L->char_w, y,
             u64_to_hex((uintptr_t)&__heap_start, buf),
             theme->colors.text_primary);
    }

    cur_y += panel_3row + L->panel_gap;

    /* -------------------------------------------------------------------------
     * BOOT SEQUENCE — full width
     * -------------------------------------------------------------------------
     */
    {
        ui_rect_t panel = ui_rect(L->left_x, cur_y, L->full_w, panel_2row);
        ui_draw_panel(fb, panel, theme, UI_PANEL_ELEVATED);
        uint32_t y = draw_panel_header(fb, L, theme, L->left_x, cur_y,
                                       L->full_w, "BOOT SEQUENCE");

        uint32_t bx = L->left_x + L->pad;
        for (int i = 0; ; i++) {
            const char *stage = get_boot_stage(s->pinfo.platform_id, i);
            if (!stage) break;
            if (i > 0) {
                mstr(fb, L, bx, y, "->", theme->colors.accent);
                bx += 3 * L->char_w;
            }
            bool is_last = (get_boot_stage(s->pinfo.platform_id, i + 1) == NULL);
            mstr(fb, L, bx, y, stage,
                 is_last ? theme->colors.accent_bright
                         : theme->colors.text_primary);
            bx += mtw(L, stage) + L->char_w;
        }
        mstr(fb, L, bx + L->char_w / 2, y, "(this code!)",
             theme->colors.text_secondary);
    }
}


/* =============================================================================
 * DRAW DYNAMIC PANELS
 * =============================================================================
 *
 * Called every UPDATE_INTERVAL_MS. Redraws only:
 *   - PROCESSOR panel     (left col, row 2) — clock frequencies + bars
 *   - SYSTEM STATUS panel (right col, row 3) — temperature + throttle
 *
 * Strategy: fill the panel rect with the panel background colour (erasing
 * stale values), re-draw the panel chrome, then redraw all content from
 * scratch using the freshly polled dynamic_state.
 *
 * Y coordinates are recomputed using the same arithmetic as draw_static_panels
 * so they always match exactly.
 *
 * TEACHING NOTE: The row Y positions are derived from panel_height() with
 * the same row counts used in draw_static_panels. Any change to a panel's
 * row count must be reflected in BOTH functions. A future improvement would
 * store computed Y positions in a persistent layout struct — for a teaching
 * OS, the explicit recomputation makes the dependency visible.
 */
static void draw_dynamic_panels(framebuffer_t *fb,
                                 const layout_t *L,
                                 const ui_theme_t *theme,
                                 const static_state_t *s,
                                 const dynamic_state_t *d)
{
    char buf[20];

    /*
     * Re-derive panel Y positions — must match draw_static_panels exactly.
     *
     * row1_y: top of BOARD / DISPLAY (after title header)
     * row2_y: top of PROCESSOR / PERIPHERALS (after row 1)
     * row3_y: top of MEMORY / SYSTEM STATUS (after row 2)
     */
    uint32_t panel_2row = panel_height(L, 2);
    uint32_t panel_3row = panel_height(L, 3);
    uint32_t panel_4row = panel_height(L, 4);

    uint32_t row1_y = L->margin    + panel_2row + L->panel_gap;
    uint32_t row2_y = row1_y       + panel_3row + L->panel_gap;
    uint32_t row3_y = row2_y       + panel_4row + L->panel_gap;

    /* -------------------------------------------------------------------------
     * PROCESSOR panel (left column, row 2)
     *
     * 4 content rows: CPU, Core, eMMC, Frame counter.
     * draw_static_panels advances cur_y by panel_4row at this row — these
     * must match.
     * -------------------------------------------------------------------------
     */
    {
        uint32_t px = L->left_x, pw = L->col_w;
        uint32_t ph = panel_4row;

        ui_rect_t panel = ui_rect(px, row2_y, pw, ph);
        ui_draw_panel(fb, panel, theme, UI_PANEL_ELEVATED);

        uint32_t y = draw_panel_header(fb, L, theme, px, row2_y, pw, "PROCESSOR");

        /* CPU clock */
        if (d->arm_hz > 0) {
            uint32_t arm_max = s->arm_max_hz > 0 ? s->arm_max_hz : d->arm_hz;
            draw_clock_row(fb, L, theme, px, y, "CPU: ",
                           d->arm_hz, arm_max,
                           theme->colors.accent, buf);
        } else {
            mstr(fb, L, px + L->pad, y, "CPU:  N/A",
                 theme->colors.text_secondary);
        }
        y += L->row_h;

        /* Core/GPU clock */
        if (d->core_hz > 0) {
            draw_clock_row(fb, L, theme, px, y, "Core:",
                           d->core_hz, d->core_hz,
                           theme->colors.success, buf);
        } else {
            mstr(fb, L, px + L->pad, y, "Core: N/A",
                 theme->colors.text_secondary);
        }
        y += L->row_h;

        /* eMMC clock */
        if (d->emmc_hz > 0) {
            draw_clock_row(fb, L, theme, px, y, "eMMC:",
                           d->emmc_hz, d->emmc_hz,
                           theme->colors.warning, buf);
        } else {
            mstr(fb, L, px + L->pad, y, "eMMC: N/A",
                 theme->colors.text_secondary);
        }
        y += L->row_h;

        /* Frame counter — visible proof the loop is running */
        mstr(fb, L, px + L->pad, y, "Frame:",
             theme->colors.text_secondary);
        mstr(fb, L, px + L->pad + 7 * L->char_w, y,
             u64_to_dec(fb->frame_count, buf),
             theme->colors.text_secondary);
    }

    /* -------------------------------------------------------------------------
     * SYSTEM STATUS panel (right column, row 3)
     *
     * 3 content rows: temperature bar, throttle/cap status.
     * draw_static_panels draws MEMORY at row3_y (left col, panel_3row) —
     * this panel goes at the same Y on the right col.
     * -------------------------------------------------------------------------
     */
    {
        uint32_t px = L->right_x, pw = L->col_w;
        uint32_t ph = panel_3row;

        ui_rect_t panel = ui_rect(px, row3_y, pw, ph);
        ui_draw_panel(fb, panel, theme, UI_PANEL_ELEVATED);

        uint32_t y = draw_panel_header(fb, L, theme, px, row3_y, pw,
                                       "SYSTEM STATUS");

        /* Temperature */
        mstr(fb, L, px + L->pad, y, "Temp:", theme->colors.text_secondary);
        if (d->have_temp) {
            int32_t celsius = d->temp_mc / 1000;
            bool too_hot    = (d->temp_mc > 80000);

            if (s->have_temp_max && s->temp_max_mc > 0) {
                uint32_t pct = (uint32_t)((d->temp_mc * 100) / s->temp_max_mc);
                if (pct > 100) pct = 100;
                uint32_t bar_x = px + L->pad + 6 * L->char_w;
                ui_rect_t bar = ui_rect(bar_x, y - 1, L->bar_w, L->bar_h);
                ui_color_t tc = too_hot ? theme->colors.error
                                        : theme->colors.success;
                ui_draw_progress_bar(fb, bar, theme, pct, tc);
                bar_x += L->bar_w + L->pad / 2;
                mstr(fb, L, bar_x, y,
                     u64_to_dec((uint32_t)celsius, buf),
                     too_hot ? theme->colors.error : theme->colors.text_primary);
                mstr(fb, L, bar_x + 4 * L->char_w, y, "C",
                     theme->colors.text_secondary);
            } else {
                mstr(fb, L, px + L->pad + 6 * L->char_w, y,
                     u64_to_dec((uint32_t)celsius, buf),
                     too_hot ? theme->colors.error : theme->colors.text_primary);
                mstr(fb, L, px + L->pad + 10 * L->char_w, y, "C",
                     theme->colors.text_secondary);
            }
        } else {
            mstr(fb, L, px + L->pad + 6 * L->char_w, y,
                 "(no sensor)", theme->colors.text_secondary);
        }
        y += L->row_h;

        /* Throttle / frequency cap status */
        if (d->have_throttle) {
            bool is_throttled = (d->throttle & HAL_THROTTLE_THROTTLED_NOW) != 0;
            bool freq_cap     = (d->throttle & HAL_THROTTLE_ARM_FREQ_CAPPED) != 0;

            mstr(fb, L, px + L->pad, y, "Throttle:",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 10 * L->char_w, y,
                 is_throttled ? "YES" : "NO",
                 is_throttled ? theme->colors.error : theme->colors.success);
            mstr(fb, L, px + L->pad + 15 * L->char_w, y,
                 "Cap:", theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 19 * L->char_w, y,
                 freq_cap ? "YES" : "NO",
                 freq_cap ? theme->colors.error : theme->colors.success);
        } else {
            mstr(fb, L, px + L->pad, y, "Throttle:",
                 theme->colors.text_secondary);
            mstr(fb, L, px + L->pad + 10 * L->char_w, y,
                 "N/A", theme->colors.text_secondary);
        }
    }
}


/* =============================================================================
 * KERNEL MAIN
 * =============================================================================
 */

/*
 * kernel_main() — platform-uniform entry point.
 *
 * boot_fb is non-NULL ONLY on x86_64 (UEFI), where soc_init.c captures
 * the GOP framebuffer before ExitBootServices() and cannot call fb_init()
 * afterward. On ARM64 and RISC-V, boot_fb is NULL and we call fb_init().
 *
 * All platform-specific boot info arrives via globals:
 *   __ram_base — physical RAM base
 *   __ram_size — total RAM bytes
 */
void kernel_main(framebuffer_t *boot_fb)
{
    /* Heap allocator — uses __ram_base/__ram_size globals */
    allocator_init_from_ram(&alloc,
                            (uintptr_t)__ram_base,
                            (size_t)__ram_size,
                            (uintptr_t)&__heap_start,
                            NULL, NULL, NULL, NULL);

    /* GPIO — DPI pin mux on BCM2710/GPi Case; no-op on other platforms */
    hal_gpio_configure_dpi();

    /* Framebuffer init */
    framebuffer_t fb;
    if (boot_fb != NULL) {
        /* x86_64: GOP framebuffer pre-initialized by soc_init.c */
        fb = *boot_fb;
    } else {
        /* ARM64 / RISC-V: initialize via platform driver */
        fb = (framebuffer_t){0};
        if (!fb_init(&fb)) {
            while (1) { cpu_idle(); }
        }
    }

    /* Theme + layout — fully resolution-independent from here */
    ui_theme_t theme = ui_theme_for_width(fb.width, UI_PALETTE_DARK);
    layout_t   L     = compute_layout(fb.width, fb.height);

    /* Collect all one-time HAL data */
    static_state_t s;
    static_state_init(&s, fb.width);

    /* First frame: full clear + all static panels */
    fb_clear(&fb, theme.colors.bg_primary);
    draw_static_panels(&fb, &L, &theme, &s);

    /* Prime dynamic panels with first poll */
    dynamic_state_t d;
    dynamic_state_poll(&d);
    draw_dynamic_panels(&fb, &L, &theme, &s, &d);
    fb_present(&fb);

    /* Render loop — only dynamic panels redrawn per tick */
    while (1) {
        delay_ms(UPDATE_INTERVAL_MS);
        dynamic_state_poll(&d);
        draw_dynamic_panels(&fb, &L, &theme, &s, &d);
        fb_present(&fb);
    }
}