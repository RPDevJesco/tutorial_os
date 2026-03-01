/*
 * drivers/framebuffer/framebuffer.rs — Framebuffer Implementation (ARGB8888)
 *
 * Tutorial-OS: Portable Framebuffer Driver (Rust)
 *
 * This is the Rust equivalent of drivers/framebuffer/framebuffer.c.
 * It provides all drawing primitives for a 32-bit ARGB framebuffer:
 *   - Pixel, line, rectangle, circle, triangle, rounded rect, gradient
 *   - Text rendering with 8×8 and 8×16 bitmap fonts at any scale
 *   - Bitmap blitting with multiple blend modes
 *   - Clipping stack, dirty tracking, double buffering
 *   - Screen operations (copy, scroll)
 *
 * KEY DESIGN:
 *   This module is 100% platform-portable. It operates on raw pixel memory
 *   and knows nothing about how the framebuffer was allocated. Platform-
 *   specific initialization (mailbox, VOP2, SimpleFB, UEFI GOP) lives in
 *   the SoC display drivers, which set up the Framebuffer struct and hand
 *   it to this code.
 *
 * VOLATILE ACCESS:
 *   All pixel writes use `core::ptr::write_volatile` because the framebuffer
 *   memory is DMA-visible to the display controller. The compiler must not
 *   reorder or optimize away these writes.
 *
 * SAFETY:
 *   The Framebuffer struct contains a raw pointer to video memory. Most
 *   methods are safe because they perform bounds checking via the clipping
 *   system. The `put_pixel_unchecked` method is unsafe for performance-
 *   critical inner loops where bounds are guaranteed by the caller.
 */

#![no_std]
#![allow(dead_code)]

use core::ptr;

// =============================================================================
// FONT DATA
// =============================================================================
//
// Each font is a [u8; N] array per glyph, covering ASCII 32-126 (95 chars).
// The 8×8 font has 8 bytes per glyph, the 8×16 font has 16 bytes per glyph.
// Each byte represents one row, MSB = leftmost pixel.

mod font_data;
use font_data::{FONT_8X8, FONT_8X16};

// =============================================================================
// DISPLAY CONSTANTS
// =============================================================================

pub const FB_DEFAULT_WIDTH: u32 = 640;
pub const FB_DEFAULT_HEIGHT: u32 = 480;
pub const FB_BITS_PER_PIXEL: u32 = 32;

/// 8×8 font character dimensions
pub const FB_CHAR_WIDTH: u32 = 8;
pub const FB_CHAR_HEIGHT: u32 = 8;

/// 8×16 "large" font character dimensions (same width, double height)
pub const FB_CHAR_WIDTH_LG: u32 = 8;
pub const FB_CHAR_HEIGHT_LG: u32 = 16;

pub const FB_MAX_DIRTY_RECTS: usize = 32;
pub const FB_MAX_CLIP_DEPTH: usize = 8;
pub const FB_BUFFER_COUNT: usize = 2;

// GameBoy display constants
pub const GB_WIDTH: u32 = 160;
pub const GB_HEIGHT: u32 = 144;
pub const GB_SCALE: u32 = 2;
pub const GB_SCALED_W: u32 = GB_WIDTH * GB_SCALE;
pub const GB_SCALED_H: u32 = GB_HEIGHT * GB_SCALE;
pub const GB_OFFSET_X: u32 = (FB_DEFAULT_WIDTH - GB_SCALED_W) / 2;
pub const GB_OFFSET_Y: u32 = (FB_DEFAULT_HEIGHT - GB_SCALED_H) / 2;

// =============================================================================
// COLOR MACROS (ARGB8888)
// =============================================================================

/// Compose an ARGB color from individual channels.
#[inline(always)]
pub const fn argb(a: u32, r: u32, g: u32, b: u32) -> u32 {
    (a << 24) | (r << 16) | (g << 8) | b
}

/// Compose an opaque RGB color (alpha = 0xFF).
#[inline(always)]
pub const fn rgb(r: u32, g: u32, b: u32) -> u32 {
    argb(255, r, g, b)
}

/// Extract alpha channel.
#[inline(always)]
pub const fn alpha(c: u32) -> u32 {
    (c >> 24) & 0xFF
}

/// Extract red channel.
#[inline(always)]
pub const fn red(c: u32) -> u32 {
    (c >> 16) & 0xFF
}

/// Extract green channel.
#[inline(always)]
pub const fn green(c: u32) -> u32 {
    (c >> 8) & 0xFF
}

/// Extract blue channel.
#[inline(always)]
pub const fn blue(c: u32) -> u32 {
    c & 0xFF
}

/// Set alpha on an existing RGB color.
#[inline(always)]
pub const fn with_alpha(rgb_color: u32, a: u32) -> u32 {
    (rgb_color & 0x00FFFFFF) | (a << 24)
}

// =============================================================================
// PREDEFINED COLORS (ARGB8888)
// =============================================================================

pub const COLOR_BLACK: u32 = 0xFF000000;
pub const COLOR_WHITE: u32 = 0xFFFFFFFF;
pub const COLOR_RED: u32 = 0xFFFF0000;
pub const COLOR_GREEN: u32 = 0xFF00FF00;
pub const COLOR_BLUE: u32 = 0xFF0000FF;
pub const COLOR_CYAN: u32 = 0xFF00FFFF;
pub const COLOR_MAGENTA: u32 = 0xFFFF00FF;
pub const COLOR_YELLOW: u32 = 0xFFFFFF00;
pub const COLOR_GRAY: u32 = 0xFF808080;
pub const COLOR_DARK_GRAY: u32 = 0xFF404040;
pub const COLOR_LIGHT_GRAY: u32 = 0xFFC0C0C0;
pub const COLOR_DARK_BLUE: u32 = 0xFF000040;
pub const COLOR_ORANGE: u32 = 0xFFFF8000;
pub const COLOR_TRANSPARENT: u32 = 0x00000000;

// Menu-specific colors
pub const COLOR_MENU_BG: u32 = 0xFF101020;
pub const COLOR_MENU_HIGHLIGHT: u32 = 0xFF303060;
pub const COLOR_MENU_TEXT: u32 = 0xFFE0E0E0;
pub const COLOR_MENU_TEXT_DIM: u32 = 0xFF808080;
pub const COLOR_MENU_ACCENT: u32 = 0xFF4080FF;

// Semi-transparent variants
pub const COLOR_BLACK_50: u32 = 0x80000000;
pub const COLOR_BLACK_75: u32 = 0xC0000000;
pub const COLOR_WHITE_50: u32 = 0x80FFFFFF;
pub const COLOR_WHITE_25: u32 = 0x40FFFFFF;

// =============================================================================
// GAMEBOY PALETTE (ARGB8888) — DMG Classic Green
// =============================================================================

pub const GB_PALETTE: [u32; 4] = [
    0xFFE0F8D0, // Lightest (white-ish green)
    0xFF88C070, // Light green
    0xFF346856, // Dark green
    0xFF081820, // Darkest (near black)
];

// =============================================================================
// BLEND MODES
// =============================================================================

/// Blend mode for bitmap blitting and compositing.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum BlendMode {
    /// No blending, source replaces destination.
    Opaque = 0,
    /// Standard alpha blending (source over).
    Alpha = 1,
    /// Additive blending.
    Additive = 2,
    /// Multiplicative blending.
    Multiply = 3,
}

