//! Platform initialization, identification, and information queries.
//!
//! The [`Platform`] trait is the broadest HAL contract — it covers everything
//! from board identification to temperature monitoring to power management.
//! In the C implementation these are free functions backed by a SoC-specific
//! `soc_init.c`; in Rust they become methods on a concrete struct that each
//! SoC crate provides.
//!
//! # Information Structs
//!
//! Where the C API uses output parameters (`hal_platform_get_info(info *)`),
//! Rust returns owned structs directly. This eliminates the possibility of
//! uninitialized fields and makes the call sites cleaner.

use crate::display::Display;
use crate::timer::Timer;
pub use crate::types::{Arch, HalError, HalResult, PlatformId};

// ============================================================================
// Information Structs
// ============================================================================

/// Static identification for the running board.
#[derive(Debug, Clone)]
pub struct PlatformInfo {
    pub platform_id: PlatformId,
    pub arch: Arch,
    pub board_name: &'static str,
    pub soc_name: &'static str,
    pub board_revision: u32,
    pub serial_number: u64,
}

/// Memory layout as reported by the platform.
#[derive(Debug, Clone, Copy)]
pub struct MemoryInfo {
    /// Base address of ARM-accessible RAM.
    pub arm_base: usize,
    /// Size of ARM-accessible RAM in bytes.
    pub arm_size: usize,
    /// Base address of the peripheral register space.
    pub peripheral_base: usize,
    /// GPU/coprocessor memory base (zero if not applicable).
    pub gpu_base: usize,
    /// GPU/coprocessor memory size in bytes.
    pub gpu_size: usize,
}

/// Clock frequencies reported by the platform.
#[derive(Debug, Clone, Copy)]
pub struct ClockInfo {
    /// ARM CPU frequency in Hz.
    pub arm_freq_hz: u32,
    /// Core/GPU clock frequency in Hz.
    pub core_freq_hz: u32,
    /// UART reference clock in Hz.
    pub uart_freq_hz: u32,
    /// eMMC/SD controller clock in Hz.
    pub emmc_freq_hz: u32,
}

/// Named clock sources for detailed queries.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ClockId {
    Arm   = 0,
    Core  = 1,
    Uart  = 2,
    Emmc  = 3,
    Pwm   = 4,
    Pixel = 5,
}

/// Named peripheral devices for power management.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum DeviceId {
    SdCard = 0,
    Uart0  = 1,
    Uart1  = 2,
    Usb    = 3,
    I2c0   = 4,
    I2c1   = 5,
    I2c2   = 6,
    Spi    = 7,
    Pwm    = 8,
}

// ============================================================================
// Throttle Status
// ============================================================================

/// Throttle status flags reported by the platform.
///
/// Matches the BCM mailbox `GET_THROTTLED` response bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ThrottleFlags(pub u32);

impl ThrottleFlags {
    pub const UNDERVOLT_NOW: u32       = 1 << 0;
    pub const ARM_FREQ_CAPPED: u32     = 1 << 1;
    pub const THROTTLED_NOW: u32       = 1 << 2;
    pub const SOFT_TEMP_LIMIT: u32     = 1 << 3;
    pub const UNDERVOLT_OCCURRED: u32  = 1 << 16;
    pub const FREQ_CAP_OCCURRED: u32   = 1 << 17;
    pub const THROTTLE_OCCURRED: u32   = 1 << 18;
    pub const SOFT_TEMP_OCCURRED: u32  = 1 << 19;

    /// Returns `true` if any throttling condition is currently active.
    #[inline]
    pub const fn is_throttled(self) -> bool {
        (self.0 & (Self::UNDERVOLT_NOW | Self::ARM_FREQ_CAPPED
            | Self::THROTTLED_NOW | Self::SOFT_TEMP_LIMIT)) != 0
    }

    /// Test whether a specific flag is set.
    #[inline]
    pub const fn contains(self, flag: u32) -> bool {
        (self.0 & flag) != 0
    }
}

// ============================================================================
// Platform Trait
// ============================================================================

/// Core platform contract that every SoC must implement.
///
/// This is the Rust equivalent of the C `hal_platform_*()` function family.
/// A SoC crate provides a concrete struct (e.g., `Bcm2710Platform`) that
/// implements this trait.
///
/// # Initialization
///
/// The [`init`](Platform::init) method replaces both `hal_platform_early_init`
/// and `hal_platform_init` from the C API. SoC implementations that need a
/// two-phase init can handle that internally.
pub trait Platform {
    type Disp: Display;
    type Tmr: Timer;

