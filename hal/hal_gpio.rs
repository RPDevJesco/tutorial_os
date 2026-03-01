/*
 * hal/hal_gpio — GPIO Pin Control Abstraction
 *
 * Tutorial-OS: HAL Interface Definitions (Rust)
 *
 * This module abstracts GPIO differences between platforms:
 *
 *   BCM2710/BCM2711:  Direct BCM GPIO registers
 *   BCM2712:          RP1 southbridge GPIO (via PCIe!)
 *   RK3528A:          Rockchip GPIO + pinmux
 *   S905X:            Amlogic GPIO/PINMUX
 *   H618:             Allwinner sunxi GPIO
 *   K1:               SpacemiT GPIO
 *
 * In Rust, we use enums instead of integer constants for pin modes,
 * and Result<(), HalError> instead of error code returns. The compiler
 * enforces that all modes are handled in match statements.
 */

#![allow(dead_code)]

use super::types::HalResult;

// =============================================================================
// GPIO TYPES
// =============================================================================

/// GPIO pin function/mode.
///
/// Generic function definitions that map to platform-specific register values.
/// Using an enum prevents passing invalid mode numbers at compile time.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum GpioFunction {
    /// Digital input
    Input   = 0,
    /// Digital output
    Output  = 1,
    /// Alternate function 0
    Alt0    = 2,
    /// Alternate function 1
    Alt1    = 3,
    /// Alternate function 2 (DPI on BCM)
    Alt2    = 4,
    /// Alternate function 3
    Alt3    = 5,
    /// Alternate function 4
    Alt4    = 6,
    /// Alternate function 5 (PWM on BCM)
    Alt5    = 7,
}

/// Pull resistor configuration.
///
/// Matches existing `gpio_pull_t` enum from the C codebase.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum GpioPull {
    /// No pull resistor (floating)
    None    = 0,
    /// Pull-down resistor to GND
    Down    = 1,
    /// Pull-up resistor to VCC
    Up      = 2,
}

// =============================================================================
// GPIO TRAIT
// =============================================================================
//
// Every platform implements this trait for its GPIO hardware.

/// GPIO operations contract.
///
/// Provides pin configuration, read/write, bulk operations, and
/// peripheral configuration (DPI, HDMI, SD, audio, UART).
pub trait Gpio {
    // =========================================================================
    // INITIALIZATION
    // =========================================================================

    /// Initialize GPIO subsystem.
    ///
    /// Called during `hal_platform_init()`. On BCM2710, this is a no-op.
    /// On BCM2712, this must wait for RP1 initialization.
    fn init(&mut self) -> HalResult<()>;

    // =========================================================================
    // BASIC PIN OPERATIONS
    // =========================================================================

    /// Set pin function/mode.
    ///
    /// Maps to: `gpio_set_function(pin, function)`
    ///
    /// Returns `Err(HalError::GpioInvalidPin)` if pin is out of range.
    fn set_function(&self, pin: u32, function: GpioFunction) -> HalResult<()>;

    /// Get current pin function.
    fn get_function(&self, pin: u32) -> HalResult<GpioFunction>;

    /// Set pull resistor configuration.
    ///
    /// Maps to: `gpio_set_pull(pin, pull)`
    fn set_pull(&self, pin: u32, pull: GpioPull) -> HalResult<()>;

    /// Set output pin HIGH.
    ///
    /// Maps to: `gpio_set_high(pin)`
    fn set_high(&self, pin: u32) -> HalResult<()>;

    /// Set output pin LOW.
    ///
    /// Maps to: `gpio_set_low(pin)`
    fn set_low(&self, pin: u32) -> HalResult<()>;

    /// Write value to output pin.
    ///
    /// `true` = HIGH, `false` = LOW.
    fn write(&self, pin: u32, value: bool) -> HalResult<()> {
        if value {
            self.set_high(pin)
        } else {
            self.set_low(pin)
        }
    }

    /// Read input pin level.
    ///
    /// Returns `true` for HIGH, `false` for LOW.
    /// Returns `false` for invalid pins (matches C behavior).
    fn read(&self, pin: u32) -> bool;

    /// Toggle output pin.
    fn toggle(&self, pin: u32) -> HalResult<()>;

    // =========================================================================
    // PIN VALIDATION
    // =========================================================================