// =============================================================================
// DATA STRUCTURES
// =============================================================================

/// Rectangle with signed position (for clipping with negative coords).
#[derive(Debug, Clone, Copy)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: u32,
    pub h: u32,
}

/// Clip rectangle (unsigned, always within framebuffer bounds).
#[derive(Debug, Clone, Copy)]
pub struct ClipRect {
    pub x: u32,
    pub y: u32,
    pub w: u32,
    pub h: u32,
}

/// Dirty rectangle for tracking changed regions.
#[derive(Debug, Clone, Copy)]
pub struct DirtyRect {
    pub x: u32,
    pub y: u32,
    pub w: u32,
    pub h: u32,
}

/// Bitmap for blitting (ARGB8888 pixel data).
pub struct Bitmap<'a> {
    pub width: u32,
    pub height: u32,
    pub data: &'a [u32],
}

// =============================================================================
// FRAMEBUFFER STRUCTURE
// =============================================================================

/// Main framebuffer structure.
///
/// This is the Rust equivalent of C's `framebuffer_t`. All drawing methods
/// operate on this struct. Platform-specific display drivers initialize
/// the fields (addr, width, height, pitch) and then hand it to the
/// portable drawing code.
///
/// # Memory Layout
///
/// Must be `#[repr(C)]` for FFI compatibility with C code that may also
/// reference this struct.
#[repr(C)]
pub struct Framebuffer {
    /// Pointer to the current drawing buffer (back buffer in double-buffered mode).
    pub addr: *mut u32,
    /// Width in pixels.
    pub width: u32,
    /// Height in pixels.
    pub height: u32,
    /// Bytes per row (may be larger than width × 4 due to alignment).
    pub pitch: u32,

    /// Double-buffer pointers.
    pub buffers: [*mut u32; FB_BUFFER_COUNT],
    /// Index of the front (displayed) buffer.
    pub front_buffer: u32,
    /// Index of the back (drawing) buffer.
    pub back_buffer: u32,
    /// Size of one buffer in bytes.
    pub buffer_size: u32,
    /// Total virtual height (height × buffer_count).
    pub virtual_height: u32,

    /// Dirty rectangle tracking.
    pub dirty_rects: [DirtyRect; FB_MAX_DIRTY_RECTS],
    pub dirty_count: u32,
    pub full_dirty: bool,

    /// Clipping stack.
    pub clip_stack: [ClipRect; FB_MAX_CLIP_DEPTH],
    pub clip_depth: u32,

    /// Frame counter (incremented on each present).
    pub frame_count: u64,
    /// Whether to wait for vsync on present.
    pub vsync_enabled: bool,
    /// Whether the framebuffer has been successfully initialized.
    pub initialized: bool,
}

// =============================================================================
// SINE LOOKUP TABLE
// =============================================================================
// Indexed by degree (0–90), returns sin × 256 (fixed-point 8.8).

const SIN_TABLE: [i16; 91] = [
    0, 4, 9, 13, 18, 22, 27, 31, 36, 40,
    44, 49, 53, 58, 62, 66, 71, 75, 79, 83,
    88, 92, 96, 100, 104, 108, 112, 116, 120, 124,
    128, 131, 135, 139, 143, 146, 150, 153, 157, 160,
    164, 167, 171, 174, 177, 181, 184, 187, 190, 193,
    196, 198, 201, 204, 207, 209, 212, 214, 217, 219,
    221, 223, 226, 228, 230, 232, 233, 235, 237, 238,
    240, 242, 243, 244, 246, 247, 248, 249, 250, 251,
    252, 252, 253, 254, 254, 255, 255, 255, 256, 256,
    256,
];

// =============================================================================
// MATH HELPERS
// =============================================================================

/// Integer square root (Newton's method).
pub fn isqrt(n: u32) -> u32 {
    if n == 0 {
        return 0;
    }
    let mut x = n;
    let mut y = (x + 1) / 2;
    while y < x {
        x = y;
        y = (x + n / x) / 2;
    }
    x
}

/// Fixed-point sin/cos lookup (returns values scaled by 256).
///
/// Uses a 91-entry table covering 0–90° and mirrors for all quadrants.
pub fn sin_cos_deg(deg: u32) -> (i32, i32) {
    let deg = deg % 360;

    let (lookup_deg, sin_sign, cos_sign) = if deg <= 90 {
        (deg, 1i32, 1i32)
    } else if deg <= 180 {
        (180 - deg, 1, -1)
    } else if deg <= 270 {
        (deg - 180, -1, -1)
    } else {
        (360 - deg, -1, 1)
    };

    let sin_val = SIN_TABLE[lookup_deg as usize] as i32 * sin_sign;
    let cos_val = SIN_TABLE[(90 - lookup_deg) as usize] as i32 * cos_sign;
    (sin_val, cos_val)
}

// =============================================================================
// COLOR UTILITIES
// =============================================================================

/// Linearly interpolate between two colors.
///
/// `t` ranges from 0 (returns `c1`) to 255 (returns `c2`).
pub fn color_lerp(c1: u32, c2: u32, t: u8) -> u32 {
    let t = t as u32;
    let inv_t = 255 - t;

    let a = (alpha(c1) * inv_t + alpha(c2) * t) / 255;
    let r = (red(c1) * inv_t + red(c2) * t) / 255;
    let g = (green(c1) * inv_t + green(c2) * t) / 255;
    let b = (blue(c1) * inv_t + blue(c2) * t) / 255;

    argb(a, r, g, b)
}

/// Standard alpha blending: source over destination.
pub fn blend_alpha(src: u32, dst: u32) -> u32 {
    let sa = alpha(src);
    if sa == 0 {
        return dst;
    }
    if sa == 255 {
        return src;
    }

    let inv_sa = 255 - sa;

    let r = (red(src) * sa + red(dst) * inv_sa) / 255;
    let g = (green(src) * sa + green(dst) * inv_sa) / 255;
    let b = (blue(src) * sa + blue(dst) * inv_sa) / 255;
    let a = (sa * 255 + alpha(dst) * inv_sa) / 255;

    argb(a, r, g, b)
}

/// Additive blending: clamped sum of channels.
pub fn blend_additive(src: u32, dst: u32) -> u32 {
    let r = core::cmp::min(red(src) + red(dst), 255);
    let g = core::cmp::min(green(src) + green(dst), 255);
    let b = core::cmp::min(blue(src) + blue(dst), 255);
    rgb(r, g, b)
}

/// Multiplicative blending: channel-wise multiply.
pub fn blend_multiply(src: u32, dst: u32) -> u32 {
    let r = (red(src) * red(dst)) / 255;
    let g = (green(src) * green(dst)) / 255;
    let b = (blue(src) * blue(dst)) / 255;
    rgb(r, g, b)
}

// =============================================================================
// PRIVATE HELPERS
// =============================================================================

#[inline(always)]
fn min_u32(a: u32, b: u32) -> u32 {
    if a < b { a } else { b }
}

#[inline(always)]
fn max_u32(a: u32, b: u32) -> u32 {
    if a > b { a } else { b }
}

#[inline(always)]
fn min_i32(a: i32, b: i32) -> i32 {
    if a < b { a } else { b }
}

