/*
 * ui_widgets.h - Reusable UI Widget Functions
 * ============================================
 *
 * Widgets are stateless drawing functions - they take bounds, theme, and
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
 *
 * USAGE:
 * ------
 *   ui_draw_panel(&fb, bounds, &theme, UI_PANEL_ELEVATED);
 *   ui_draw_button(&fb, bounds, &theme, "OK", UI_BUTTON_FOCUSED);
 *   ui_draw_list_item(&fb, bounds, &theme, "item.txt", true);
 */

#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "ui_types.h"
#include "ui_theme.h"
#include "framebuffer.h"

/* =============================================================================
 * PANEL WIDGET
 * =============================================================================
 */

/*
 * Panel visual styles
 */
typedef enum {
    UI_PANEL_FLAT,      /* No shadow */
    UI_PANEL_ELEVATED,  /* Subtle shadow */
    UI_PANEL_MODAL,     /* Strong shadow (for dialogs) */
    UI_PANEL_OUTLINED,  /* Border only, no fill */
} ui_panel_style_t;

/*
 * Draw a panel (container background)
 */
void ui_draw_panel(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    ui_panel_style_t style
);

/*
 * Draw a panel with header
 */
void ui_draw_panel_with_header(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *title
);


/* =============================================================================
 * BUTTON WIDGET
 * =============================================================================
 */

/*
 * Button states
 */
typedef enum {
    UI_BUTTON_NORMAL,
    UI_BUTTON_FOCUSED,
    UI_BUTTON_PRESSED,
    UI_BUTTON_DISABLED,
} ui_button_state_t;

/*
 * Draw a button
 */
void ui_draw_button(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *label,
    ui_button_state_t state
);


/* =============================================================================
 * LIST ITEM WIDGET
 * =============================================================================
 */

/*
 * Draw a list item (for menus, file browsers, etc.)
 */
void ui_draw_list_item(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *label,
    bool selected
);

/*
 * Draw a list item with badge (e.g., "GBC" tag)
 */
void ui_draw_list_item_with_badge(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *label,
    const char *badge,  /* NULL for no badge */
    bool selected
);


/* =============================================================================
 * BADGE WIDGET
 * =============================================================================
 */

/*
 * Draw a small badge/tag
 */
void ui_draw_badge(
    framebuffer_t *fb,
    int32_t x,
    int32_t y,
    const ui_theme_t *theme,
    const char *label
);


/* =============================================================================
 * SCROLLBAR WIDGET
 * =============================================================================
 */

/*
 * Draw a vertical scrollbar
 *
 * @param scroll_offset  Current scroll position (0 to total-visible)
 * @param visible_count  Number of items visible at once
 * @param total_count    Total number of items
 */
void ui_draw_scrollbar_v(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    uint32_t scroll_offset,
    uint32_t visible_count,
    uint32_t total_count
);


/* =============================================================================
 * PROGRESS BAR WIDGET
 * =============================================================================
 */

/*
 * Draw a horizontal progress bar
 *
 * @param progress_pct  Progress percentage (0 to 100)
 * @param color         Fill color (or 0 to use accent color)
 */
void ui_draw_progress_bar(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    uint32_t progress_pct,  /* 0-100 */
    ui_color_t color  /* 0 = use accent */
);


/* =============================================================================
 * DIVIDER WIDGET
 * =============================================================================
 */

/*
 * Draw a horizontal divider line
 */
void ui_draw_divider_h(
    framebuffer_t *fb,
    int32_t x,
    int32_t y,
    uint32_t width,
    const ui_theme_t *theme
);

/*
 * Draw a vertical divider line
 */
void ui_draw_divider_v(
    framebuffer_t *fb,
    int32_t x,
    int32_t y,
    uint32_t height,
    const ui_theme_t *theme
);


/* =============================================================================
 * TOAST/NOTIFICATION WIDGET
 * =============================================================================
 */

typedef enum {
    UI_TOAST_INFO,
    UI_TOAST_SUCCESS,
    UI_TOAST_WARNING,
    UI_TOAST_ERROR,
} ui_toast_style_t;

/*
 * Draw a toast notification
 */
void ui_draw_toast(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *message,
    ui_toast_style_t style
);


/* =============================================================================
 * HELP BAR WIDGET
 * =============================================================================
 */

/*
 * Help bar hint (key + action pair)
 */
typedef struct {
    const char *key;    /* e.g., "A" or "START" */
    const char *action; /* e.g., "Select" or "Menu" */
} ui_help_hint_t;

/*
 * Draw a help bar with button hints
 *
 * @param hints       Array of hint structs
 * @param hint_count  Number of hints in array
 */
void ui_draw_help_bar(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const ui_help_hint_t *hints,
    size_t hint_count
);


/* =============================================================================
 * SECTION HEADER WIDGET
 * =============================================================================
 */

/*
 * Draw a section header with underline
 */
void ui_draw_section_header(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *title
);

#endif /* UI_WIDGETS_H */