/*
 * framebuffer.h - 32-bit ARGB8888 Framebuffer Driver
 * ===================================================
 *
 * This driver provides:
 *   - Framebuffer allocation via VideoCore mailbox
 *   - Double buffering with vsync support
 *   - Basic drawing (pixels, rectangles, lines)
 *   - Advanced drawing (circles, arcs, triangles, rounded rects, gradients)
 *   - Alpha blending and compositing
 *   - Clipping rectangle stack
 *   - Dirty region tracking for partial updates
 *   - 8x8 and 8x16 bitmap fonts
 *   - Bitmap/sprite blitting with blend modes
 *   - GameBoy screen blitting (DMG and GBC) with 2x scaling
 *   - Screen scrolling and copy operations
 *
 * COLOR FORMAT:
 * -------------
 * Colors are 32-bit ARGB8888:
 *   Bits [31:24] = Alpha (8 bits)
 *   Bits [23:16] = Red (8 bits)
 *   Bits [15:8]  = Green (8 bits)
 *   Bits [7:0]   = Blue (8 bits)
 *
 * Use the color helper macros: FB_ARGB(), FB_RGB(), etc.
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "types.h"

/* =============================================================================
 * DISPLAY CONSTANTS
 * =============================================================================
 */

#define FB_DEFAULT_WIDTH    640
#define FB_DEFAULT_HEIGHT   480
#define FB_BITS_PER_PIXEL   32

#define FB_CHAR_WIDTH       8
#define FB_CHAR_HEIGHT      8
#define FB_CHAR_WIDTH_LG    8
#define FB_CHAR_HEIGHT_LG   16

#define FB_MAX_DIRTY_RECTS  32
#define FB_MAX_CLIP_DEPTH   8
#define FB_BUFFER_COUNT     2

/* GameBoy display */
#define GB_WIDTH            160
#define GB_HEIGHT           144
#define GB_SCALE            2
#define GB_SCALED_W         (GB_WIDTH * GB_SCALE)
#define GB_SCALED_H         (GB_HEIGHT * GB_SCALE)
#define GB_OFFSET_X         ((FB_DEFAULT_WIDTH - GB_SCALED_W) / 2)
#define GB_OFFSET_Y         ((FB_DEFAULT_HEIGHT - GB_SCALED_H) / 2)


/* =============================================================================
 * COLOR MACROS (ARGB8888)
 * =============================================================================
 */

#define FB_ARGB(a, r, g, b) ((uint32_t)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))
#define FB_RGB(r, g, b)     FB_ARGB(255, r, g, b)

#define FB_ALPHA(c)         (((c) >> 24) & 0xFF)
#define FB_RED(c)           (((c) >> 16) & 0xFF)
#define FB_GREEN(c)         (((c) >> 8) & 0xFF)
#define FB_BLUE(c)          ((c) & 0xFF)

#define FB_WITH_ALPHA(rgb, a) (((rgb) & 0x00FFFFFF) | ((uint32_t)(a) << 24))


/* =============================================================================
 * PREDEFINED COLORS (ARGB8888)
 * =============================================================================
 */

#define FB_COLOR_BLACK          0xFF000000
#define FB_COLOR_WHITE          0xFFFFFFFF
#define FB_COLOR_RED            0xFFFF0000
#define FB_COLOR_GREEN          0xFF00FF00
#define FB_COLOR_BLUE           0xFF0000FF
#define FB_COLOR_CYAN           0xFF00FFFF
#define FB_COLOR_MAGENTA        0xFFFF00FF
#define FB_COLOR_YELLOW         0xFFFFFF00
#define FB_COLOR_GRAY           0xFF808080
#define FB_COLOR_DARK_GRAY      0xFF404040
#define FB_COLOR_LIGHT_GRAY     0xFFC0C0C0
#define FB_COLOR_DARK_BLUE      0xFF000040
#define FB_COLOR_ORANGE         0xFFFF8000
#define FB_COLOR_TRANSPARENT    0x00000000

/* Menu-specific colors */
#define FB_COLOR_MENU_BG        0xFF101020
#define FB_COLOR_MENU_HIGHLIGHT 0xFF303060
#define FB_COLOR_MENU_TEXT      0xFFE0E0E0
#define FB_COLOR_MENU_TEXT_DIM  0xFF808080
#define FB_COLOR_MENU_ACCENT    0xFF4080FF

/* Semi-transparent variants */
#define FB_COLOR_BLACK_50       0x80000000
#define FB_COLOR_BLACK_75       0xC0000000
#define FB_COLOR_WHITE_50       0x80FFFFFF
#define FB_COLOR_WHITE_25       0x40FFFFFF


