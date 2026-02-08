/*
 * ui_theme.h - UI Theme System
 * ============================
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
 *   ui_theme_t theme = UI_THEME_DARK;
 *   canvas_fill_rect(canvas, bounds, theme.colors.bg_primary);
 *   text_draw(renderer, x, y, "Hello", theme.colors.text_primary);
 *
 * COLOR ORGANIZATION:
 * -------------------
 * Colors are organized into:
 *   - Backgrounds (primary, secondary, elevated, overlay)
 *   - Accents (normal, dim, bright)
 *   - Text (primary, secondary, disabled, on-accent)
 *   - Semantic (success, warning, error, info)
 *   - Utility (border, divider, shadow)
 *
 * SPACING SYSTEM:
 * ---------------
 * Spacing uses a scale that adapts to screen resolution:
 *   - xs: 2-8px (extra small gaps)
 *   - sm: 4-16px (small gaps)
 *   - md: 8-24px (medium gaps)
 *   - lg: 12-32px (large gaps)
 *   - xl: 16-48px (extra large gaps)
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#include "ui_types.h"

/* =============================================================================
 * COLOR PALETTE
 * =============================================================================
 */

/*
 * Complete color palette for a theme
 */
typedef struct {
    /* Backgrounds */
    ui_color_t bg_primary;      /* Main background */
    ui_color_t bg_secondary;    /* Subtle contrast */
    ui_color_t bg_elevated;     /* Panels, cards */
    ui_color_t bg_overlay;      /* Modals, dropdowns */

    /* Accent colors */
    ui_color_t accent;          /* Primary highlight */
    ui_color_t accent_dim;      /* Unfocused, secondary */
    ui_color_t accent_bright;   /* Hover, active */

    /* Text colors */
    ui_color_t text_primary;    /* High contrast */
    ui_color_t text_secondary;  /* Medium contrast */
    ui_color_t text_disabled;   /* Low contrast */
    ui_color_t text_on_accent;  /* Text on accent background */

    /* Semantic colors */
    ui_color_t success;
    ui_color_t warning;
    ui_color_t error;
    ui_color_t info;

    /* Utility colors */
    ui_color_t border;
    ui_color_t divider;
    ui_color_t shadow;
    ui_color_t transparent;
} ui_color_palette_t;


/* =============================================================================
 * BUILT-IN PALETTES
 * =============================================================================
 */

/* Dark theme palette */
#define UI_PALETTE_DARK ((ui_color_palette_t){ \
    .bg_primary     = 0xFF0A0A14, \
    .bg_secondary   = 0xFF14142D, \
    .bg_elevated    = 0xFF1E1E3C, \
    .bg_overlay     = 0xC8000000, \
    .accent         = 0xFF4080FF, \
    .accent_dim     = 0xFF285099, \
    .accent_bright  = 0xFF60A0FF, \
    .text_primary   = 0xFFE8E8F0, \
    .text_secondary = 0xFFA0A0B0, \
    .text_disabled  = 0xFF606070, \
    .text_on_accent = 0xFF0A0A14, \
    .success        = 0xFF40C080, \
    .warning        = 0xFFF0A030, \
    .error          = 0xFFE04040, \
    .info           = 0xFF40A0E0, \
    .border         = 0xFF3C3C50, \
    .divider        = 0xFF28283C, \
    .shadow         = 0x80000000, \
    .transparent    = 0x00000000, \
})

/* Light theme palette */
#define UI_PALETTE_LIGHT ((ui_color_palette_t){ \
    .bg_primary     = 0xFFF5F5FA, \
    .bg_secondary   = 0xFFEBEBF2, \
    .bg_elevated    = 0xFFFFFFFF, \
    .bg_overlay     = 0xC8FFFFFF, \
    .accent         = 0xFF3064DC, \
    .accent_dim     = 0xFF5082C8, \
    .accent_bright  = 0xFF2050C8, \
    .text_primary   = 0xFF14141E, \
    .text_secondary = 0xFF505064, \
    .text_disabled  = 0xFFA0A0B0, \
    .text_on_accent = 0xFFFFFFFF, \
    .success        = 0xFF20A060, \
    .warning        = 0xFFDC8C20, \
    .error          = 0xFFC83030, \
    .info           = 0xFF308CC8, \
    .border         = 0xFFC8C8D2, \
    .divider        = 0xFFDCDCE6, \
    .shadow         = 0x40000000, \
    .transparent    = 0x00000000, \
})

