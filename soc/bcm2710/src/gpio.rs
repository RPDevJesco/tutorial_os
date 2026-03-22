//! BCM2710 GPIO implementation.
//!
//! 54 GPIO pins (0–53), each with a 3-bit function select in the GPFSEL
//! registers.  BCM uses a non-sequential encoding for alternate functions
//! (ALT0=4, ALT1=5, ALT2=6, ALT3=7, ALT4=3, ALT5=2).  The mapping
//! tables in this module translate between the HAL's sequential enum and
//! BCM's hardware encoding.

use hal::gpio::{Function, Gpio, Peripheral, Pull};
use hal::types::{HalError, HalResult};
use crate::regs;

/// BCM2710 GPIO controller.
pub struct Bcm2710Gpio;

// ============================================================================
// HAL ↔ BCM function code mapping
// ============================================================================

fn hal_to_bcm(func: Function) -> u32 {
    match func {
        Function::Input  => regs::gpio_func::INPUT,
        Function::Output => regs::gpio_func::OUTPUT,
        Function::Alt0   => regs::gpio_func::ALT0,
        Function::Alt1   => regs::gpio_func::ALT1,
        Function::Alt2   => regs::gpio_func::ALT2,
        Function::Alt3   => regs::gpio_func::ALT3,
        Function::Alt4   => regs::gpio_func::ALT4,
        Function::Alt5   => regs::gpio_func::ALT5,
    }
}

fn bcm_to_hal(bcm: u32) -> Function {
    match bcm {
        0 => Function::Input,
        1 => Function::Output,
        4 => Function::Alt0,
        5 => Function::Alt1,
        6 => Function::Alt2,
        7 => Function::Alt3,
        3 => Function::Alt4,
        2 => Function::Alt5,
        _ => Function::Input,
    }
}

// ============================================================================
// Internal register-level operations
// ============================================================================

fn set_function_raw(pin: u32, func: u32) {
    let reg = regs::GPIO_BASE + (pin / 10) as usize * 4;
    let shift = (pin % 10) * 3;
    let mask = 0x7 << shift;
    let val = unsafe { common::mmio::read32(reg) };
    unsafe { common::mmio::write32(reg, (val & !mask) | (func << shift)) };
}

fn get_function_raw(pin: u32) -> u32 {
    let reg = regs::GPIO_BASE + (pin / 10) as usize * 4;
    let shift = (pin % 10) * 3;
    (unsafe { common::mmio::read32(reg) } >> shift) & 0x7
}

fn set_pull_raw(pin: u32, pull: u32, timer: &crate::timer::Bcm2710Timer) {
    use hal::timer::Timer;

    // BCM2710 pull configuration requires a specific dance:
    // 1. Write pull type to GPPUD
    // 2. Wait 150 cycles (~150 µs at 1 MHz timer)
    // 3. Write clock to GPPUDCLK for the pin
    // 4. Wait 150 cycles
    // 5. Clear GPPUD
    // 6. Clear GPPUDCLK
    unsafe { common::mmio::write32(regs::GPPUD, pull) };
    timer.delay_us(150);

    let (clk_reg, bit) = if pin < 32 {
        (regs::GPPUDCLK0, 1u32 << pin)
    } else {
        (regs::GPPUDCLK1, 1u32 << (pin - 32))
    };
    unsafe { common::mmio::write32(clk_reg, bit) };
    timer.delay_us(150);

    unsafe { common::mmio::write32(regs::GPPUD, 0) };
    unsafe { common::mmio::write32(clk_reg, 0) };
}

// ============================================================================
// Gpio trait implementation
// ============================================================================

