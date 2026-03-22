//! Fundamental types used throughout the Hardware Abstraction Layer.
//!
//! This module provides:
//! - [`HalError`] — a rich error enum covering every HAL subsystem
//! - [`PlatformId`] / [`Arch`] — board and architecture identification
//! - Alignment and bit-manipulation helpers as `const fn`
//!
//! ## Repr Stability
//!
//! [`HalError`], [`PlatformId`], and [`Arch`] carry explicit `#[repr]`
//! discriminants that match the C implementation's values. This is
//! intentional — UART debug output and mixed-language debugging sessions
//! can compare error codes directly across the two implementations.

// ============================================================================
// HAL Error Codes
// ============================================================================
//
// Every HAL function returns `Result<T, HalError>`. Error codes are grouped
// by subsystem (0xNNxx where NN = subsystem), matching the C enum values
// for cross-implementation debugging.

/// Unified error type for all HAL operations.
///
/// Subsystem prefixes:
/// - `0x00xx` — General
/// - `0x01xx` — Display
/// - `0x02xx` — GPIO
/// - `0x03xx` — Timer
/// - `0x04xx` — UART
/// - `0x05xx` — USB
/// - `0x06xx` — Storage
/// - `0x07xx` — Audio
/// - `0x08xx` — DSI
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
#[non_exhaustive]
pub enum HalError {
    // General errors (0x00xx)
    /// Generic, unspecified error.
    Generic             = 0x0001,
    /// Subsystem not yet initialized.
    NotInitialized      = 0x0002,
    /// Subsystem already initialized.
    AlreadyInitialized  = 0x0003,
    /// An argument was out of range or otherwise invalid.
    InvalidArgument     = 0x0004,
    /// A required reference was missing (Rust equivalent of C's NULL ptr).
    MissingReference    = 0x0005,
    /// A hardware operation did not complete within the expected time.
    Timeout             = 0x0006,
    /// The requested resource is currently in use.
    Busy                = 0x0007,
    /// This feature is not available on the current platform.
    NotSupported        = 0x0008,
    /// Memory allocation failed.
    OutOfMemory         = 0x0009,
    /// A hardware fault was detected.
    HardwareFault       = 0x000A,

    // Display errors (0x01xx)
    /// Display initialization failed.
    DisplayInit         = 0x0100,
    /// The requested display mode is not valid.
    DisplayMode         = 0x0101,
    /// No framebuffer has been allocated.
    DisplayNoFramebuffer = 0x0102,
    /// Mailbox communication with the display subsystem failed.
    DisplayMailbox      = 0x0103,

    // GPIO errors (0x02xx)
    /// Pin number is outside the valid range for this platform.
    GpioInvalidPin      = 0x0200,
    /// The requested pin mode is not supported.
    GpioInvalidMode     = 0x0201,

    // Timer errors (0x03xx)
    /// The specified timer is not valid.
    TimerInvalid        = 0x0300,

    // UART errors (0x04xx)
    /// The specified UART instance does not exist.
    UartInvalid         = 0x0400,
    /// The requested baud rate cannot be achieved.
    UartBaudRate        = 0x0401,
    /// The UART buffer has overflowed.
    UartOverflow        = 0x0402,

    // USB errors (0x05xx)
    /// USB controller initialization failed.
    UsbInit             = 0x0500,
    /// No USB device is connected.
    UsbNoDevice         = 0x0501,
    /// USB enumeration failed.
    UsbEnumeration      = 0x0502,
    /// A USB transfer encountered an error.
    UsbTransfer         = 0x0503,
    /// A USB endpoint stalled.
    UsbStall            = 0x0504,

    // Storage errors (0x06xx)
    /// Storage controller initialization failed.
    StorageInit         = 0x0600,
    /// No storage media is inserted.
    StorageNoCard       = 0x0601,
    /// A read operation failed.
    StorageRead         = 0x0602,
    /// A write operation failed.
    StorageWrite        = 0x0603,
    /// A CRC check failed on storage data.
    StorageCrc          = 0x0604,

    // Audio errors (0x07xx)
    /// Audio subsystem initialization failed.
    AudioInit           = 0x0700,
    /// The requested audio format is not supported.
    AudioFormat         = 0x0701,

