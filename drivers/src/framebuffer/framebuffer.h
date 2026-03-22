/*
 * drivers/framebuffer/framebuffer.h — Portable Framebuffer Driver
 * ================================================================
 *
 * This header defines the framebuffer_t struct and the entire drawing API
 * that sits on top of it. Every pixel-producing function in Tutorial-OS
 * takes a framebuffer_t* as its first argument.
 *
 * PORTABILITY DESIGN
 * ==================
 * framebuffer_t is intentionally architecture-neutral. The drawing code
 * in framebuffer.c knows nothing about how the buffer was allocated or
 * where it lives — that is the display driver's job:
 *
 *   ARM64 (BCM2710/2711/2712) — mailbox allocates the framebuffer; the GPU
 *     returns a bus address which the mailbox layer translates to a CPU
 *     physical address. Stored in fb->addr.
 *
 *   RISC-V (KyX1, JH7110) — U-Boot allocates the framebuffer and passes
 *     its physical address in the device tree (simple-framebuffer node).
 *     Parsed by display_simplefb.c and stored in fb->addr.
 *
 *   x86_64 (LattePanda MU/IOTA) — UEFI GOP (Graphics Output Protocol)
 *     allocates the framebuffer. The physical base address is a full 64-bit
 *     value returned by GOP->Mode->FrameBufferBase (EFI_PHYSICAL_ADDRESS,
 *     typedef'd as UINT64). Stored in fb->addr via a uintptr_t cast.
 *
 * FRAMEBUFFER ADDRESS WIDTH
 * =========================
 * fb->addr is declared as uint32_t* (a native pointer). On all three of
 * our target architectures, pointers are 64 bits wide:
 *
 *   ARM64   — 64-bit virtual addresses (MMU off in Tutorial-OS: physical = virtual)
 *   RISC-V  — 64-bit physical addresses (satp=0, bare/Mbare mode)
 *   x86_64  — 64-bit virtual addresses (paging active under UEFI)
 *
 * This means fb->addr can hold any physical address on all three platforms.
 * The x86_64 GOP framebuffer address confirmed from UART log has non-zero
 * upper 32 bits. Assigning it correctly:
 *
 *   fb->addr = (uint32_t *)(uintptr_t)gop->Mode->FrameBufferBase;  // CORRECT
 *   fb->addr = (uint32_t *)(uint32_t)gop->Mode->FrameBufferBase;   // WRONG: truncates!
 *
 * The intermediate cast through uintptr_t is the portable pattern. It is
 * used consistently throughout the display drivers for exactly this reason.
 *
 * THE CLIP STACK ZERO-INIT RULE
 * ==============================
 * Always zero-initialize framebuffer_t before calling the display driver:
 *
 *   framebuffer_t fb = {0};          // C designated initializer — CORRECT
 *   framebuffer_t fb;                // uninitialized — WRONG on RISC-V
 *
 * The RISC-V OpenSBI/U-Boot stack contains garbage values. An uninitialized
 * clip_stack[0] will produce a clip region with garbage dimensions, causing
 * all drawing calls to be clipped away silently. This is not hypothetical —
 * it was the root cause of the Milk-V Mars display bring-up failure.
 * ARM64 post-boot stack happens to be clean, which is why this bug only
 * manifests on RISC-V. Zero-init costs nothing and is always correct.
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "hal_types.h"

/* Include MMIO primitives — drawing code uses WRITE_VOLATILE/READ_VOLATILE */
#include "mmio.h"


/* =============================================================================
 * COMPILE-TIME CONFIGURATION
 * =============================================================================
 */

/* Number of framebuffers for double-buffering (1 = single, 2 = double) */
#ifndef FB_BUFFER_COUNT
#define FB_BUFFER_COUNT     2
#endif

/* Maximum depth of the clip rectangle stack */
#ifndef FB_MAX_CLIP_DEPTH
#define FB_MAX_CLIP_DEPTH   16
#endif

/* Maximum number of tracked dirty rectangles before collapsing to full-dirty */
#ifndef FB_MAX_DIRTY_RECTS
#define FB_MAX_DIRTY_RECTS  32
#endif

/* Default resolution (overridden by BOARD_DEFINES in board.mk) */
#ifndef FB_DEFAULT_WIDTH
#define FB_DEFAULT_WIDTH    640
#endif
#ifndef FB_DEFAULT_HEIGHT
#define FB_DEFAULT_HEIGHT   480
#endif

/* Pixel depth */
#define FB_BITS_PER_PIXEL   32

/* 8×8 font character dimensions */
#define FB_CHAR_WIDTH       8
#define FB_CHAR_HEIGHT      8

/* 8×16 "large" font character dimensions */
#define FB_CHAR_WIDTH_LG    8
#define FB_CHAR_HEIGHT_LG   16