#[inline(always)]
fn max_i32(a: i32, b: i32) -> i32 {
    if a > b { a } else { b }
}

#[inline(always)]
fn abs_i32(x: i32) -> i32 {
    if x < 0 { -x } else { x }
}

// =============================================================================
// FRAMEBUFFER IMPLEMENTATION
// =============================================================================

impl Framebuffer {
    // =========================================================================
    // CONSTRUCTION
    // =========================================================================

    /// Create a zeroed/uninitialized framebuffer.
    ///
    /// Call a platform-specific init method (or set fields manually) before
    /// drawing. This matches the C pattern where `framebuffer_t fb = {0};`
    /// is followed by `fb_init(&fb)` or `hal_display_init(&fb)`.
    pub const fn new() -> Self {
        Self {
            addr: core::ptr::null_mut(),
            width: 0,
            height: 0,
            pitch: 0,
            buffers: [core::ptr::null_mut(); FB_BUFFER_COUNT],
            front_buffer: 0,
            back_buffer: 1,
            buffer_size: 0,
            virtual_height: 0,
            dirty_rects: [DirtyRect { x: 0, y: 0, w: 0, h: 0 }; FB_MAX_DIRTY_RECTS],
            dirty_count: 0,
            full_dirty: false,
            clip_stack: [ClipRect { x: 0, y: 0, w: 0, h: 0 }; FB_MAX_CLIP_DEPTH],
            clip_depth: 0,
            frame_count: 0,
            vsync_enabled: false,
            initialized: false,
        }
    }

    // =========================================================================
    // ACCESSORS
    // =========================================================================

    #[inline]
    pub fn width(&self) -> u32 {
        self.width
    }

    #[inline]
    pub fn height(&self) -> u32 {
        self.height
    }

    #[inline]
    pub fn pitch(&self) -> u32 {
        self.pitch
    }

    #[inline]
    pub fn buffer(&self) -> *mut u32 {
        self.addr
    }

    #[inline]
    pub fn frame_count(&self) -> u64 {
        self.frame_count
    }

    #[inline]
    pub fn is_initialized(&self) -> bool {
        self.initialized
    }

    /// Total size of one buffer in bytes.
    #[inline]
    pub fn size(&self) -> u32 {
        self.pitch * self.height
    }

    /// Pitch measured in u32 words (pixels for ARGB8888).
    #[inline]
    fn pitch_words(&self) -> u32 {
        self.pitch / 4
    }

    // =========================================================================
    // DISPLAY CONTROL
    // =========================================================================

    pub fn set_vsync(&mut self, enabled: bool) {
        self.vsync_enabled = enabled;
    }

    // =========================================================================
    // CLIPPING
    // =========================================================================

    /// Check if a pixel is outside the current clip rectangle.
    #[inline(always)]
    fn is_clipped(&self, x: u32, y: u32) -> bool {
        let clip = &self.clip_stack[self.clip_depth as usize];
        x < clip.x || y < clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h
    }

    /// Push a new clip rectangle onto the stack (intersected with current).
    ///
    /// Returns `true` if successful, `false` if stack is full.
    pub fn push_clip(&mut self, rect: Rect) -> bool {
        if self.clip_depth as usize >= FB_MAX_CLIP_DEPTH - 1 {
            return false;
        }

        let current = &self.clip_stack[self.clip_depth as usize];
        let x1 = max_i32(rect.x, current.x as i32);
        let y1 = max_i32(rect.y, current.y as i32);
        let x2 = min_i32(rect.x + rect.w as i32, (current.x + current.w) as i32);
        let y2 = min_i32(rect.y + rect.h as i32, (current.y + current.h) as i32);

        self.clip_depth += 1;
        let new_clip = &mut self.clip_stack[self.clip_depth as usize];

        if x2 <= x1 || y2 <= y1 {
            new_clip.x = 0;
            new_clip.y = 0;
            new_clip.w = 0;
            new_clip.h = 0;
        } else {
            new_clip.x = x1 as u32;
            new_clip.y = y1 as u32;
            new_clip.w = (x2 - x1) as u32;
            new_clip.h = (y2 - y1) as u32;
        }
        true
    }

    /// Pop the top clip rectangle.
    pub fn pop_clip(&mut self) {
        if self.clip_depth > 0 {
            self.clip_depth -= 1;
        }
    }

    /// Reset clipping to the full framebuffer.
    pub fn reset_clip(&mut self) {
        self.clip_depth = 0;
        self.clip_stack[0] = ClipRect {
            x: 0,
            y: 0,
            w: self.width,
            h: self.height,
        };
    }

    /// Get the current clip rectangle.
    pub fn get_clip(&self) -> ClipRect {
        self.clip_stack[self.clip_depth as usize]
    }

    // =========================================================================
    // DIRTY TRACKING
    // =========================================================================

    /// Mark a rectangular region as dirty.
    pub fn mark_dirty(&mut self, x: u32, y: u32, w: u32, h: u32) {
        if self.full_dirty {
            return;
        }
        if self.dirty_count as usize >= FB_MAX_DIRTY_RECTS {
            self.full_dirty = true;
            return;
        }
        self.dirty_rects[self.dirty_count as usize] = DirtyRect { x, y, w, h };
        self.dirty_count += 1;
    }

    /// Mark the entire framebuffer as dirty.
    pub fn mark_all_dirty(&mut self) {
        self.full_dirty = true;
    }

    /// Clear all dirty tracking.
    pub fn clear_dirty(&mut self) {
        self.dirty_count = 0;
        self.full_dirty = false;
    }

    /// Check if any region is dirty.
    pub fn is_dirty(&self) -> bool {
        self.full_dirty || self.dirty_count > 0
    }

    // =========================================================================
    // BASIC DRAWING — PIXELS
    // =========================================================================

    /// Draw a single pixel (bounds-checked and clip-checked).
    pub fn put_pixel(&mut self, x: u32, y: u32, color: u32) {
        if x >= self.width || y >= self.height || self.is_clipped(x, y) {
            return;
        }
        let offset = y * self.pitch_words() + x;
        unsafe {
            ptr::write_volatile(self.addr.add(offset as usize), color);
        }
    }

    /// Draw a pixel with alpha blending against the existing color.
    pub fn put_pixel_blend(&mut self, x: u32, y: u32, color: u32) {
        if x >= self.width || y >= self.height || self.is_clipped(x, y) {
            return;
        }
        let offset = y * self.pitch_words() + x;
        unsafe {
            let dst = ptr::read_volatile(self.addr.add(offset as usize));
            ptr::write_volatile(self.addr.add(offset as usize), blend_alpha(color, dst));
        }
    }

    /// Draw a pixel without any bounds checking.
    ///
    /// # Safety
    /// Caller must ensure `x < width` and `y < height`.
    pub unsafe fn put_pixel_unchecked(&mut self, x: u32, y: u32, color: u32) {
        let offset = y * self.pitch_words() + x;
        ptr::write_volatile(self.addr.add(offset as usize), color);
    }

    /// Read a pixel value (returns TRANSPARENT if out of bounds).
    pub fn get_pixel(&self, x: u32, y: u32) -> u32 {
        if x >= self.width || y >= self.height {
            return COLOR_TRANSPARENT;
        }
        let offset = y * self.pitch_words() + x;
        unsafe { ptr::read_volatile(self.addr.add(offset as usize)) }
    }

