/*
 * ui/themes/ui_theme.rs — UI Theme System
 * =========================================
 *
 * Rust port of ui/themes/ui_theme.h
 *
 * Centralized color palettes, spacing, and typography for consistent styling.
 * Themes are resolution-aware and can be customized per-platform.
 *
 * DESIGN PHILOSOPHY:
 * ------------------
 *   - All visual constants in one place
 *   - Semantic color naming ("accent" not "blue")
 *   - Resolution-aware scaling
 *   - Multiple built-in themes
 *
 * USAGE:
 * ------
 *   let theme = Theme::dark_640();
 *   fb.fill_rect(bounds.x as u32, bounds.y as u32, bounds.w, bounds.h,
 *                theme.colors.bg_primary);
 *
 * KEY DIFFERENCES FROM C:
 * -----------------------
 *   - Palettes and spacing are const values (not macros)
 *   - Theme creation uses associated functions instead of macros
 *   - Semantic accessors are methods on Theme
 */

#![allow(dead_code)]

use super::core::Color;

// =============================================================================
// COLOR PALETTE
// =============================================================================

/// Complete color palette for a theme.
#[derive(Debug, Clone, Copy)]
pub struct ColorPalette {
    /* Backgrounds */
    pub bg_primary: Color,      // Main background
    pub bg_secondary: Color,    // Subtle contrast
    pub bg_elevated: Color,     // Panels, cards
    pub bg_overlay: Color,      // Modals, dropdowns

    /* Accent colors */
    pub accent: Color,          // Primary highlight
    pub accent_dim: Color,      // Unfocused, secondary
    pub accent_bright: Color,   // Hover, active

    /* Text colors */
    pub text_primary: Color,    // High contrast
    pub text_secondary: Color,  // Medium contrast
    pub text_disabled: Color,   // Low contrast
    pub text_on_accent: Color,  // Text on accent background

    /* Semantic colors */
    pub success: Color,
    pub warning: Color,
    pub error: Color,
    pub info: Color,

    /* Utility colors */
    pub border: Color,
    pub divider: Color,
    pub shadow: Color,
    pub transparent: Color,
}

// =============================================================================
// BUILT-IN PALETTES
// =============================================================================

/// Dark theme palette.
pub const PALETTE_DARK: ColorPalette = ColorPalette {
    bg_primary:     0xFF0A_0A14,
    bg_secondary:   0xFF14_142D,
    bg_elevated:    0xFF1E_1E3C,
    bg_overlay:     0xC800_0000,
    accent:         0xFF40_80FF,
    accent_dim:     0xFF28_5099,
    accent_bright:  0xFF60_A0FF,
    text_primary:   0xFFE8_E8F0,
    text_secondary: 0xFFA0_A0B0,
    text_disabled:  0xFF60_6070,
    text_on_accent: 0xFF0A_0A14,
    success:        0xFF40_C080,
    warning:        0xFFF0_A030,
    error:          0xFFE0_4040,
    info:           0xFF40_A0E0,
    border:         0xFF3C_3C50,
    divider:        0xFF28_283C,
    shadow:         0x8000_0000,
    transparent:    0x0000_0000,
};

/// Light theme palette.
pub const PALETTE_LIGHT: ColorPalette = ColorPalette {
    bg_primary:     0xFFF5_F5FA,
    bg_secondary:   0xFFEB_EBF2,
    bg_elevated:    0xFFFF_FFFF,
    bg_overlay:     0xC8FF_FFFF,
    accent:         0xFF30_64DC,
    accent_dim:     0xFF50_82C8,
    accent_bright:  0xFF20_50C8,
    text_primary:   0xFF14_141E,
    text_secondary: 0xFF50_5064,
    text_disabled:  0xFFA0_A0B0,
    text_on_accent: 0xFFFF_FFFF,
    success:        0xFF20_A060,
    warning:        0xFFDC_8C20,
    error:          0xFFC8_3030,
    info:           0xFF30_8CC8,
    border:         0xFFC8_C8D2,
    divider:        0xFFDC_DCE6,
    shadow:         0x4000_0000,
    transparent:    0x0000_0000,
};

