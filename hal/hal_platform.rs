/*
 * hal/hal_platform — Platform Initialization and Information
 *
 * Tutorial-OS: HAL Interface Definitions (Rust)
 *
 * This module provides:
 *   - Platform initialization sequence
 *   - Hardware information queries (temperature, clocks, memory)
 *   - Board identification
 *
 * In C, these are function declarations implemented per-platform.
 * In Rust, they're a trait that each platform implements, plus data
 * structures that use Rust's type system for safety.
 *
 * This HAL abstracts hardware queries for platforms that don't have mailbox
 * (Rockchip, Amlogic, Allwinner use different mechanisms).
 */

#![allow(dead_code)]

use super::types::{HalResult, PlatformId, Arch, bit};

// =============================================================================
// PLATFORM INFORMATION STRUCTURES
// =============================================================================

/// Platform information — identifies the board we're running on.
#[derive(Debug, Clone)]
pub struct PlatformInfo {
    /// Platform identifier
    pub platform_id: PlatformId,
    /// CPU architecture (ARM64, RISC-V, etc.)
    pub arch: Arch,
    /// Human-readable board name (e.g., "Raspberry Pi Zero 2W")
    pub board_name: &'static str,
    /// SoC name (e.g., "BCM2710", "KyX1")
    pub soc_name: &'static str,
    /// Board revision code
    pub board_revision: u32,
    /// Board serial number (0 if not available)
    pub serial_number: u64,
}

/// Memory region information.
#[derive(Debug, Clone, Copy)]
pub struct MemoryInfo {
    /// ARM accessible RAM base address
    pub arm_base: usize,
    /// ARM accessible RAM size in bytes
    pub arm_size: usize,
    /// Peripheral register base address
    pub peripheral_base: usize,
    /// GPU memory base (0 if not applicable)
    pub gpu_base: usize,
    /// GPU memory size (0 if not applicable)
    pub gpu_size: usize,
}

/// Clock frequency information.
#[derive(Debug, Clone, Copy)]
pub struct ClockInfo {
    /// ARM CPU frequency in Hz
    pub arm_freq_hz: u32,
    /// Core/GPU frequency in Hz
    pub core_freq_hz: u32,
    /// UART clock frequency in Hz
    pub uart_freq_hz: u32,
    /// EMMC/SD clock frequency in Hz
    pub emmc_freq_hz: u32,
}

/// Clock identifiers for detailed queries.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ClockId {
    Arm     = 0,
    Core    = 1,
    Uart    = 2,
    Emmc    = 3,
    Pwm     = 4,
    Pixel   = 5,
}

/// Device identifiers for power management.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum DeviceId {
    SdCard  = 0,
    Uart0   = 1,
    Uart1   = 2,
    Usb     = 3,
    I2c0    = 4,
    I2c1    = 5,
    I2c2    = 6,
    Spi     = 7,
    Pwm     = 8,
}

// =============================================================================
// THROTTLE STATUS FLAGS
// =============================================================================
//
// These match the VideoCore mailbox throttle flags. On platforms without
// mailbox, implementations return 0 (no throttling info available).

pub const THROTTLE_UNDERVOLT_NOW: u32       = bit(0);
pub const THROTTLE_ARM_FREQ_CAPPED: u32     = bit(1);
pub const THROTTLE_THROTTLED_NOW: u32       = bit(2);
pub const THROTTLE_SOFT_TEMP_LIMIT: u32     = bit(3);
pub const THROTTLE_UNDERVOLT_OCCURRED: u32  = bit(16);
pub const THROTTLE_FREQ_CAP_OCCURRED: u32   = bit(17);
pub const THROTTLE_THROTTLE_OCCURRED: u32   = bit(18);
pub const THROTTLE_SOFT_TEMP_OCCURRED: u32  = bit(19);

// =============================================================================
// PLATFORM TRAIT
// =============================================================================
//
// Every platform implements this trait. It covers initialization, hardware
// information queries, power management, and system control.

/// The platform operations contract.
///
/// Each SoC implements this trait to provide platform-specific initialization
/// and hardware query functionality.
///
/// # Example Implementation
///
/// ```rust,ignore
/// pub struct Bcm2710Platform {
///     initialized: bool,
/// }
///
/// impl Platform for Bcm2710Platform {
///     fn early_init(&mut self) -> HalResult<()> {
///         // Set up MMIO base addresses
///         Ok(())
///     }
///     // ... etc
/// }
/// ```
pub trait Platform {
    // =========================================================================
    // INITIALIZATION
    // =========================================================================

    /// Early platform initialization.
    ///
    /// Called very early in boot, before most other subsystems.
    /// Sets up MMIO base addresses and basic hardware state.
    fn early_init(&mut self) -> HalResult<()>;

    /// Full platform initialization.
    ///
    /// Called after early init. Initializes all HAL subsystems:
    ///   - Timer
    ///   - GPIO
    ///   - UART (for debug output)
    fn init(&mut self) -> HalResult<()>;

    /// Check if platform is initialized.
    fn is_initialized(&self) -> bool;

    // =========================================================================
    // PLATFORM INFORMATION
    // =========================================================================

    /// Get complete platform information.
    fn get_info(&self) -> HalResult<PlatformInfo>;

