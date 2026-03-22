/*
 * drivers/framebuffer/fb_pixel.h — Format-Aware Pixel Access Helpers
 *
 * Tutorial-OS: Framebuffer Pixel Abstraction
 *
 * WHY THIS EXISTS:
 * ================
 *
 * Every framebuffer drawing function in Tutorial-OS was written for ARGB8888
 * (32-bit pixels, 4 bytes each). The code assumes:
 *   - fb->addr is uint32_t *
 *   - fb->pitch / 4 gives the stride in pixels
 *   - row[x] reads/writes one pixel as a uint32_t
 *
 * The RP2350/ILI9488 board breaks this assumption. With only 520 KB of SRAM,
 * an ARGB8888 framebuffer for 320×480 would need 614 KB — more than exists.
 * The RGB565 format (2 bytes per pixel, 307 KB) fits, but every drawing
 * function must now handle two pixel sizes.
 *
 * These inline helpers provide format-aware pixel read/write that:
 *   1. Accept ARGB8888 colors (the universal drawing API doesn't change)
 *   2. Store as RGB565 or ARGB8888 depending on fb->pixel_format
 *   3. Read back as ARGB8888 (for blending operations)
 *
 * THE CONVERSION COST:
 * ====================
 *
 * On platforms where pixel_format is ARGB8888 (every board except RP2350),
 * the format check is a never-taken branch that the CPU's branch predictor
 * eliminates in ~0 cycles after the first iteration. No performance impact
 * on existing platforms.
 *
 * On RP2350, the ARGB→RGB565 conversion adds ~5 instructions per pixel.
 * At 150 MHz Cortex-M33, this is negligible compared to SPI transfer time
 * (which dominates frame time at ~40 ms per full-screen push).
 *
 * TEACHING VALUE:
 * ===============
 *
 * This is a textbook "abstraction layer" inside the framebuffer driver.
 * The drawing functions deal in ARGB colors (the logical representation).
 * The pixel helpers deal in storage formats (the physical representation).
 * The display_spi.c present function deals in wire formats (RGB666).
 *
 *   Drawing API:  ARGB8888 colors (always)
 *        ↓ fb_write_px()
 *   Shadow buffer: RGB565 (RP2350) or ARGB8888 (all others)
 *        ↓ hal_display_present()
 *   Wire format:  RGB666 (ILI9488) or scan-out (HDMI/DPI/GOP)
 */

#ifndef FB_PIXEL_H
#define FB_PIXEL_H

/* Forward declaration — full struct in framebuffer.h */
struct framebuffer;

/* ─────────────────────────────────────────────────────────────────────────────
 * RGB565 ↔ ARGB8888 CONVERSION
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * ARGB8888: [AAAAAAAA RRRRRRRR GGGGGGGG BBBBBBBB] = 32 bits
 * RGB565:   [RRRRRGGG GGGBBBBB]                    = 16 bits
 *
 * Compression (ARGB → 565): take the top bits of each channel.
 * Expansion (565 → ARGB):   shift bits up and replicate MSBs into LSBs
 *                            for better color accuracy. Alpha is set to 0xFF.
 */

static inline uint16_t fb_argb_to_565(uint32_t argb)
{
    uint16_t r = (argb >> 19) & 0x1F;   /* 5 bits of red   */
    uint16_t g = (argb >> 10) & 0x3F;   /* 6 bits of green  */
    uint16_t b = (argb >>  3) & 0x1F;   /* 5 bits of blue   */
    return (r << 11) | (g << 5) | b;
}

static inline uint32_t fb_565_to_argb(uint16_t c)
{
    /* Expand and replicate MSBs into LSBs for better accuracy */
    uint8_t r = ((c >> 11) & 0x1F);
    uint8_t g = ((c >>  5) & 0x3F);
    uint8_t b = ( c        & 0x1F);
    r = (r << 3) | (r >> 2);   /* 5→8 bits */
    g = (g << 2) | (g >> 4);   /* 6→8 bits */
    b = (b << 3) | (b >> 2);   /* 5→8 bits */
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * ROW POINTER ACCESS
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Returns a byte pointer to the start of row `y`. This works for any pixel
 * format because fb->pitch is always in bytes.
 *
 * Replaces the old pattern:
 *   uint32_t *row = fb->addr + y * (fb->pitch / 4);
 *
 * With:
 *   uint8_t *row = fb_row(fb, y);
 *
 * The caller then uses fb_write_px() / fb_read_px() to access pixels,
 * which handle the format-specific offset and width internally.
 */

static inline uint8_t *fb_row(const struct framebuffer *fb, uint32_t y)
{
    return (uint8_t *)fb->addr + y * fb->pitch;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * PIXEL WRITE
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Write an ARGB8888 color to pixel position `x` in the given row.
 * Converts to RGB565 if the framebuffer is in that format.
 *
 * Replaces the old pattern:
 *   WRITE_VOLATILE(&row[x], color);
 */

static inline void fb_write_px(const struct framebuffer *fb,
                                uint8_t *row, uint32_t x, uint32_t color)
{
    if (fb->pixel_format == FB_FORMAT_RGB565) {
        uint16_t *px = (uint16_t *)row + x;
        *px = fb_argb_to_565(color);
    } else {
        uint32_t *px = (uint32_t *)row + x;
        *px = color;
    }
}

/* Volatile variant for memory-mapped framebuffers (HDMI/DPI/GOP scan-out) */
static inline void fb_write_px_v(const struct framebuffer *fb,
                                  uint8_t *row, uint32_t x, uint32_t color)
{
    if (fb->pixel_format == FB_FORMAT_RGB565) {
        volatile uint16_t *px = (volatile uint16_t *)row + x;
        *px = fb_argb_to_565(color);
    } else {
        volatile uint32_t *px = (volatile uint32_t *)row + x;
        *px = color;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * PIXEL READ
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Read a pixel from position `x` in the given row, returning ARGB8888.
 * Expands from RGB565 if the framebuffer is in that format.
 *
 * Replaces the old pattern:
 *   uint32_t pixel = READ_VOLATILE(&row[x]);
 */

static inline uint32_t fb_read_px(const struct framebuffer *fb,
                                   const uint8_t *row, uint32_t x)
{
    if (fb->pixel_format == FB_FORMAT_RGB565) {
        const uint16_t *px = (const uint16_t *)row + x;
        return fb_565_to_argb(*px);
    } else {
        const uint32_t *px = (const uint32_t *)row + x;
        return *px;
    }
}

/* Volatile variant */
static inline uint32_t fb_read_px_v(const struct framebuffer *fb,
                                     const uint8_t *row, uint32_t x)
{
    if (fb->pixel_format == FB_FORMAT_RGB565) {
        const volatile uint16_t *px = (const volatile uint16_t *)row + x;
        return fb_565_to_argb(*px);
    } else {
        const volatile uint32_t *px = (const volatile uint32_t *)row + x;
        return *px;
    }
}

#endif /* FB_PIXEL_H */