/// Classic Game Boy green palette.
pub const PALETTE_GAMEBOY: ColorPalette = ColorPalette {
    bg_primary:     0xFF0F_380F,
    bg_secondary:   0xFF30_6230,
    bg_elevated:    0xFF8B_AC0F,
    bg_overlay:     0xC80F_380F,
    accent:         0xFF9B_BC0F,
    accent_dim:     0xFF8B_AC0F,
    accent_bright:  0xFFCA_DC9F,
    text_primary:   0xFF9B_BC0F,
    text_secondary: 0xFF8B_AC0F,
    text_disabled:  0xFF30_6230,
    text_on_accent: 0xFF0F_380F,
    success:        0xFF9B_BC0F,
    warning:        0xFF8B_AC0F,
    error:          0xFF0F_380F,
    info:           0xFF8B_AC0F,
    border:         0xFF30_6230,
    divider:        0xFF30_6230,
    shadow:         0x4000_0000,
    transparent:    0x0000_0000,
};

/// Cyan/teal theme.
pub const PALETTE_CYAN: ColorPalette = ColorPalette {
    bg_primary:     0xFF28_5064,
    bg_secondary:   0xFF32_6478,
    bg_elevated:    0xFF3C_788C,
    bg_overlay:     0xC814_3246,
    accent:         0xFF00_DCDC,
    accent_dim:     0xFF00_A0B4,
    accent_bright:  0xFF64_FFFF,
    text_primary:   0xFFFF_FFFF,
    text_secondary: 0xFFC8_DCE6,
    text_disabled:  0xFF78_96A0,
    text_on_accent: 0xFF00_2832,
    success:        0xFF50_DC78,
    warning:        0xFFFF_C850,
    error:          0xFFFF_6464,
    info:           0xFF64_C8FF,
    border:         0xFF50_8CA0,
    divider:        0xFF46_788C,
    shadow:         0x6400_1E28,
    transparent:    0x0000_0000,
};


// =============================================================================
// SPACING CONSTANTS
// =============================================================================

/// Spacing scale for consistent gaps and padding.
#[derive(Debug, Clone, Copy)]
pub struct Spacing {
    pub xs: u32,    // Extra small
    pub sm: u32,    // Small
    pub md: u32,    // Medium
    pub lg: u32,    // Large
    pub xl: u32,    // Extra large
}

/// Low resolution spacing (320×200).
pub const SPACING_LOW: Spacing = Spacing { xs: 2, sm: 4, md: 8, lg: 12, xl: 16 };

/// Medium resolution spacing (640×480).
pub const SPACING_MED: Spacing = Spacing { xs: 4, sm: 8, md: 16, lg: 24, xl: 32 };

/// High resolution spacing (1280+).
pub const SPACING_HIGH: Spacing = Spacing { xs: 8, sm: 16, md: 24, lg: 32, xl: 48 };


// =============================================================================
// BORDER RADIUS
// =============================================================================

/// Border radius presets for rounded corners.
#[derive(Debug, Clone, Copy)]
pub struct Radii {
    pub none: u32,  // 0 — no rounding
    pub sm: u32,    // Small radius (buttons)
    pub md: u32,    // Medium radius (cards)
    pub lg: u32,    // Large radius (modals)
    pub full: u32,  // Fully rounded (badges)
}

/// Low resolution radii.
pub const RADII_LOW: Radii = Radii { none: 0, sm: 2, md: 4, lg: 6, full: 100 };

/// Medium resolution radii.
pub const RADII_MED: Radii = Radii { none: 0, sm: 4, md: 8, lg: 12, full: 100 };

/// High resolution radii.
pub const RADII_HIGH: Radii = Radii { none: 0, sm: 8, md: 12, lg: 16, full: 100 };


// =============================================================================
// SHADOW CONFIGURATION
// =============================================================================

/// Shadow parameters for elevated elements.
#[derive(Debug, Clone, Copy)]
pub struct Shadow {
    pub offset_x: i32,     // Horizontal offset
    pub offset_y: i32,     // Vertical offset
    pub blur: u32,         // Blur radius (0 = hard shadow)
    pub color: Color,      // Shadow color
}

/// No shadow.
pub const SHADOW_NONE: Shadow = Shadow { offset_x: 0, offset_y: 0, blur: 0, color: 0 };