/* =============================================================================
 * GAMEBOY PALETTE (ARGB8888)
 * =============================================================================
 */

extern const uint32_t gb_palette[4];


/* =============================================================================
 * BLEND MODES
 * =============================================================================
 */

typedef enum {
    FB_BLEND_OPAQUE,    /* No blending, source replaces destination */
    FB_BLEND_ALPHA,     /* Standard alpha blending (source over) */
    FB_BLEND_ADDITIVE,  /* Additive blending */
    FB_BLEND_MULTIPLY,  /* Multiplicative blending */
} fb_blend_mode_t;


/* =============================================================================
 * DATA STRUCTURES
 * =============================================================================
 */

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
} fb_rect_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} fb_clip_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} fb_dirty_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    const uint32_t *data;
} fb_bitmap_t;

/*
 * Main framebuffer structure
 *
 * NOTE: Named "struct framebuffer" to match forward declaration in hal_display.h
 */
typedef struct framebuffer {
    uint32_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;

    uint32_t *buffers[FB_BUFFER_COUNT];
    uint32_t front_buffer;
    uint32_t back_buffer;
    uint32_t buffer_size;
    uint32_t virtual_height;

    fb_dirty_t dirty_rects[FB_MAX_DIRTY_RECTS];
    uint32_t dirty_count;
    bool full_dirty;

    fb_clip_t clip_stack[FB_MAX_CLIP_DEPTH];
    uint32_t clip_depth;

    uint64_t frame_count;
    bool vsync_enabled;
    bool initialized;
} framebuffer_t;


/* =============================================================================
 * INITIALIZATION
 * =============================================================================
 */

bool fb_init(framebuffer_t *fb);
bool fb_init_with_size(framebuffer_t *fb, uint32_t width, uint32_t height);


/* =============================================================================
 * ACCESSORS
 * =============================================================================
 */

static inline uint32_t fb_width(const framebuffer_t *fb) { return fb->width; }
static inline uint32_t fb_height(const framebuffer_t *fb) { return fb->height; }
static inline uint32_t fb_pitch(const framebuffer_t *fb) { return fb->pitch; }
static inline uint32_t *fb_buffer(framebuffer_t *fb) { return fb->addr; }
static inline uint64_t fb_frame_count(const framebuffer_t *fb) { return fb->frame_count; }
static inline bool fb_is_initialized(const framebuffer_t *fb) { return fb->initialized; }
static inline uint32_t fb_size(const framebuffer_t *fb) { return fb->pitch * fb->height; }


/* =============================================================================
 * DISPLAY CONTROL
 * =============================================================================
 */

void fb_set_vsync(framebuffer_t *fb, bool enabled);
void fb_present(framebuffer_t *fb);
void fb_present_immediate(framebuffer_t *fb);


/* =============================================================================
 * CLIPPING
 * =============================================================================
 */

bool fb_push_clip(framebuffer_t *fb, fb_rect_t rect);
void fb_pop_clip(framebuffer_t *fb);
void fb_reset_clip(framebuffer_t *fb);
fb_clip_t fb_get_clip(const framebuffer_t *fb);


/* =============================================================================
 * DIRTY TRACKING
 * =============================================================================
 */

