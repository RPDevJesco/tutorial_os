/*
 * hal/hal_display.h - Display/Framebuffer Abstraction
 *
 * Tutorial-OS: HAL Interface Definitions
 *
 * This header abstracts display initialization and buffer management.
 * Only the initialization and buffer swap operations are platform-specific.
 *
 */

#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include "hal_types.h"

/* =============================================================================
 * FORWARD DECLARATIONS
 * =============================================================================
 * We forward-declare it here so the HAL can return a pointer to it.
 */

/* Forward declaration - actual struct is in framebuffer.h */
struct framebuffer;
typedef struct framebuffer framebuffer_t;

/* =============================================================================
 * DISPLAY TYPES
 * =============================================================================
 */

/*
 * Display output type
 */
typedef enum {
    HAL_DISPLAY_TYPE_NONE       = 0,    /* No display */
    HAL_DISPLAY_TYPE_DPI        = 1,    /* Parallel RGB (GPi Case, DPI HATs) */
    HAL_DISPLAY_TYPE_HDMI       = 2,    /* HDMI output */
    HAL_DISPLAY_TYPE_DSI        = 3,    /* MIPI DSI (Pi display) */
    HAL_DISPLAY_TYPE_COMPOSITE  = 4,    /* Composite video */
    HAL_DISPLAY_TYPE_SPI        = 5,    /* SPI display */
} hal_display_type_t;

/*
 * Pixel format
 */
typedef enum {
    HAL_PIXEL_FORMAT_ARGB8888   = 0,    /* 32-bit with alpha (default) */
    HAL_PIXEL_FORMAT_XRGB8888   = 1,    /* 32-bit, alpha ignored */
    HAL_PIXEL_FORMAT_RGB888     = 2,    /* 24-bit packed */
    HAL_PIXEL_FORMAT_RGB565     = 3,    /* 16-bit */
    HAL_PIXEL_FORMAT_ARGB1555   = 4,    /* 16-bit with 1-bit alpha */
} hal_pixel_format_t;

/*
 * Display configuration
 */
typedef struct {
    uint32_t width;                     /* Display width in pixels */
    uint32_t height;                    /* Display height in pixels */
    hal_display_type_t type;            /* Output type (DPI, HDMI, etc.) */
    hal_pixel_format_t format;          /* Pixel format */
    uint32_t buffer_count;              /* Number of buffers (1 or 2) */
    bool vsync_enabled;                 /* Wait for vsync on present */
} hal_display_config_t;

/*
 * Display information (read-only)
 */
typedef struct {
    uint32_t width;                     /* Actual width */
    uint32_t height;                    /* Actual height */
    uint32_t pitch;                     /* Bytes per row */
    uint32_t bits_per_pixel;            /* Bits per pixel (32 for ARGB8888) */
    hal_display_type_t type;            /* Output type */
    hal_pixel_format_t format;          /* Pixel format */
    void *framebuffer_base;             /* Base address of framebuffer */
    uint32_t framebuffer_size;          /* Total framebuffer size in bytes */
    uint32_t buffer_count;              /* Number of buffers */
} hal_display_info_t;

/* =============================================================================
 * DISPLAY INITIALIZATION
 * =============================================================================
 */

/*
 * Initialize display with default settings
 *
 * Uses board-specific defaults:
 *   - GPi Case 2W: 640x480 DPI
 *   - Pi 5 with HDMI: 1920x1080 HDMI
 *   - etc.
 *
 * This allocates and initializes a framebuffer_t internally.
 *
 * @param fb_out    Output: pointer to initialized framebuffer
 * @return          HAL_SUCCESS or error code
 *
 */
hal_error_t hal_display_init(framebuffer_t **fb_out);

/*
 * Initialize display with specific size
 *
 * @param width     Desired width in pixels
 * @param height    Desired height in pixels
 * @param fb_out    Output: pointer to initialized framebuffer
 * @return          HAL_SUCCESS or error code
 *
 */
hal_error_t hal_display_init_with_size(uint32_t width, uint32_t height,
                                       framebuffer_t **fb_out);

/*
 * Initialize display with full configuration
 *
 * @param config    Display configuration
 * @param fb_out    Output: pointer to initialized framebuffer
 * @return          HAL_SUCCESS or error code
 */
hal_error_t hal_display_init_with_config(const hal_display_config_t *config,
                                         framebuffer_t **fb_out);

/*
 * Shutdown display
 *
 * Releases framebuffer memory and disables display output.
 *
 * @return  HAL_SUCCESS or error code
 */
hal_error_t hal_display_shutdown(void);

