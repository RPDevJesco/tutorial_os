/*
 * ui_widgets.c - UI Widget Implementations
 * ========================================
 *
 * See ui_widgets.h for widget documentation.
 *
 * IMPLEMENTATION NOTES:
 * ---------------------
 * Widgets follow a consistent pattern:
 *   1. Extract colors from theme
 *   2. Calculate geometry
 *   3. Draw background/shadow
 *   4. Draw content (text, icons)
 *
 * All widgets are designed to be:
 *   - Self-contained (no hidden state)
 *   - Resolution-independent (use theme spacing)
 *   - Efficient (minimize draw calls)
 *
 * This calls framebuffer functions directly (no vtable abstraction).
 */

#include "ui_widgets.h"


/* =============================================================================
 * HELPER FUNCTIONS
 * =============================================================================
 */

/*
 * Clamp uint32_t to 0-100 range (for percentages)
 */
static inline uint32_t clamp100(uint32_t v)
{
    return (v > 100) ? 100 : v;
}

/*
 * Local strlen to avoid external dependency
 */
static inline size_t ui_strlen(const char *s)
{
    if (!s) return 0;
    size_t len = 0;
    while (*s++) len++;
    return len;
}

/*
 * Local memcpy to avoid external dependency
 */
static inline void ui_memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
}

/*
 * Calculate how many characters fit in a given width
 */
static inline uint32_t chars_that_fit(uint32_t width, bool large)
{
    uint32_t char_w = large ? FB_CHAR_WIDTH_LG : FB_CHAR_WIDTH;
    return char_w > 0 ? width / char_w : 0;
}


/* =============================================================================
 * PANEL WIDGET
 * =============================================================================
 */

void ui_draw_panel(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    ui_panel_style_t style)
{
    uint32_t radius = theme->radii.md;

    /* Handle negative coordinates */
    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    switch (style) {
        case UI_PANEL_FLAT:
            fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, radius,
                                 theme->colors.bg_secondary);
            break;

        case UI_PANEL_ELEVATED: {
            /* Draw shadow first */
            uint32_t sx = x + (uint32_t)theme->shadow.offset_x;
            uint32_t sy = y + (uint32_t)theme->shadow.offset_y;
            fb_fill_rounded_rect(fb, sx, sy, bounds.w, bounds.h, radius,
                                 theme->shadow.color);
            /* Draw panel on top */
            fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, radius,
                                 ui_theme_panel_bg(theme));
            break;
        }

        case UI_PANEL_MODAL: {
            /* Larger shadow for modals */
            uint32_t sx = x + (uint32_t)(theme->shadow.offset_x * 2);
            uint32_t sy = y + (uint32_t)(theme->shadow.offset_y * 2);
            fb_fill_rounded_rect(fb, sx, sy, bounds.w, bounds.h, radius,
                                 theme->shadow.color);
            fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, radius,
                                 ui_theme_panel_bg(theme));
            break;
        }

        case UI_PANEL_OUTLINED:
            fb_draw_rounded_rect(fb, x, y, bounds.w, bounds.h, radius,
                                 ui_theme_panel_border(theme));
            break;
    }
}

void ui_draw_panel_with_header(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *title)
{
    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    uint32_t header_height = theme->line_height + theme->spacing.sm * 2;

    /* Draw panel background */
    ui_draw_panel(fb, bounds, theme, UI_PANEL_ELEVATED);

    /* Draw header background */
    uint32_t radius = theme->radii.md;
    fb_fill_rounded_rect(fb, x, y, bounds.w, header_height + radius, radius,
                         ui_theme_header_bg(theme));

    /* Draw title text */
    uint32_t text_x = x + theme->spacing.md;
    uint32_t text_y = y + theme->spacing.sm;
    fb_draw_string_transparent(fb, text_x, text_y, title,
                               ui_theme_header_text(theme));

    /* Draw divider under header */
    fb_draw_hline(fb, x, y + header_height, bounds.w, theme->colors.divider);
}


/* =============================================================================
 * BUTTON WIDGET
 * =============================================================================
 */