    // =========================================================================
    // BASIC DRAWING — CLEAR & RECTANGLES
    // =========================================================================

    /// Clear the entire framebuffer to a solid color.
    pub fn clear(&mut self, color: u32) {
        let pw = self.pitch_words();
        for y in 0..self.height {
            let row_base = (y * pw) as usize;
            for x in 0..self.width {
                unsafe {
                    ptr::write_volatile(self.addr.add(row_base + x as usize), color);
                }
            }
        }
        self.mark_all_dirty();
    }

    /// Fill a rectangle with a solid color (clipped).
    pub fn fill_rect(&mut self, x: u32, y: u32, w: u32, h: u32, color: u32) {
        let clip = self.clip_stack[self.clip_depth as usize];
        let x1 = max_u32(x, clip.x);
        let y1 = max_u32(y, clip.y);
        let x2 = min_u32(x + w, clip.x + clip.w);
        let y2 = min_u32(y + h, clip.y + clip.h);

        if x2 <= x1 || y2 <= y1 {
            return;
        }

        let pw = self.pitch_words();
        for py in y1..y2 {
            let row_base = (py * pw) as usize;
            for px in x1..x2 {
                unsafe {
                    ptr::write_volatile(self.addr.add(row_base + px as usize), color);
                }
            }
        }
        self.mark_dirty(x1, y1, x2 - x1, y2 - y1);
    }

    /// Fill a rectangle with alpha blending.
    pub fn fill_rect_blend(&mut self, x: u32, y: u32, w: u32, h: u32, color: u32) {
        let clip = self.clip_stack[self.clip_depth as usize];
        let x1 = max_u32(x, clip.x);
        let y1 = max_u32(y, clip.y);
        let x2 = min_u32(x + w, clip.x + clip.w);
        let y2 = min_u32(y + h, clip.y + clip.h);

        if x2 <= x1 || y2 <= y1 {
            return;
        }

        let pw = self.pitch_words();
        for py in y1..y2 {
            let row_base = (py * pw) as usize;
            for px in x1..x2 {
                unsafe {
                    let dst = ptr::read_volatile(self.addr.add(row_base + px as usize));
                    ptr::write_volatile(
                        self.addr.add(row_base + px as usize),
                        blend_alpha(color, dst),
                    );
                }
            }
        }
        self.mark_dirty(x1, y1, x2 - x1, y2 - y1);
    }

    /// Draw a rectangle outline (1px border).
    pub fn draw_rect(&mut self, x: u32, y: u32, w: u32, h: u32, color: u32) {
        if w == 0 || h == 0 {
            return;
        }
        self.draw_hline(x, y, w, color);
        self.draw_hline(x, y + h - 1, w, color);
        self.draw_vline(x, y, h, color);
        self.draw_vline(x + w - 1, y, h, color);
    }

    // =========================================================================
    // BASIC DRAWING — LINES
    // =========================================================================

    /// Draw a horizontal line (clipped).
    pub fn draw_hline(&mut self, x: u32, y: u32, len: u32, color: u32) {
        let clip = self.clip_stack[self.clip_depth as usize];
        if y < clip.y || y >= clip.y + clip.h {
            return;
        }

        let x1 = max_u32(x, clip.x);
        let x2 = min_u32(x + len, clip.x + clip.w);
        if x2 <= x1 {
            return;
        }

        let row_base = (y * self.pitch_words()) as usize;
        for px in x1..x2 {
            unsafe {
                ptr::write_volatile(self.addr.add(row_base + px as usize), color);
            }
        }
    }

    /// Draw a horizontal line with signed coordinates (safe for negative values).
    fn draw_hline_safe(&mut self, x: i32, y: i32, len: i32, color: u32) {
        if y < 0 || y >= self.height as i32 {
            return;
        }
        if x >= self.width as i32 {
            return;
        }
        let mut x = x;
        let mut len = len;
        if x + len <= 0 {
            return;
        }
        if x < 0 {
            len += x;
            x = 0;
        }
        if x + len > self.width as i32 {
            len = self.width as i32 - x;
        }
        if len > 0 {
            self.draw_hline(x as u32, y as u32, len as u32, color);
        }
    }

    /// Draw a vertical line (clipped).
    pub fn draw_vline(&mut self, x: u32, y: u32, len: u32, color: u32) {
        let clip = self.clip_stack[self.clip_depth as usize];
        if x < clip.x || x >= clip.x + clip.w {
            return;
        }

        let y1 = max_u32(y, clip.y);
        let y2 = min_u32(y + len, clip.y + clip.h);
        if y2 <= y1 {
            return;
        }

        let pw = self.pitch_words();
        for py in y1..y2 {
            unsafe {
                ptr::write_volatile(
                    self.addr.add((py * pw + x) as usize),
                    color,
                );
            }
        }
    }

    /// Draw an arbitrary line (Bresenham's algorithm).
    pub fn draw_line(&mut self, x0: i32, y0: i32, x1: i32, y1: i32, color: u32) {
        let dx = abs_i32(x1 - x0);
        let dy = -abs_i32(y1 - y0);
        let sx: i32 = if x0 < x1 { 1 } else { -1 };
        let sy: i32 = if y0 < y1 { 1 } else { -1 };
        let mut err = dx + dy;
        let mut x0 = x0;
        let mut y0 = y0;

        loop {
            if x0 >= 0 && y0 >= 0 {
                self.put_pixel(x0 as u32, y0 as u32, color);
            }
            if x0 == x1 && y0 == y1 {
                break;
            }
            let e2 = 2 * err;
            if e2 >= dy {
                err += dy;
                x0 += sx;
            }
            if e2 <= dx {
                err += dx;
                y0 += sy;
            }
        }
    }

    /// Draw a thick line by drawing parallel offset lines.
    pub fn draw_line_thick(
        &mut self,
        x0: i32, y0: i32,
        x1: i32, y1: i32,
        thickness: u32,
        color: u32,
    ) {
        if thickness <= 1 {
            self.draw_line(x0, y0, x1, y1, color);
            return;
        }

        let dx = x1 - x0;
        let dy = y1 - y0;
        let len_sq = dx * dx + dy * dy;

        if len_sq == 0 {
            self.fill_circle(x0, y0, thickness / 2, color);
            return;
        }

        let len = isqrt(len_sq as u32) as i32;
        if len == 0 {
            self.fill_circle(x0, y0, thickness / 2, color);
            return;
        }

        let half_thick = thickness as i32 / 2;

        for offset in -half_thick..=half_thick {
            let ox = (-dy * offset) / len;
            let oy = (dx * offset) / len;
            self.draw_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
        }
    }

    // =========================================================================
    // ADVANCED DRAWING — CIRCLES
    // =========================================================================