impl Shadow {
    /// Create a simple shadow with equal x/y offset.
    #[inline]
    pub const fn simple(offset: i32, color: Color) -> Self {
        Self { offset_x: offset, offset_y: offset, blur: 0, color }
    }
}


// =============================================================================
// COMPLETE THEME
// =============================================================================

/// Complete theme — combines palette, spacing, radii, shadow, and typography.
#[derive(Debug, Clone, Copy)]
pub struct Theme {
    pub colors: ColorPalette,
    pub spacing: Spacing,
    pub radii: Radii,
    pub shadow: Shadow,
    pub line_height: u32,       // Normal text line height
    pub line_height_sm: u32,    // Small text line height
}

impl Theme {
    /// Create theme for a specific screen width.
    pub const fn for_width(width: u32, palette: ColorPalette) -> Self {
        if width <= 400 {
            Self {
                colors: palette,
                spacing: SPACING_LOW,
                radii: RADII_LOW,
                shadow: Shadow::simple(1, palette.shadow),
                line_height: 8,
                line_height_sm: 7,
            }
        } else if width <= 800 {
            Self {
                colors: palette,
                spacing: SPACING_MED,
                radii: RADII_MED,
                shadow: Shadow::simple(2, palette.shadow),
                line_height: 12,
                line_height_sm: 10,
            }
        } else {
            Self {
                colors: palette,
                spacing: SPACING_HIGH,
                radii: RADII_HIGH,
                shadow: Shadow::simple(4, palette.shadow),
                line_height: 16,
                line_height_sm: 12,
            }
        }
    }

    // =========================================================================
    // Pre-built themes
    // =========================================================================

    /// Dark theme for 640×480.
    pub const fn dark_640() -> Self {
        Self::for_width(640, PALETTE_DARK)
    }

    /// Dark theme for 320×200.
    pub const fn dark_320() -> Self {
        Self::for_width(320, PALETTE_DARK)
    }

    /// Game Boy theme for 320×200.
    pub const fn gameboy_320() -> Self {
        Self::for_width(320, PALETTE_GAMEBOY)
    }

    /// Cyan theme for 640×480.
    pub const fn cyan_640() -> Self {
        Self::for_width(640, PALETTE_CYAN)
    }

    // =========================================================================
    // Semantic color accessors
    // =========================================================================

    /// Panel background color.
    #[inline(always)]
    pub const fn panel_bg(&self) -> Color {
        self.colors.bg_elevated
    }

    /// Panel border color.
    #[inline(always)]
    pub const fn panel_border(&self) -> Color {
        self.colors.border
    }

    /// Button background (normal state).
    #[inline(always)]
    pub const fn button_bg(&self) -> Color {
        self.colors.bg_elevated
    }

    /// Button background (focused state).
    #[inline(always)]
    pub const fn button_bg_focused(&self) -> Color {
        self.colors.accent_dim
    }

    /// Button background (pressed state).
    #[inline(always)]
    pub const fn button_bg_pressed(&self) -> Color {
        self.colors.accent
    }

    /// Button text (normal/focused state).
    #[inline(always)]
    pub const fn button_text(&self) -> Color {
        self.colors.text_primary
    }

    /// Button text (pressed state).
    #[inline(always)]
    pub const fn button_text_pressed(&self) -> Color {
        self.colors.text_on_accent
    }

    /// List item background (selected).
    #[inline(always)]
    pub const fn list_item_bg_selected(&self) -> Color {
        self.colors.accent_dim
    }

    /// List item text (normal).
    #[inline(always)]
    pub const fn list_item_text(&self) -> Color {
        self.colors.text_secondary
    }

    /// List item text (selected).
    #[inline(always)]
    pub const fn list_item_text_selected(&self) -> Color {
        self.colors.text_primary
    }

    /// Scrollbar track color.
    #[inline(always)]
    pub const fn scrollbar_track(&self) -> Color {
        self.colors.bg_secondary
    }

    /// Scrollbar thumb color.
    #[inline(always)]
    pub const fn scrollbar_thumb(&self) -> Color {
        self.colors.accent
    }

    /// Header background color.
    #[inline(always)]
    pub const fn header_bg(&self) -> Color {
        self.colors.bg_elevated
    }

    /// Header text color.
    #[inline(always)]
    pub const fn header_text(&self) -> Color {
        self.colors.text_primary
    }
}