void ui_draw_button(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *label,
    ui_button_state_t state)
{
    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    uint32_t radius = theme->radii.sm;
    ui_color_t bg, fg;
    bool draw_shadow = true;

    switch (state) {
        case UI_BUTTON_NORMAL:
            bg = ui_theme_button_bg(theme);
            fg = ui_theme_button_text(theme);
            break;
        case UI_BUTTON_FOCUSED:
            bg = ui_theme_button_bg_focused(theme);
            fg = ui_theme_button_text(theme);
            break;
        case UI_BUTTON_PRESSED:
            bg = ui_theme_button_bg_pressed(theme);
            fg = ui_theme_button_text_pressed(theme);
            draw_shadow = false;
            break;
        case UI_BUTTON_DISABLED:
        default:
            bg = theme->colors.bg_secondary;
            fg = theme->colors.text_disabled;
            draw_shadow = false;
            break;
    }

    /* Draw shadow */
    if (draw_shadow) {
        uint32_t sy = y + (uint32_t)theme->shadow.offset_y;
        fb_fill_rounded_rect(fb, x, sy, bounds.w, bounds.h, radius,
                             theme->shadow.color);
    }

    /* Draw button background */
    fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, radius, bg);

    /* Center text */
    uint32_t text_w = fb_text_width(label);
    uint32_t text_h = FB_CHAR_HEIGHT;
    uint32_t text_x = x + (bounds.w - text_w) / 2;
    uint32_t text_y = y + (bounds.h - text_h) / 2;
    fb_draw_string_transparent(fb, text_x, text_y, label, fg);
}


/* =============================================================================
 * LIST ITEM WIDGET
 * =============================================================================
 */

void ui_draw_list_item(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *label,
    bool selected)
{
    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    uint32_t radius = theme->radii.sm;

    /* Selection highlight */
    if (selected) {
        fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, radius,
                             ui_theme_list_item_bg_selected(theme));
    }

    ui_color_t text_color = selected
        ? ui_theme_list_item_text_selected(theme)
        : ui_theme_list_item_text(theme);

    /* Selection indicator */
    uint32_t indicator_x = x + theme->spacing.xs;
    if (selected) {
        fb_draw_string_transparent(fb, indicator_x, y + theme->spacing.xs,
                                   ">", theme->colors.accent_bright);
    }

    /* Label text */
    uint32_t text_x = x + theme->spacing.md;
    uint32_t text_y = y + theme->spacing.xs;

    /* Truncate if needed */
    uint32_t max_chars = chars_that_fit(bounds.w - theme->spacing.lg, false);
    size_t label_len = ui_strlen(label);

    if (label_len > max_chars && max_chars > 3) {
        /* Draw truncated with ellipsis */
        char truncated[256];
        size_t copy_len = max_chars - 3;
        if (copy_len > sizeof(truncated) - 4) copy_len = sizeof(truncated) - 4;

        ui_memcpy(truncated, label, copy_len);
        truncated[copy_len] = '.';
        truncated[copy_len + 1] = '.';
        truncated[copy_len + 2] = '.';
        truncated[copy_len + 3] = '\0';

        fb_draw_string_transparent(fb, text_x, text_y, truncated, text_color);
    } else {
        fb_draw_string_transparent(fb, text_x, text_y, label, text_color);
    }
}

void ui_draw_list_item_with_badge(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *label,
    const char *badge,
    bool selected)
{
    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    uint32_t radius = theme->radii.sm;

    /* Selection highlight */
    if (selected) {
        fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, radius,
                             ui_theme_list_item_bg_selected(theme));
    }

    ui_color_t text_color = selected
        ? ui_theme_list_item_text_selected(theme)
        : ui_theme_list_item_text(theme);

    /* Selection indicator */
    uint32_t indicator_x = x + theme->spacing.xs;
    if (selected) {
        fb_draw_string_transparent(fb, indicator_x, y + theme->spacing.xs,
                                   ">", theme->colors.accent_bright);
    }

    /* Calculate badge size if present */
    uint32_t badge_width = 0;
    uint32_t badge_padding = theme->spacing.xs;
    if (badge && badge[0]) {
        badge_width = fb_text_width(badge) + badge_padding * 2 + theme->spacing.sm;
    }

    /* Label text with room for badge */
    uint32_t text_x = x + theme->spacing.md;
    uint32_t text_y = y + theme->spacing.xs;
    uint32_t available_width = bounds.w - theme->spacing.lg - badge_width;

    uint32_t max_chars = chars_that_fit(available_width, false);
    size_t label_len = ui_strlen(label);

    if (label_len > max_chars && max_chars > 3) {
        char truncated[256];
        size_t copy_len = max_chars - 3;
        if (copy_len > sizeof(truncated) - 4) copy_len = sizeof(truncated) - 4;

        ui_memcpy(truncated, label, copy_len);
        truncated[copy_len] = '.';
        truncated[copy_len + 1] = '.';
        truncated[copy_len + 2] = '.';
        truncated[copy_len + 3] = '\0';

        fb_draw_string_transparent(fb, text_x, text_y, truncated, text_color);
    } else {
        fb_draw_string_transparent(fb, text_x, text_y, label, text_color);
    }

    /* Draw badge if present */
    if (badge && badge[0]) {
        uint32_t badge_text_w = fb_text_width(badge);
        uint32_t badge_h = FB_CHAR_HEIGHT + badge_padding;
        uint32_t badge_w = badge_text_w + badge_padding * 2;
        uint32_t badge_x = x + bounds.w - badge_w - theme->spacing.sm;
        uint32_t badge_y = y + (bounds.h - badge_h) / 2;

        fb_fill_rounded_rect(fb, badge_x, badge_y, badge_w, badge_h,
                             theme->radii.sm, theme->colors.accent_dim);
        fb_draw_string_transparent(fb, badge_x + badge_padding,
                                   badge_y + badge_padding / 2,
                                   badge, theme->colors.text_on_accent);
    }
}


