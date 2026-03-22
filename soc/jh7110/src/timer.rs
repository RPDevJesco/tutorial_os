//! Timer driver for the StarFive JH7110.
//!
//! Port of `timer.c`.  Implements [`hal::timer::Timer`] using the RISC-V
//! `rdtime` CSR, which counts at 24 MHz on the JH7110.
//!
//! # Nearly Identical to KyX1
//!
//! Both SoCs use a 24 MHz reference oscillator as the `rdtime` source.
//! The only differences are the function prefix and the CPU frequency
//! nominal max (1.5 GHz U74 vs 1.6 GHz X60).
//!
//! # Timer Arithmetic
//!
//! - 24 MHz = 24 ticks per microsecond
//! - 32-bit µs counter wraps after ~71 minutes
//! - 64-bit µs counter wraps after ~584,942 years

use hal::timer::Timer;
use hal::types::HalResult;
use crate::{cpu, regs};

/// Ticks per microsecond at 24 MHz.
const TICKS_PER_US: u64 = 24;

/// JH7110 timer instance (zero-sized — all state is in hardware).
pub struct Jh7110Timer;

impl Timer for Jh7110Timer {
    fn init(&self) -> HalResult<()> {
        // The rdtime CSR is always accessible from S-mode via OpenSBI.
        // No explicit init needed.
        Ok(())
    }

    fn now_us(&self) -> u64 {
        cpu::read_time() / TICKS_PER_US
    }

    fn now_us_32(&self) -> u32 {
        (cpu::read_time() / TICKS_PER_US) as u32
    }

    fn delay_us(&self, us: u64) {
        let start = cpu::read_time();
        let end = start + us * TICKS_PER_US;
        while cpu::read_time() < end {}
    }

    fn delay_ms(&self, ms: u64) {
        // Repeated 1000 µs delays to avoid overflow for large values.
        for _ in 0..ms {
            self.delay_us(1000);
        }
    }
}

// ============================================================================
// Additional Timing Functions (not part of hal::Timer)
// ============================================================================

impl Jh7110Timer {
    /// Raw 64-bit timer counter value (each tick = 1/24,000,000 second).
    pub fn ticks(&self) -> u64 {
        cpu::read_time()
    }

    /// Measure actual CPU clock frequency by correlating `rdcycle` with `rdtime`.
    ///
    /// Returns frequency in MHz.  Uses `calibration_ms` milliseconds of
    /// wall-clock time for measurement (longer = more accurate).
    ///
    /// # Algorithm
    ///
    /// 1. Read time T1 and cycle counter C1
    /// 2. Wait `calibration_ms` using rdtime
    /// 3. Read time T2 and cycle counter C2
    /// 4. CPU freq = (C2 − C1) × TIMER_FREQ / (T2 − T1)
    pub fn measure_cpu_freq_mhz(&self, calibration_ms: u32) -> u32 {
        let cal = calibration_ms.clamp(1, 1000);

        // Read start values as close together as possible
        let t1 = cpu::read_time();
        let c1 = cpu::read_cycle();

        // Wait exactly cal ms using the 24 MHz timer
        let ticks_to_wait = cal as u64 * (regs::TIMER_FREQ_HZ / 1000);
        let t_end = t1 + ticks_to_wait;
        while cpu::read_time() < t_end {}

        // Read end values
        let c2 = cpu::read_cycle();
        let t2 = cpu::read_time();

        let delta_cycles = c2.wrapping_sub(c1);
        let delta_time = t2.wrapping_sub(t1);

        if delta_time == 0 {
            return (regs::CPU_FREQ_HZ / 1_000_000) as u32;
        }

        // freq_mhz = delta_cycles * (TIMER_FREQ_HZ / 1_000_000) / delta_time
        let freq_mhz = delta_cycles * (regs::TIMER_FREQ_HZ / 1_000_000) / delta_time;

        // Sanity check: U74 is rated 1.5 GHz max
        if freq_mhz < 100 || freq_mhz > 2000 {
            return (regs::CPU_FREQ_HZ / 1_000_000) as u32;
        }

        freq_mhz as u32
    }
}