/* GameBoy display constants */
#define GB_WIDTH            160
#define GB_HEIGHT           144
#define GB_SCALE            2
#define GB_SCALED_W         (GB_WIDTH  * GB_SCALE)
#define GB_SCALED_H         (GB_HEIGHT * GB_SCALE)
#define GB_OFFSET_X         ((FB_DEFAULT_WIDTH  - GB_SCALED_W) / 2)
#define GB_OFFSET_Y         ((FB_DEFAULT_HEIGHT - GB_SCALED_H) / 2)


/* =============================================================================
 * COLOR CHANNEL MACROS (ARGB8888)
 * =============================================================================
 * All colors are 0xAARRGGBB.  Drawing code always works in ARGB; the display
 * driver swaps R↔B at present-time when hardware scans out ABGR.
 */

#define FB_ARGB(a,r,g,b)    ((uint32_t)(((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b)))
#define FB_RGB(r,g,b)       FB_ARGB(255, r, g, b)
#define FB_ALPHA(c)         (((uint32_t)(c) >> 24) & 0xFF)
#define FB_RED(c)           (((uint32_t)(c) >> 16) & 0xFF)
#define FB_GREEN(c)         (((uint32_t)(c) >>  8) & 0xFF)
#define FB_BLUE(c)          ( (uint32_t)(c)        & 0xFF)
#define FB_WITH_ALPHA(c,a)  (((uint32_t)(c) & 0x00FFFFFF) | ((uint32_t)(a) << 24))


/* =============================================================================
 * COLOR CONSTANTS (ARGB8888)
 * =============================================================================
 *
 * All colors are stored as 0xAARRGGBB in memory. The pixel format field
 * (fb->pixel_format) tells the display driver whether to swap R and B
 * before writing to hardware (needed for platforms that scan out ABGR).
 *
 * Drawing code always works in ARGB. Format conversion happens at present
 * time in the display driver, not in the drawing primitives.
 */

#define FB_COLOR_TRANSPARENT    0x00000000
#define FB_COLOR_BLACK          0xFF000000
#define FB_COLOR_WHITE          0xFFFFFFFF
#define FB_COLOR_RED            0xFFFF0000
#define FB_COLOR_GREEN          0xFF00FF00
#define FB_COLOR_BLUE           0xFF0000FF
#define FB_COLOR_YELLOW         0xFFFFFF00
#define FB_COLOR_CYAN           0xFF00FFFF
#define FB_COLOR_MAGENTA        0xFFFF00FF
#define FB_COLOR_GRAY           0xFF808080
#define FB_COLOR_LIGHT_GRAY     0xFFC0C0C0
#define FB_COLOR_DARK_GRAY      0xFF404040
#define FB_COLOR_ORANGE         0xFFFF8000
#define FB_COLOR_PURPLE         0xFF800080
#define FB_COLOR_TEAL           0xFF008080
#define FB_COLOR_PINK           0xFFFFC0CB
#define FB_COLOR_BROWN          0xFF8B4513

/* UI palette */
#define FB_COLOR_BG             0xFF1A1A2E
#define FB_COLOR_PRIMARY        0xFF4080FF
#define FB_COLOR_SUCCESS        0xFF40C080
#define FB_COLOR_WARNING        0xFFFFAA00
#define FB_COLOR_ERROR          0xFFFF4040
#define FB_COLOR_TEXT           0xFFE0E0E0
#define FB_COLOR_TEXT_DIM       0xFF808080
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

/* Game Boy palette */
extern const uint32_t gb_palette[4];


/* =============================================================================
 * BLEND MODES
 * =============================================================================
 */

typedef enum {
    FB_BLEND_OPAQUE,    /* No blending — source replaces destination */
    FB_BLEND_ALPHA,     /* Standard alpha blending (source over) */
    FB_BLEND_ADDITIVE,  /* Additive blending */
    FB_BLEND_MULTIPLY,  /* Multiplicative blending */
} fb_blend_mode_t;


/* =============================================================================
 * DATA STRUCTURES
 * =============================================================================
 */

/* Rectangle with signed position (allows negative x/y for clipping math) */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t w;
    uint32_t h;
} fb_rect_t;

/* Clip rectangle — always within framebuffer bounds, so unsigned */
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} fb_clip_t;

/* Dirty rectangle for incremental present */
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} fb_dirty_t;

/* Bitmap for blitting (ARGB8888 pixel data, tightly packed) */
typedef struct {
    uint32_t        width;
    uint32_t        height;
    const uint32_t *data;
} fb_bitmap_t;

