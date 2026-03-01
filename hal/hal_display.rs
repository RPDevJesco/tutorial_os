/*
 * hal/hal_display — Display/Framebuffer Abstraction
 *
 * Tutorial-OS: HAL Interface Definitions (Rust)
 *
 * This module abstracts display initialization and buffer management.
 * Only the initialization and buffer swap operations are platform-specific.
 *
 * KEY DESIGN:
 *   The framebuffer_t struct and all fb_*() drawing functions remain in
 *   their existing locations (drivers/framebuffer/). This module only
 *   abstracts the two things that differ between platforms:
 *     1. How the framebuffer is allocated (mailbox, VOP2, DE3, etc.)
 *     2. How buffers are swapped (vsync mechanism)
 *
 *   Drawing code is completely portable and stays unchanged!
 */

#![allow(dead_code)]

use super::types::HalResult;

// =============================================================================
// DISPLAY TYPES
// =============================================================================

/// Display output type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum DisplayType {
    /// No display
    None        = 0,
    /// Parallel RGB (GPi Case, DPI HATs)
    Dpi         = 1,
    /// HDMI output
    Hdmi        = 2,
    /// MIPI DSI (Pi display)
    Dsi         = 3,
    /// Composite video
    Composite   = 4,
    /// SPI display
    Spi         = 5,
}

/// Pixel format.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PixelFormat {
    /// 32-bit with alpha (default)
    Argb8888    = 0,
    /// 32-bit, alpha ignored
    Xrgb8888    = 1,
    /// 24-bit packed
    Rgb888      = 2,
    /// 16-bit
    Rgb565      = 3,
    /// 16-bit with 1-bit alpha
    Argb1555    = 4,
}

/// Display configuration — what we WANT.
///
/// Pass this to `init_with_config()` to request specific display parameters.
#[derive(Debug, Clone, Copy)]
pub struct DisplayConfig {
    /// Display width in pixels
    pub width: u32,
    /// Display height in pixels
    pub height: u32,
    /// Output type (DPI, HDMI, etc.)
    pub display_type: DisplayType,
    /// Pixel format
    pub format: PixelFormat,
    /// Number of buffers (1 = single, 2 = double-buffered)
    pub buffer_count: u32,
    /// Wait for vsync on present
    pub vsync_enabled: bool,
}

impl DisplayConfig {
    /// Create a sensible default configuration.
    ///
    /// 640x480, DPI, ARGB8888, double-buffered, vsync on.
    pub const fn default() -> Self {
        Self {
            width: 640,
            height: 480,
            display_type: DisplayType::Dpi,
            format: PixelFormat::Argb8888,
            buffer_count: 2,
            vsync_enabled: true,
        }
    }
}

/// Display information — what we GOT (read-only).
///
/// Returned by `get_info()` after display initialization.
#[derive(Debug, Clone, Copy)]
pub struct DisplayInfo {
    /// Actual width in pixels
    pub width: u32,
    /// Actual height in pixels
    pub height: u32,
    /// Bytes per row
    pub pitch: u32,
    /// Bits per pixel (32 for ARGB8888)
    pub bits_per_pixel: u32,
    /// Output type
    pub display_type: DisplayType,
    /// Pixel format
    pub format: PixelFormat,
    /// Base address of framebuffer
    pub framebuffer_base: usize,
    /// Total framebuffer size in bytes
    pub framebuffer_size: u32,
    /// Number of buffers
    pub buffer_count: u32,
}

// =============================================================================
// FRAMEBUFFER REFERENCE
// =============================================================================
//
// The actual framebuffer struct lives in drivers/framebuffer/ and is shared
// between C and Rust via FFI. We use an opaque pointer here to avoid
// duplicating the struct definition.

/// Opaque framebuffer handle.
///
/// The actual `framebuffer_t` struct is defined in `drivers/framebuffer/framebuffer.h`.
/// We reference it here as an opaque type. Platform implementations return a
/// pointer to an initialized framebuffer, and all `fb_*()` drawing functions
/// accept this same pointer.
///
/// In a pure Rust implementation, this would be a full struct. In the
/// intertwined C/Rust codebase, it's an opaque FFI type.
#[repr(C)]
pub struct Framebuffer {
    _opaque: [u8; 0],
}

// =============================================================================
// DISPLAY TRAIT
// =============================================================================

/// Display operations contract.
///
/// Abstracts framebuffer allocation and buffer management. Drawing functions
/// (`fb_clear`, `fb_draw_string`, etc.) are NOT part of this trait — they
/// work on the `Framebuffer` pointer and are completely portable.
///
/// # The Two Things That Change
///
/// Only two operations differ between platforms:
///   1. **Initialization** — how the framebuffer is allocated
///   2. **Present** — how buffers are swapped to the display
///
/// Everything else (drawing, text, shapes, bitmaps) is portable!
pub trait Display {
    // =========================================================================
    // INITIALIZATION
    // =========================================================================