    /// Fill a circle using the midpoint algorithm with horizontal spans.
    pub fn fill_circle(&mut self, cx: i32, cy: i32, radius: u32, color: u32) {
        if radius == 0 {
            if cx >= 0 && cy >= 0 && (cx as u32) < self.width && (cy as u32) < self.height {
                self.put_pixel(cx as u32, cy as u32, color);
            }
            return;
        }

        let r = radius as i32;
        let mut x: i32 = 0;
        let mut y: i32 = r;
        let mut d: i32 = 1 - r;

        while x <= y {
            self.draw_hline_safe(cx - x, cy + y, 2 * x + 1, color);
            self.draw_hline_safe(cx - x, cy - y, 2 * x + 1, color);
            self.draw_hline_safe(cx - y, cy + x, 2 * y + 1, color);
            self.draw_hline_safe(cx - y, cy - x, 2 * y + 1, color);

            if d < 0 {
                d += 2 * x + 3;
            } else {
                d += 2 * (x - y) + 5;
                y -= 1;
            }
            x += 1;
        }
    }

    /// Draw a circle outline using the midpoint algorithm.
    pub fn draw_circle(&mut self, cx: i32, cy: i32, radius: u32, color: u32) {
        if radius == 0 {
            if cx >= 0 && cy >= 0 {
                self.put_pixel(cx as u32, cy as u32, color);
            }
            return;
        }

        let mut x: i32 = 0;
        let mut y: i32 = radius as i32;
        let mut d: i32 = 1 - radius as i32;

        while x <= y {
            // 8-way symmetry
            self.put_pixel((cx + x) as u32, (cy + y) as u32, color);
            self.put_pixel((cx - x) as u32, (cy + y) as u32, color);
            self.put_pixel((cx + x) as u32, (cy - y) as u32, color);
            self.put_pixel((cx - x) as u32, (cy - y) as u32, color);
            self.put_pixel((cx + y) as u32, (cy + x) as u32, color);
            self.put_pixel((cx - y) as u32, (cy + x) as u32, color);
            self.put_pixel((cx + y) as u32, (cy - x) as u32, color);
            self.put_pixel((cx - y) as u32, (cy - x) as u32, color);

            if d < 0 {
                d += 2 * x + 3;
            } else {
                d += 2 * (x - y) + 5;
                y -= 1;
            }
            x += 1;
        }
    }

    /// Draw an arc (portion of circle outline, in degrees).
    pub fn draw_arc(
        &mut self,
        cx: u32, cy: u32,
        radius: u32,
        start_deg: u32, end_deg: u32,
        color: u32,
    ) {
        if radius == 0 {
            self.put_pixel(cx, cy, color);
            return;
        }

        let start = start_deg % 360;
        let end = if end_deg <= start_deg { end_deg + 360 } else { end_deg };

        let mut prev_x: i32 = -1;
        let mut prev_y: i32 = -1;

        for deg in start..=end {
            let angle = deg % 360;
            let (sin_val, cos_val) = sin_cos_deg(angle);

            let px = cx as i32 + (cos_val * radius as i32) / 256;
            let py = cy as i32 + (sin_val * radius as i32) / 256;

            if prev_x >= 0 && prev_y >= 0 && (px != prev_x || py != prev_y) {
                self.draw_line(prev_x, prev_y, px, py, color);
            }

            prev_x = px;
            prev_y = py;
        }
    }

    // =========================================================================
    // ADVANCED DRAWING — ROUNDED RECTANGLES
    // =========================================================================

    /// Fill a rounded rectangle.
    pub fn fill_rounded_rect(
        &mut self,
        x: u32, y: u32,
        w: u32, h: u32,
        mut radius: u32,
        color: u32,
    ) {
        if radius == 0 {
            self.fill_rect(x, y, w, h, color);
            return;
        }
        if radius > w / 2 {
            radius = w / 2;
        }
        if radius > h / 2 {
            radius = h / 2;
        }

        // Center rectangles (cross shape)
        self.fill_rect(x + radius, y, w - 2 * radius, h, color);
        self.fill_rect(x, y + radius, radius, h - 2 * radius, color);
        self.fill_rect(x + w - radius, y + radius, radius, h - 2 * radius, color);

        // Corner circles using filled quarter-circle approach
        let r = radius as i32;
        for dy in 0..=r {
            for dx in 0..=r {
                if dx * dx + dy * dy <= r * r {
                    // Top-left
                    self.put_pixel(x + radius - dx as u32, y + radius - dy as u32, color);
                    // Top-right
                    self.put_pixel(x + w - 1 - radius + dx as u32, y + radius - dy as u32, color);
                    // Bottom-left
                    self.put_pixel(x + radius - dx as u32, y + h - 1 - radius + dy as u32, color);
                    // Bottom-right
                    self.put_pixel(x + w - 1 - radius + dx as u32, y + h - 1 - radius + dy as u32, color);
                }
            }
        }
    }

    /// Draw a rounded rectangle outline.
    pub fn draw_rounded_rect(
        &mut self,
        x: u32, y: u32,
        w: u32, h: u32,
        mut radius: u32,
        color: u32,
    ) {
        if radius == 0 {
            self.draw_rect(x, y, w, h, color);
            return;
        }
        if radius > w / 2 {
            radius = w / 2;
        }
        if radius > h / 2 {
            radius = h / 2;
        }

        // Straight edges
        self.draw_hline(x + radius, y, w - 2 * radius, color);
        self.draw_hline(x + radius, y + h - 1, w - 2 * radius, color);
        self.draw_vline(x, y + radius, h - 2 * radius, color);
        self.draw_vline(x + w - 1, y + radius, h - 2 * radius, color);

        // Corner arcs using midpoint circle
        let mut px: i32 = 0;
        let mut py: i32 = radius as i32;
        let mut d: i32 = 1 - radius as i32;

        while px <= py {
            // Top-left
            self.put_pixel(x + radius - px as u32, y + radius - py as u32, color);
            self.put_pixel(x + radius - py as u32, y + radius - px as u32, color);
            // Top-right
            self.put_pixel(x + w - 1 - radius + px as u32, y + radius - py as u32, color);
            self.put_pixel(x + w - 1 - radius + py as u32, y + radius - px as u32, color);
            // Bottom-left
            self.put_pixel(x + radius - px as u32, y + h - 1 - radius + py as u32, color);
            self.put_pixel(x + radius - py as u32, y + h - 1 - radius + px as u32, color);
            // Bottom-right
            self.put_pixel(x + w - 1 - radius + px as u32, y + h - 1 - radius + py as u32, color);
            self.put_pixel(x + w - 1 - radius + py as u32, y + h - 1 - radius + px as u32, color);

            if d < 0 {
                d += 2 * px + 3;
            } else {
                d += 2 * (px - py) + 5;
                py -= 1;
            }
            px += 1;
        }
    }

    // =========================================================================
    // ADVANCED DRAWING — TRIANGLES
    // =========================================================================

    /// Draw a triangle outline.
    pub fn draw_triangle(
        &mut self,
        x0: i32, y0: i32,
        x1: i32, y1: i32,
        x2: i32, y2: i32,
        color: u32,
    ) {
        self.draw_line(x0, y0, x1, y1, color);
        self.draw_line(x1, y1, x2, y2, color);
        self.draw_line(x2, y2, x0, y0, color);
    }