/*
 * Pixel format — describes byte order as stored in the framebuffer.
 *
 * Drawing code always uses ARGB (0xAARRGGBB). On platforms where the
 * display hardware scans out in a different byte order, the display driver
 * swaps channels at present time (or writes in the native order directly).
 *
 *   FB_FORMAT_ARGB8888 — Default. R at byte 2, G at byte 1, B at byte 0
 *                        (little-endian storage: B, G, R, A in memory).
 *                        Used by: BCM2710, BCM2711, BCM2712, LattePanda MU (BGR
 *                        GOP fmt=1 means BGR in GOP terms = ARGB in memory).
 *
 *   FB_FORMAT_ABGR8888 — B and R swapped relative to ARGB. B at byte 2,
 *                        G at byte 1, R at byte 0.
 *                        Used by: JH7110 DC8200 in its default configuration.
 *
 * Note on LattePanda MU: GOP PixelFormat=1 (PixelBlueGreenRedReserved8BitPerColor)
 * means the hardware expects B in the low byte, G in the middle, R in the high
 * byte — which is exactly how 0xAARRGGBB is laid out in little-endian memory.
 * So despite the GOP calling it "BGR", no byte-swapping is needed. The existing
 * ARGB drawing code produces correct output on LattePanda MU without modification.
 */
typedef enum {
    FB_FORMAT_ARGB8888 = 0,   /* Default — ARM64 BCM, x86_64 GOP */
    FB_FORMAT_ABGR8888 = 1,   /* JH7110 DC8200 */
    FB_FORMAT_RGB565   = 2,   /* RP2350 ILI9488 SPI — 16 bit, no alpha */
} fb_pixel_format_t;


/* =============================================================================
 * FRAMEBUFFER STRUCTURE
 * =============================================================================
 *
 * NOTE: Named "struct framebuffer" (not anonymous) to allow forward declaration
 * in hal_display.h without a circular include dependency.
 *
 * FIELD NOTES
 * -----------
 * addr       — Pointer to the current drawing surface (back buffer in
 *              double-buffered mode; only buffer in single-buffered mode).
 *              This is a native pointer — 64 bits on all supported architectures.
 *              Assigned from a uintptr_t cast of the physical/virtual base
 *              address returned by the display driver. Never cast through
 *              uint32_t on x86_64 — the upper 32 bits would be lost.
 *
 * pitch      — Bytes per row. May be larger than width × 4 due to hardware
 *              alignment requirements (e.g., GOP may align rows to 256 bytes).
 *              Always use pitch for row stride, never width × 4.
 *
 * buffer_size — Size of one buffer in bytes (pitch × height). Declared as
 *              size_t so it is correct on both 32-bit and 64-bit targets.
 *              For 4K displays (3840×2160×4 = 33,177,600 bytes) this exceeds
 *              what fits comfortably in uint32_t and requires size_t.
 *
 * buffers[]  — Array of pointers to each physical buffer. Both entries point
 *              to the same address in single-buffered SimpleFB mode (RISC-V).
 *              In double-buffered BCM mode, [0] and [1] are separate regions
 *              within a single large allocation. On x86_64, only [0] is used
 *              (GOP SimpleFB mode; double-buffering requires a second
 *              allocation which is deferred to a later tutorial chapter).
 */

