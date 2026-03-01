/*
 * ui/widgets/ui_widgets.rs — Reusable UI Widget Functions
 * ========================================================
 *
 * Rust port of ui/widgets/ui_widgets.h + ui/widgets/ui_widgets.c
 *
 * Widgets are stateless drawing functions — they take bounds, theme, and
 * data, then draw themselves. This is "immediate mode" UI design.
 *
 * WIDGET PHILOSOPHY:
 * ------------------
 *   - Widgets are functions, not objects
 *   - State is owned externally (in your menu, dialog, etc.)
 *   - Widgets receive a Rect for their bounds
 *   - Widgets use Theme for consistent styling
 *   - No heap allocation inside widgets
 *
 * AVAILABLE WIDGETS:
 * ------------------
 *   - Panel: Background container with optional shadow
 *   - Button: Clickable button with states
 *   - List Item: Row in a list with selection highlight
 *   - Badge: Small tag/label
 *   - Scrollbar: Vertical scroll indicator
 *   - Progress Bar: Horizontal progress indicator
 *   - Divider: Horizontal/vertical separator line
 *   - Toast: Notification message
 *   - Help Bar: Row of button hints
 *   - Section Header: Title with underline
 *
 * USAGE:
 * ------
 *   draw_panel(fb, bounds, &theme, PanelStyle::Elevated);
 *   draw_button(fb, bounds, &theme, "OK", ButtonState::Focused);
 *   draw_list_item(fb, bounds, &theme, "item.txt", true);
 *
 * IMPLEMENTATION NOTES:
 * ---------------------
 * Widgets follow a consistent pattern:
 *   1. Early-return if bounds are off-screen (negative coords)
 *   2. Extract colors from theme
 *   3. Calculate geometry
 *   4. Draw background/shadow
 *   5. Draw content (text, icons)
 *
 * All widgets call Framebuffer methods directly (no vtable abstraction),
 * matching the C implementation's approach.
 *
 * KEY DIFFERENCES FROM C:
 * -----------------------
 *   - Enums instead of typedef'd C enums
 *   - &str instead of const char* (null-terminated strings in no_std
 *     use the same framebuffer string-drawing methods)
 *   - Widget functions take &mut Framebuffer + &Theme
 *   - No need for manual strlen/memcpy — Rust slices handle this
 */

#![allow(dead_code)]

use super::core::{Color, Rect};
use super::themes::Theme;

// We reference the Framebuffer type from the drivers module.
// Adjust this import path to match your project's module structure.
use crate::drivers::framebuffer::Framebuffer;
use crate::drivers::framebuffer::{FB_CHAR_WIDTH, FB_CHAR_HEIGHT, FB_CHAR_WIDTH_LG};


// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/// Clamp u32 to 0–100 range (for percentages).
#[inline(always)]
const fn clamp100(v: u32) -> u32 {
    if v > 100 { 100 } else { v }
}

/// Calculate how many characters fit in a given pixel width.
#[inline]
fn chars_that_fit(width: u32, large: bool) -> u32 {
    let char_w = if large { FB_CHAR_WIDTH_LG } else { FB_CHAR_WIDTH };
    if char_w > 0 { width / char_w } else { 0 }
}

/// Measure the pixel width of a string using the normal (8×8) font.
///
/// Standalone function matching C's `fb_text_width()`. This doesn't need
/// a Framebuffer instance since the font is monospace with a fixed width.
#[inline]
pub fn text_width(s: &str) -> u32 {
    s.len() as u32 * FB_CHAR_WIDTH
}

/// Measure the pixel width of a string using the large (8×16) font.
#[inline]
pub fn text_width_large(s: &str) -> u32 {
    s.len() as u32 * FB_CHAR_WIDTH_LG
}


// =============================================================================
// PANEL WIDGET
// =============================================================================

/// Panel visual styles.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PanelStyle {
    /// No shadow.
    Flat,
    /// Subtle shadow.
    Elevated,
    /// Strong shadow (for dialogs).
    Modal,
    /// Border only, no fill.
    Outlined,
}