/* =============================================================================
 * BADGE WIDGET
 * =============================================================================
 */

void ui_draw_badge(
    framebuffer_t *fb,
    int32_t x,
    int32_t y,
    const ui_theme_t *theme,
    const char *label)
{
    if (x < 0 || y < 0 || !label) return;

    uint32_t padding_x = theme->spacing.xs;
    uint32_t padding_y = theme->spacing.xs / 2;

    uint32_t text_w = fb_text_width(label);
    uint32_t text_h = FB_CHAR_HEIGHT;

    uint32_t width = text_w + padding_x * 2;
    uint32_t height = text_h + padding_y * 2;

    /* Badge background */
    fb_fill_rounded_rect(fb, (uint32_t)x, (uint32_t)y, width, height,
                         theme->radii.sm, theme->colors.accent_bright);

    /* Badge text */
    fb_draw_string_transparent(fb, (uint32_t)x + padding_x,
                               (uint32_t)y + padding_y,
                               label, theme->colors.text_on_accent);
}


/* =============================================================================
 * SCROLLBAR WIDGET
 * =============================================================================
 */

void ui_draw_scrollbar_v(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    uint32_t scroll_offset,
    uint32_t visible_count,
    uint32_t total_count)
{
    if (total_count <= visible_count) {
        return;  /* No scrollbar needed */
    }

    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    uint32_t radius = bounds.w / 2;

    /* Track */
    fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, radius,
                         ui_theme_scrollbar_track(theme));

    /* Calculate thumb size */
    uint32_t thumb_height = (visible_count * bounds.h) / total_count;
    if (thumb_height < theme->spacing.md) {
        thumb_height = theme->spacing.md;
    }

    /* Calculate thumb position */
    uint32_t scroll_range = total_count - visible_count;
    uint32_t thumb_range = bounds.h - thumb_height;
    uint32_t thumb_offset = 0;
    if (scroll_range > 0 && thumb_range > 0) {
        thumb_offset = (scroll_offset * thumb_range) / scroll_range;
    }

    /* Thumb */
    fb_fill_rounded_rect(fb, x, y + thumb_offset, bounds.w, thumb_height,
                         radius, ui_theme_scrollbar_thumb(theme));
}


/* =============================================================================
 * PROGRESS BAR WIDGET
 * =============================================================================
 */

void ui_draw_progress_bar(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    uint32_t progress_pct,
    ui_color_t color)
{
    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    uint32_t radius = theme->radii.sm;
    ui_color_t fill_color = color ? color : theme->colors.accent;

    /* Background track */
    fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, radius,
                         ui_theme_scrollbar_track(theme));

    /* Clamp progress to 0-100 */
    progress_pct = clamp100(progress_pct);

    /* Fill */
    if (progress_pct > 0) {
        uint32_t fill_width = (progress_pct * bounds.w) / 100;
        if (fill_width < radius * 2) {
            fill_width = radius * 2;
        }

        fb_fill_rounded_rect(fb, x, y, fill_width, bounds.h, radius, fill_color);
    }
}


/* =============================================================================
 * DIVIDER WIDGET
 * =============================================================================
 */