    /// Create the platform's display driver.
    fn create_display(&self) -> Self::Disp;

    /// Get the platform's timer.
    fn timer(&self) -> Self::Tmr;

    // ---- Lifecycle ----

    /// Initialize the platform and all core HAL subsystems (timer, GPIO, UART).
    ///
    /// This is typically called once at the start of `kernel_main`.
    fn init(&mut self) -> HalResult<()>;

    /// Returns `true` once [`init`](Platform::init) has completed successfully.
    fn is_initialized(&self) -> bool;

    // ---- Identification ----

    /// Full platform identification.
    fn info(&self) -> PlatformInfo;

    /// Board identifier.
    fn platform_id(&self) -> PlatformId;

    /// Human-readable board name (e.g., `"Raspberry Pi Zero 2W"`).
    fn board_name(&self) -> &'static str;

    /// SoC name (e.g., `"BCM2710"`).
    fn soc_name(&self) -> &'static str;

    // ---- Memory ----

    /// Query the platform's memory layout.
    fn memory_info(&self) -> HalResult<MemoryInfo>;

    /// ARM-accessible RAM in bytes.
    fn arm_memory(&self) -> usize;

    /// Total RAM in bytes (ARM + GPU on Pi platforms).
    fn total_memory(&self) -> usize;

    // ---- Clocks ----

    /// Query all clock frequencies.
    fn clock_info(&self) -> HalResult<ClockInfo>;

    /// ARM CPU frequency in Hz, or 0 on error.
    fn arm_freq_hz(&self) -> u32;

    /// Measured ARM frequency (may differ from max if throttled).
    fn arm_freq_measured_hz(&self) -> u32 {
        // Default: same as reported frequency.
        self.arm_freq_hz()
    }

    /// Query a specific clock's rate in Hz.
    fn clock_rate(&self, clock: ClockId) -> u32;

    // ---- Temperature ----

    /// CPU temperature in millidegrees Celsius.
    ///
    /// Returns `Err(HalError::NotSupported)` on platforms without a
    /// thermal sensor.
    fn temperature_mc(&self) -> HalResult<i32>;

    /// CPU temperature in whole degrees Celsius, or `-1` on error.
    fn temperature_celsius(&self) -> i32 {
        self.temperature_mc().map(|mc| mc / 1000).unwrap_or(-1)
    }

    /// Maximum temperature before the platform begins throttling.
    fn max_temperature_mc(&self) -> HalResult<i32> {
        Err(HalError::NotSupported)
    }

    // ---- Throttling ----

    /// Query current throttle status flags.
    fn throttle_status(&self) -> HalResult<ThrottleFlags> {
        Err(HalError::NotSupported)
    }

    /// Returns `true` if any throttling condition is currently active.
    fn is_throttled(&self) -> bool {
        self.throttle_status().map(|f| f.is_throttled()).unwrap_or(false)
    }

    // ---- Power Management ----

    /// Set a peripheral's power state.
    fn set_power(&self, _device: DeviceId, _on: bool) -> HalResult<()> {
        Err(HalError::NotSupported)
    }

    /// Query a peripheral's current power state.
    fn get_power(&self, _device: DeviceId) -> HalResult<bool> {
        Err(HalError::NotSupported)
    }

    // ---- System Control ----

    /// Reboot the system.
    ///
    /// On success this never returns — the system resets.  Implementations
    /// should loop forever after issuing the reset in case it doesn't take
    /// effect immediately.
    fn reboot(&self) -> HalResult<()> {
        Err(HalError::NotSupported)
    }

    /// Halt or shut down the system.
    ///
    /// On success this never returns.
    fn shutdown(&self) -> HalResult<()> {
        Err(HalError::NotSupported)
    }

    /// Halt with an error message. Does not return.
    fn panic(&self, message: &str) -> !;

    // ---- Debug Output ----

    /// Write a single character to the debug console (UART).
    fn debug_putc(&self, c: u8);

    /// Write a string to the debug console.
    fn debug_puts(&self, s: &str) {
        for b in s.bytes() {
            self.debug_putc(b);
        }
    }
}