    /// Fill a triangle using scanline decomposition.
    pub fn fill_triangle(
        &mut self,
        mut x0: i32, mut y0: i32,
        mut x1: i32, mut y1: i32,
        mut x2: i32, mut y2: i32,
        color: u32,
    ) {
        // Sort vertices by y coordinate
        if y0 > y1 {
            core::mem::swap(&mut x0, &mut x1);
            core::mem::swap(&mut y0, &mut y1);
        }
        if y1 > y2 {
            core::mem::swap(&mut x1, &mut x2);
            core::mem::swap(&mut y1, &mut y2);
        }
        if y0 > y1 {
            core::mem::swap(&mut x0, &mut x1);
            core::mem::swap(&mut y0, &mut y1);
        }

        if y0 == y2 {
            return; // Degenerate
        }

        if y1 == y2 {
            self.fill_flat_bottom_triangle(x0, y0, x1, y1, x2, color);
        } else if y0 == y1 {
            self.fill_flat_top_triangle(x0, x1, y0, x2, y2, color);
        } else {
            let x3 = x0 + ((y1 - y0) * (x2 - x0)) / (y2 - y0);
            self.fill_flat_bottom_triangle(x0, y0, x1, y1, x3, color);
            self.fill_flat_top_triangle(x1, x3, y1, x2, y2, color);
        }
    }

    fn fill_flat_bottom_triangle(
        &mut self,
        x0: i32, y0: i32,
        x1: i32, y1: i32,
        x2: i32,
        color: u32,
    ) {
        if y1 == y0 {
            return;
        }
        let dy = y1 - y0;
        let dx1 = x1 - x0;
        let dx2 = x2 - x0;

        for y in y0..=y1 {
            let t = y - y0;
            let mut cx1 = x0 + (dx1 * t) / dy;
            let mut cx2 = x0 + (dx2 * t) / dy;
            if cx1 > cx2 {
                core::mem::swap(&mut cx1, &mut cx2);
            }
            if y >= 0 {
                self.draw_hline(max_i32(cx1, 0) as u32, y as u32, (cx2 - cx1 + 1) as u32, color);
            }
        }
    }

    fn fill_flat_top_triangle(
        &mut self,
        x0: i32, x1: i32,
        y0: i32,
        x2: i32, y2: i32,
        color: u32,
    ) {
        if y2 == y0 {
            return;
        }
        let dy = y2 - y0;
        let dx1 = x2 - x0;
        let dx2 = x2 - x1;

        for y in (y0..=y2).rev() {
            let t = y2 - y;
            let mut cx1 = x2 - (dx1 * t) / dy;
            let mut cx2 = x2 - (dx2 * t) / dy;
            if cx1 > cx2 {
                core::mem::swap(&mut cx1, &mut cx2);
            }
            if y >= 0 {
                self.draw_hline(max_i32(cx1, 0) as u32, y as u32, (cx2 - cx1 + 1) as u32, color);
            }
        }
    }

    // =========================================================================
    // ADVANCED DRAWING — GRADIENTS & FADE
    // =========================================================================

    /// Fill a rectangle with a vertical gradient (top to bottom).
    pub fn fill_rect_gradient_v(
        &mut self,
        x: u32, y: u32,
        w: u32, h: u32,
        top_color: u32, bottom_color: u32,
    ) {
        for row in 0..h {
            let t = if h > 1 { ((row * 255) / (h - 1)) as u8 } else { 0 };
            let c = color_lerp(top_color, bottom_color, t);
            self.draw_hline(x, y + row, w, c);
        }
    }

    /// Fill a rectangle with a horizontal gradient (left to right).
    pub fn fill_rect_gradient_h(
        &mut self,
        x: u32, y: u32,
        w: u32, h: u32,
        left_color: u32, right_color: u32,
    ) {
        for col in 0..w {
            let t = if w > 1 { ((col * 255) / (w - 1)) as u8 } else { 0 };
            let c = color_lerp(left_color, right_color, t);
            self.draw_vline(x + col, y, h, c);
        }
    }

    /// Apply a fade effect over the entire framebuffer.
    pub fn fade(&mut self, amount: u8) {
        let fade_color = with_alpha(COLOR_BLACK, amount as u32);
        let w = self.width;
        let h = self.height;
        self.fill_rect_blend(0, 0, w, h, fade_color);
    }

    // =========================================================================
    // TEXT RENDERING
    // =========================================================================

    /// Draw a character at 1× scale with opaque background.
    pub fn draw_char(&mut self, x: u32, y: u32, c: u8, fg: u32, bg: u32) {
        let ch = if c < 32 || c > 126 { b'?' } else { c };
        let glyph = &FONT_8X8[(ch - 32) as usize];

        for row in 0..8u32 {
            let bits = glyph[row as usize];
            for col in 0..8u32 {
                let color = if bits & (0x80 >> col) != 0 { fg } else { bg };
                self.put_pixel(x + col, y + row, color);
            }
        }
    }

    /// Draw a character at 1× scale with transparent background.
    pub fn draw_char_transparent(&mut self, x: u32, y: u32, c: u8, fg: u32) {
        let ch = if c < 32 || c > 126 { b'?' } else { c };
        let glyph = &FONT_8X8[(ch - 32) as usize];

        for row in 0..8u32 {
            let bits = glyph[row as usize];
            for col in 0..8u32 {
                if bits & (0x80 >> col) != 0 {
                    self.put_pixel(x + col, y + row, fg);
                }
            }
        }
    }

    /// Draw a character using the 8×16 "large" font with opaque background.
    pub fn draw_char_large(&mut self, x: u32, y: u32, c: u8, fg: u32, bg: u32) {
        let ch = if c < 32 || c > 126 { b'?' } else { c };
        let glyph = &FONT_8X16[(ch - 32) as usize];

        for row in 0..16u32 {
            let bits = glyph[row as usize];
            for col in 0..8u32 {
                let color = if bits & (0x80 >> col) != 0 { fg } else { bg };
                self.put_pixel(x + col, y + row, color);
            }
        }
    }

    /// Draw a character using the 8×16 "large" font with transparent background.
    pub fn draw_char_large_transparent(&mut self, x: u32, y: u32, c: u8, fg: u32) {
        let ch = if c < 32 || c > 126 { b'?' } else { c };
        let glyph = &FONT_8X16[(ch - 32) as usize];

        for row in 0..16u32 {
            let bits = glyph[row as usize];
            for col in 0..8u32 {
                if bits & (0x80 >> col) != 0 {
                    self.put_pixel(x + col, y + row, fg);
                }
            }
        }
    }