/// Draw a panel (container background).
pub fn draw_panel(fb: &mut Framebuffer, bounds: Rect, theme: &Theme, style: PanelStyle) {
    if bounds.x < 0 || bounds.y < 0 { return; }
    let x = bounds.x as u32;
    let y = bounds.y as u32;
    let radius = theme.radii.md;

    match style {
        PanelStyle::Flat => {
            fb.fill_rounded_rect(x, y, bounds.w, bounds.h, radius,
                                 theme.colors.bg_secondary);
        }

        PanelStyle::Elevated => {
            // Draw shadow first
            let sx = x + theme.shadow.offset_x as u32;
            let sy = y + theme.shadow.offset_y as u32;
            fb.fill_rounded_rect(sx, sy, bounds.w, bounds.h, radius,
                                 theme.shadow.color);
            // Draw panel on top
            fb.fill_rounded_rect(x, y, bounds.w, bounds.h, radius,
                                 theme.panel_bg());
        }

        PanelStyle::Modal => {
            // Larger shadow for modals
            let sx = x + (theme.shadow.offset_x * 2) as u32;
            let sy = y + (theme.shadow.offset_y * 2) as u32;
            fb.fill_rounded_rect(sx, sy, bounds.w, bounds.h, radius,
                                 theme.shadow.color);
            fb.fill_rounded_rect(x, y, bounds.w, bounds.h, radius,
                                 theme.panel_bg());
        }

        PanelStyle::Outlined => {
            fb.draw_rounded_rect(x, y, bounds.w, bounds.h, radius,
                                 theme.panel_border());
        }
    }
}

/// Draw a panel with a header title bar.
pub fn draw_panel_with_header(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    title: &str,
) {
    if bounds.x < 0 || bounds.y < 0 { return; }
    let x = bounds.x as u32;
    let y = bounds.y as u32;

    let header_height = theme.line_height + theme.spacing.sm * 2;

    // Draw panel background
    draw_panel(fb, bounds, theme, PanelStyle::Elevated);

    // Draw header background
    let radius = theme.radii.md;
    fb.fill_rounded_rect(x, y, bounds.w, header_height + radius, radius,
                         theme.header_bg());

    // Draw title text
    let text_x = x + theme.spacing.md;
    let text_y = y + theme.spacing.sm;
    fb.draw_string_transparent(text_x, text_y, title, theme.header_text());

    // Draw divider under header
    fb.draw_hline(x, y + header_height, bounds.w, theme.colors.divider);
}


// =============================================================================
// BUTTON WIDGET
// =============================================================================

/// Button states.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ButtonState {
    Normal,
    Focused,
    Pressed,
    Disabled,
}

/// Draw a button.
pub fn draw_button(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    label: &str,
    state: ButtonState,
) {
    if bounds.x < 0 || bounds.y < 0 { return; }
    let x = bounds.x as u32;
    let y = bounds.y as u32;

    let radius = theme.radii.sm;
    let (bg, fg, draw_shadow) = match state {
        ButtonState::Normal => {
            (theme.button_bg(), theme.button_text(), true)
        }
        ButtonState::Focused => {
            (theme.button_bg_focused(), theme.button_text(), true)
        }
        ButtonState::Pressed => {
            (theme.button_bg_pressed(), theme.button_text_pressed(), false)
        }
        ButtonState::Disabled => {
            (theme.colors.bg_secondary, theme.colors.text_disabled, false)
        }
    };

    // Draw shadow
    if draw_shadow {
        let sy = y + theme.shadow.offset_y as u32;
        fb.fill_rounded_rect(x, sy, bounds.w, bounds.h, radius,
                             theme.shadow.color);
    }

    // Draw button background
    fb.fill_rounded_rect(x, y, bounds.w, bounds.h, radius, bg);

    // Center text
    let text_w = text_width(label);
    let text_h = FB_CHAR_HEIGHT;
    let text_x = x + (bounds.w.saturating_sub(text_w)) / 2;
    let text_y = y + (bounds.h.saturating_sub(text_h)) / 2;
    fb.draw_string_transparent(text_x, text_y, label, fg);
}


// =============================================================================
// LIST ITEM WIDGET
// =============================================================================

/// Draw a list item (for menus, file browsers, etc.).
pub fn draw_list_item(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    label: &str,
    selected: bool,
) {
    if bounds.x < 0 || bounds.y < 0 { return; }
    let x = bounds.x as u32;
    let y = bounds.y as u32;

    let radius = theme.radii.sm;

    // Selection highlight
    if selected {
        fb.fill_rounded_rect(x, y, bounds.w, bounds.h, radius,
                             theme.list_item_bg_selected());
    }

    let text_color = if selected {
        theme.list_item_text_selected()
    } else {
        theme.list_item_text()
    };

    // Selection indicator
    if selected {
        let indicator_x = x + theme.spacing.xs;
        fb.draw_string_transparent(indicator_x, y + theme.spacing.xs,
                                   ">", theme.colors.accent_bright);
    }

    // Label text
    let text_x = x + theme.spacing.md;
    let text_y = y + theme.spacing.xs;

    // Truncate if needed
    let max_chars = chars_that_fit(bounds.w.saturating_sub(theme.spacing.lg), false) as usize;
    let label_len = label.len();

    if label_len > max_chars && max_chars > 3 {
        // Draw truncated with ellipsis
        let copy_len = max_chars - 3;
        let mut truncated = [0u8; 256];
        let safe_len = if copy_len > 252 { 252 } else { copy_len };

        for (i, &b) in label.as_bytes().iter().take(safe_len).enumerate() {
            truncated[i] = b;
        }
        truncated[safe_len] = b'.';
        truncated[safe_len + 1] = b'.';
        truncated[safe_len + 2] = b'.';
        truncated[safe_len + 3] = 0;

        // SAFETY: We just built valid ASCII bytes + null terminator
        let trunc_str = unsafe {
            core::str::from_utf8_unchecked(&truncated[..safe_len + 3])
        };
        fb.draw_string_transparent(text_x, text_y, trunc_str, text_color);
    } else {
        fb.draw_string_transparent(text_x, text_y, label, text_color);
    }
}

