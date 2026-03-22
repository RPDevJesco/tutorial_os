//! Theme system — colour palettes and style tokens.
//!
//! Themes define the visual identity of the UI.  Every widget reads its
//! colours and spacing from the active [`Theme`] rather than hard-coding
//! values.  This lets the Hardware Inspector render identically on both
//! the C and Rust implementations while allowing users to switch palettes.

use crate::core::types::Color;

/// Complete visual theme for the UI.
#[derive(Debug, Clone, Copy)]
pub struct Theme {
    /// Primary background colour.
    pub bg: Color,
    /// Primary foreground (text) colour.
    pub fg: Color,
    /// Accent colour for highlights and interactive elements.
    pub accent: Color,
    /// Muted text colour (secondary information).
    pub muted: Color,
    /// Panel / card background colour.
    pub panel_bg: Color,
    /// Border colour for panels and separators.
    pub border: Color,
    /// Success indicator colour.
    pub success: Color,
    /// Warning indicator colour.
    pub warning: Color,
    /// Error indicator colour.
    pub error: Color,
    /// Header / title bar background.
    pub header_bg: Color,
    /// Header foreground.
    pub header_fg: Color,
}

impl Theme {
    /// Dark theme matching the Tutorial-OS Hardware Inspector.
    pub const DARK: Self = Self {
        bg:        Color::rgb(24, 24, 32),
        fg:        Color::rgb(220, 220, 220),
        accent:    Color::rgb(80, 160, 240),
        muted:     Color::rgb(140, 140, 160),
        panel_bg:  Color::rgb(36, 36, 48),
        border:    Color::rgb(64, 64, 80),
        success:   Color::rgb(80, 200, 120),
        warning:   Color::rgb(240, 200, 60),
        error:     Color::rgb(240, 80, 80),
        header_bg: Color::rgb(40, 60, 100),
        header_fg: Color::rgb(240, 240, 255),
    };

    /// Light theme for high-contrast environments.
    pub const LIGHT: Self = Self {
        bg:        Color::rgb(240, 240, 244),
        fg:        Color::rgb(30, 30, 40),
        accent:    Color::rgb(40, 100, 200),
        muted:     Color::rgb(120, 120, 140),
        panel_bg:  Color::rgb(255, 255, 255),
        border:    Color::rgb(200, 200, 210),
        success:   Color::rgb(40, 160, 80),
        warning:   Color::rgb(200, 160, 20),
        error:     Color::rgb(200, 50, 50),
        header_bg: Color::rgb(40, 80, 160),
        header_fg: Color::rgb(255, 255, 255),
    };
}

impl Default for Theme {
    fn default() -> Self {
        Self::DARK
    }
}
