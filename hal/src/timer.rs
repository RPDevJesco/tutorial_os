//! Timer and delay abstractions.
//!
//! Every platform has different timer hardware — BCM system timers at 1 MHz,
//! RISC-V `rdtime` at 24 MHz, x86 HPET/TSC — but the contract is uniform:
//! a monotonically increasing microsecond counter and busy-wait delays built
//! on top of it.
//!
//! # Stopwatch
//!
//! The [`Stopwatch`] utility provides zero-cost execution-time measurement
//! using the trait's `now_us` method.  It replaces the C `hal_stopwatch_t`
//! struct without requiring a heap allocation.

use crate::types::HalResult;

// ============================================================================
// Timer Trait
// ============================================================================

/// Microsecond-resolution timer contract.
///
/// Implementations must provide at least [`now_us`](Timer::now_us) and
/// [`delay_us`](Timer::delay_us).  Everything else has sensible defaults.
pub trait Timer {
    /// Initialize the timer subsystem.
    ///
    /// Called during platform init.  Most platforms (e.g., BCM system timer)
    /// need no explicit init; RISC-V platforms may need SBI setup.
    fn init(&self) -> HalResult<()> {
        Ok(())
    }

    /// Current time in microseconds (64-bit, monotonic).
    ///
    /// Wraps after ~584 942 years — effectively never.
    fn now_us(&self) -> u64;

    /// Current time in microseconds, truncated to 32 bits.
    ///
    /// Wraps every ~71 minutes.  Useful for tight polling loops where
    /// the cost of a 64-bit read matters.
    fn now_us_32(&self) -> u32 {
        self.now_us() as u32
    }

    /// Current time in milliseconds.
    fn now_ms(&self) -> u64 {
        self.now_us() / 1_000
    }

    /// Busy-wait for `us` microseconds.
    fn delay_us(&self, us: u64);

    /// Busy-wait for `ms` milliseconds.
    fn delay_ms(&self, ms: u64) {
        self.delay_us(ms * 1_000);
    }

    /// Busy-wait for `s` seconds.
    fn delay_s(&self, s: u32) {
        self.delay_us(s as u64 * 1_000_000);
    }

    /// Returns `true` if `timeout_us` microseconds have elapsed since
    /// `start_us` (handles 32-bit wrap correctly via wrapping subtraction).
    ///
    /// ```ignore
    /// let start = timer.now_us_32();
    /// while !ready {
    ///     if timer.has_timed_out(start, 1_000) {
    ///         return Err(HalError::Timeout);
    ///     }
    /// }
    /// ```
    fn has_timed_out(&self, start_us: u32, timeout_us: u32) -> bool {
        self.now_us_32().wrapping_sub(start_us) >= timeout_us
    }

    /// Microseconds elapsed since `start_us` (wrapping-safe).
    fn elapsed_us(&self, start_us: u32) -> u32 {
        self.now_us_32().wrapping_sub(start_us)
    }
}

// ============================================================================
// Stopwatch Utility
// ============================================================================

/// Execution-time measurement utility built on any [`Timer`] implementor.
///
/// ```ignore
/// let mut sw = Stopwatch::new();
/// sw.start(&timer);
/// // ... work ...
/// sw.stop(&timer);
/// debug_printf("took {} us\n", sw.elapsed_us(&timer));
/// ```
#[derive(Debug, Clone, Copy)]
pub struct Stopwatch {
    start_ticks: u64,
    stop_ticks: u64,
    running: bool,
}

impl Stopwatch {
    /// Create a new stopwatch in the stopped state.
    pub const fn new() -> Self {
        Self {
            start_ticks: 0,
            stop_ticks: 0,
            running: false,
        }
    }

    /// Start (or restart) the stopwatch.
    pub fn start(&mut self, timer: &dyn Timer) {
        self.start_ticks = timer.now_us();
        self.running = true;
    }

    /// Stop the stopwatch.
    pub fn stop(&mut self, timer: &dyn Timer) {
        self.stop_ticks = timer.now_us();
        self.running = false;
    }

    /// Elapsed time in microseconds.
    ///
    /// If the stopwatch is still running, returns the live elapsed time.
    pub fn elapsed_us(&self, timer: &dyn Timer) -> u64 {
        let end = if self.running {
            timer.now_us()
        } else {
            self.stop_ticks
        };
        end - self.start_ticks
    }

    /// Elapsed time in milliseconds.
    pub fn elapsed_ms(&self, timer: &dyn Timer) -> u64 {
        self.elapsed_us(timer) / 1_000
    }

    /// Whether the stopwatch is currently running.
    pub const fn is_running(&self) -> bool {
        self.running
    }
}