impl Bcm2710Gpio {
    /// Configure DPI pins — GPIO 0-17 and 20-27 to ALT2, skipping 18-19 for audio.
    fn configure_dpi(&self) -> HalResult<()> {
        use hal::timer::Timer;
        let alt2 = regs::gpio_func::ALT2;

        // GPFSEL0: GPIO 0-9 all ALT2
        let gpfsel0 = (alt2) | (alt2 << 3) | (alt2 << 6) | (alt2 << 9)
                     | (alt2 << 12) | (alt2 << 15) | (alt2 << 18) | (alt2 << 21)
                     | (alt2 << 24) | (alt2 << 27);

        // GPFSEL1: GPIO 10-17 ALT2, GPIO 18-19 left as input (for PWM audio)
        let gpfsel1 = (alt2) | (alt2 << 3) | (alt2 << 6) | (alt2 << 9)
                     | (alt2 << 12) | (alt2 << 15) | (alt2 << 18) | (alt2 << 21);
        // No bits 24-29 — GPIO 18/19 reserved for audio

        // GPFSEL2: GPIO 20-27 all ALT2
        let gpfsel2 = (alt2) | (alt2 << 3) | (alt2 << 6) | (alt2 << 9)
                     | (alt2 << 12) | (alt2 << 15) | (alt2 << 18) | (alt2 << 21);

        unsafe {
            common::mmio::write32(regs::GPFSEL0, gpfsel0);
            common::mmio::write32(regs::GPFSEL1, gpfsel1);
            common::mmio::write32(regs::GPFSEL2, gpfsel2);
        }

        hal::cpu::dmb();

        // Disable pulls on DPI pins
        let timer = crate::timer::Bcm2710Timer;
        unsafe { common::mmio::write32(regs::GPPUD, regs::gpio_pull::OFF) };
        timer.delay_us(150);
        unsafe { common::mmio::write32(regs::GPPUDCLK0, 0x0FF3_FFFF) }; // bits 0-17, 20-27
        timer.delay_us(150);
        unsafe { common::mmio::write32(regs::GPPUD, 0) };
        unsafe { common::mmio::write32(regs::GPPUDCLK0, 0) };

        Ok(())
    }

    fn configure_audio(&self) -> HalResult<()> {
        use hal::timer::Timer;
        let alt5 = regs::gpio_func::ALT5;
        let timer = crate::timer::Bcm2710Timer;

        let gpfsel1 = unsafe { common::mmio::read32(regs::GPFSEL1) };
        let gpfsel1 = (gpfsel1 & 0xC0FF_FFFF) | (alt5 << 24) | (alt5 << 27);
        unsafe { common::mmio::write32(regs::GPFSEL1, gpfsel1) };

        unsafe { common::mmio::write32(regs::GPPUD, regs::gpio_pull::OFF) };
        timer.delay_us(150);
        unsafe { common::mmio::write32(regs::GPPUDCLK0, (1 << 18) | (1 << 19)) };
        timer.delay_us(150);
        unsafe { common::mmio::write32(regs::GPPUD, 0) };
        unsafe { common::mmio::write32(regs::GPPUDCLK0, 0) };

        Ok(())
    }

    fn configure_sdcard(&self) -> HalResult<()> {
        use hal::timer::Timer;
        let alt0 = regs::gpio_func::ALT0;
        let timer = crate::timer::Bcm2710Timer;

        // GPIO 48-49 in GPFSEL4 (bits 24-29)
        let gpfsel4 = unsafe { common::mmio::read32(regs::GPFSEL4) };
        let gpfsel4 = (gpfsel4 & 0xC0FF_FFFF) | (alt0 << 24) | (alt0 << 27);
        unsafe { common::mmio::write32(regs::GPFSEL4, gpfsel4) };

        // GPIO 50-53 in GPFSEL5 (bits 0-11)
        let gpfsel5 = unsafe { common::mmio::read32(regs::GPFSEL5) };
        let gpfsel5 = (gpfsel5 & 0xFFFF_F000) | alt0 | (alt0 << 3) | (alt0 << 6) | (alt0 << 9);
        unsafe { common::mmio::write32(regs::GPFSEL5, gpfsel5) };

        // Pull-ups on data/cmd lines (GPIO 49-53)
        unsafe { common::mmio::write32(regs::GPPUD, regs::gpio_pull::UP) };
        timer.delay_us(150);
        unsafe { common::mmio::write32(regs::GPPUDCLK1, (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21)) };
        timer.delay_us(150);
        unsafe { common::mmio::write32(regs::GPPUD, 0) };
        unsafe { common::mmio::write32(regs::GPPUDCLK1, 0) };

        Ok(())
    }

    fn configure_uart(&self, num: u32) -> HalResult<()> {
        let timer = crate::timer::Bcm2710Timer;
        match num {
            0 => {
                set_function_raw(14, regs::gpio_func::ALT0);
                set_function_raw(15, regs::gpio_func::ALT0);
                set_pull_raw(14, regs::gpio_pull::OFF, &timer);
                set_pull_raw(15, regs::gpio_pull::UP, &timer);
                Ok(())
            }
            1 => {
                set_function_raw(14, regs::gpio_func::ALT5);
                set_function_raw(15, regs::gpio_func::ALT5);
                set_pull_raw(14, regs::gpio_pull::OFF, &timer);
                set_pull_raw(15, regs::gpio_pull::UP, &timer);
                Ok(())
            }
            _ => Err(HalError::InvalidArgument),
        }
    }