void fb_mark_dirty(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void fb_mark_all_dirty(framebuffer_t *fb);
void fb_clear_dirty(framebuffer_t *fb);
bool fb_is_dirty(const framebuffer_t *fb);


/* =============================================================================
 * COLOR UTILITIES
 * =============================================================================
 */

uint32_t fb_color_lerp(uint32_t c1, uint32_t c2, uint8_t t);
uint32_t fb_blend_alpha(uint32_t src, uint32_t dst);
uint32_t fb_blend_additive(uint32_t src, uint32_t dst);
uint32_t fb_blend_multiply(uint32_t src, uint32_t dst);


/* =============================================================================
 * BASIC DRAWING
 * =============================================================================
 */

void fb_clear(framebuffer_t *fb, uint32_t color);
void fb_put_pixel(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color);
void fb_put_pixel_blend(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color);
void fb_put_pixel_unchecked(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_get_pixel(const framebuffer_t *fb, uint32_t x, uint32_t y);
void fb_fill_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_fill_rect_blend(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_hline(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t len, uint32_t color);
void fb_draw_vline(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t len, uint32_t color);
void fb_draw_line(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void fb_draw_line_thick(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        uint32_t thickness, uint32_t color);


/* =============================================================================
 * ADVANCED DRAWING
 * =============================================================================
 */

void fb_fill_circle(framebuffer_t *fb, int32_t cx, int32_t cy, uint32_t radius, uint32_t color);
void fb_draw_circle(framebuffer_t *fb, int32_t cx, int32_t cy, uint32_t radius, uint32_t color);
void fb_draw_arc(framebuffer_t *fb, uint32_t cx, uint32_t cy, uint32_t radius,
                 uint32_t start_deg, uint32_t end_deg, uint32_t color);
void fb_fill_rounded_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t radius, uint32_t color);
void fb_draw_rounded_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t radius, uint32_t color);
void fb_draw_triangle(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint32_t color);
void fb_fill_triangle(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint32_t color);
void fb_fill_rect_gradient_v(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t top_color, uint32_t bottom_color);
void fb_fill_rect_gradient_h(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t left_color, uint32_t right_color);
void fb_fade(framebuffer_t *fb, uint8_t amount);


/* =============================================================================
 * TEXT RENDERING
 * =============================================================================
 */

void fb_draw_char(framebuffer_t *fb, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_char_transparent(framebuffer_t *fb, uint32_t x, uint32_t y, char c, uint32_t fg);
void fb_draw_char_large(framebuffer_t *fb, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_char_large_transparent(framebuffer_t *fb, uint32_t x, uint32_t y, char c, uint32_t fg);
void fb_draw_char_scaled(framebuffer_t *fb, uint32_t x, uint32_t y, char c,
                         uint32_t fg, uint32_t bg, uint32_t scale);
void fb_draw_string(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);
void fb_draw_string_transparent(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str, uint32_t fg);
void fb_draw_string_large(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);
void fb_draw_string_large_transparent(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str, uint32_t fg);
void fb_draw_string_scaled(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str,
                           uint32_t fg, uint32_t bg, uint32_t scale);
void fb_draw_string_centered(framebuffer_t *fb, uint32_t y, const char *str, uint32_t fg, uint32_t bg);
void fb_draw_string_center(framebuffer_t *fb, const char *str, uint32_t fg, uint32_t bg);
uint32_t fb_measure_string(const char *str, bool large);
uint32_t fb_text_width(const char *str);
uint32_t fb_text_width_large(const char *str);


/* =============================================================================
 * BITMAP BLITTING
 * =============================================================================
 */

void fb_blit_bitmap(framebuffer_t *fb, int32_t x, int32_t y, const fb_bitmap_t *bitmap);
void fb_blit_bitmap_alpha(framebuffer_t *fb, int32_t x, int32_t y, const fb_bitmap_t *bitmap);
void fb_blit_bitmap_blend(framebuffer_t *fb, int32_t x, int32_t y, const fb_bitmap_t *bitmap,
                          fb_blend_mode_t blend);
void fb_blit_bitmap_scaled(framebuffer_t *fb, int32_t x, int32_t y, const fb_bitmap_t *bitmap,
                           uint32_t scale_x, uint32_t scale_y);
void fb_blit_bitmap_region(framebuffer_t *fb, const fb_bitmap_t *bitmap,
                           uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h,
                           int32_t dst_x, int32_t dst_y);


/* =============================================================================
 * SCREEN OPERATIONS
 * =============================================================================
 */

void fb_copy_rect(framebuffer_t *fb, uint32_t src_x, uint32_t src_y,
                  uint32_t dst_x, uint32_t dst_y, uint32_t w, uint32_t h);
void fb_scroll_v(framebuffer_t *fb, int32_t pixels, uint32_t fill_color);
void fb_scroll_h(framebuffer_t *fb, int32_t pixels, uint32_t fill_color);


/* =============================================================================
 * GAMEBOY SCREEN BLITTING
 * =============================================================================
 */

void fb_blit_gb_screen_dmg(framebuffer_t *fb, const uint8_t *pal_data);
void fb_blit_gb_screen_dmg_palette(framebuffer_t *fb, const uint8_t *pal_data, const uint32_t *palette);
void fb_blit_gb_screen_gbc(framebuffer_t *fb, const uint8_t *rgb_data);
void fb_draw_gb_border(framebuffer_t *fb, uint32_t color);

/* Legacy compatibility */
#define fb_blit_gb_screen(fb, pixels, palette) fb_blit_gb_screen_dmg_palette(fb, pixels, palette)


/* =============================================================================
 * MATH HELPERS
 * =============================================================================
 */

uint32_t fb_isqrt(uint32_t n);
void fb_sin_cos_deg(uint32_t deg, int32_t *sin_out, int32_t *cos_out);


#endif /* FRAMEBUFFER_H */