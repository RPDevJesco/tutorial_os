/*
 * ui/mod.rs — Building a User Interface from Scratch (Rust)
 * ==========================================================
 *
 * Rust port of the ui/ directory from Tutorial-OS.
 *
 * This module contains a complete UI system built without any external
 * libraries. Everything from colors to widgets is implemented from first
 * principles.
 *
 * ARCHITECTURE:
 * -------------
 *   ui/
 *   ├── core/               # Fundamental types (colors, rectangles)
 *   │   └── ui_types.rs     # Color, Rect, alignment enums
 *   ├── themes/             # Visual styling
 *   │   └── ui_theme.rs     # Palettes, spacing, radii
 *   └── widgets/            # Reusable components
 *       └── ui_widgets.rs   # Panel, Button, List, Badge, etc.
 *
 * DESIGN PHILOSOPHY:
 * ------------------
 *
 * ### Immediate Mode UI
 *
 * Traditional UI frameworks retain objects (buttons, labels, etc.) in memory.
 * We use "immediate mode" where widgets are just functions:
 *
 * ```rust,ignore
 * // Retained mode (traditional)
 * let btn = Button::new("Click me");
 * btn.set_position(100, 50);
 * btn.render();
 *
 * // Immediate mode (our approach)
 * draw_button(fb, bounds, &theme, "Click me", ButtonState::Normal);
 * ```
 *
 * Benefits:
 *   - No memory management for widgets
 *   - State lives in your code, not the UI framework
 *   - Easy to reason about
 *   - No hidden dependencies
 *
 * ### Platform Agnostic
 *
 * The UI code doesn't know about hardware. It calls methods on a
 * `Framebuffer` struct that abstracts all pixel operations:
 *
 * ```rust,ignore
 * // Same widget code works on any platform with a Framebuffer
 * draw_panel(fb, bounds, &theme, PanelStyle::Elevated);
 * ```
 *
 * This allows the same UI code to work on:
 *   - Raspberry Pi (VideoCore framebuffer)
 *   - RISC-V boards (U-Boot SimpleFB)
 *   - x86_64 (UEFI GOP)
 *   - Emulated environments
 *
 * ### Theme-Driven Styling
 *
 * All colors, spacing, and radii come from a theme:
 *
 * ```rust,ignore
 * // Don't hardcode colors!
 * // Bad:  fb.fill_rect(x, y, w, h, 0xFF3366FF);
 * // Good: fb.fill_rect(x, y, w, h, theme.colors.accent);
 * ```
 *
 * This makes it easy to:
 *   - Support dark/light modes
 *   - Adapt to different resolutions
 *   - Maintain visual consistency
 *
 * USAGE EXAMPLE:
 * --------------
 *
 * ```rust,ignore
 * use ui::prelude::*;
 *
 * fn draw_menu(fb: &mut Framebuffer, theme: &Theme,
 *              items: &[&str], selected: usize, scroll: u32) {
 *     // Background
 *     fb.clear(theme.colors.bg_primary);
 *
 *     // Title panel
 *     let header = Rect::new(10, 10, 300, 40);
 *     draw_panel_with_header(fb, header, theme, "My Menu");
 *
 *     // List items
 *     let mut item_rect = Rect::new(10, 60, 300, 24);
 *     for (i, name) in items.iter().enumerate() {
 *         draw_list_item(fb, item_rect, theme, name, i == selected);
 *         item_rect.y += 26;
 *     }
 *
 *     // Scrollbar
 *     let scrollbar = Rect::new(320, 60, 8, 200);
 *     draw_scrollbar_v(fb, scrollbar, theme, scroll, 8, items.len() as u32);
 *
 *     // Help bar
 *     let help = Rect::new(0, 460, 640, 20);
 *     draw_help_bar(fb, help, theme, &[
 *         HelpHint { key: "A", action: "Select" },
 *         HelpHint { key: "B", action: "Back" },
 *     ]);
 * }
 * ```
 */

#![no_std]
#![allow(dead_code)]

pub mod core;
pub mod themes;
pub mod widgets;

// =============================================================================
// PRELUDE
// =============================================================================
//
// Import `use ui::prelude::*;` to get the most commonly used types in scope.

pub mod prelude {
    // Core types
    pub use super::core::{
        Color, Rect, RECT_ZERO,
        rgb, argb,
        color_alpha, color_red, color_green, color_blue,
        color_with_alpha, color_lerp, color_blend_over,
        color_darken, color_lighten,
        HAlign, VAlign, FontSize, BlendMode,
        FontMetrics, FONT_4X6, FONT_8X8, FONT_8X16,
    };

    // Theme types
    pub use super::themes::{
        Theme,
        ColorPalette,
        PALETTE_DARK, PALETTE_LIGHT, PALETTE_GAMEBOY, PALETTE_CYAN,
        Spacing, SPACING_LOW, SPACING_MED, SPACING_HIGH,
        Radii, RADII_LOW, RADII_MED, RADII_HIGH,
        Shadow, SHADOW_NONE,
    };

    // Widget functions and types
    pub use super::widgets::{
        text_width, text_width_large,
        PanelStyle, draw_panel, draw_panel_with_header,
        ButtonState, draw_button,
        draw_list_item, draw_list_item_with_badge,
        draw_badge,
        draw_scrollbar_v,
        draw_progress_bar,
        draw_divider_h, draw_divider_v,
        ToastStyle, draw_toast,
        HelpHint, draw_help_bar,
        draw_section_header,
    };
}