    fn configure_safe_shutdown(&self) -> HalResult<()> {
        let timer = crate::timer::Bcm2710Timer;
        set_function_raw(26, regs::gpio_func::INPUT);
        set_pull_raw(26, regs::gpio_pull::UP, &timer);
        Ok(())
    }
}

impl Gpio for Bcm2710Gpio {
    fn set_function(&self, pin: u32, func: Function) -> HalResult<()> {
        if pin > regs::GPIO_MAX_PIN { return Err(HalError::GpioInvalidPin); }
        set_function_raw(pin, hal_to_bcm(func));
        Ok(())
    }

    fn get_function(&self, pin: u32) -> HalResult<Function> {
        if pin > regs::GPIO_MAX_PIN { return Err(HalError::GpioInvalidPin); }
        Ok(bcm_to_hal(get_function_raw(pin)))
    }

    fn set_pull(&self, pin: u32, pull: Pull) -> HalResult<()> {
        if pin > regs::GPIO_MAX_PIN { return Err(HalError::GpioInvalidPin); }
        let bcm_pull = match pull {
            Pull::None => regs::gpio_pull::OFF,
            Pull::Down => regs::gpio_pull::DOWN,
            Pull::Up   => regs::gpio_pull::UP,
        };
        let timer = crate::timer::Bcm2710Timer;
        set_pull_raw(pin, bcm_pull, &timer);
        Ok(())
    }

    fn set_high(&self, pin: u32) -> HalResult<()> {
        if pin > regs::GPIO_MAX_PIN { return Err(HalError::GpioInvalidPin); }
        let (reg, bit) = if pin < 32 { (regs::GPSET0, 1u32 << pin) } else { (regs::GPSET1, 1u32 << (pin - 32)) };
        unsafe { common::mmio::write32(reg, bit) };
        Ok(())
    }

    fn set_low(&self, pin: u32) -> HalResult<()> {
        if pin > regs::GPIO_MAX_PIN { return Err(HalError::GpioInvalidPin); }
        let (reg, bit) = if pin < 32 { (regs::GPCLR0, 1u32 << pin) } else { (regs::GPCLR1, 1u32 << (pin - 32)) };
        unsafe { common::mmio::write32(reg, bit) };
        Ok(())
    }

    fn read(&self, pin: u32) -> bool {
        if pin > regs::GPIO_MAX_PIN { return false; }
        let (reg, bit) = if pin < 32 { (regs::GPLEV0, 1u32 << pin) } else { (regs::GPLEV1, 1u32 << (pin - 32)) };
        (unsafe { common::mmio::read32(reg) } & bit) != 0
    }

    fn is_valid(&self, pin: u32) -> bool { pin <= regs::GPIO_MAX_PIN }
    fn max_pin(&self) -> u32 { regs::GPIO_MAX_PIN }

    fn set_mask(&self, mask: u32, bank: u32) -> HalResult<()> {
        match bank {
            0 => { unsafe { common::mmio::write32(regs::GPSET0, mask) }; Ok(()) }
            1 => { unsafe { common::mmio::write32(regs::GPSET1, mask) }; Ok(()) }
            _ => Err(HalError::InvalidArgument),
        }
    }

    fn clear_mask(&self, mask: u32, bank: u32) -> HalResult<()> {
        match bank {
            0 => { unsafe { common::mmio::write32(regs::GPCLR0, mask) }; Ok(()) }
            1 => { unsafe { common::mmio::write32(regs::GPCLR1, mask) }; Ok(()) }
            _ => Err(HalError::InvalidArgument),
        }
    }

    fn read_mask(&self, bank: u32) -> u32 {
        match bank {
            0 => unsafe { common::mmio::read32(regs::GPLEV0) },
            1 => unsafe { common::mmio::read32(regs::GPLEV1) },
            _ => 0,
        }
    }

    fn configure_peripheral(&self, peripheral: Peripheral) -> HalResult<()> {
        match peripheral {
            Peripheral::Dpi          => self.configure_dpi(),
            Peripheral::Hdmi         => Ok(()), // No GPIO config needed on BCM2710
            Peripheral::SdCard       => self.configure_sdcard(),
            Peripheral::Audio        => self.configure_audio(),
            Peripheral::Uart(n)      => self.configure_uart(n),
            Peripheral::SafeShutdown => self.configure_safe_shutdown(),
            _ => Err(HalError::NotSupported),
        }
    }

    fn safe_shutdown_triggered(&self) -> bool {
        // GPIO 26 is active-low on GPi Case
        !self.read(26)
    }
}