/// Draw a list item with a badge (e.g., "GBC" tag).
pub fn draw_list_item_with_badge(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    label: &str,
    badge: Option<&str>,
    selected: bool,
) {
    // Draw the base list item
    draw_list_item(fb, bounds, theme, label, selected);

    // Draw badge if present
    if let Some(badge_text) = badge {
        if badge_text.is_empty() { return; }

        if bounds.x < 0 || bounds.y < 0 { return; }
        let x = bounds.x as u32;
        let y = bounds.y as u32;

        let badge_padding = theme.spacing.xs;
        let badge_text_w = text_width(badge_text);
        let badge_h = FB_CHAR_HEIGHT + badge_padding;
        let badge_w = badge_text_w + badge_padding * 2;
        let badge_x = x + bounds.w.saturating_sub(badge_w + theme.spacing.sm);
        let badge_y = y + (bounds.h.saturating_sub(badge_h)) / 2;

        fb.fill_rounded_rect(badge_x, badge_y, badge_w, badge_h,
                             theme.radii.sm, theme.colors.accent_dim);
        fb.draw_string_transparent(badge_x + badge_padding,
                                   badge_y + badge_padding / 2,
                                   badge_text, theme.colors.text_on_accent);
    }
}


// =============================================================================
// BADGE WIDGET
// =============================================================================

/// Draw a small badge/tag.
pub fn draw_badge(
    fb: &mut Framebuffer,
    x: i32,
    y: i32,
    theme: &Theme,
    label: &str,
) {
    if x < 0 || y < 0 || label.is_empty() { return; }
    let x = x as u32;
    let y = y as u32;

    let padding_x = theme.spacing.xs;
    let padding_y = theme.spacing.xs / 2;

    let tw = text_width(label);
    let th = FB_CHAR_HEIGHT;

    let width = tw + padding_x * 2;
    let height = th + padding_y * 2;

    // Badge background
    fb.fill_rounded_rect(x, y, width, height,
                         theme.radii.sm, theme.colors.accent_bright);

    // Badge text
    fb.draw_string_transparent(x + padding_x, y + padding_y,
                               label, theme.colors.text_on_accent);
}


// =============================================================================
// SCROLLBAR WIDGET
// =============================================================================

/// Draw a vertical scrollbar.
///
/// # Parameters
/// - `scroll_offset`: Current scroll position (0 to total − visible)
/// - `visible_count`: Number of items visible at once
/// - `total_count`: Total number of items
pub fn draw_scrollbar_v(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    scroll_offset: u32,
    visible_count: u32,
    total_count: u32,
) {
    if total_count <= visible_count { return; } // No scrollbar needed
    if bounds.x < 0 || bounds.y < 0 { return; }

    let x = bounds.x as u32;
    let y = bounds.y as u32;
    let radius = bounds.w / 2;

    // Track
    fb.fill_rounded_rect(x, y, bounds.w, bounds.h, radius,
                         theme.scrollbar_track());

    // Calculate thumb size
    let mut thumb_height = (visible_count * bounds.h) / total_count;
    if thumb_height < theme.spacing.md {
        thumb_height = theme.spacing.md;
    }

    // Calculate thumb position
    let scroll_range = total_count - visible_count;
    let thumb_range = bounds.h.saturating_sub(thumb_height);
    let thumb_offset = if scroll_range > 0 && thumb_range > 0 {
        (scroll_offset * thumb_range) / scroll_range
    } else {
        0
    };

    // Thumb
    fb.fill_rounded_rect(x, y + thumb_offset, bounds.w, thumb_height,
                         radius, theme.scrollbar_thumb());
}


// =============================================================================
// PROGRESS BAR WIDGET
// =============================================================================

/// Draw a horizontal progress bar.
///
/// # Parameters
/// - `progress_pct`: Progress percentage (0–100)
/// - `color`: Fill color, or `0` to use accent color
pub fn draw_progress_bar(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    progress_pct: u32,
    color: Color,
) {
    if bounds.x < 0 || bounds.y < 0 { return; }
    let x = bounds.x as u32;
    let y = bounds.y as u32;

    let radius = theme.radii.sm;
    let fill_color = if color != 0 { color } else { theme.colors.accent };

    // Background track
    fb.fill_rounded_rect(x, y, bounds.w, bounds.h, radius,
                         theme.scrollbar_track());

    // Clamp progress to 0–100
    let pct = clamp100(progress_pct);

    // Fill
    if pct > 0 {
        let mut fill_width = (pct * bounds.w) / 100;
        if fill_width < radius * 2 {
            fill_width = radius * 2;
        }

        fb.fill_rounded_rect(x, y, fill_width, bounds.h, radius, fill_color);
    }
}