void ui_draw_divider_h(
    framebuffer_t *fb,
    int32_t x,
    int32_t y,
    uint32_t width,
    const ui_theme_t *theme)
{
    if (x < 0 || y < 0) return;
    fb_draw_hline(fb, (uint32_t)x, (uint32_t)y, width, theme->colors.divider);
}

void ui_draw_divider_v(
    framebuffer_t *fb,
    int32_t x,
    int32_t y,
    uint32_t height,
    const ui_theme_t *theme)
{
    if (x < 0 || y < 0) return;
    fb_draw_vline(fb, (uint32_t)x, (uint32_t)y, height, theme->colors.divider);
}


/* =============================================================================
 * TOAST WIDGET
 * =============================================================================
 */

void ui_draw_toast(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *message,
    ui_toast_style_t style)
{
    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    ui_color_t bg, fg;

    switch (style) {
        case UI_TOAST_SUCCESS:
            bg = theme->colors.success;
            fg = theme->colors.text_on_accent;
            break;
        case UI_TOAST_WARNING:
            bg = theme->colors.warning;
            fg = theme->colors.text_on_accent;
            break;
        case UI_TOAST_ERROR:
            bg = theme->colors.error;
            fg = theme->colors.text_on_accent;
            break;
        case UI_TOAST_INFO:
        default:
            bg = theme->colors.info;
            fg = theme->colors.text_on_accent;
            break;
    }

    /* Background with shadow */
    uint32_t sx = x + (uint32_t)theme->shadow.offset_x;
    uint32_t sy = y + (uint32_t)theme->shadow.offset_y;
    fb_fill_rounded_rect(fb, sx, sy, bounds.w, bounds.h, theme->radii.md,
                         theme->shadow.color);
    fb_fill_rounded_rect(fb, x, y, bounds.w, bounds.h, theme->radii.md, bg);

    /* Centered text */
    uint32_t text_w = fb_text_width(message);
    uint32_t text_h = FB_CHAR_HEIGHT;
    uint32_t text_x = x + (bounds.w - text_w) / 2;
    uint32_t text_y = y + (bounds.h - text_h) / 2;
    fb_draw_string_transparent(fb, text_x, text_y, message, fg);
}


/* =============================================================================
 * HELP BAR WIDGET
 * =============================================================================
 */

void ui_draw_help_bar(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const ui_help_hint_t *hints,
    size_t hint_count)
{
    if (bounds.x < 0 || bounds.y < 0) return;

    uint32_t x = (uint32_t)bounds.x + theme->spacing.sm;
    uint32_t y = (uint32_t)bounds.y + theme->spacing.xs;

    for (size_t i = 0; i < hint_count; i++) {
        const ui_help_hint_t *hint = &hints[i];

        /* Key badge */
        uint32_t key_width = fb_text_width(hint->key) + theme->spacing.xs * 2;
        uint32_t key_height = theme->line_height + theme->spacing.xs;

        fb_fill_rounded_rect(fb, x, y, key_width, key_height, theme->radii.sm,
                             theme->colors.accent);

        fb_draw_string_transparent(fb, x + theme->spacing.xs,
                                   y + theme->spacing.xs / 2,
                                   hint->key, theme->colors.text_on_accent);

        x += key_width + 2;

        /* Action text */
        fb_draw_string_transparent(fb, x, y + theme->spacing.xs / 2,
                                   hint->action, theme->colors.text_secondary);

        x += fb_text_width(hint->action) + theme->spacing.sm;
    }
}


/* =============================================================================
 * SECTION HEADER WIDGET
 * =============================================================================
 */

void ui_draw_section_header(
    framebuffer_t *fb,
    ui_rect_t bounds,
    const ui_theme_t *theme,
    const char *title)
{
    if (bounds.x < 0 || bounds.y < 0) return;
    uint32_t x = (uint32_t)bounds.x;
    uint32_t y = (uint32_t)bounds.y;

    uint32_t text_y = y + theme->spacing.xs;

    /* Use small font (8x8 in this framebuffer) */
    fb_draw_string_transparent(fb, x, text_y, title, theme->colors.text_secondary);

    /* Underline */
    uint32_t text_width = fb_text_width(title);
    uint32_t line_y = text_y + FB_CHAR_HEIGHT + 1;

    fb_draw_hline(fb, x, line_y, text_width + theme->spacing.sm,
                  theme->colors.divider);
}