/*
 * ui/core/ui_types.rs — Core UI Type Definitions
 * ================================================
 *
 * Rust port of ui/core/ui_types.h
 *
 * This module defines the fundamental types used throughout the UI system:
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
 * COORDINATE SYSTEM:
 * ------------------
 *   - Origin (0,0) is top-left
 *   - X increases rightward
 *   - Y increases downward
 *   - Coordinates are signed (i32) to allow off-screen positioning
 *   - Sizes are unsigned (u32) since negative sizes make no sense
 *
 * KEY DIFFERENCES FROM C:
 * -----------------------
 *   - Color is a newtype wrapper around u32 for type safety
 *   - Rect methods are inherent impls instead of inline functions
 *   - Enums use Rust's exhaustive matching
 *   - No NULL — Option<T> where needed
 */

#![allow(dead_code)]

// =============================================================================
// COLOR TYPE AND HELPERS
// =============================================================================

/// Color type — ARGB8888 format.
///
/// We use a type alias (matching C's typedef) for simplicity.
/// In the framebuffer, pixel layout is: `[Alpha:8][Red:8][Green:8][Blue:8]`.
pub type Color = u32;

/// Create color from RGB (fully opaque).
#[inline(always)]
pub const fn rgb(r: u8, g: u8, b: u8) -> Color {
    0xFF00_0000 | ((r as u32) << 16) | ((g as u32) << 8) | (b as u32)
}

/// Create color from ARGB.
#[inline(always)]
pub const fn argb(a: u8, r: u8, g: u8, b: u8) -> Color {
    ((a as u32) << 24) | ((r as u32) << 16) | ((g as u32) << 8) | (b as u32)
}

/// Extract alpha component.
#[inline(always)]
pub const fn color_alpha(c: Color) -> u8 {
    ((c >> 24) & 0xFF) as u8
}

/// Extract red component.
#[inline(always)]
pub const fn color_red(c: Color) -> u8 {
    ((c >> 16) & 0xFF) as u8
}

/// Extract green component.
#[inline(always)]
pub const fn color_green(c: Color) -> u8 {
    ((c >> 8) & 0xFF) as u8
}

/// Extract blue component.
#[inline(always)]
pub const fn color_blue(c: Color) -> u8 {
    (c & 0xFF) as u8
}

/// Set alpha, preserving RGB.
#[inline(always)]
pub const fn color_with_alpha(c: Color, a: u8) -> Color {
    (c & 0x00FF_FFFF) | ((a as u32) << 24)
}

/// Linear interpolation between two colors.
///
/// `t` is the blend factor: 0 = `a`, 255 = `b`.
#[inline]
pub const fn color_lerp(a: Color, b: Color, t: u8) -> Color {
    let inv_t = 255u32 - t as u32;
    let t32 = t as u32;

    let ra = color_red(a) as u32;
    let ga = color_green(a) as u32;
    let ba = color_blue(a) as u32;
    let aa = color_alpha(a) as u32;

    let rb = color_red(b) as u32;
    let gb = color_green(b) as u32;
    let bb = color_blue(b) as u32;
    let ab = color_alpha(b) as u32;

    let r = ((ra * inv_t + rb * t32) / 255) as u8;
    let g = ((ga * inv_t + gb * t32) / 255) as u8;
    let bl = ((ba * inv_t + bb * t32) / 255) as u8;
    let al = ((aa * inv_t + ab * t32) / 255) as u8;

    argb(al, r, g, bl)
}

/// Alpha blend: source over destination.
///
/// Standard "source over" compositing operation.
#[inline]
pub const fn color_blend_over(dst: Color, src: Color) -> Color {
    let sa = color_alpha(src) as u32;
    if sa == 255 { return src; }
    if sa == 0 { return dst; }

    let inv_sa = 255 - sa;

    let r = ((color_red(src) as u32 * sa + color_red(dst) as u32 * inv_sa) / 255) as u8;
    let g = ((color_green(src) as u32 * sa + color_green(dst) as u32 * inv_sa) / 255) as u8;
    let b = ((color_blue(src) as u32 * sa + color_blue(dst) as u32 * inv_sa) / 255) as u8;
    let a = (sa + (color_alpha(dst) as u32 * inv_sa) / 255) as u8;

    argb(a, r, g, b)
}

/// Darken a color by a factor.
///
/// `factor`: 0 = black, 255 = unchanged.
#[inline]
pub const fn color_darken(c: Color, factor: u8) -> Color {
    let f = factor as u32;
    let r = ((color_red(c) as u32 * f) / 255) as u8;
    let g = ((color_green(c) as u32 * f) / 255) as u8;
    let b = ((color_blue(c) as u32 * f) / 255) as u8;
    argb(color_alpha(c), r, g, b)
}

/// Lighten a color by a factor.
///
/// `factor`: 0 = unchanged, 255 = white.
#[inline]
pub const fn color_lighten(c: Color, factor: u8) -> Color {
    let f = factor as u32;
    let r = (color_red(c) as u32 + ((255 - color_red(c) as u32) * f) / 255) as u8;
    let g = (color_green(c) as u32 + ((255 - color_green(c) as u32) * f) / 255) as u8;
    let b = (color_blue(c) as u32 + ((255 - color_blue(c) as u32) * f) / 255) as u8;
    argb(color_alpha(c), r, g, b)
}


// =============================================================================
// RECTANGLE TYPE
// =============================================================================

/// Rectangle — position and size.
///
/// The fundamental layout primitive. Position uses signed integers to
/// allow off-screen values (which get clipped during rendering).
#[derive(Debug, Clone, Copy)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: u32,
    pub h: u32,
}

