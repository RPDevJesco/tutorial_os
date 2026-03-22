/*
 * ui_canvas.h - Canvas and Text Renderer Interfaces
 * ==================================================
 *
 * This header defines the interface between platform-agnostic UI code and
 * platform-specific rendering. Any platform must implement these functions.
 *
 * WHY FUNCTION POINTERS?
 * ----------------------
 * In C, we use a struct of function pointers to achieve polymorphism.
 * This is similar to:
 *   - C++ virtual functions / interfaces
 *   - Rust traits
 *   - Go interfaces
 *
 * The platform provides a struct populated with its implementation functions,
 * and the UI code calls through these pointers without knowing the details.
 *
 * CANVAS TRAIT:
 * -------------
 * Provides basic drawing operations:
 *   - Pixel operations (draw_pixel, get_pixel)
 *   - Rectangle operations (fill_rect, draw_rect)
 *   - Line operations (draw_hline, draw_vline)
 *   - Clipping support
 *
 * STYLED CANVAS:
 * --------------
 * Extends Canvas with higher-level operations:
 *   - Rounded rectangles
 *   - Circles
 *   - Gradients
 *   - Alpha blending
 *
 * TEXT RENDERER:
 * --------------
 * Provides text drawing:
 *   - Draw string
 *   - Draw character
 *   - Measure text
 *   - Font metrics
 *
 * IMPLEMENTATION REQUIREMENTS:
 * ----------------------------
 *   - All drawing must respect the clip rectangle
 *   - Out-of-bounds coordinates should be handled gracefully (no crashes)
 *   - Colors are ARGB8888 format
 *   - Coordinate (0,0) is top-left
 */

#ifndef UI_CANVAS_H
#define UI_CANVAS_H

#include "ui_types.h"

/* =============================================================================
 * CANVAS INTERFACE
 * =============================================================================
 *
 * Basic drawing operations that any framebuffer can implement.
 */

/*
 * Canvas vtable - function pointer table for canvas operations
 *
 * Platform implementations provide this struct with pointers to their
 * actual drawing functions.
 */
typedef struct ui_canvas_vtable {
    /*
     * Get canvas dimensions
     */
    uint32_t (*width)(void *ctx);
    uint32_t (*height)(void *ctx);

    /*
     * Pixel operations
     */
    void (*draw_pixel)(void *ctx, int32_t x, int32_t y, ui_color_t color);
    ui_color_t (*get_pixel)(void *ctx, int32_t x, int32_t y);

    /*
     * Rectangle operations
     */
    void (*fill_rect)(void *ctx, ui_rect_t rect, ui_color_t color);
    void (*draw_rect)(void *ctx, ui_rect_t rect, ui_color_t color);

    /*
     * Line operations
     */
    void (*draw_hline)(void *ctx, int32_t x, int32_t y, uint32_t len, ui_color_t color);
    void (*draw_vline)(void *ctx, int32_t x, int32_t y, uint32_t len, ui_color_t color);
    void (*draw_line)(void *ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, ui_color_t color);

    /*
     * Clipping
     */
    void (*set_clip)(void *ctx, ui_rect_t rect);
    void (*clear_clip)(void *ctx);

    /*
     * Display
     */
    void (*clear)(void *ctx, ui_color_t color);
    void (*present)(void *ctx);  /* Swap buffers / flush */

} ui_canvas_vtable_t;

/*
 * Canvas object - combines vtable with context pointer
 */
typedef struct ui_canvas {
    const ui_canvas_vtable_t *vt;   /* Virtual function table */
    void *ctx;                       /* Platform-specific context */
} ui_canvas_t;


/* =============================================================================
 * CANVAS CONVENIENCE FUNCTIONS
 * =============================================================================
 *
 * These call through the vtable for cleaner code.
 */

static inline uint32_t ui_canvas_width(ui_canvas_t *c)
{
    return c->vt->width(c->ctx);
}

static inline uint32_t ui_canvas_height(ui_canvas_t *c)
{
    return c->vt->height(c->ctx);
}

static inline ui_rect_t ui_canvas_bounds(ui_canvas_t *c)
{
    return ui_rect(0, 0, c->vt->width(c->ctx), c->vt->height(c->ctx));
}