/*
 * Check if display is initialized
 *
 * @return  true if display is ready
 */
bool hal_display_is_initialized(void);

/* =============================================================================
 * DISPLAY INFORMATION
 * =============================================================================
 */

/*
 * Get display information
 *
 * @param info  Output: display info structure
 * @return      HAL_SUCCESS or HAL_ERROR_NOT_INIT
 */
hal_error_t hal_display_get_info(hal_display_info_t *info);

/*
 * Get display width
 *
 * @return  Width in pixels, or 0 if not initialized
 */
uint32_t hal_display_get_width(void);

/*
 * Get display height
 *
 * @return  Height in pixels, or 0 if not initialized
 */
uint32_t hal_display_get_height(void);

/*
 * Get framebuffer pitch (bytes per row)
 *
 * @return  Pitch in bytes, or 0 if not initialized
 */
uint32_t hal_display_get_pitch(void);

/* =============================================================================
 * DISPLAY CONTROL
 * =============================================================================
 */

/*
 * Present the back buffer (swap and display)
 *
 * If double-buffering is enabled, swaps front and back buffers.
 * If vsync is enabled, waits for vertical blank first.
 *
 * Maps to : fb_present(fb)
 *
 * @param fb    Framebuffer (from hal_display_init)
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_display_present(framebuffer_t *fb);

/*
 * Present immediately without vsync
 *
 * Maps to: fb_present_immediate(fb)
 *
 * @param fb    Framebuffer
 * @return      HAL_SUCCESS or error code
 */
hal_error_t hal_display_present_immediate(framebuffer_t *fb);

/*
 * Enable/disable vsync
 *
 * Maps to: fb_set_vsync(fb, enabled)
 *
 * @param fb        Framebuffer
 * @param enabled   true to wait for vsync on present
 * @return          HAL_SUCCESS or error code
 */
hal_error_t hal_display_set_vsync(framebuffer_t *fb, bool enabled);

/*
 * Wait for vertical blank
 *
 * Blocks until the next vertical blanking period.
 * Useful for timing-critical updates.
 *
 * @return  HAL_SUCCESS or HAL_ERROR_NOT_SUPPORTED
 */
hal_error_t hal_display_wait_vsync(void);

/* =============================================================================
 * DEFAULT CONFIGURATION
 * =============================================================================
 * Board-specific default configurations.
 */

/*
 * Get default display configuration for current board
 *
 * Fills in appropriate defaults:
 *   - GPi Case 2W: 640x480, DPI, double-buffered
 *   - Pi 5: 1920x1080, HDMI, double-buffered
 *   - etc.
 *
 * @param config    Output: default configuration
 * @return          HAL_SUCCESS
 */
hal_error_t hal_display_get_default_config(hal_display_config_t *config);

/* =============================================================================
 * NOTES ON COMPATIBILITY
 * =============================================================================
 *
 * All the fb_*() functions in framebuffer.c continue to work exactly as before:
 *   - fb_clear(fb, color)
 *   - fb_fill_rect(fb, x, y, w, h, color)
 *   - fb_draw_string(fb, x, y, str, fg, bg)
 *   - fb_blit_bitmap(fb, x, y, bitmap, blend)
 *   - etc.
 *
 * They operate on the framebuffer_t* that hal_display_init() returns.
 *
 * The ONLY changes are:
 *   1. Initialization: fb_init(&fb) -> hal_display_init(&fb)
 *   2. Present: fb_present(&fb) -> hal_display_present(fb)
 *
 * Everything else stays exactly the same!
 *
 * PLATFORM-SPECIFIC IMPLEMENTATION:
 *
 * BCM2710/BCM2711 (Pi Zero 2W, Pi 4, CM4):
 *   - Uses VideoCore mailbox to allocate framebuffer
 *   - Uses existing mailbox-based code in framebuffer.c
 *   - Vsync via mailbox tag
 *
 * bcm2712 (Pi 5, CM5):
 *   - Similar mailbox interface but different tags
 *   - HDMI encoder on RP1 for HDMI output
 *
 * RK3528A (Rock 2A):
 *   - VOP2 (Video Output Processor) configuration
 *   - HDMI encoder setup
 *   - No mailbox - direct register programming
 *
 * S905X (Le Potato):
 *   - Amlogic VPU configuration
 *   - Canvas + OSD layer setup
 *
 * H618 (KICKPI K2B):
 *   - DE3 (Display Engine 3) configuration
 *   - HDMI or CVBS output
 *
 * K1 RISC-V (Orange Pi RV2):
 *   - SpacemiT display controller
 *   - HDMI output
 */

#endif /* HAL_DISPLAY_H */
