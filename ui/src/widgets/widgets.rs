//! Widget implementations for the Hardware Inspector UI.
//!
//! Every visual element on screen is a [`Widget`].  Widgets are composed
//! hierarchically — a `Panel` contains child widgets, each laid out within
//! the panel's content area.
//!
//! # Trait vs Enum
//!
//! The C implementation uses a vtable struct with function pointers
//! (`ui_widget_t`).  In Rust we define a [`Widget`] trait that each
//! concrete widget implements.  For the fixed set of widgets in
//! Tutorial-OS this would also work as an enum, but the trait approach
//! mirrors the C architecture more closely and allows SoC crates to
//! add custom widgets without modifying this crate.

use crate::core::canvas::Canvas;
use crate::core::types::{Color, Rect, Insets};
use crate::themes::Theme;

// ============================================================================
// Widget Trait
// ============================================================================

/// A drawable UI element with a known size.
pub trait Widget {
    /// Render this widget into the given canvas using `theme` colours.
    fn draw(&self, canvas: &mut Canvas<'_>, theme: &Theme);

    /// The preferred size of this widget in pixels.
    fn preferred_size(&self) -> (u32, u32);
}

// ============================================================================
// Label
// ============================================================================

/// A single line of text.
pub struct Label<'a> {
    pub text: &'a str,
    pub colour: Option<Color>,
}

impl<'a> Label<'a> {
    pub const fn new(text: &'a str) -> Self {
        Self { text, colour: None }
    }

    pub const fn with_colour(text: &'a str, colour: Color) -> Self {
        Self { text, colour: Some(colour) }
    }
}

impl Widget for Label<'_> {
    fn draw(&self, canvas: &mut Canvas<'_>, theme: &Theme) {
        let fg = self.colour.unwrap_or(theme.fg);
        canvas.draw_text(0, 0, self.text, fg, theme.bg);
    }

    fn preferred_size(&self) -> (u32, u32) {
        (self.text.len() as u32 * 8, 16)
    }
}

// ============================================================================
// Panel
// ============================================================================

/// A titled container with a border and padding.
pub struct Panel<'a> {
    pub title: &'a str,
    pub padding: Insets,
}

impl<'a> Panel<'a> {
    pub const fn new(title: &'a str) -> Self {
        Self {
            title,
            padding: Insets::uniform(8),
        }
    }

    /// Draw the panel chrome (border + title bar) and return the inner
    /// content [`Rect`] in local coordinates.
    pub fn draw_chrome(&self, canvas: &mut Canvas<'_>, bounds: Rect, theme: &Theme) -> Rect {
        // Background
        canvas.fill_rect(bounds, theme.panel_bg);

        // Border
        canvas.draw_rect(bounds, theme.border);

        // Title bar
        let title_h = 20u32;
        let title_rect = Rect::new(bounds.x, bounds.y, bounds.width, title_h);
        canvas.fill_rect(title_rect, theme.header_bg);
        canvas.draw_text(
            bounds.x + 6,
            bounds.y + 2,
            self.title,
            theme.header_fg,
            theme.header_bg,
        );

        // Content area
        Rect::new(
            bounds.x + self.padding.left as i32,
            bounds.y + title_h as i32 + self.padding.top as i32,
            bounds.width.saturating_sub(self.padding.left + self.padding.right),
            bounds.height.saturating_sub(title_h + self.padding.top + self.padding.bottom),
        )
    }
}

// ============================================================================
// InfoRow
// ============================================================================

/// A key-value information row (e.g., `"CPU Temp:  48°C"`).
pub struct InfoRow<'a> {
    pub label: &'a str,
    pub value: &'a str,
    pub value_colour: Option<Color>,
}

impl<'a> InfoRow<'a> {
    pub const fn new(label: &'a str, value: &'a str) -> Self {
        Self { label, value, value_colour: None }
    }

    pub const fn with_colour(label: &'a str, value: &'a str, colour: Color) -> Self {
        Self { label, value, value_colour: Some(colour) }
    }
}

impl Widget for InfoRow<'_> {
    fn draw(&self, canvas: &mut Canvas<'_>, theme: &Theme) {
        let label_width = self.label.len() as i32 * 8;
        canvas.draw_text(0, 0, self.label, theme.muted, theme.panel_bg);
        let vc = self.value_colour.unwrap_or(theme.fg);
        canvas.draw_text(label_width + 8, 0, self.value, vc, theme.panel_bg);
    }

    fn preferred_size(&self) -> (u32, u32) {
        let total_chars = self.label.len() + 1 + self.value.len();
        (total_chars as u32 * 8, 16)
    }
}

// ============================================================================
// ProgressBar
// ============================================================================

/// A horizontal progress bar.
pub struct ProgressBar {
    /// Progress as a fraction in `[0.0, 1.0]`.
    ///
    /// Stored as fixed-point `u32` out of 1000 to avoid floating point
    /// in a `no_std` environment.
    pub value_per_mille: u32,
    pub width: u32,
    pub height: u32,
}

impl ProgressBar {
    /// Create a progress bar.  `value` is clamped to `0..=1000`.
    pub const fn new(value_per_mille: u32, width: u32, height: u32) -> Self {
        let v = if value_per_mille > 1000 { 1000 } else { value_per_mille };
        Self { value_per_mille: v, width, height }
    }
}

impl Widget for ProgressBar {
    fn draw(&self, canvas: &mut Canvas<'_>, theme: &Theme) {
        let bounds = Rect::new(0, 0, self.width, self.height);

        // Background track
        canvas.fill_rect(bounds, theme.border);

        // Filled portion
        let fill_w = (self.width as u64 * self.value_per_mille as u64 / 1000) as u32;
        if fill_w > 0 {
            let colour = if self.value_per_mille > 850 {
                theme.error
            } else if self.value_per_mille > 650 {
                theme.warning
            } else {
                theme.success
            };
            canvas.fill_rect(Rect::new(0, 0, fill_w, self.height), colour);
        }

        // Border
        canvas.draw_rect(bounds, theme.border);
    }

    fn preferred_size(&self) -> (u32, u32) {
        (self.width, self.height)
    }
}