// =============================================================================
// DIVIDER WIDGET
// =============================================================================

/// Draw a horizontal divider line.
pub fn draw_divider_h(
    fb: &mut Framebuffer,
    x: i32,
    y: i32,
    width: u32,
    theme: &Theme,
) {
    if x < 0 || y < 0 { return; }
    fb.draw_hline(x as u32, y as u32, width, theme.colors.divider);
}

/// Draw a vertical divider line.
pub fn draw_divider_v(
    fb: &mut Framebuffer,
    x: i32,
    y: i32,
    height: u32,
    theme: &Theme,
) {
    if x < 0 || y < 0 { return; }
    fb.draw_vline(x as u32, y as u32, height, theme.colors.divider);
}


// =============================================================================
// TOAST / NOTIFICATION WIDGET
// =============================================================================

/// Toast notification style.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ToastStyle {
    Info,
    Success,
    Warning,
    Error,
}

/// Draw a toast notification.
pub fn draw_toast(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    message: &str,
    style: ToastStyle,
) {
    if bounds.x < 0 || bounds.y < 0 { return; }
    let x = bounds.x as u32;
    let y = bounds.y as u32;

    let (bg, fg) = match style {
        ToastStyle::Success => (theme.colors.success, theme.colors.text_on_accent),
        ToastStyle::Warning => (theme.colors.warning, theme.colors.text_on_accent),
        ToastStyle::Error   => (theme.colors.error,   theme.colors.text_on_accent),
        ToastStyle::Info    => (theme.colors.info,     theme.colors.text_on_accent),
    };

    // Background with shadow
    let sx = x + theme.shadow.offset_x as u32;
    let sy = y + theme.shadow.offset_y as u32;
    fb.fill_rounded_rect(sx, sy, bounds.w, bounds.h, theme.radii.md,
                         theme.shadow.color);
    fb.fill_rounded_rect(x, y, bounds.w, bounds.h, theme.radii.md, bg);

    // Centered text
    let tw = text_width(message);
    let th = FB_CHAR_HEIGHT;
    let text_x = x + (bounds.w.saturating_sub(tw)) / 2;
    let text_y = y + (bounds.h.saturating_sub(th)) / 2;
    fb.draw_string_transparent(text_x, text_y, message, fg);
}


// =============================================================================
// HELP BAR WIDGET
// =============================================================================

/// Help bar hint (key + action pair).
pub struct HelpHint<'a> {
    /// e.g., "A" or "START"
    pub key: &'a str,
    /// e.g., "Select" or "Menu"
    pub action: &'a str,
}

/// Draw a help bar with button hints.
pub fn draw_help_bar(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    hints: &[HelpHint],
) {
    if bounds.x < 0 || bounds.y < 0 { return; }

    let mut x = bounds.x as u32 + theme.spacing.sm;
    let y = bounds.y as u32 + theme.spacing.xs;

    for hint in hints {
        // Key badge
        let key_width = text_width(hint.key) + theme.spacing.xs * 2;
        let key_height = theme.line_height + theme.spacing.xs;

        fb.fill_rounded_rect(x, y, key_width, key_height, theme.radii.sm,
                             theme.colors.accent);

        fb.draw_string_transparent(x + theme.spacing.xs,
                                   y + theme.spacing.xs / 2,
                                   hint.key, theme.colors.text_on_accent);

        x += key_width + 2;

        // Action text
        fb.draw_string_transparent(x, y + theme.spacing.xs / 2,
                                   hint.action, theme.colors.text_secondary);

        x += text_width(hint.action) + theme.spacing.sm;
    }
}


// =============================================================================
// SECTION HEADER WIDGET
// =============================================================================

/// Draw a section header with underline.
pub fn draw_section_header(
    fb: &mut Framebuffer,
    bounds: Rect,
    theme: &Theme,
    title: &str,
) {
    if bounds.x < 0 || bounds.y < 0 { return; }
    let x = bounds.x as u32;
    let y = bounds.y as u32;

    let text_y = y + theme.spacing.xs;

    // Use small font (8×8 in this framebuffer)
    fb.draw_string_transparent(x, text_y, title, theme.colors.text_secondary);

    // Underline
    let tw = text_width(title);
    let line_y = text_y + FB_CHAR_HEIGHT + 1;

    fb.draw_hline(x, line_y, tw + theme.spacing.sm, theme.colors.divider);
}
