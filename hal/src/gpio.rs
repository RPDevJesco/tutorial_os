//! GPIO pin control abstraction.
//!
//! GPIO register layouts vary wildly across SoCs — BCM uses GPFSEL/GPSET/GPCLR,
//! BCM2712 routes through the RP1 southbridge over PCIe, RISC-V platforms have
//! entirely different pinmux schemes, and x86_64 has no traditional GPIO at all.
//!
//! The [`Gpio`] trait provides a uniform interface over all of these.  Where the
//! C API exposes per-peripheral configuration helpers (`hal_gpio_configure_dpi`,
//! `hal_gpio_configure_sdcard`, etc.), the Rust version groups them under
//! [`Gpio::configure_peripheral`] with a [`Peripheral`] enum discriminant.

use crate::types::{HalError, HalResult};

// ============================================================================
// Pin Function & Pull Configuration
// ============================================================================

/// Pin function mode.
///
/// `Input` and `Output` are universal.  The `Alt` variants map to
/// platform-specific alternate functions (e.g., ALT2 = DPI on BCM,
/// completely different on RISC-V).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Function {
    Input  = 0,
    Output = 1,
    Alt0   = 2,
    Alt1   = 3,
    Alt2   = 4,
    Alt3   = 5,
    Alt4   = 6,
    Alt5   = 7,
}

/// Pull resistor configuration.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Pull {
    /// No pull resistor (floating).
    None = 0,
    /// Pull-down to GND.
    Down = 1,
    /// Pull-up to VCC.
    Up   = 2,
}

/// Named peripheral configurations for [`Gpio::configure_peripheral`].
///
/// Rather than a separate method per peripheral (the C approach), Rust
/// uses a single method with an enum discriminant.  This makes it easy
/// to iterate, log, or extend without touching the trait definition.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum Peripheral {
    /// Parallel RGB display output (DPI).
    Dpi,
    /// HDMI output pin configuration.
    Hdmi,
    /// SD card / SDHOST pins.
    SdCard,
    /// PWM audio output pins.
    Audio,
    /// UART pins.  The `u32` selects the UART instance
    /// (0 = PL011, 1 = mini UART on BCM, etc.).
    Uart(u32),
    /// Safe-shutdown button (board-specific, e.g., GPi Case 2W GPIO 26).
    SafeShutdown,
}

// ============================================================================
// GPIO Trait
// ============================================================================

/// GPIO pin control contract.
///
/// Pin numbers are platform-specific: 0–53 on BCM, bank×32+offset on
/// Rockchip, etc.  The [`is_valid`](Gpio::is_valid) and
/// [`max_pin`](Gpio::max_pin) methods let portable code discover the range.
pub trait Gpio {
    // ---- Lifecycle ----

    /// Initialize the GPIO subsystem.
    ///
    /// On BCM2710 this is a no-op.  On BCM2712 it must wait for the RP1
    /// southbridge.  On RISC-V it may configure the pinmux controller.
    fn init(&self) -> HalResult<()> {
        Ok(())
    }

    // ---- Single-Pin Operations ----

    /// Set the function (mode) of a pin.
    fn set_function(&self, pin: u32, func: Function) -> HalResult<()>;

    /// Query the current function of a pin.
    fn get_function(&self, pin: u32) -> HalResult<Function>;

    /// Configure the pull resistor on a pin.
    fn set_pull(&self, pin: u32, pull: Pull) -> HalResult<()>;

    /// Drive an output pin high.
    fn set_high(&self, pin: u32) -> HalResult<()>;

    /// Drive an output pin low.
    fn set_low(&self, pin: u32) -> HalResult<()>;

    /// Set an output pin to `high` (`true`) or `low` (`false`).
    fn write(&self, pin: u32, high: bool) -> HalResult<()> {
        if high { self.set_high(pin) } else { self.set_low(pin) }
    }

    /// Read the current level of an input pin.
    ///
    /// Returns `false` for invalid pins (matches C behaviour).
    fn read(&self, pin: u32) -> bool;

    /// Toggle an output pin.
    fn toggle(&self, pin: u32) -> HalResult<()> {
        let level = self.read(pin);
        self.write(pin, !level)
    }

    // ---- Validation ----

    /// Returns `true` if `pin` is within the valid range for this platform.
    fn is_valid(&self, pin: u32) -> bool;

    /// Highest valid pin number (e.g., 53 for BCM2710).
    fn max_pin(&self) -> u32;

    // ---- Bulk Operations ----

    /// Set multiple pins high at once via bitmask.
    ///
    /// `bank` selects the 32-pin group (0 = pins 0–31, 1 = pins 32–63).
    fn set_mask(&self, mask: u32, bank: u32) -> HalResult<()>;

    /// Clear multiple pins at once via bitmask.
    fn clear_mask(&self, mask: u32, bank: u32) -> HalResult<()>;

    /// Read all pin levels in a bank as a bitmask.
    fn read_mask(&self, bank: u32) -> u32;

    // ---- Peripheral Configuration ----

    /// Configure a group of pins for a named peripheral function.
    ///
    /// Returns [`HalError::NotSupported`] if the peripheral is not
    /// available on the current platform.
    fn configure_peripheral(&self, peripheral: Peripheral) -> HalResult<()> {
        let _ = peripheral;
        Err(HalError::NotSupported)
    }

    /// Check if the safe-shutdown button has been triggered.
    ///
    /// Only meaningful on boards that have such a button (e.g., GPi Case 2W).
    fn safe_shutdown_triggered(&self) -> bool {
        false
    }
}
