//! Canvas — the drawing surface abstraction for the UI layer.
//!
//! A [`Canvas`] wraps a [`Framebuffer`](drivers::framebuffer::Framebuffer)
//! and adds higher-level drawing operations (clipped rectangles, themed
//! text, etc.) that widgets use to render themselves.
//!
//! In the C implementation this was a vtable (`ui_canvas_t` with function
//! pointers).  In Rust it is a concrete struct that owns a reference to
//! the underlying framebuffer — no dynamic dispatch needed because there
//! is only one kind of canvas in Tutorial-OS.

use crate::core::types::{Color, Rect};
use drivers::framebuffer::Framebuffer;

/// A clipped drawing surface backed by a [`Framebuffer`].
///
/// The `clip` rectangle restricts all drawing operations to a sub-region
/// of the screen.  Widgets receive a `Canvas` with the clip set to their
/// layout bounds, so they can draw with local coordinates without worrying
/// about overwriting neighbours.
pub struct Canvas<'fb> {
    fb: &'fb mut Framebuffer,
    /// Active clip rectangle in absolute screen coordinates.
    clip: Rect,
    /// Translation offset (widgets draw relative to their origin).
    origin_x: i32,
    origin_y: i32,
}

impl<'fb> Canvas<'fb> {
    /// Create a canvas covering the full framebuffer.
    pub fn new(fb: &'fb mut Framebuffer) -> Self {
        let clip = Rect::new(0, 0, fb.width(), fb.height());
        Self {
            fb,
            clip,
            origin_x: 0,
            origin_y: 0,
        }
    }

    /// Create a sub-canvas clipped and translated to `bounds`.
    ///
    /// Drawing at `(0, 0)` in the sub-canvas maps to `(bounds.x, bounds.y)`
    /// on screen, and nothing outside `bounds` is touched.
    pub fn sub_canvas(&mut self, bounds: Rect) -> Canvas<'_> {
        let abs_bounds = Rect::new(
            bounds.x + self.origin_x,
            bounds.y + self.origin_y,
            bounds.width,
            bounds.height,
        );
        let clip = self.clip.intersect(&abs_bounds).unwrap_or(Rect::new(0, 0, 0, 0));
        Canvas {
            fb: self.fb,
            clip,
            origin_x: abs_bounds.x,
            origin_y: abs_bounds.y,
        }
    }

    /// The clip rectangle in absolute screen coordinates.
    pub fn clip(&self) -> Rect {
        self.clip
    }

    // ---- Drawing ----

    /// Fill the entire clip region with `colour`.
    pub fn clear(&mut self, colour: Color) {
        let r = Rect::new(0, 0, self.clip.width, self.clip.height);
        self.fill_rect(r, colour);
    }

    /// Fill a rectangle (in local coordinates) with `colour`, clipped.
    pub fn fill_rect(&mut self, rect: Rect, colour: Color) {
        let abs = Rect::new(
            rect.x + self.origin_x,
            rect.y + self.origin_y,
            rect.width,
            rect.height,
        );
        if let Some(clipped) = self.clip.intersect(&abs) {
            self.fb.fill_rect(
                clipped.x as u32,
                clipped.y as u32,
                clipped.width,
                clipped.height,
                colour.raw(),
            );
        }
    }

    /// Draw an unfilled rectangle (1px border, in local coordinates, clipped).
    pub fn draw_rect(&mut self, rect: Rect, colour: Color) {
        let c = colour.raw();
        let abs = Rect::new(
            rect.x + self.origin_x,
            rect.y + self.origin_y,
            rect.width,
            rect.height,
        );
        // Top
        self.clip_hline(abs.x, abs.y, abs.width, c);
        // Bottom
        self.clip_hline(abs.x, abs.bottom() - 1, abs.width, c);
        // Left
        self.clip_vline(abs.x, abs.y, abs.height, c);
        // Right
        self.clip_vline(abs.right() - 1, abs.y, abs.height, c);
    }

    /// Draw a string at local `(x, y)` using the built-in 8×16 font.
    pub fn draw_text(&mut self, x: i32, y: i32, text: &str, fg: Color, bg: Color) {
        let abs_x = x + self.origin_x;
        let abs_y = y + self.origin_y;

        // Early reject if entirely outside clip.
        if abs_y + 16 <= self.clip.y || abs_y >= self.clip.bottom() {
            return;
        }

        let mut cx = abs_x;
        for &b in text.as_bytes() {
            if cx + 8 > self.clip.right() {
                break;
            }
            if cx + 8 > self.clip.x {
                self.fb.draw_char(cx as u32, abs_y as u32, b, fg.raw(), bg.raw());
            }
            cx += 8;
        }
    }

    // ---- Internal helpers ----

    fn clip_hline(&mut self, x: i32, y: i32, w: u32, colour: u32) {
        let line = Rect::new(x, y, w, 1);
        if let Some(c) = self.clip.intersect(&line) {
            self.fb.fill_rect(c.x as u32, c.y as u32, c.width, 1, colour);
        }
    }

    fn clip_vline(&mut self, x: i32, y: i32, h: u32, colour: u32) {
        let line = Rect::new(x, y, 1, h);
        if let Some(c) = self.clip.intersect(&line) {
            self.fb.fill_rect(c.x as u32, c.y as u32, 1, c.height, colour);
        }
    }
}
