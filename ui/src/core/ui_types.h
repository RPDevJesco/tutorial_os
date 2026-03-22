/*
 * ui_types.h - Core UI Type Definitions
 * =====================================
 *
 * This header defines the fundamental types used throughout the UI system:
 *   - Colors (ARGB8888 format)
 *   - Rectangles (position + size)
 *   - Alignment enums
 *   - Font size selection
 *
 * DESIGN PHILOSOPHY:
 * ------------------
 * The UI system is designed to be:
 *   - Platform-agnostic (no hardware dependencies)
 *   - Heap-free (all data can live on stack or in static storage)
 *   - Resolution-aware (themes scale to display size)
 *   - Immediate-mode (widgets are functions, not objects)
 *
 * COLOR FORMAT:
 * -------------
 * Colors are stored as 32-bit ARGB8888:
 *   Bits 31-24: Alpha (0=transparent, 255=opaque)
 *   Bits 23-16: Red
 *   Bits 15-8:  Green
 *   Bits 7-0:   Blue
 *
 * This matches common framebuffer formats and allows easy alpha blending.
 *
 * COORDINATE SYSTEM:
 * ------------------
 *   - Origin (0,0) is top-left
 *   - X increases rightward
 *   - Y increases downward
 *   - Coordinates are signed (i32) to allow off-screen positioning
 *   - Sizes are unsigned (u32) since negative sizes make no sense
 */

#ifndef UI_TYPES_H
#define UI_TYPES_H

#include "types.h"  /* Bare-metal type definitions */

/* =============================================================================
 * COLOR TYPE AND HELPERS
 * =============================================================================
 */

/*
 * Color type - ARGB8888 format
 *
 * We use a typedef for clarity, but it's just a uint32_t.
 */
typedef uint32_t ui_color_t;

/*
 * Create color from RGB (fully opaque)
 */
static inline ui_color_t ui_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/*
 * Create color from ARGB
 */
static inline ui_color_t ui_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/*
 * Extract alpha component
 */
static inline uint8_t ui_color_alpha(ui_color_t c)
{
    return (c >> 24) & 0xFF;
}

/*
 * Extract red component
 */
static inline uint8_t ui_color_red(ui_color_t c)
{
    return (c >> 16) & 0xFF;
}

/*
 * Extract green component
 */
static inline uint8_t ui_color_green(ui_color_t c)
{
    return (c >> 8) & 0xFF;
}

/*
 * Extract blue component
 */
static inline uint8_t ui_color_blue(ui_color_t c)
{
    return c & 0xFF;
}

/*
 * Set alpha, preserving RGB
 */
