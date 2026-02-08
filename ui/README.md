# UI - Building a User Interface from Scratch

This directory contains a complete UI system built without any external
libraries. Everything from colors to widgets is implemented from first principles.

## 🎨 Architecture

```
ui/
├── core/           # Fundamental types (colors, rectangles)
│   ├── ui_types.h  # Color, Rect, alignment enums
│   └── ui_canvas.h # Canvas/TextRenderer interfaces
├── themes/         # Visual styling
│   └── ui_theme.h  # Palettes, spacing, radii
└── widgets/        # Reusable components
    ├── ui_widgets.h
    └── ui_widgets.c
```

## 🧱 Design Philosophy

### Immediate Mode UI
Traditional UI frameworks retain objects (buttons, labels, etc.) in memory.
We use "immediate mode" where widgets are just functions:

```c
// Retained mode (traditional)
Button *btn = create_button("Click me");
btn->set_position(100, 50);
btn->render();

// Immediate mode (our approach)
ui_draw_button(&renderer, bounds, &theme, "Click me", UI_BUTTON_NORMAL);
```

Benefits:
- No memory management for widgets
- State lives in your code, not the UI framework
- Easy to reason about
- No hidden dependencies

### Platform Agnostic
The UI code doesn't know about hardware. It uses interfaces:

```c
// Canvas interface (platform implements this)
typedef struct {
    void (*draw_pixel)(void *ctx, int x, int y, color_t color);
    void (*fill_rect)(void *ctx, rect_t rect, color_t color);
    // ...
} canvas_vtable_t;
```

This allows the same UI code to work on:
- Framebuffer displays
- HDMI output
- Emulated environments

### Theme-Driven Styling
All colors, spacing, and radii come from a theme:

```c
// Don't hardcode colors!
// Bad:  fill_rect(bounds, 0xFF3366FF);
// Good: fill_rect(bounds, theme.colors.accent);
```

This makes it easy to:
- Support dark/light modes
- Adapt to different resolutions
- Maintain visual consistency

## 📁 File Guide

### core/ui_types.h
Fundamental types used everywhere:

**Colors (ARGB8888)**
```c
ui_color_t c = ui_rgb(255, 128, 0);      // Orange
ui_color_t transparent = ui_argb(128, 0, 0, 0);  // 50% black
ui_color_t blended = ui_color_blend_over(dst, src);
```

**Rectangles**
```c
ui_rect_t r = ui_rect(10, 20, 100, 50);  // x, y, width, height
ui_rect_t inner = ui_rect_inset(r, 5);   // Shrink by 5px each side
ui_rect_t centered = ui_rect_center_in(outer, 50, 30);
```

**Alignment**
```c
ui_halign_t h = UI_ALIGN_CENTER;
ui_valign_t v = UI_ALIGN_MIDDLE;
ui_font_size_t f = UI_FONT_NORMAL;
```

### core/ui_canvas.h
Interface definitions for rendering:

**Canvas** - Basic drawing
```c
void (*fill_rect)(ctx, rect, color);
void (*draw_line)(ctx, x0, y0, x1, y1, color);
```

**StyledCanvas** - Advanced drawing
```c
void (*fill_rounded_rect)(ctx, rect, radius, color);
void (*fill_circle)(ctx, cx, cy, radius, color);
```

**TextRenderer** - Text output
```c
void (*draw_text)(ctx, x, y, text, color, font);
uint32_t (*measure_text)(ctx, text, font);
```

### themes/ui_theme.h
Visual styling constants:

**Color Palettes**
```c
UI_PALETTE_DARK     // Dark theme colors
UI_PALETTE_LIGHT    // Light theme colors
UI_PALETTE_GAMEBOY  // Classic GB green
UI_PALETTE_CYAN     // Teal accent theme
```

**Spacing**
```c
theme.spacing.xs    // 2-8px
theme.spacing.sm    // 4-16px
theme.spacing.md    // 8-24px
theme.spacing.lg    // 12-32px
```

**Border Radii**
```c
theme.radii.sm      // Buttons, inputs
theme.radii.md      // Cards, panels
theme.radii.lg      // Modals, dialogs
```

### widgets/ui_widgets.h/c
Reusable UI components:

| Widget | Purpose |
|--------|---------|
| `ui_draw_panel()` | Container background |
| `ui_draw_button()` | Clickable button |
| `ui_draw_list_item()` | Row in a list |
| `ui_draw_badge()` | Small label/tag |
| `ui_draw_scrollbar_v()` | Scroll indicator |
| `ui_draw_progress_bar()` | Progress indicator |
| `ui_draw_toast()` | Notification popup |
| `ui_draw_help_bar()` | Button hints |

## 🎯 Usage Example

```c
void draw_menu(ui_renderer_t *r, const ui_theme_t *theme) {
    // Background
    ui_rect_t screen = ui_canvas_bounds(&r->canvas);
    ui_canvas_fill_rect(&r->canvas, screen, theme->colors.bg_primary);
    
    // Title panel
    ui_rect_t header = ui_rect(10, 10, 300, 40);
    ui_draw_panel(&r->canvas, header, theme, UI_PANEL_ELEVATED);
    ui_text_draw(&r->text, 20, 20, "My Menu", 
                 theme->colors.text_primary, UI_FONT_NORMAL);
    
    // List items
    ui_rect_t item = ui_rect(10, 60, 300, 24);
    for (int i = 0; i < item_count; i++) {
        ui_draw_list_item(r, item, theme, items[i].name, i == selected);
        item.y += 26;
    }
    
    // Scrollbar
    ui_rect_t scrollbar = ui_rect(320, 60, 8, 200);
    ui_draw_scrollbar_v(&r->canvas, scrollbar, theme,
                        scroll_offset, visible_count, total_count);
}
```

## 🔧 Implementing a Platform Backend

To use this UI system, implement the vtables:

```c
// framebuffer implementation
static void my_fill_rect(void *ctx, ui_rect_t rect, ui_color_t color) {
    framebuffer_t *fb = ctx;
    for (int y = rect.y; y < rect.y + rect.h; y++) {
        for (int x = rect.x; x < rect.x + rect.w; x++) {
            fb->pixels[y * fb->width + x] = color;
        }
    }
}

// Build the vtable
static ui_canvas_vtable_t my_canvas_vt = {
    .fill_rect = my_fill_rect,
    .draw_pixel = my_draw_pixel,
    // ... other functions
};

// Create canvas
ui_canvas_t canvas = {
    .vt = &my_canvas_vt,
    .ctx = &my_framebuffer
};
```

## 📚 Further Reading

- "Immediate-Mode Graphical User Interfaces" - Casey Muratori
- "Dear ImGui" - ocornut (popular immediate mode library)
- "Retained vs Immediate Mode" - various game dev resources
