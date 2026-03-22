//! Core UI type definitions — geometry, colour, and layout primitives.

/// A 2D point in pixel coordinates.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

impl Point {
    pub const fn new(x: i32, y: i32) -> Self { Self { x, y } }
    pub const ZERO: Self = Self { x: 0, y: 0 };
}

/// A 2D size in pixels.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Size {
    pub width: u32,
    pub height: u32,
}

impl Size {
    pub const fn new(width: u32, height: u32) -> Self { Self { width, height } }
    pub const ZERO: Self = Self { width: 0, height: 0 };
}

/// An axis-aligned rectangle in pixel coordinates.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

impl Rect {
    pub const fn new(x: i32, y: i32, width: u32, height: u32) -> Self {
        Self { x, y, width, height }
    }

    pub const fn from_point_size(origin: Point, size: Size) -> Self {
        Self { x: origin.x, y: origin.y, width: size.width, height: size.height }
    }

    pub const fn origin(&self) -> Point { Point { x: self.x, y: self.y } }
    pub const fn size(&self) -> Size { Size { width: self.width, height: self.height } }

    pub const fn right(&self) -> i32 { self.x + self.width as i32 }
    pub const fn bottom(&self) -> i32 { self.y + self.height as i32 }

    pub const fn contains(&self, p: Point) -> bool {
        p.x >= self.x
            && p.y >= self.y
            && p.x < self.right()
            && p.y < self.bottom()
    }

    /// Intersection of two rectangles. Returns `None` if they don't overlap.
    pub fn intersect(&self, other: &Rect) -> Option<Rect> {
        let x0 = self.x.max(other.x);
        let y0 = self.y.max(other.y);
        let x1 = self.right().min(other.right());
        let y1 = self.bottom().min(other.bottom());
        if x1 > x0 && y1 > y0 {
            Some(Rect::new(x0, y0, (x1 - x0) as u32, (y1 - y0) as u32))
        } else {
            None
        }
    }

    /// Shrink the rectangle inward by `insets`.
    pub const fn inset(&self, insets: Insets) -> Rect {
        Rect {
            x: self.x + insets.left as i32,
            y: self.y + insets.top as i32,
            width: self.width.saturating_sub(insets.left + insets.right),
            height: self.height.saturating_sub(insets.top + insets.bottom),
        }
    }
}

/// Padding / margin insets.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Insets {
    pub top: u32,
    pub right: u32,
    pub bottom: u32,
    pub left: u32,
}

impl Insets {
    pub const fn uniform(v: u32) -> Self {
        Self { top: v, right: v, bottom: v, left: v }
    }

    pub const fn symmetric(h: u32, v: u32) -> Self {
        Self { top: v, right: h, bottom: v, left: h }
    }

    pub const ZERO: Self = Self { top: 0, right: 0, bottom: 0, left: 0 };
}

/// 32-bit ARGB colour value.
///
/// This is a thin wrapper around `u32` that provides named constructors.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Color(pub u32);

impl Color {
    pub const fn argb(a: u8, r: u8, g: u8, b: u8) -> Self {
        Self((a as u32) << 24 | (r as u32) << 16 | (g as u32) << 8 | (b as u32))
    }

    pub const fn rgb(r: u8, g: u8, b: u8) -> Self {
        Self::argb(0xFF, r, g, b)
    }

    pub const fn raw(self) -> u32 { self.0 }

    // Common colours
    pub const BLACK: Self       = Self::rgb(0, 0, 0);
    pub const WHITE: Self       = Self::rgb(255, 255, 255);
    pub const RED: Self         = Self::rgb(255, 0, 0);
    pub const GREEN: Self       = Self::rgb(0, 255, 0);
    pub const BLUE: Self        = Self::rgb(0, 0, 255);
    pub const TRANSPARENT: Self = Self(0x00000000);
}