    /// Get platform ID.
    fn get_id(&self) -> PlatformId;

    /// Get human-readable board name (e.g., "Raspberry Pi Zero 2W").
    fn get_board_name(&self) -> &'static str;

    /// Get SoC name (e.g., "BCM2710").
    fn get_soc_name(&self) -> &'static str;

    // =========================================================================
    // MEMORY INFORMATION
    // =========================================================================

    /// Get memory region information.
    ///
    /// On BCM platforms, queries via mailbox.
    /// On other platforms, reads from device tree or fixed values.
    fn get_memory_info(&self) -> HalResult<MemoryInfo>;

    /// Get ARM accessible RAM size in bytes.
    fn get_arm_memory(&self) -> usize;

    /// Get total RAM size in bytes (ARM + GPU on Pi).
    fn get_total_memory(&self) -> usize;

    // =========================================================================
    // CLOCK INFORMATION
    // =========================================================================

    /// Get clock frequency information for all clocks.
    fn get_clock_info(&self) -> HalResult<ClockInfo>;

    /// Get ARM CPU frequency in Hz.
    fn get_arm_freq(&self) -> u32;

    /// Get measured ARM CPU frequency in Hz.
    ///
    /// On BCM platforms, can differ from max freq if throttled.
    fn get_arm_freq_measured(&self) -> u32;

    /// Get a specific clock rate by ID.
    ///
    /// Returns frequency in Hz, or 0 on error.
    fn get_clock_rate(&self, clock_id: ClockId) -> u32;

    // =========================================================================
    // TEMPERATURE MONITORING
    // =========================================================================

    /// Get CPU temperature in millicelsius.
    ///
    /// Returns `Err(HalError::NotSupported)` on platforms without
    /// temperature sensors.
    fn get_temperature(&self) -> HalResult<i32>;

    /// Get CPU temperature in degrees Celsius (convenience).
    ///
    /// Returns -1 on error.
    fn get_temp_celsius(&self) -> i32 {
        self.get_temperature()
            .map(|mc| mc / 1000)
            .unwrap_or(-1)
    }

    /// Get max temperature before throttling (in millicelsius).
    fn get_max_temperature(&self) -> HalResult<i32>;

    // =========================================================================
    // THROTTLING STATUS
    // =========================================================================

    /// Get throttle status flags.
    ///
    /// Returns a bitmask of `THROTTLE_*` constants.
    fn get_throttle_status(&self) -> HalResult<u32>;

    /// Check if any throttling is currently active.
    fn is_throttled(&self) -> bool {
        self.get_throttle_status()
            .map(|s| s & (THROTTLE_UNDERVOLT_NOW | THROTTLE_THROTTLED_NOW
                         | THROTTLE_ARM_FREQ_CAPPED | THROTTLE_SOFT_TEMP_LIMIT) != 0)
            .unwrap_or(false)
    }

    // =========================================================================
    // POWER MANAGEMENT
    // =========================================================================

    /// Set device power state.
    fn set_power(&self, device: DeviceId, on: bool) -> HalResult<()>;

    /// Get device power state.
    fn get_power(&self, device: DeviceId) -> HalResult<bool>;

    // =========================================================================
    // SYSTEM CONTROL
    // =========================================================================

    /// Reboot the system. Does not return on success.
    fn reboot(&self) -> HalResult<()>;

    /// Halt/shutdown the system. Does not return on success.
    fn shutdown(&self) -> HalResult<()>;

    /// Panic — halt with error message. Does not return.
    fn panic(&self, message: &str) -> !;

    // =========================================================================
    // DEBUG OUTPUT
    // =========================================================================

    /// Write a single character to the debug console (UART).
    fn debug_putc(&self, c: u8);

    /// Write a string to the debug console.
    fn debug_puts(&self, s: &str) {
        for byte in s.bytes() {
            self.debug_putc(byte);
        }
    }
}

// =============================================================================
// PLATFORM-SPECIFIC NOTES
// =============================================================================
//
// BCM2710/BCM2711 (Pi Zero 2W, Pi 4, CM4):
//   - Hardware info via VideoCore mailbox
//   - Temperature from mailbox tag
//   - Power management via mailbox
//
// BCM2712 (Pi 5, CM5):
//   - Similar mailbox interface
//   - Some info from RP1 registers
//
// RK3528A (Rock 2A):
//   - Info from device tree or fixed values
//   - Temperature from thermal sensor registers
//   - Power via PMU (Power Management Unit)
//
// S905X (Le Potato):
//   - Info from device tree
//   - Temperature from thermal sensor
//   - Power via PMIC
//
// H618 (KICKPI K2B):
//   - Info from device tree
//   - Temperature from thermal sensor
//   - Power via AXP PMU
//
// K1 RISC-V (Orange Pi RV2):
//   - Info from device tree or SBI
//   - May need different query mechanisms
//
// Intel N150 (LattePanda IOTA):
//   - Info from CPUID, MSRs, ACPI tables
//   - Temperature from IA32_THERM_STATUS MSR
//   - Clock rates from CPUID leaf 0x16 or MSRs
//   - Power management via ACPI
//   - No GPU memory split (integrated, shared RAM)
//   - Throttle status from IA32_PACKAGE_THERM_STATUS