/* Classic Game Boy green palette */
#define UI_PALETTE_GAMEBOY ((ui_color_palette_t){ \
    .bg_primary     = 0xFF0F380F, \
    .bg_secondary   = 0xFF306230, \
    .bg_elevated    = 0xFF8BAC0F, \
    .bg_overlay     = 0xC80F380F, \
    .accent         = 0xFF9BBC0F, \
    .accent_dim     = 0xFF8BAC0F, \
    .accent_bright  = 0xFFCADC9F, \
    .text_primary   = 0xFF9BBC0F, \
    .text_secondary = 0xFF8BAC0F, \
    .text_disabled  = 0xFF306230, \
    .text_on_accent = 0xFF0F380F, \
    .success        = 0xFF9BBC0F, \
    .warning        = 0xFF8BAC0F, \
    .error          = 0xFF0F380F, \
    .info           = 0xFF8BAC0F, \
    .border         = 0xFF306230, \
    .divider        = 0xFF306230, \
    .shadow         = 0x40000000, \
    .transparent    = 0x00000000, \
})

/* Cyan/teal theme */
#define UI_PALETTE_CYAN ((ui_color_palette_t){ \
    .bg_primary     = 0xFF285064, \
    .bg_secondary   = 0xFF326478, \
    .bg_elevated    = 0xFF3C788C, \
    .bg_overlay     = 0xC8143246, \
    .accent         = 0xFF00DCDC, \
    .accent_dim     = 0xFF00A0B4, \
    .accent_bright  = 0xFF64FFFF, \
    .text_primary   = 0xFFFFFFFF, \
    .text_secondary = 0xFFC8DCE6, \
    .text_disabled  = 0xFF7896A0, \
    .text_on_accent = 0xFF002832, \
    .success        = 0xFF50DC78, \
    .warning        = 0xFFFFC850, \
    .error          = 0xFFFF6464, \
    .info           = 0xFF64C8FF, \
    .border         = 0xFF508CA0, \
    .divider        = 0xFF46788C, \
    .shadow         = 0x64001E28, \
    .transparent    = 0x00000000, \
})


/* =============================================================================
 * SPACING CONSTANTS
 * =============================================================================
 */

typedef struct {
    uint32_t xs;    /* Extra small */
    uint32_t sm;    /* Small */
    uint32_t md;    /* Medium */
    uint32_t lg;    /* Large */
    uint32_t xl;    /* Extra large */
} ui_spacing_t;

/* Low resolution spacing (320x200) */
#define UI_SPACING_LOW ((ui_spacing_t){ 2, 4, 8, 12, 16 })

/* Medium resolution spacing (640x480) */
#define UI_SPACING_MED ((ui_spacing_t){ 4, 8, 16, 24, 32 })

/* High resolution spacing (1280+) */
#define UI_SPACING_HIGH ((ui_spacing_t){ 8, 16, 24, 32, 48 })


/* =============================================================================
 * BORDER RADIUS
 * =============================================================================
 */

typedef struct {
    uint32_t none;  /* 0 - no rounding */
    uint32_t sm;    /* Small radius (buttons) */
    uint32_t md;    /* Medium radius (cards) */
    uint32_t lg;    /* Large radius (modals) */
    uint32_t full;  /* Fully rounded (badges) */
} ui_radii_t;

/* Low resolution radii */
#define UI_RADII_LOW ((ui_radii_t){ 0, 2, 4, 6, 100 })

/* Medium resolution radii */
#define UI_RADII_MED ((ui_radii_t){ 0, 4, 8, 12, 100 })

/* High resolution radii */
#define UI_RADII_HIGH ((ui_radii_t){ 0, 8, 12, 16, 100 })


/* =============================================================================
 * SHADOW CONFIGURATION
 * =============================================================================
 */

typedef struct {
    int32_t offset_x;   /* Horizontal offset */
    int32_t offset_y;   /* Vertical offset */
    uint32_t blur;      /* Blur radius (0 = hard shadow) */
    ui_color_t color;   /* Shadow color */
} ui_shadow_t;

#define UI_SHADOW_NONE ((ui_shadow_t){ 0, 0, 0, 0 })

static inline ui_shadow_t ui_shadow_simple(int32_t offset, ui_color_t color)
{
    ui_shadow_t s = { offset, offset, 0, color };
    return s;
}


