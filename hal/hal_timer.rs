/*
 * hal/hal_timer — Timer and Delay Functions
 *
 * Tutorial-OS: HAL Interface Definitions (Rust)
 *
 * This module abstracts timing operations. Different platforms have
 * different timer hardware:
 *
 *   BCM2710/BCM2711/BCM2712: System Timer (1MHz)
 *   RK3528A:                 ARM Generic Timer + Rockchip timers
 *   S905X:                   ARM Generic Timer
 *   H618:                    Allwinner Timer
 *   K1 (RISC-V):             CLINT timer (via SBI)
 *   Intel N150 (x86_64):     TSC / HPET / APIC Timer
 */

#![allow(dead_code)]

use super::types::HalResult;

// =============================================================================
// TIMER TRAIT
// =============================================================================
//
// Every platform implements this trait for its specific timer hardware.

/// Timer operations contract.
///
/// Provides monotonic timing and busy-wait delays. Every platform must
/// implement `init()`, `get_ticks()`, `get_ticks32()`, and `delay_us()`.
///
/// The remaining methods have default implementations that build on these
/// primitives.
pub trait Timer {
    /// Initialize the timer subsystem.
    ///
    /// Called during `hal_platform_init()`. Most platforms don't need
    /// explicit timer initialization, but some (like RISC-V) do.
    fn init(&mut self) -> HalResult<()>;

    /// Get current time in microseconds (64-bit).
    ///
    /// Returns a monotonically increasing counter. Wraps after ~584,942 years.
    ///
    /// On BCM2710, this reads from the 1 MHz system timer.
    /// On RISC-V K1, this reads `rdtime` at 24 MHz and divides by 24.
    fn get_ticks(&self) -> u64;

    /// Get current time in microseconds (32-bit for compatibility).
    ///
    /// Matches existing usage from `SYSTIMER_CLO` reads.
    /// Wraps every ~71 minutes.
    fn get_ticks32(&self) -> u32 {
        self.get_ticks() as u32
    }

    /// Get current time in milliseconds.
    ///
    /// Convenience function. Less precision but easier to work with.
    fn get_ms(&self) -> u64 {
        self.get_ticks() / 1000
    }

    /// Delay for specified microseconds.
    ///
    /// Busy-waits using the hardware timer. This is accurate to within
    /// a few microseconds on most platforms.
    fn delay_us(&self, us: u32);

    /// Delay for specified milliseconds.
    ///
    /// Convenience function. Calls `delay_us()` internally.
    fn delay_ms(&self, ms: u32) {
        self.delay_us(ms * 1000);
    }

    /// Delay for specified seconds.
    fn delay_s(&self, s: u32) {
        self.delay_us(s * 1_000_000);
    }
}

// =============================================================================
// TIMEOUT HELPERS
// =============================================================================
//
// Useful for polling hardware with timeouts. These are free functions that
// work with any Timer implementation.

/// Check if timeout has elapsed since `start_us`.
///
/// # Example
///
/// ```rust,ignore
/// let start = timer.get_ticks32();
/// while !condition {
///     if timeout_elapsed(timer, start, 1000) {  // 1ms timeout
///         return Err(HalError::Timeout);
///     }
/// }
/// ```
#[inline]
pub fn timeout_elapsed(timer: &dyn Timer, start_us: u32, timeout_us: u32) -> bool {
    timer.get_ticks32().wrapping_sub(start_us) >= timeout_us
}

/// Calculate elapsed microseconds since `start_us`.
///
/// Handles 32-bit wrapping correctly.
#[inline]
pub fn elapsed_us(timer: &dyn Timer, start_us: u32) -> u32 {
    timer.get_ticks32().wrapping_sub(start_us)
}

// =============================================================================
// STOPWATCH UTILITY
// =============================================================================
//
// For measuring code execution time. Zero-allocation, no heap required.

/// A simple stopwatch for measuring elapsed time.
///
/// # Example
///
/// ```rust,ignore
/// let mut sw = Stopwatch::new();
/// sw.start(&timer);
///
/// // ... do some work ...
///
/// sw.stop(&timer);
/// let elapsed = sw.elapsed_us(&timer);
/// ```
#[derive(Debug, Clone, Copy)]
pub struct Stopwatch {
    start_ticks: u64,
    stop_ticks: u64,
    running: bool,
}

impl Stopwatch {
    /// Create a new stopped stopwatch.
    pub const fn new() -> Self {
        Self {
            start_ticks: 0,
            stop_ticks: 0,
            running: false,
        }
    }

    /// Start (or restart) the stopwatch.
    pub fn start(&mut self, timer: &dyn Timer) {
        self.start_ticks = timer.get_ticks();
        self.running = true;
    }

    /// Stop the stopwatch, freezing the elapsed time.
    pub fn stop(&mut self, timer: &dyn Timer) {
        self.stop_ticks = timer.get_ticks();
        self.running = false;
    }

    /// Get elapsed time in microseconds.
    ///
    /// If the stopwatch is running, returns the time since `start()`.
    /// If stopped, returns the time between `start()` and `stop()`.
    pub fn elapsed_us(&self, timer: &dyn Timer) -> u64 {
        if self.running {
            timer.get_ticks() - self.start_ticks
        } else {
            self.stop_ticks - self.start_ticks
        }
    }

    /// Get elapsed time in milliseconds.
    pub fn elapsed_ms(&self, timer: &dyn Timer) -> u64 {
        self.elapsed_us(timer) / 1000
    }

    /// Check if the stopwatch is currently running.
    pub fn is_running(&self) -> bool {
        self.running
    }

    /// Reset the stopwatch to zero (stopped state).
    pub fn reset(&mut self) {
        self.start_ticks = 0;
        self.stop_ticks = 0;
        self.running = false;
    }
}

// =============================================================================
// PLATFORM-SPECIFIC NOTES
// =============================================================================
//
// BCM2710/BCM2711 (Pi Zero 2W, Pi 3, Pi 4, CM4):
//   - System Timer at PERIPHERAL_BASE + 0x3000
//   - 1MHz counter (1 tick = 1 microsecond)
//   - The existing delay_us() reads SYSTIMER_CLO directly
//
// BCM2712 (Pi 5, CM5):
//   - Similar system timer, but different base address
//   - Also has ARM Generic Timer available
//
// RK3528A (Rock 2A):
//   - ARM Generic Timer (CNTPCT_EL0)
//   - Frequency from CNTFRQ_EL0 (usually 24MHz)
//   - Also has Rockchip-specific timers
//
// S905X (Le Potato):
//   - ARM Generic Timer
//   - Frequency varies by board config
//
// H618 (KICKPI K2B):
//   - ARM Generic Timer
//   - Also has Allwinner Timer/Watchdog
//
// K1 RISC-V (Orange Pi RV2):
//   - CLINT (Core Local Interruptor) provides mtime
//   - Accessed via SBI calls in S-mode
//   - Frequency from device tree (24 MHz)
//
// Intel N150 (LattePanda IOTA):
//   - TSC (Time Stamp Counter) — high resolution, per-core
//   - HPET (High Precision Event Timer) — system-wide
//   - APIC Timer — per-core, for scheduling
//   - Frequency from CPUID or calibration against known timer
