//! Display and framebuffer abstraction.
//!
//! Only initialization, buffer management, and presentation are
//! platform-specific.  All drawing operations (`fb_clear`, `fb_fill_rect`,
//! `fb_draw_string`, etc.) live in the portable `drivers::framebuffer` crate
//! and operate on the [`FramebufferInfo`] returned by [`Display::init`].
//!
//! # Ownership Model
//!
//! The C API allocates a `framebuffer_t` internally and returns a pointer.
//! The Rust version returns a [`FramebufferInfo`] value that describes the
//! framebuffer region — the actual pixel memory is hardware-owned and
//! accessed through a raw pointer.  The `drivers::framebuffer` crate wraps
//! that pointer in a safe abstraction.
//!
//! # Presentation
//!
//! [`Display::present`] tells the GPU which buffer to scan out and returns
//! the index of the buffer the CPU should draw into next.  The caller is
//! responsible for updating the portable `Framebuffer` state via its
//! `swap_to()` method:
//!
//! ```ignore
//! let back = disp.present()?;
//! fb.swap_to(back);
//! ```
//!
//! This keeps the HAL trait free of driver types while giving the
//! `Framebuffer` driver full ownership of its own bookkeeping.

use crate::types::{HalError, HalResult};

// ============================================================================
// Display Types
// ============================================================================

/// Display output interface type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum DisplayType {
    /// No display connected.
    None      = 0,
    /// Parallel RGB (DPI) — e.g., GPi Case displays.
    Dpi       = 1,
    /// HDMI output.
    Hdmi      = 2,
    /// MIPI DSI serial display.
    Dsi       = 3,
    /// Composite video output.
    Composite = 4,
    /// SPI-attached display.
    Spi       = 5,
}

/// Pixel format of the framebuffer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PixelFormat {
    /// 32-bit with alpha channel (default for Tutorial-OS).
    Argb8888 = 0,
    /// 32-bit, alpha ignored.
    Xrgb8888 = 1,
    /// 24-bit packed RGB.
    Rgb888   = 2,
    /// 16-bit RGB565.
    Rgb565   = 3,
    /// 16-bit with 1-bit alpha.
    Argb1555 = 4,
}

impl PixelFormat {
    /// Bits per pixel for this format.
    pub const fn bits_per_pixel(self) -> u32 {
        match self {
            Self::Argb8888 | Self::Xrgb8888 => 32,
            Self::Rgb888 => 24,
            Self::Rgb565 | Self::Argb1555 => 16,
        }
    }
}

/// Requested display configuration (input to init).
#[derive(Debug, Clone, Copy)]
pub struct DisplayConfig {
    /// Desired width in pixels.
    pub width: u32,
    /// Desired height in pixels.
    pub height: u32,
    /// Output type (DPI, HDMI, etc.).
    pub display_type: DisplayType,
    /// Pixel format.
    pub format: PixelFormat,
    /// Number of framebuffers (1 = single, 2 = double-buffered).
    pub buffer_count: u32,
    /// Whether to synchronize presentation with vertical blank.
    pub vsync: bool,
}

impl Default for DisplayConfig {
    fn default() -> Self {
        Self {
            width: 640,
            height: 480,
            display_type: DisplayType::Hdmi,
            format: PixelFormat::Argb8888,
            buffer_count: 2,
            vsync: true,
        }
    }
}

/// Describes an allocated framebuffer (returned by display init).
///
/// This is a description, not an owner.  The pixel memory lives in a
/// hardware-allocated region (mailbox on BCM, SimpleFB on RISC-V,
/// GOP on x86).  The `base` pointer is valid for the lifetime of the
/// display session.
#[derive(Debug, Clone, Copy)]
pub struct FramebufferInfo {
    /// Width in pixels.
    pub width: u32,
    /// Height in pixels.
    pub height: u32,
    /// Bytes per row (may be larger than `width × bytes_per_pixel` due
    /// to hardware alignment).
    pub pitch: u32,
    /// Bits per pixel.
    pub bits_per_pixel: u32,
    /// Output type.
    pub display_type: DisplayType,
    /// Pixel format.
    pub format: PixelFormat,
    /// Base address of the framebuffer pixel data.
    ///
    /// # Safety
    ///
    /// This is a raw pointer to hardware-mapped memory.  All accesses
    /// must be volatile.  The `drivers::framebuffer` crate handles this.
    pub base: *mut u32,
    /// Total framebuffer allocation in bytes.
    pub size: u32,
    /// Number of buffers in the allocation.
    pub buffer_count: u32,
}

// Safety: The framebuffer base pointer refers to a hardware-owned memory
// region that is valid for the lifetime of the display session and is not
// aliased by Rust-managed memory.  Send is needed so the kernel can pass
// FramebufferInfo across initialization boundaries.
unsafe impl Send for FramebufferInfo {}

// ============================================================================
// Display Trait
// ============================================================================

/// Display initialization and control contract.
///
/// Drawing primitives are **not** part of this trait — they live in the
/// portable `drivers::framebuffer` crate.  This trait covers only the
/// operations that differ per platform: allocation, presentation, and
/// configuration.
pub trait Display {
    /// Initialize the display with board-specific defaults and return the
    /// framebuffer descriptor.
    ///
    /// Replaces both `hal_display_init` and `hal_display_init_with_size`
    /// from the C API.  Pass `None` for board defaults.
    fn init(&mut self, config: Option<&DisplayConfig>) -> HalResult<FramebufferInfo>;

    /// Shut down the display and release the framebuffer.
    fn shutdown(&mut self) -> HalResult<()>;

    /// Returns `true` once [`init`](Display::init) has completed successfully.
    fn is_initialized(&self) -> bool;

    // ---- Information ----

    /// Current display width, or 0 if not initialized.
    fn width(&self) -> u32;

    /// Current display height, or 0 if not initialized.
    fn height(&self) -> u32;

    /// Bytes per row, or 0 if not initialized.
    fn pitch(&self) -> u32;

    // ---- Presentation ----

    /// Flip to the next buffer and tell the GPU to scan it out.
    ///
    /// Returns the index of the buffer the CPU should draw into next
    /// (the new back buffer).  The caller passes this to the portable
    /// `Framebuffer::swap_to()` so the driver updates its own state.
    ///
    /// If vsync is enabled, this blocks until the next vertical blank.
    fn present(&mut self) -> HalResult<u32>;

    /// Present immediately without waiting for vsync.
    fn present_immediate(&mut self) -> HalResult<u32> {
        self.present()
    }

    /// Enable or disable vsync for subsequent [`present`](Display::present) calls.
    fn set_vsync(&mut self, _enabled: bool) -> HalResult<()> {
        Err(HalError::NotSupported)
    }

    /// Block until the next vertical blanking period.
    fn wait_vsync(&self) -> HalResult<()> {
        Err(HalError::NotSupported)
    }

    // ---- Configuration ----

    /// Board-specific default display configuration.
    ///
    /// For example, GPi Case 2W → 640×480 DPI; Pi 5 → 1920×1080 HDMI.
    fn default_config(&self) -> DisplayConfig;
}