/// Zero rectangle at origin.
pub const RECT_ZERO: Rect = Rect { x: 0, y: 0, w: 0, h: 0 };

impl Rect {
    /// Create a rectangle.
    #[inline(always)]
    pub const fn new(x: i32, y: i32, w: u32, h: u32) -> Self {
        Self { x, y, w, h }
    }

    /// Right edge (x + width).
    #[inline(always)]
    pub const fn right(self) -> i32 {
        self.x + self.w as i32
    }

    /// Bottom edge (y + height).
    #[inline(always)]
    pub const fn bottom(self) -> i32 {
        self.y + self.h as i32
    }

    /// Center X coordinate.
    #[inline(always)]
    pub const fn center_x(self) -> i32 {
        self.x + (self.w / 2) as i32
    }

    /// Center Y coordinate.
    #[inline(always)]
    pub const fn center_y(self) -> i32 {
        self.y + (self.h / 2) as i32
    }

    /// Check if rectangle has zero area.
    #[inline(always)]
    pub const fn is_empty(self) -> bool {
        self.w == 0 || self.h == 0
    }

    /// Inset rectangle by uniform amount.
    #[inline]
    pub const fn inset(self, amount: u32) -> Self {
        let doubled = amount * 2;
        Self {
            x: self.x + amount as i32,
            y: self.y + amount as i32,
            w: if self.w > doubled { self.w - doubled } else { 0 },
            h: if self.h > doubled { self.h - doubled } else { 0 },
        }
    }

    /// Offset rectangle by dx, dy.
    #[inline(always)]
    pub const fn offset(self, dx: i32, dy: i32) -> Self {
        Self {
            x: self.x + dx,
            y: self.y + dy,
            w: self.w,
            h: self.h,
        }
    }

    /// Expand rectangle on all sides.
    #[inline]
    pub const fn expand(self, amount: u32) -> Self {
        Self {
            x: self.x - amount as i32,
            y: self.y - amount as i32,
            w: self.w + amount * 2,
            h: self.h + amount * 2,
        }
    }

    /// Center a smaller rectangle inside a larger one.
    #[inline]
    pub const fn center_in(outer: Rect, inner_w: u32, inner_h: u32) -> Self {
        Self {
            x: outer.x + (outer.w.wrapping_sub(inner_w) / 2) as i32,
            y: outer.y + (outer.h.wrapping_sub(inner_h) / 2) as i32,
            w: inner_w,
            h: inner_h,
        }
    }

    /// Split rectangle horizontally at height. Returns the top portion.
    #[inline]
    pub const fn split_h(self, height: u32) -> Self {
        Self {
            x: self.x,
            y: self.y,
            w: self.w,
            h: if height < self.h { height } else { self.h },
        }
    }

    /// Split rectangle vertically at width. Returns the left portion.
    #[inline]
    pub const fn split_v(self, width: u32) -> Self {
        Self {
            x: self.x,
            y: self.y,
            w: if width < self.w { width } else { self.w },
            h: self.h,
        }
    }

    /// Check if a point is inside the rectangle.
    #[inline]
    pub const fn contains(self, px: i32, py: i32) -> bool {
        px >= self.x && px < self.right() && py >= self.y && py < self.bottom()
    }

    /// Compute intersection of two rectangles.
    #[inline]
    pub fn intersect(a: Rect, b: Rect) -> Self {
        let x1 = if a.x > b.x { a.x } else { b.x };
        let y1 = if a.y > b.y { a.y } else { b.y };
        let x2 = if a.right() < b.right() { a.right() } else { b.right() };
        let y2 = if a.bottom() < b.bottom() { a.bottom() } else { b.bottom() };

        if x2 <= x1 || y2 <= y1 {
            return RECT_ZERO;
        }

        Self {
            x: x1,
            y: y1,
            w: (x2 - x1) as u32,
            h: (y2 - y1) as u32,
        }
    }
}


// =============================================================================
// ALIGNMENT ENUMS
// =============================================================================

/// Horizontal alignment.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum HAlign {
    Left,
    Center,
    Right,
}

/// Vertical alignment.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum VAlign {
    Top,
    Middle,
    Bottom,
}

/// Font size selection.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum FontSize {
    /// e.g., 4×6 pixels
    Small,
    /// e.g., 8×8 pixels
    Normal,
    /// e.g., 8×16 pixels
    Large,
}

/// Blend mode for drawing.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum BlendMode {
    /// No blending, replace destination.
    Opaque,
    /// Standard alpha blending.
    Alpha,
    /// Add source to destination.
    Additive,
    /// Multiply source with destination.
    Multiply,
}


// =============================================================================
// FONT METRICS
// =============================================================================

/// Font metrics for layout calculations.
#[derive(Debug, Clone, Copy)]
pub struct FontMetrics {
    /// Width of one character (monospace).
    pub char_width: u32,
    /// Height of one character.
    pub char_height: u32,
    /// Height including line spacing.
    pub line_height: u32,
}

/// 4×6 font metrics preset.
pub const FONT_4X6: FontMetrics = FontMetrics { char_width: 4, char_height: 6, line_height: 7 };

/// 8×8 font metrics preset.
pub const FONT_8X8: FontMetrics = FontMetrics { char_width: 8, char_height: 8, line_height: 10 };

/// 8×16 font metrics preset.
pub const FONT_8X16: FontMetrics = FontMetrics { char_width: 8, char_height: 16, line_height: 18 };
