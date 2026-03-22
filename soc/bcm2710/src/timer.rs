//! BCM2710 system timer — 1 MHz free-running counter.
//!
//! The system timer runs automatically from boot with no initialisation.
//! At 1 MHz, one tick equals one microsecond.

use hal::timer::Timer;
use hal::types::HalResult;
use crate::regs;

/// BCM2710 system timer.
pub struct Bcm2710Timer;

impl Timer for Bcm2710Timer {
    fn init(&self) -> HalResult<()> {
        // System timer runs from boot — nothing to do.
        Ok(())
    }

    fn now_us(&self) -> u64 {
        // Read the 64-bit counter atomically.
        // Read CHI, CLO, CHI again — retry if CHI changed (wrap during read).
        loop {
            let hi1 = unsafe { common::mmio::read32(regs::SYSTIMER_CHI) };
            let lo  = unsafe { common::mmio::read32(regs::SYSTIMER_CLO) };
            let hi2 = unsafe { common::mmio::read32(regs::SYSTIMER_CHI) };
            if hi1 == hi2 {
                return ((hi1 as u64) << 32) | (lo as u64);
            }
        }
    }

    fn now_us_32(&self) -> u32 {
        // Just the low 32 bits — wraps every ~71 minutes.
        unsafe { common::mmio::read32(regs::SYSTIMER_CLO) }
    }

    fn delay_us(&self, us: u64) {
        let start = self.now_us_32();
        let us32 = us as u32;
        while self.now_us_32().wrapping_sub(start) < us32 {
            hal::cpu::nop();
        }
    }

    fn delay_ms(&self, ms: u64) {
        // Avoid overflow: delay in 1-second chunks for large values.
        let mut remaining = ms;
        while remaining > 1000 {
            self.delay_us(1_000_000);
            remaining -= 1000;
        }
        self.delay_us(remaining * 1000);
    }
}