    // DSI errors (0x08xx)
    /// DSI subsystem initialization failed.
    DsiInit             = 0x0800,
    /// D-PHY configuration failed.
    DsiPhy              = 0x0801,
    /// PLL did not achieve lock within the timeout.
    DsiPll              = 0x0802,
    /// Invalid lane configuration.
    DsiLanes            = 0x0803,
    /// A DSI command or transfer timed out.
    DsiTimeout          = 0x0804,
    /// The panel returned an error in its ACK.
    DsiAckError         = 0x0805,
    /// DSI FIFO overflow or underflow.
    DsiFifo             = 0x0806,
    /// CRC mismatch on received DSI data.
    DsiCrc              = 0x0807,
    /// ECC error in a DSI packet header.
    DsiEcc              = 0x0808,
    /// Bus contention detected on the DSI link.
    DsiContention       = 0x0809,
    /// Operation requires video mode but DSI is in command mode.
    DsiNotVideoMode     = 0x080A,
    /// Operation requires command mode but DSI is in video mode.
    DsiNotCommandMode   = 0x080B,
    /// The panel did not respond.
    DsiPanelNoResponse  = 0x080C,
    
    // I2C errors (0x09xx)
    /// The I2C target did not acknowledge (NACK).
    I2cNack             = 0x0900,
    I2cBusError         = 0x0901,  // optional
}

impl HalError {
    /// Returns the raw `u16` error code, useful for UART debug output.
    #[inline]
    pub const fn code(self) -> u16 {
        self as u16
    }
}

impl core::fmt::Display for HalError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        // In a no_std bare-metal context this is primarily for debug output.
        // The Debug derive gives us the variant name; Display adds the hex code.
        write!(f, "{:?} (0x{:04X})", self, self.code())
    }
}

/// Convenience type alias used throughout the HAL.
pub type HalResult<T> = Result<T, HalError>;

// ============================================================================
// Platform Identification
// ============================================================================
//
// Values are stable and match the C implementation's enum discriminants.
// SoC-specific code may use these in match arms, and the Rust
// implementation mirrors them with matching repr for cross-language
// debugging compatibility.

/// Identifies which board the OS is running on.
///
/// Discriminant values match the C `hal_platform_id_t` enum for
/// cross-implementation debugging compatibility.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
#[non_exhaustive]
pub enum PlatformId {
    // Raspberry Pi family (BCM SoCs)
    RpiZero2W       = 0x0001,
    Rpi3B           = 0x0002,
    Rpi3BPlus       = 0x0003,
    Rpi4B           = 0x0010,
    RpiCm4          = 0x0011,
    Rpi5            = 0x0020,
    RpiCm5          = 0x0021,

    // Other ARM64 boards
    RadxaRock2A     = 0x0200,

    // ARM32 boards
    Pico2Lafvin     = 0x3000,

    // RISC-V boards
    OrangePiRv2     = 0x1000,
    MilkVMars       = 0x1001,

    // x86_64 boards
    LattePandaMu    = 0x2000,
    LattePandaIota  = 0x2001,

    Unknown         = 0xFFFF,
}

/// CPU architecture family.
///
/// Discriminant values match `hal_arch_t` in the C implementation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
#[non_exhaustive]
pub enum Arch {
    Arm64   = 0x01,
    Arm32   = 0x02,
    RiscV64 = 0x03,
    X86_64  = 0x04,
    Unknown = 0xFF,
}

// ============================================================================
// Utility Functions
// ============================================================================
//
// These replace the C macro suite (BIT, ALIGN_UP, CLAMP, etc.) with
// const fn that the compiler can evaluate at compile time.

/// Returns a `usize` with only bit `n` set.
///
/// ```ignore
/// assert_eq!(bit(3), 0b1000);
/// ```
#[inline(always)]
pub const fn bit(n: u32) -> usize {
    1usize << n
}

/// Round `x` up to the next multiple of `align`.
///
/// `align` must be a power of two.
#[inline(always)]
pub const fn align_up(x: usize, align: usize) -> usize {
    (x + align - 1) & !(align - 1)
}

/// Round `x` down to the previous multiple of `align`.
///
/// `align` must be a power of two.
#[inline(always)]
pub const fn align_down(x: usize, align: usize) -> usize {
    x & !(align - 1)
}

/// Returns `true` if `x` is aligned to `align` (a power of two).
#[inline(always)]
pub const fn is_aligned(x: usize, align: usize) -> bool {
    (x & (align - 1)) == 0
}

/// Clamp `x` to the inclusive range `[lo, hi]`.
#[inline(always)]
pub const fn clamp(x: i64, lo: i64, hi: i64) -> i64 {
    if x < lo {
        lo
    } else if x > hi {
        hi
    } else {
        x
    }
}