static inline void ui_canvas_draw_pixel(ui_canvas_t *c, int32_t x, int32_t y, ui_color_t color)
{
    c->vt->draw_pixel(c->ctx, x, y, color);
}

static inline ui_color_t ui_canvas_get_pixel(ui_canvas_t *c, int32_t x, int32_t y)
{
    return c->vt->get_pixel(c->ctx, x, y);
}

static inline void ui_canvas_fill_rect(ui_canvas_t *c, ui_rect_t rect, ui_color_t color)
{
    c->vt->fill_rect(c->ctx, rect, color);
}

static inline void ui_canvas_draw_rect(ui_canvas_t *c, ui_rect_t rect, ui_color_t color)
{
    c->vt->draw_rect(c->ctx, rect, color);
}

static inline void ui_canvas_draw_hline(ui_canvas_t *c, int32_t x, int32_t y, uint32_t len, ui_color_t color)
{
    c->vt->draw_hline(c->ctx, x, y, len, color);
}

static inline void ui_canvas_draw_vline(ui_canvas_t *c, int32_t x, int32_t y, uint32_t len, ui_color_t color)
{
    c->vt->draw_vline(c->ctx, x, y, len, color);
}

static inline void ui_canvas_draw_line(ui_canvas_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, ui_color_t color)
{
    c->vt->draw_line(c->ctx, x0, y0, x1, y1, color);
}

static inline void ui_canvas_set_clip(ui_canvas_t *c, ui_rect_t rect)
{
    c->vt->set_clip(c->ctx, rect);
}

static inline void ui_canvas_clear_clip(ui_canvas_t *c)
{
    c->vt->clear_clip(c->ctx);
}

static inline void ui_canvas_clear(ui_canvas_t *c, ui_color_t color)
{
    c->vt->clear(c->ctx, color);
}

static inline void ui_canvas_present(ui_canvas_t *c)
{
    c->vt->present(c->ctx);
}


/* =============================================================================
 * STYLED CANVAS INTERFACE
 * =============================================================================
 *
 * Extended drawing operations. These can have default implementations
 * that call basic Canvas operations.
 */

typedef struct ui_styled_canvas_vtable {
    /* Inherit basic canvas operations */
    ui_canvas_vtable_t base;

    /* Rounded rectangles */
    void (*fill_rounded_rect)(void *ctx, ui_rect_t rect, uint32_t radius, ui_color_t color);
    void (*draw_rounded_rect)(void *ctx, ui_rect_t rect, uint32_t radius, ui_color_t color);

    /* Circles */
    void (*fill_circle)(void *ctx, int32_t cx, int32_t cy, uint32_t radius, ui_color_t color);
    void (*draw_circle)(void *ctx, int32_t cx, int32_t cy, uint32_t radius, ui_color_t color);

    /* Gradients */
    void (*fill_rect_gradient_v)(void *ctx, ui_rect_t rect, ui_color_t top, ui_color_t bottom);
    void (*fill_rect_gradient_h)(void *ctx, ui_rect_t rect, ui_color_t left, ui_color_t right);

    /* Blending */
    void (*fill_rect_blend)(void *ctx, ui_rect_t rect, ui_color_t color);

} ui_styled_canvas_vtable_t;

typedef struct ui_styled_canvas {
    const ui_styled_canvas_vtable_t *vt;
    void *ctx;
} ui_styled_canvas_t;


/* =============================================================================
 * STYLED CANVAS CONVENIENCE FUNCTIONS
 * =============================================================================
 */

static inline void ui_styled_fill_rounded_rect(ui_styled_canvas_t *c, ui_rect_t rect, uint32_t radius, ui_color_t color)
{
    c->vt->fill_rounded_rect(c->ctx, rect, radius, color);
}

static inline void ui_styled_draw_rounded_rect(ui_styled_canvas_t *c, ui_rect_t rect, uint32_t radius, ui_color_t color)
{
    c->vt->draw_rounded_rect(c->ctx, rect, radius, color);
}

static inline void ui_styled_fill_circle(ui_styled_canvas_t *c, int32_t cx, int32_t cy, uint32_t radius, ui_color_t color)
{
    c->vt->fill_circle(c->ctx, cx, cy, radius, color);
}