static inline ui_color_t ui_color_with_alpha(ui_color_t c, uint8_t a)
{
    return (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
}

/*
 * Linear interpolation between two colors
 *
 * @param a  Start color
 * @param b  End color
 * @param t  Blend factor (0=a, 255=b)
 */
static inline ui_color_t ui_color_lerp(ui_color_t a, ui_color_t b, uint8_t t)
{
    uint32_t inv_t = 255 - t;

    uint32_t ra = ui_color_red(a);
    uint32_t ga = ui_color_green(a);
    uint32_t ba = ui_color_blue(a);
    uint32_t aa = ui_color_alpha(a);

    uint32_t rb = ui_color_red(b);
    uint32_t gb = ui_color_green(b);
    uint32_t bb = ui_color_blue(b);
    uint32_t ab = ui_color_alpha(b);

    uint8_t r = (uint8_t)((ra * inv_t + rb * t) / 255);
    uint8_t g = (uint8_t)((ga * inv_t + gb * t) / 255);
    uint8_t bl = (uint8_t)((ba * inv_t + bb * t) / 255);
    uint8_t al = (uint8_t)((aa * inv_t + ab * t) / 255);

    return ui_argb(al, r, g, bl);
}

/*
 * Alpha blend: source over destination
 *
 * Standard "source over" compositing operation.
 */
static inline ui_color_t ui_color_blend_over(ui_color_t dst, ui_color_t src)
{
    uint32_t sa = ui_color_alpha(src);
    if (sa == 255) return src;
    if (sa == 0) return dst;

    uint32_t inv_sa = 255 - sa;

    uint32_t r = (ui_color_red(src) * sa + ui_color_red(dst) * inv_sa) / 255;
    uint32_t g = (ui_color_green(src) * sa + ui_color_green(dst) * inv_sa) / 255;
    uint32_t b = (ui_color_blue(src) * sa + ui_color_blue(dst) * inv_sa) / 255;
    uint32_t a = sa + (ui_color_alpha(dst) * inv_sa) / 255;

    return ui_argb((uint8_t)a, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/*
 * Darken a color by a factor
 *
 * @param c       Color to darken
 * @param factor  0=black, 255=unchanged
 */
static inline ui_color_t ui_color_darken(ui_color_t c, uint8_t factor)
{
    uint32_t f = factor;
    uint8_t r = (uint8_t)((ui_color_red(c) * f) / 255);
    uint8_t g = (uint8_t)((ui_color_green(c) * f) / 255);
    uint8_t b = (uint8_t)((ui_color_blue(c) * f) / 255);
    return ui_argb(ui_color_alpha(c), r, g, b);
}

/*
 * Lighten a color by a factor
 *
 * @param c       Color to lighten
 * @param factor  0=unchanged, 255=white
 */
static inline ui_color_t ui_color_lighten(ui_color_t c, uint8_t factor)
{
    uint32_t f = factor;
    uint8_t r = (uint8_t)(ui_color_red(c) + ((255 - ui_color_red(c)) * f) / 255);
    uint8_t g = (uint8_t)(ui_color_green(c) + ((255 - ui_color_green(c)) * f) / 255);
    uint8_t b = (uint8_t)(ui_color_blue(c) + ((255 - ui_color_blue(c)) * f) / 255);
    return ui_argb(ui_color_alpha(c), r, g, b);
}


/* =============================================================================
 * RECTANGLE TYPE
 * =============================================================================
 */

/*
 * Rectangle - position and size
 *
 * The fundamental layout primitive. Position uses signed integers to
 * allow off-screen values (which get clipped during rendering).
 */
typedef struct {
    int32_t x;      /* Left edge */
    int32_t y;      /* Top edge */
    uint32_t w;     /* Width */
    uint32_t h;     /* Height */
} ui_rect_t;

/*
 * Create a rectangle
 */
static inline ui_rect_t ui_rect(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    ui_rect_t r = { x, y, w, h };
    return r;
}

/*
 * Zero rectangle at origin
 */
#define UI_RECT_ZERO ui_rect(0, 0, 0, 0)

/*
 * Right edge (x + width)
 */
static inline int32_t ui_rect_right(ui_rect_t r)
{
    return r.x + (int32_t)r.w;
}

/*
 * Bottom edge (y + height)
 */
static inline int32_t ui_rect_bottom(ui_rect_t r)
{
    return r.y + (int32_t)r.h;
}

/*
 * Center X coordinate
 */
static inline int32_t ui_rect_center_x(ui_rect_t r)
{
    return r.x + (int32_t)(r.w / 2);
}

/*
 * Center Y coordinate
 */
static inline int32_t ui_rect_center_y(ui_rect_t r)
{
    return r.y + (int32_t)(r.h / 2);
}

/*
 * Check if rectangle has zero area
 */
static inline bool ui_rect_is_empty(ui_rect_t r)
{
    return r.w == 0 || r.h == 0;
}

/*
 * Inset rectangle by uniform amount
 */
static inline ui_rect_t ui_rect_inset(ui_rect_t r, uint32_t amount)
{
    uint32_t doubled = amount * 2;
    return ui_rect(
        r.x + (int32_t)amount,
        r.y + (int32_t)amount,
        r.w > doubled ? r.w - doubled : 0,
        r.h > doubled ? r.h - doubled : 0
    );
}

/*
 * Offset rectangle by dx, dy
 */
static inline ui_rect_t ui_rect_offset(ui_rect_t r, int32_t dx, int32_t dy)
{
    return ui_rect(r.x + dx, r.y + dy, r.w, r.h);
}

/*
 * Expand rectangle on all sides
 */
static inline ui_rect_t ui_rect_expand(ui_rect_t r, uint32_t amount)
{
    return ui_rect(
        r.x - (int32_t)amount,
        r.y - (int32_t)amount,
        r.w + amount * 2,
        r.h + amount * 2
    );
}

/*
 * Center a smaller rectangle inside a larger one
 */
static inline ui_rect_t ui_rect_center_in(ui_rect_t outer, uint32_t inner_w, uint32_t inner_h)
{
    int32_t x = outer.x + (int32_t)(outer.w - inner_w) / 2;
    int32_t y = outer.y + (int32_t)(outer.h - inner_h) / 2;
    return ui_rect(x, y, inner_w, inner_h);
}

/*
 * Split rectangle horizontally at height
 *
 * Returns the top portion. Caller can compute bottom from original.
 */
static inline ui_rect_t ui_rect_split_top(ui_rect_t r, uint32_t height)
{
    if (height > r.h) height = r.h;
    return ui_rect(r.x, r.y, r.w, height);
}

/*
 * Take bottom portion of rectangle
 */
static inline ui_rect_t ui_rect_split_bottom(ui_rect_t r, uint32_t height)
{
    if (height > r.h) height = r.h;
    return ui_rect(r.x, r.y + (int32_t)(r.h - height), r.w, height);
}

/*
 * Take left portion of rectangle
 */
static inline ui_rect_t ui_rect_split_left(ui_rect_t r, uint32_t width)
{
    if (width > r.w) width = r.w;
    return ui_rect(r.x, r.y, width, r.h);
}

/*
 * Take right portion of rectangle
 */
static inline ui_rect_t ui_rect_split_right(ui_rect_t r, uint32_t width)
{
    if (width > r.w) width = r.w;
    return ui_rect(r.x + (int32_t)(r.w - width), r.y, width, r.h);
}

/*
 * Check if point is inside rectangle
 */
static inline bool ui_rect_contains(ui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && x < ui_rect_right(r) &&
           y >= r.y && y < ui_rect_bottom(r);
}

/*
 * Intersect two rectangles
 */
static inline ui_rect_t ui_rect_intersect(ui_rect_t a, ui_rect_t b)
{
    int32_t x1 = (a.x > b.x) ? a.x : b.x;
    int32_t y1 = (a.y > b.y) ? a.y : b.y;
    int32_t x2 = (ui_rect_right(a) < ui_rect_right(b)) ? ui_rect_right(a) : ui_rect_right(b);
    int32_t y2 = (ui_rect_bottom(a) < ui_rect_bottom(b)) ? ui_rect_bottom(a) : ui_rect_bottom(b);

    if (x2 <= x1 || y2 <= y1) {
        return UI_RECT_ZERO;
    }

    return ui_rect(x1, y1, (uint32_t)(x2 - x1), (uint32_t)(y2 - y1));
}


/* =============================================================================
 * ALIGNMENT ENUMS
 * =============================================================================
 */

/*
 * Horizontal alignment
 */
typedef enum {
    UI_ALIGN_LEFT,
    UI_ALIGN_CENTER,
    UI_ALIGN_RIGHT,
} ui_halign_t;

/*
 * Vertical alignment
 */
typedef enum {
    UI_ALIGN_TOP,
    UI_ALIGN_MIDDLE,
    UI_ALIGN_BOTTOM,
} ui_valign_t;

/*
 * Font size selection
 */
typedef enum {
    UI_FONT_SMALL,      /* e.g., 4x6 pixels */
    UI_FONT_NORMAL,     /* e.g., 8x8 pixels */
    UI_FONT_LARGE,      /* e.g., 8x16 pixels */
} ui_font_size_t;

/*
 * Blend mode for drawing
 */
typedef enum {
    UI_BLEND_OPAQUE,    /* No blending, replace destination */
    UI_BLEND_ALPHA,     /* Standard alpha blending */
    UI_BLEND_ADDITIVE,  /* Add source to destination */
    UI_BLEND_MULTIPLY,  /* Multiply source with destination */
} ui_blend_mode_t;


/* =============================================================================
 * FONT METRICS
 * =============================================================================
 */

/*
 * Font metrics for layout calculations
 */
typedef struct {
    uint32_t char_width;    /* Width of one character (monospace) */
    uint32_t char_height;   /* Height of one character */
    uint32_t line_height;   /* Height including line spacing */
} ui_font_metrics_t;

/* Common font metric presets */
#define UI_FONT_4X6   ((ui_font_metrics_t){ 4, 6, 7 })
#define UI_FONT_8X8   ((ui_font_metrics_t){ 8, 8, 10 })
#define UI_FONT_8X16  ((ui_font_metrics_t){ 8, 16, 18 })

#endif /* UI_TYPES_H */