    /// Initialize display with default settings.
    ///
    /// Uses board-specific defaults:
    ///   - GPi Case 2W: 640x480 DPI
    ///   - Pi 5 with HDMI: 1920x1080 HDMI
    ///   - etc.
    ///
    /// Returns a pointer to the initialized framebuffer.
    fn init(&mut self) -> HalResult<*mut Framebuffer>;

    /// Initialize display with specific size.
    fn init_with_size(&mut self, width: u32, height: u32)
        -> HalResult<*mut Framebuffer>;

    /// Initialize display with full configuration.
    fn init_with_config(&mut self, config: &DisplayConfig)
        -> HalResult<*mut Framebuffer>;

    /// Shutdown display and release resources.
    fn shutdown(&mut self) -> HalResult<()>;

    /// Check if display is initialized.
    fn is_initialized(&self) -> bool;

    // =========================================================================
    // DISPLAY INFORMATION
    // =========================================================================

    /// Get display information.
    fn get_info(&self) -> HalResult<DisplayInfo>;

    /// Get display width in pixels (0 if not initialized).
    fn get_width(&self) -> u32;

    /// Get display height in pixels (0 if not initialized).
    fn get_height(&self) -> u32;

    /// Get framebuffer pitch (bytes per row, 0 if not initialized).
    fn get_pitch(&self) -> u32;

    // =========================================================================
    // DISPLAY CONTROL
    // =========================================================================

    /// Present the back buffer (swap and display).
    ///
    /// If double-buffering is enabled, swaps front and back buffers.
    /// If vsync is enabled, waits for vertical blank first.
    ///
    /// Maps to: `fb_present(fb)`
    fn present(&self, fb: *mut Framebuffer) -> HalResult<()>;

    /// Present immediately without vsync.
    ///
    /// Maps to: `fb_present_immediate(fb)`
    fn present_immediate(&self, fb: *mut Framebuffer) -> HalResult<()>;

    /// Enable/disable vsync.
    ///
    /// Maps to: `fb_set_vsync(fb, enabled)`
    fn set_vsync(&self, fb: *mut Framebuffer, enabled: bool) -> HalResult<()>;

    /// Wait for vertical blank.
    ///
    /// Blocks until the next vertical blanking period.
    /// Returns `Err(HalError::NotSupported)` if not available.
    fn wait_vsync(&self) -> HalResult<()>;

    // =========================================================================
    // DEFAULT CONFIGURATION
    // =========================================================================

    /// Get default display configuration for current board.
    ///
    /// Returns appropriate defaults:
    ///   - GPi Case 2W: 640x480, DPI, double-buffered
    ///   - Pi 5: 1920x1080, HDMI, double-buffered
    ///   - etc.
    fn get_default_config(&self) -> DisplayConfig;
}

// =============================================================================
// NOTES ON COMPATIBILITY
// =============================================================================
//
// All the fb_*() drawing functions continue to work exactly as before:
//   - fb_clear(fb, color)
//   - fb_fill_rect(fb, x, y, w, h, color)
//   - fb_draw_string(fb, x, y, str, fg, bg)
//   - fb_blit_bitmap(fb, x, y, bitmap, blend)
//   - etc.
//
// They operate on the Framebuffer pointer that Display::init() returns.
//
// The ONLY changes from the caller's perspective:
//   1. Initialization: fb_init(&fb) -> display.init()
//   2. Present: fb_present(&fb) -> display.present(fb)
//
// Everything else stays exactly the same!
//
// =============================================================================
// PLATFORM-SPECIFIC IMPLEMENTATION NOTES
// =============================================================================
//
// BCM2710/BCM2711 (Pi Zero 2W, Pi 4, CM4):
//   - Uses VideoCore mailbox to allocate framebuffer
//   - Vsync via mailbox tag
//
// BCM2712 (Pi 5, CM5):
//   - Similar mailbox interface but different tags
//   - HDMI encoder on RP1 for HDMI output
//
// RK3528A (Rock 2A):
//   - VOP2 (Video Output Processor) configuration
//   - HDMI encoder setup
//   - No mailbox — direct register programming
//
// S905X (Le Potato):
//   - Amlogic VPU configuration
//   - Canvas + OSD layer setup
//
// H618 (KICKPI K2B):
//   - DE3 (Display Engine 3) configuration
//   - HDMI or CVBS output
//
// K1 RISC-V (Orange Pi RV2):
//   - SpacemiT display controller
//   - SimpleFB via U-Boot
//
// Intel N150 (LattePanda IOTA):
//   - UEFI GOP (Graphics Output Protocol) framebuffer
//   - Or direct Intel HD Graphics programming
