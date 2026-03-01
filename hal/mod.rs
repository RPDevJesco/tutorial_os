/*
 * hal/mod.rs — Hardware Abstraction Layer Master Module (Rust version of hal.h)
 *
 * Tutorial-OS: HAL Interface Definitions (Rust)
 *
 * This is the single module to import for HAL access.
 * It re-exports all HAL submodules.
 *
 * USAGE IN kernel/main.rs:
 *
 *   use hal::prelude::*;
 *
 *   fn kernel_main(dtb: usize, ram_base: usize, ram_size: usize) {
 *       // Initialize platform
 *       let mut platform = Platform::new();  // Board-specific type
 *       platform.init().expect("platform init failed");
 *
 *       // Initialize display
 *       let mut display = Display::new();
 *       let fb = display.init().expect("display init failed");
 *
 *       // Use existing drawing functions — they still work!
 *       unsafe {
 *           fb_clear(fb, 0xFF000000);
 *           fb_draw_string(fb, 10, 10, "Hello from HAL!\0".as_ptr(), 0xFFFFFFFF, 0xFF000000);
 *       }
 *
 *       // Present to screen
 *       display.present(fb).expect("present failed");
 *   }
 *
 * EQUIVALENT C:
 *   #include "hal/hal.h"
 */

#![no_std]
#![allow(dead_code)]

// =============================================================================
// HAL VERSION
// =============================================================================

pub const VERSION_MAJOR: u32    = 1;
pub const VERSION_MINOR: u32    = 0;
pub const VERSION_PATCH: u32    = 0;
pub const VERSION_STRING: &str  = "1.0.0";

// =============================================================================
// HAL SUBMODULES
// =============================================================================

/// Fundamental types, error codes, MMIO access, and memory barriers.
pub mod hal_types;

/// CPU operations contract (trait for platform-specific implementation).
pub mod hal_cpu;

/// Platform initialization and information (temperature, clocks, memory).
pub mod hal_platform;

/// Timer and delay functions.
pub mod hal_timer;

/// GPIO pin control abstraction.
pub mod hal_gpio;

/// Display/framebuffer abstraction.
pub mod hal_display;

// =============================================================================
// PRELUDE
// =============================================================================
//
// Import `use hal::prelude::*;` to get the most commonly used types and
// traits in scope. This mirrors the "include hal.h gets everything" pattern
// from C.

pub mod prelude {
    // Error handling
    pub use super::types::{HalError, HalResult};

    // Platform identification
    pub use super::types::{PlatformId, Arch};

    // MMIO access (unsafe, but frequently needed in OS code)
    pub use super::types::{
        mmio_read8, mmio_read16, mmio_read32, mmio_read64,
        mmio_write8, mmio_write16, mmio_write32, mmio_write64,
        mmio_read32_mb, mmio_write32_mb,
    };

    // Memory barriers
    pub use super::types::{dmb, dsb, isb, wfi, wfe, sev, cpu_relax};

    // Utility functions
    pub use super::types::{bit, bit64, align_up, align_down, is_aligned};

    // Traits — import these to call HAL methods on platform types
    pub use super::cpu::CpuOps;
    pub use super::platform::Platform;
    pub use super::timer::Timer;
    pub use super::gpio::Gpio;
    pub use super::display::Display;

    // Common data structures
    pub use super::platform::{
        PlatformInfo, MemoryInfo, ClockInfo, ClockId, DeviceId,
    };
    pub use super::timer::Stopwatch;
    pub use super::gpio::{GpioFunction, GpioPull};
    pub use super::display::{
        DisplayType, PixelFormat, DisplayConfig, DisplayInfo, Framebuffer,
    };
}

// =============================================================================
// CONVENIENCE MACROS
// =============================================================================

/// Standard initialization sequence.
///
/// Call this at the start of `kernel_main()` to initialize the platform.
/// Returns early with an error if initialization fails.
///
/// Rust equivalent of C's `HAL_INIT_ALL()` macro.
///
/// # Example
///
/// ```rust,ignore
/// fn kernel_main() -> HalResult<()> {
///     let mut platform = MyPlatform::new();
///     hal_init_all!(platform);
///     // ... rest of kernel
///     Ok(())
/// }
/// ```
#[macro_export]
macro_rules! hal_init_all {
    ($platform:expr) => {
        $platform.init()?;
    };
}

/// Check expression and panic on error.
///
/// Rust equivalent of C's `HAL_CHECK()` macro. In idiomatic Rust you'd
/// normally use `?` for propagation, but this is useful in contexts where
/// you want to halt on any error (like early boot).
///
/// # Example
///
/// ```rust,ignore
/// hal_check!(display.init(), platform);
/// ```
#[macro_export]
macro_rules! hal_check {
    ($expr:expr, $platform:expr) => {
        match $expr {
            Ok(val) => val,
            Err(_) => $platform.panic(concat!(stringify!($expr), " failed")),
        }
    };
}

// =============================================================================
// BUILD CONFIGURATION
// =============================================================================
//
// In the C version, these are #define directives set by the build system.
// In Rust, we use Cargo features for platform selection.
//
// Build with:
//   cargo build --features "board-rpi-zero2w-gpi"
//   cargo build --features "board-orangepi-rv2"
//   cargo build --features "board-lattepanda-iota"
//
// The feature flags map to conditional compilation:
//
//   #[cfg(feature = "soc-bcm2710")]
//   mod bcm2710;
//
//   #[cfg(feature = "soc-kyx1")]
//   mod kyx1;
//
// This is handled in the soc/ module, not here. The HAL interfaces
// are platform-independent by design.