    /// Draw a character at arbitrary scale with opaque background.
    pub fn draw_char_scaled(&mut self, x: u32, y: u32, c: u8, fg: u32, bg: u32, scale: u32) {
        let ch = if c < 32 || c > 126 { b'?' } else { c };
        let glyph = &FONT_8X8[(ch - 32) as usize];

        for row in 0..8u32 {
            let bits = glyph[row as usize];
            for col in 0..8u32 {
                let color = if bits & (0x80 >> col) != 0 { fg } else { bg };
                for sy in 0..scale {
                    for sx in 0..scale {
                        self.put_pixel(x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }

    /// Draw a string at 1× scale with opaque background.
    pub fn draw_string(&mut self, mut x: u32, y: u32, s: &[u8], fg: u32, bg: u32) {
        for &ch in s {
            if ch == 0 {
                break;
            }
            if x + FB_CHAR_WIDTH > self.width {
                break;
            }
            self.draw_char(x, y, ch, fg, bg);
            x += FB_CHAR_WIDTH;
        }
    }

    /// Draw a string at 1× scale with transparent background.
    pub fn draw_string_transparent(&mut self, mut x: u32, y: u32, s: &[u8], fg: u32) {
        for &ch in s {
            if ch == 0 {
                break;
            }
            if x + FB_CHAR_WIDTH > self.width {
                break;
            }
            self.draw_char_transparent(x, y, ch, fg);
            x += FB_CHAR_WIDTH;
        }
    }

    /// Draw a string using the 8×16 "large" font with opaque background.
    pub fn draw_string_large(&mut self, mut x: u32, y: u32, s: &[u8], fg: u32, bg: u32) {
        for &ch in s {
            if ch == 0 {
                break;
            }
            if x + FB_CHAR_WIDTH_LG > self.width {
                break;
            }
            self.draw_char_large(x, y, ch, fg, bg);
            x += FB_CHAR_WIDTH_LG;
        }
    }

    /// Draw a string using the 8×16 "large" font with transparent background.
    pub fn draw_string_large_transparent(&mut self, mut x: u32, y: u32, s: &[u8], fg: u32) {
        for &ch in s {
            if ch == 0 {
                break;
            }
            if x + FB_CHAR_WIDTH_LG > self.width {
                break;
            }
            self.draw_char_large_transparent(x, y, ch, fg);
            x += FB_CHAR_WIDTH_LG;
        }
    }

    /// Draw a string at arbitrary scale with opaque background.
    pub fn draw_string_scaled(
        &mut self,
        mut x: u32,
        y: u32,
        s: &[u8],
        fg: u32,
        bg: u32,
        scale: u32,
    ) {
        let char_w = FB_CHAR_WIDTH * scale;
        for &ch in s {
            if ch == 0 {
                break;
            }
            if x + char_w > self.width {
                break;
            }
            self.draw_char_scaled(x, y, ch, fg, bg, scale);
            x += char_w;
        }
    }

    /// Draw a string centered horizontally at a given y.
    pub fn draw_string_centered(&mut self, y: u32, s: &[u8], fg: u32, bg: u32) {
        let tw = text_width(s);
        let x = if tw < self.width { (self.width - tw) / 2 } else { 0 };
        self.draw_string(x, y, s, fg, bg);
    }

    /// Draw a string centered both horizontally and vertically.
    pub fn draw_string_center(&mut self, s: &[u8], fg: u32, bg: u32) {
        let tw = text_width(s);
        let x = if tw < self.width { (self.width - tw) / 2 } else { 0 };
        let y = (self.height - FB_CHAR_HEIGHT) / 2;
        self.draw_string(x, y, s, fg, bg);
    }

    // =========================================================================
    // BITMAP BLITTING
    // =========================================================================

    /// Blit a bitmap at position (x, y) with opaque blend.
    pub fn blit_bitmap(&mut self, x: i32, y: i32, bitmap: &Bitmap) {
        self.blit_bitmap_blend(x, y, bitmap, BlendMode::Opaque);
    }

    /// Blit a bitmap at position (x, y) with alpha blending.
    pub fn blit_bitmap_alpha(&mut self, x: i32, y: i32, bitmap: &Bitmap) {
        self.blit_bitmap_blend(x, y, bitmap, BlendMode::Alpha);
    }

    /// Blit a bitmap with the specified blend mode.
    pub fn blit_bitmap_blend(&mut self, x: i32, y: i32, bitmap: &Bitmap, blend: BlendMode) {
        let clip = self.clip_stack[self.clip_depth as usize];

        let src_x = if x < clip.x as i32 { (clip.x as i32 - x) as u32 } else { 0 };
        let src_y = if y < clip.y as i32 { (clip.y as i32 - y) as u32 } else { 0 };

        let dst_x = max_i32(x, clip.x as i32) as u32;
        let dst_y = max_i32(y, clip.y as i32) as u32;

        let mut w = bitmap.width - src_x;
        if dst_x + w > clip.x + clip.w {
            w = clip.x + clip.w - dst_x;
        }
        if dst_x + w > self.width {
            w = self.width - dst_x;
        }

        let mut h = bitmap.height - src_y;
        if dst_y + h > clip.y + clip.h {
            h = clip.y + clip.h - dst_y;
        }
        if dst_y + h > self.height {
            h = self.height - dst_y;
        }

        if w == 0 || h == 0 {
            return;
        }

        let pw = self.pitch_words();

        for row in 0..h {
            let src_row = ((src_y + row) * bitmap.width) as usize;
            let dst_row_base = ((dst_y + row) * pw + dst_x) as usize;

            for col in 0..w {
                let src_pixel = bitmap.data[src_row + (src_x + col) as usize];

                unsafe {
                    let dst_ptr = self.addr.add(dst_row_base + col as usize);
                    match blend {
                        BlendMode::Opaque => {
                            ptr::write_volatile(dst_ptr, src_pixel);
                        }
                        BlendMode::Alpha => {
                            let dst_pixel = ptr::read_volatile(dst_ptr);
                            ptr::write_volatile(dst_ptr, blend_alpha(src_pixel, dst_pixel));
                        }
                        BlendMode::Additive => {
                            let dst_pixel = ptr::read_volatile(dst_ptr);
                            ptr::write_volatile(dst_ptr, blend_additive(src_pixel, dst_pixel));
                        }
                        BlendMode::Multiply => {
                            let dst_pixel = ptr::read_volatile(dst_ptr);
                            ptr::write_volatile(dst_ptr, blend_multiply(src_pixel, dst_pixel));
                        }
                    }
                }
            }
        }

        self.mark_dirty(dst_x, dst_y, w, h);
    }

    /// Blit a bitmap with integer scaling.
    pub fn blit_bitmap_scaled(
        &mut self,
        x: i32, y: i32,
        bitmap: &Bitmap,
        scale_x: u32, scale_y: u32,
    ) {
        if scale_x == 0 || scale_y == 0 {
            return;
        }
        for row in 0..bitmap.height {
            for col in 0..bitmap.width {
                let color = bitmap.data[(row * bitmap.width + col) as usize];
                let bx = x + (col * scale_x) as i32;
                let by = y + (row * scale_y) as i32;
                for sy in 0..scale_y {
                    for sx in 0..scale_x {
                        let px = bx + sx as i32;
                        let py = by + sy as i32;
                        if px >= 0 && py >= 0 {
                            self.put_pixel(px as u32, py as u32, color);
                        }
                    }
                }
            }
        }
    }

    /// Blit a sub-region of a bitmap.
    pub fn blit_bitmap_region(
        &mut self,
        bitmap: &Bitmap,
        src_x: u32, src_y: u32,
        src_w: u32, src_h: u32,
        dst_x: i32, dst_y: i32,
    ) {
        for row in 0..src_h {
            for col in 0..src_w {
                let color = bitmap.data[((src_y + row) * bitmap.width + src_x + col) as usize];
                let px = dst_x + col as i32;
                let py = dst_y + row as i32;
                if px >= 0 && py >= 0 {
                    self.put_pixel(px as u32, py as u32, color);
                }
            }
        }
    }

    // =========================================================================
    // SCREEN OPERATIONS
    // =========================================================================

    /// Copy a rectangle from one position to another within the framebuffer.
    ///
    /// Handles overlapping regions correctly by choosing copy direction.
    pub fn copy_rect(
        &mut self,
        src_x: u32, src_y: u32,
        dst_x: u32, dst_y: u32,
        w: u32, h: u32,
    ) {
        let pw = self.pitch_words();

        if src_y < dst_y || (src_y == dst_y && src_x < dst_x) {
            // Copy bottom-to-top, right-to-left
            for row in (0..h).rev() {
                for col in (0..w).rev() {
                    let src_idx = ((src_y + row) * pw + src_x + col) as usize;
                    let dst_idx = ((dst_y + row) * pw + dst_x + col) as usize;
                    unsafe {
                        let pixel = ptr::read_volatile(self.addr.add(src_idx));
                        ptr::write_volatile(self.addr.add(dst_idx), pixel);
                    }
                }
            }
        } else {
            // Copy top-to-bottom, left-to-right
            for row in 0..h {
                for col in 0..w {
                    let src_idx = ((src_y + row) * pw + src_x + col) as usize;
                    let dst_idx = ((dst_y + row) * pw + dst_x + col) as usize;
                    unsafe {
                        let pixel = ptr::read_volatile(self.addr.add(src_idx));
                        ptr::write_volatile(self.addr.add(dst_idx), pixel);
                    }
                }
            }
        }

        self.mark_dirty(dst_x, dst_y, w, h);
    }

    /// Scroll vertically by `pixels` (positive = down, negative = up).
    pub fn scroll_v(&mut self, pixels: i32, fill_color: u32) {
        if pixels == 0 {
            return;
        }

        let abs_pixels = abs_i32(pixels) as u32;
        if abs_pixels >= self.height {
            self.clear(fill_color);
            return;
        }

        let w = self.width;
        let h = self.height;

        if pixels > 0 {
            // Scroll down
            self.copy_rect(0, 0, 0, abs_pixels, w, h - abs_pixels);
            self.fill_rect(0, 0, w, abs_pixels, fill_color);
        } else {
            // Scroll up
            self.copy_rect(0, abs_pixels, 0, 0, w, h - abs_pixels);
            self.fill_rect(0, h - abs_pixels, w, abs_pixels, fill_color);
        }
    }

    /// Scroll horizontally by `pixels` (positive = right, negative = left).
    pub fn scroll_h(&mut self, pixels: i32, fill_color: u32) {
        if pixels == 0 {
            return;
        }

        let abs_pixels = abs_i32(pixels) as u32;
        if abs_pixels >= self.width {
            self.clear(fill_color);
            return;
        }

        let w = self.width;
        let h = self.height;

        if pixels > 0 {
            // Scroll right
            self.copy_rect(0, 0, abs_pixels, 0, w - abs_pixels, h);
            self.fill_rect(0, 0, abs_pixels, h, fill_color);
        } else {
            // Scroll left
            self.copy_rect(abs_pixels, 0, 0, 0, w - abs_pixels, h);
            self.fill_rect(w - abs_pixels, 0, abs_pixels, h, fill_color);
        }
    }

    // =========================================================================
    // GAMEBOY SCREEN BLITTING
    // =========================================================================

    /// Blit a DMG GameBoy screen (2-bit palette indices, 160×144) with default palette.
    pub fn blit_gb_screen_dmg(&mut self, pal_data: &[u8]) {
        self.blit_gb_screen_dmg_palette(pal_data, &GB_PALETTE);
    }

    /// Blit a DMG GameBoy screen with custom palette.
    pub fn blit_gb_screen_dmg_palette(&mut self, pal_data: &[u8], palette: &[u32; 4]) {
        let pw = self.pitch_words();
        let mut scanline = [0u32; (GB_WIDTH * GB_SCALE) as usize];

        for y in 0..GB_HEIGHT {
            let src_row = (y * GB_WIDTH) as usize;

            for x in 0..GB_WIDTH {
                let pal_idx = pal_data[src_row + x as usize];
                let color = if (pal_idx as usize) < 4 {
                    palette[pal_idx as usize]
                } else {
                    COLOR_BLACK
                };
                scanline[(x * 2) as usize] = color;
                scanline[(x * 2 + 1) as usize] = color;
            }

            let dst_y = GB_OFFSET_Y + y * GB_SCALE;
            for scale_row in 0..GB_SCALE {
                let row_base = ((dst_y + scale_row) * pw + GB_OFFSET_X) as usize;
                for i in 0..GB_SCALED_W as usize {
                    unsafe {
                        ptr::write_volatile(self.addr.add(row_base + i), scanline[i]);
                    }
                }
            }
        }
    }

    /// Blit a GBC GameBoy Color screen (RGB888 data, 160×144).
    pub fn blit_gb_screen_gbc(&mut self, rgb_data: &[u8]) {
        let pw = self.pitch_words();
        let mut scanline = [0u32; (GB_WIDTH * GB_SCALE) as usize];

        for y in 0..GB_HEIGHT {
            let src_row = (y * GB_WIDTH * 3) as usize;

            for x in 0..GB_WIDTH {
                let idx = src_row + (x * 3) as usize;
                let color = 0xFF000000
                    | ((rgb_data[idx] as u32) << 16)
                    | ((rgb_data[idx + 1] as u32) << 8)
                    | (rgb_data[idx + 2] as u32);
                scanline[(x * 2) as usize] = color;
                scanline[(x * 2 + 1) as usize] = color;
            }

            let dst_y = GB_OFFSET_Y + y * GB_SCALE;
            for scale_row in 0..GB_SCALE {
                let row_base = ((dst_y + scale_row) * pw + GB_OFFSET_X) as usize;
                for i in 0..GB_SCALED_W as usize {
                    unsafe {
                        ptr::write_volatile(self.addr.add(row_base + i), scanline[i]);
                    }
                }
            }
        }
    }

    /// Draw a border around the GameBoy screen area.
    pub fn draw_gb_border(&mut self, color: u32) {
        let border: u32 = 4;
        let x = GB_OFFSET_X - border;
        let y = GB_OFFSET_Y - border;
        let w = GB_SCALED_W + border * 2;
        let h = GB_SCALED_H + border * 2;

        self.fill_rect(x, y, w, border, color);
        self.fill_rect(x, y + h - border, w, border, color);
        self.fill_rect(x, y, border, h, color);
        self.fill_rect(x + w - border, y, border, h, color);
    }
}

// =============================================================================
// FREE-STANDING TEXT MEASUREMENT FUNCTIONS
// =============================================================================

/// Measure the pixel width of a string at 1× scale.
pub fn text_width(s: &[u8]) -> u32 {
    let mut len = 0u32;
    for &ch in s {
        if ch == 0 {
            break;
        }
        len += 1;
    }
    len * FB_CHAR_WIDTH
}

/// Measure the pixel width of a string at 2× (large) scale.
pub fn text_width_large(s: &[u8]) -> u32 {
    let mut len = 0u32;
    for &ch in s {
        if ch == 0 {
            break;
        }
        len += 1;
    }
    len * FB_CHAR_WIDTH_LG
}

/// Measure the pixel width of a string at the specified scale.
pub fn measure_string(s: &[u8], large: bool) -> u32 {
    let mut len = 0u32;
    for &ch in s {
        if ch == 0 {
            break;
        }
        len += 1;
    }
    len * if large { FB_CHAR_WIDTH_LG } else { FB_CHAR_WIDTH }
}