/* =============================================================================
 * COMPLETE THEME
 * =============================================================================
 */

typedef struct {
    ui_color_palette_t colors;
    ui_spacing_t spacing;
    ui_radii_t radii;
    ui_shadow_t shadow;
    uint32_t line_height;       /* Normal text line height */
    uint32_t line_height_sm;    /* Small text line height */
} ui_theme_t;


/* =============================================================================
 * THEME CREATION HELPERS
 * =============================================================================
 */

/*
 * Create theme for specific screen width
 */
static inline ui_theme_t ui_theme_for_width(uint32_t width, ui_color_palette_t palette)
{
    ui_theme_t theme;
    theme.colors = palette;

    if (width <= 400) {
        theme.spacing = UI_SPACING_LOW;
        theme.radii = UI_RADII_LOW;
        theme.shadow = ui_shadow_simple(1, palette.shadow);
        theme.line_height = 8;
        theme.line_height_sm = 7;
    } else if (width <= 800) {
        theme.spacing = UI_SPACING_MED;
        theme.radii = UI_RADII_MED;
        theme.shadow = ui_shadow_simple(2, palette.shadow);
        theme.line_height = 12;
        theme.line_height_sm = 10;
    } else {
        theme.spacing = UI_SPACING_HIGH;
        theme.radii = UI_RADII_HIGH;
        theme.shadow = ui_shadow_simple(4, palette.shadow);
        theme.line_height = 16;
        theme.line_height_sm = 12;
    }

    return theme;
}


/* =============================================================================
 * PRE-BUILT THEMES
 * =============================================================================
 */

/* Dark theme for 640x480 */
#define UI_THEME_DARK_640 ui_theme_for_width(640, UI_PALETTE_DARK)

/* Dark theme for 320x200 */
#define UI_THEME_DARK_320 ui_theme_for_width(320, UI_PALETTE_DARK)

/* Game Boy theme for 320x200 */
#define UI_THEME_GAMEBOY_320 ui_theme_for_width(320, UI_PALETTE_GAMEBOY)

/* Cyan theme for 640x480 */
#define UI_THEME_CYAN_640 ui_theme_for_width(640, UI_PALETTE_CYAN)


/* =============================================================================
 * SEMANTIC COLOR ACCESSORS
 * =============================================================================
 *
 * These provide context-specific colors derived from the palette.
 */

/* Panel colors */
static inline ui_color_t ui_theme_panel_bg(const ui_theme_t *t)
{
    return t->colors.bg_elevated;
}

static inline ui_color_t ui_theme_panel_border(const ui_theme_t *t)
{
    return t->colors.border;
}

/* Button colors */
static inline ui_color_t ui_theme_button_bg(const ui_theme_t *t)
{
    return t->colors.bg_elevated;
}

static inline ui_color_t ui_theme_button_bg_focused(const ui_theme_t *t)
{
    return t->colors.accent_dim;
}

static inline ui_color_t ui_theme_button_bg_pressed(const ui_theme_t *t)
{
    return t->colors.accent;
}

static inline ui_color_t ui_theme_button_text(const ui_theme_t *t)
{
    return t->colors.text_primary;
}

static inline ui_color_t ui_theme_button_text_pressed(const ui_theme_t *t)
{
    return t->colors.text_on_accent;
}

/* List item colors */
static inline ui_color_t ui_theme_list_item_bg_selected(const ui_theme_t *t)
{
    return t->colors.accent_dim;
}

static inline ui_color_t ui_theme_list_item_text(const ui_theme_t *t)
{
    return t->colors.text_secondary;
}

static inline ui_color_t ui_theme_list_item_text_selected(const ui_theme_t *t)
{
    return t->colors.text_primary;
}

/* Scrollbar colors */
static inline ui_color_t ui_theme_scrollbar_track(const ui_theme_t *t)
{
    return t->colors.bg_secondary;
}

static inline ui_color_t ui_theme_scrollbar_thumb(const ui_theme_t *t)
{
    return t->colors.accent;
}

/* Header colors */
static inline ui_color_t ui_theme_header_bg(const ui_theme_t *t)
{
    return t->colors.bg_elevated;
}

static inline ui_color_t ui_theme_header_text(const ui_theme_t *t)
{
    return t->colors.text_primary;
}

#endif /* UI_THEME_H */