static inline void ui_styled_draw_circle(ui_styled_canvas_t *c, int32_t cx, int32_t cy, uint32_t radius, ui_color_t color)
{
    c->vt->draw_circle(c->ctx, cx, cy, radius, color);
}

static inline void ui_styled_fill_rect_gradient_v(ui_styled_canvas_t *c, ui_rect_t rect, ui_color_t top, ui_color_t bottom)
{
    c->vt->fill_rect_gradient_v(c->ctx, rect, top, bottom);
}

static inline void ui_styled_fill_rect_gradient_h(ui_styled_canvas_t *c, ui_rect_t rect, ui_color_t left, ui_color_t right)
{
    c->vt->fill_rect_gradient_h(c->ctx, rect, left, right);
}

static inline void ui_styled_fill_rect_blend(ui_styled_canvas_t *c, ui_rect_t rect, ui_color_t color)
{
    c->vt->fill_rect_blend(c->ctx, rect, color);
}


/* =============================================================================
 * TEXT RENDERER INTERFACE
 * =============================================================================
 */

typedef struct ui_text_renderer_vtable {
    /* Draw text */
    void (*draw_text)(void *ctx, int32_t x, int32_t y, const char *text, ui_color_t color, ui_font_size_t font);
    void (*draw_text_bg)(void *ctx, int32_t x, int32_t y, const char *text, ui_color_t fg, ui_color_t bg, ui_font_size_t font);

    /* Draw single character */
    void (*draw_char)(void *ctx, int32_t x, int32_t y, char ch, ui_color_t color, ui_font_size_t font);
    void (*draw_char_bg)(void *ctx, int32_t x, int32_t y, char ch, ui_color_t fg, ui_color_t bg, ui_font_size_t font);

    /* Metrics */
    void (*char_size)(void *ctx, ui_font_size_t font, uint32_t *width, uint32_t *height);
    uint32_t (*measure_text)(void *ctx, const char *text, ui_font_size_t font);
    uint32_t (*line_height)(void *ctx, ui_font_size_t font);

} ui_text_renderer_vtable_t;

typedef struct ui_text_renderer {
    const ui_text_renderer_vtable_t *vt;
    void *ctx;
} ui_text_renderer_t;


/* =============================================================================
 * TEXT RENDERER CONVENIENCE FUNCTIONS
 * =============================================================================
 */

static inline void ui_text_draw(ui_text_renderer_t *r, int32_t x, int32_t y, const char *text, ui_color_t color, ui_font_size_t font)
{
    r->vt->draw_text(r->ctx, x, y, text, color, font);
}

static inline void ui_text_draw_bg(ui_text_renderer_t *r, int32_t x, int32_t y, const char *text, ui_color_t fg, ui_color_t bg, ui_font_size_t font)
{
    r->vt->draw_text_bg(r->ctx, x, y, text, fg, bg, font);
}

static inline void ui_text_draw_char(ui_text_renderer_t *r, int32_t x, int32_t y, char ch, ui_color_t color, ui_font_size_t font)
{
    r->vt->draw_char(r->ctx, x, y, ch, color, font);
}

static inline uint32_t ui_text_measure(ui_text_renderer_t *r, const char *text, ui_font_size_t font)
{
    return r->vt->measure_text(r->ctx, text, font);
}

static inline void ui_text_measure_size(ui_text_renderer_t *r, const char *text, ui_font_size_t font, uint32_t *w, uint32_t *h)
{
    *w = r->vt->measure_text(r->ctx, text, font);
    r->vt->char_size(r->ctx, font, NULL, h);
}

static inline uint32_t ui_text_line_height(ui_text_renderer_t *r, ui_font_size_t font)
{
    return r->vt->line_height(r->ctx, font);
}

static inline uint32_t ui_text_chars_that_fit(ui_text_renderer_t *r, uint32_t width, ui_font_size_t font)
{
    uint32_t char_w, char_h;
    r->vt->char_size(r->ctx, font, &char_w, &char_h);
    return char_w > 0 ? width / char_w : 0;
}


/* =============================================================================
 * COMBINED RENDERER
 * =============================================================================
 *
 * For convenience, a combined renderer that provides both styled canvas
 * and text rendering.
 */

typedef struct ui_renderer {
    ui_styled_canvas_t canvas;
    ui_text_renderer_t text;
} ui_renderer_t;

#endif /* UI_CANVAS_H */