typedef struct framebuffer {
    /*
     * Buffer address and geometry.
     * addr is the active drawing buffer. On x86_64, this holds a full
     * 64-bit GOP framebuffer address — assign via (uint32_t *)(uintptr_t).
     */
    uint32_t *addr;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;            /* Bytes per row — always use this, not width×4 */

    /* Double-buffer state */
    uint32_t *buffers[FB_BUFFER_COUNT];
    uint32_t  front_buffer;     /* Index of displayed buffer */
    uint32_t  back_buffer;      /* Index of drawing buffer */
    size_t    buffer_size;      /* Bytes per buffer (pitch × height) — size_t for >4GB correctness */
    uint32_t  virtual_height;   /* Total pixel height (height × buffer_count) */

    /* Dirty rectangle tracking */
    fb_dirty_t dirty_rects[FB_MAX_DIRTY_RECTS];
    uint32_t   dirty_count;
    bool       full_dirty;

    /* Clipping stack
     *
     * clip_stack[0] is the base clip — always the full framebuffer extent.
     * It MUST be initialized to {0, 0, width, height} by the display driver.
     * Leaving it zero-initialized ({0,0,0,0}) clips ALL drawing to nothing.
     * This is the most common bare-metal display bring-up mistake.
     */
    fb_clip_t clip_stack[FB_MAX_CLIP_DEPTH];
    uint32_t  clip_depth;

    /* Metadata */
    uint64_t         frame_count;
    bool             vsync_enabled;
    bool             initialized;
    fb_pixel_format_t pixel_format;

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

static inline uint32_t   fb_width(const framebuffer_t *fb)  { return fb->width; }
static inline uint32_t   fb_height(const framebuffer_t *fb) { return fb->height; }
static inline uint32_t   fb_pitch(const framebuffer_t *fb)  { return fb->pitch; }
static inline uint32_t  *fb_buffer(framebuffer_t *fb)       { return fb->addr; }
static inline uint64_t   fb_frame_count(const framebuffer_t *fb) { return fb->frame_count; }
static inline bool        fb_is_initialized(const framebuffer_t *fb) { return fb->initialized; }
static inline size_t      fb_size(const framebuffer_t *fb)  { return fb->buffer_size; }


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

bool     fb_push_clip(framebuffer_t *fb, fb_rect_t rect);
void     fb_pop_clip(framebuffer_t *fb);
void     fb_reset_clip(framebuffer_t *fb);
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
 * BASIC DRAWING
 * =============================================================================
 */

void     fb_clear(framebuffer_t *fb, uint32_t color);
void     fb_put_pixel(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color);
void     fb_put_pixel_blend(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color);
void     fb_put_pixel_unchecked(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_get_pixel(const framebuffer_t *fb, uint32_t x, uint32_t y);

void fb_fill_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_fill_rect_blend(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_line(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void fb_draw_circle(framebuffer_t *fb, int32_t cx, int32_t cy, uint32_t radius, uint32_t color);
void fb_fill_circle(framebuffer_t *fb, int32_t cx, int32_t cy, uint32_t radius, uint32_t color);

void fb_fill_rounded_rect(framebuffer_t *fb, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t radius, uint32_t color);
void fb_draw_rounded_rect(framebuffer_t *fb, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t radius, uint32_t color);

void fb_copy_rect(framebuffer_t *fb,
                  uint32_t src_x, uint32_t src_y,
                  uint32_t dst_x, uint32_t dst_y,
                  uint32_t w, uint32_t h);
void fb_scroll_v(framebuffer_t *fb, int32_t pixels, uint32_t fill_color);


/* =============================================================================
 * TEXT RENDERING
 * =============================================================================
 */

void fb_draw_char(framebuffer_t *fb, uint32_t x, uint32_t y,
                  char c, uint32_t fg, uint32_t bg);
void fb_draw_char_transparent(framebuffer_t *fb, uint32_t x, uint32_t y,
                               char c, uint32_t fg);
void fb_draw_string(framebuffer_t *fb, uint32_t x, uint32_t y,
                    const char *str, uint32_t fg, uint32_t bg);
void fb_draw_string_transparent(framebuffer_t *fb, uint32_t x, uint32_t y,
                                 const char *str, uint32_t fg);
void fb_draw_string_scaled(framebuffer_t *fb, uint32_t x, uint32_t y,
                           const char *str, uint32_t fg, uint32_t bg,
                           uint32_t scale);
void fb_draw_string_scaled_transparent(framebuffer_t *fb, uint32_t x, uint32_t y,
                                       const char *str, uint32_t fg,
                                       uint32_t scale);


/* =============================================================================
 * BITMAP BLITTING
 * =============================================================================
 */

void fb_blit(framebuffer_t *fb, int32_t x, int32_t y,
             const fb_bitmap_t *bitmap, fb_blend_mode_t blend);


/* =============================================================================
 * COLOR UTILITIES
 * =============================================================================
 */

static inline uint32_t fb_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g <<  8) |  (uint32_t)b;
}

static inline uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return fb_rgba(r, g, b, 0xFF);
}

/* Blend functions — defined in framebuffer.c */
uint32_t fb_blend_alpha(uint32_t src, uint32_t dst);
uint32_t fb_blend_additive(uint32_t src, uint32_t dst);
uint32_t fb_blend_multiply(uint32_t src, uint32_t dst);
uint32_t fb_color_lerp(uint32_t c1, uint32_t c2, uint8_t t);

/* Line primitives (forward-declared; defined after fb_draw_rect in framebuffer.c) */
void fb_draw_hline(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t len, uint32_t color);
void fb_draw_vline(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t len, uint32_t color);

/* Text metrics */
uint32_t fb_text_width(const char *str);
uint32_t fb_text_width_large(const char *str);
uint32_t fb_measure_string(const char *str, bool large);

/* Bitmap blitting */
void fb_blit_bitmap_blend(framebuffer_t *fb, int32_t x, int32_t y,
                          const fb_bitmap_t *bitmap, fb_blend_mode_t blend);

/* Game Boy screen blitting */
void fb_blit_gb_screen_dmg_palette(framebuffer_t *fb,
                                   const uint8_t *pal_data,
                                   const uint32_t *palette);


#endif /* FRAMEBUFFER_H */