    /// Check if pin number is valid for this platform.
    fn is_valid(&self, pin: u32) -> bool;

    /// Get maximum pin number for this platform.
    ///
    /// Returns 53 for BCM2710, 57 for BCM2711, etc.
    fn max_pin(&self) -> u32;

    // =========================================================================
    // BULK OPERATIONS
    // =========================================================================
    //
    // For efficiency when manipulating multiple pins at once.

    /// Set multiple pins high at once.
    ///
    /// `mask` is a bitmask of pins to set (pin 0 = bit 0, etc.).
    /// `bank` selects the pin bank (0 = pins 0-31, 1 = pins 32-63).
    fn set_mask(&self, mask: u32, bank: u32) -> HalResult<()>;

    /// Clear multiple pins at once.
    fn clear_mask(&self, mask: u32, bank: u32) -> HalResult<()>;

    /// Read multiple pins at once.
    ///
    /// Returns a bitmask of pin levels.
    fn read_mask(&self, bank: u32) -> u32;

    // =========================================================================
    // PERIPHERAL CONFIGURATION
    // =========================================================================
    //
    // These configure groups of pins for specific hardware functions.
    // Maps to existing gpio_configure_for_*() functions.

    /// Configure pins for DPI display output.
    ///
    /// On GPi Case 2W: GPIO 0-17 and 20-27 to ALT2,
    /// skipping GPIO 18-19 for audio.
    ///
    /// Returns `Err(HalError::NotSupported)` on platforms without DPI.
    fn configure_dpi(&self) -> HalResult<()>;

    /// Configure pins for HDMI output.
    ///
    /// On BCM2710, HDMI doesn't require GPIO configuration.
    /// On other platforms, may need pin setup.
    fn configure_hdmi(&self) -> HalResult<()>;

    /// Configure pins for SD card.
    ///
    /// On BCM2710: GPIO 48-53 to ALT0 for SDHOST.
    fn configure_sdcard(&self) -> HalResult<()>;

    /// Configure pins for PWM audio output.
    ///
    /// On BCM2710: GPIO 18-19 to ALT5 for PWM0/PWM1.
    fn configure_audio(&self) -> HalResult<()>;

    /// Configure pins for UART.
    ///
    /// `uart_num`: 0 = PL011, 1 = mini UART on BCM.
    fn configure_uart(&self, uart_num: u32) -> HalResult<()>;

    // =========================================================================
    // SAFE SHUTDOWN PIN (Board-specific)
    // =========================================================================

    /// Configure safe shutdown monitoring.
    ///
    /// On GPi Case 2W: GPIO 26 triggers safe shutdown when pressed.
    ///
    /// Returns `Err(HalError::NotSupported)` on boards without this feature.
    fn configure_safe_shutdown(&self) -> HalResult<()>;

    /// Check if safe shutdown has been triggered.
    fn safe_shutdown_triggered(&self) -> bool;
}

// =============================================================================
// PLATFORM-SPECIFIC NOTES
// =============================================================================
//
// BCM2710/BCM2711 (Pi Zero 2W, Pi 3, Pi 4, CM4):
//   - 54 GPIO pins (0-53)
//   - Function values: 000=IN, 001=OUT, 100=ALT0, 101=ALT1, etc.
//   - Pull config requires GPPUD + GPPUDCLK sequence (BCM2710)
//   - Pull config via direct 2-bit registers (BCM2711, simpler!)
//
// BCM2712 (Pi 5, CM5):
//   - GPIO is on RP1 southbridge chip (via PCIe!)
//   - Must initialize PCIe first
//   - Different register layout than BCM2710
//   - 28 GPIO pins directly accessible
//
// RK3528A (Rock 2A):
//   - 5 GPIO banks (GPIO0-GPIO4), 32 pins each = 160 pins
//   - Separate IOMUX/pinmux configuration in GRF
//   - Different pull resistor mechanism
//
// S905X (Le Potato):
//   - Multiple GPIO banks (GPIOX, GPIOY, GPIOAO, etc.)
//   - Complex pinmux in different register space
//
// H618 (KICKPI K2B):
//   - Allwinner GPIO ports (PA, PB, PC, ...)
//   - 4-bit function select per pin
//   - Separate pull/drive strength config
//
// K1 RISC-V (Orange Pi RV2):
//   - SpacemiT GPIO controller
//   - Different register layout entirely
