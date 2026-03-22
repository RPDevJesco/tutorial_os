//! GPIO driver for the StarFive JH7110.
//!
//! Port of `gpio.c`.  Implements [`hal::gpio::Gpio`] using the JH7110's
//! per-pin dout/doen register model.
//!
//! # Two Hardware Subsystems
//!
//! 1. **sys_iomux** (0x13000000) — Pin function selection (pinmux).
//!    Controls which peripheral signal is routed to each physical pad.
//!
//! 2. **sys_gpio** (0x13040000) — GPIO output/input data.
//!    - DOUT register: output data (0 = low, 1 = high)
//!    - DOEN register: output enable (0 = output, 1 = input/high-Z)
//!    - DIN  register: read input data
//!
//! # Contrast with KyX1
//!
//! The KyX1 uses 4 MMP-style GPIO banks (32 pins each) with PDR/PSR/PCR/DIR
//! registers.  The JH7110 uses individual 32-bit registers per GPIO pin.
//!
//! # Heartbeat LED
//!
//! The Milk-V Mars SoM has no standardized user LED.  The heartbeat
//! functions are no-ops that log to UART, maintaining API uniformity
//! with boards that do have LEDs.

use hal::gpio::{Function, Gpio, Peripheral, Pull};
use hal::types::{HalError, HalResult};
use crate::regs;

/// JH7110 GPIO controller instance (zero-sized).
pub struct Jh7110Gpio;

// ============================================================================
// Low-level GPIO access
// ============================================================================

/// Configure a GPIO pin as output.
fn set_output(pin: u32) {
    if pin > regs::GPIO_MAX_PIN { return; }
    unsafe { common::mmio::write32(regs::gpio_doen(pin), regs::GPIO_OUTPUT_EN) };
}

/// Configure a GPIO pin as input.
fn set_input(pin: u32) {
    if pin > regs::GPIO_MAX_PIN { return; }
    unsafe { common::mmio::write32(regs::gpio_doen(pin), regs::GPIO_INPUT_EN) };
}

/// Write a GPIO output value.
fn gpio_write(pin: u32, high: bool) {
    if pin > regs::GPIO_MAX_PIN { return; }
    unsafe { common::mmio::write32(regs::gpio_dout(pin), if high { 1 } else { 0 }) };
}

/// Read a GPIO input value.
fn gpio_read(pin: u32) -> bool {
    if pin > regs::GPIO_MAX_PIN { return false; }
    (unsafe { common::mmio::read32(regs::gpio_din(pin)) } & 1) != 0
}

// ============================================================================
// Boot Mode GPIO
// ============================================================================

/// Read the boot mode from AON GPIO pins (RGPIO0, RGPIO1).
///
/// Returns the 2-bit boot mode value from the DIP switch (SW2).
pub fn get_boot_mode() -> u32 {
    let aon_din = regs::AON_GPIO_BASE + 0x080;
    (unsafe { common::mmio::read32(aon_din) }) & 0x3
}

// ============================================================================
// Heartbeat / Status LED
// ============================================================================

/// Initialize the heartbeat LED.
///
/// No-op on Milk-V Mars (no standardized user LED on the SoM).
pub fn init_heartbeat_led() {
    crate::uart::puts(b"[gpio] Note: No standard user LED on Milk-V Mars SoM\n");
}

/// Set the heartbeat LED state.
///
/// No-op on Milk-V Mars.
pub fn set_led(_on: bool) {}

// ============================================================================
// hal::Gpio Trait Implementation
// ============================================================================

impl Gpio for Jh7110Gpio {
    fn init(&self) -> HalResult<()> {
        // UART0 iomux (GPIO5=TX, GPIO6=RX) is already configured by U-Boot.
        // No additional GPIO init needed.
        Ok(())
    }

    fn set_function(&self, pin: u32, func: Function) -> HalResult<()> {
        if pin > regs::GPIO_MAX_PIN {
            return Err(HalError::GpioInvalidPin);
        }
        match func {
            Function::Input => set_input(pin),
            Function::Output => set_output(pin),
            // Alt functions would require sys_iomux programming.
            // For now, only GPIO input/output are supported.
            _ => return Err(HalError::NotSupported),
        }
        Ok(())
    }

    fn get_function(&self, pin: u32) -> HalResult<Function> {
        if pin > regs::GPIO_MAX_PIN {
            return Err(HalError::GpioInvalidPin);
        }
        let doen = unsafe { common::mmio::read32(regs::gpio_doen(pin)) };
        if doen == regs::GPIO_OUTPUT_EN {
            Ok(Function::Output)
        } else {
            Ok(Function::Input)
        }
    }

    fn set_pull(&self, pin: u32, _pull: Pull) -> HalResult<()> {
        if pin > regs::GPIO_MAX_PIN {
            return Err(HalError::GpioInvalidPin);
        }
        // Pull configuration requires sys_iomux PADCFG register writes.
        // Not yet implemented — U-Boot leaves pulls in a working state.
        Err(HalError::NotSupported)
    }

    fn set_high(&self, pin: u32) -> HalResult<()> {
        if pin > regs::GPIO_MAX_PIN {
            return Err(HalError::GpioInvalidPin);
        }
        gpio_write(pin, true);
        Ok(())
    }

    fn set_low(&self, pin: u32) -> HalResult<()> {
        if pin > regs::GPIO_MAX_PIN {
            return Err(HalError::GpioInvalidPin);
        }
        gpio_write(pin, false);
        Ok(())
    }

    fn read(&self, pin: u32) -> bool {
        gpio_read(pin)
    }

    fn is_valid(&self, pin: u32) -> bool {
        pin <= regs::GPIO_MAX_PIN
    }

    fn max_pin(&self) -> u32 {
        regs::GPIO_MAX_PIN
    }

    fn set_mask(&self, mask: u32, bank: u32) -> HalResult<()> {
        // JH7110 uses per-pin registers, so we iterate the mask bits.
        let base_pin = bank * 32;
        for bit in 0..32u32 {
            if (mask & (1 << bit)) != 0 {
                let pin = base_pin + bit;
                if pin <= regs::GPIO_MAX_PIN {
                    gpio_write(pin, true);
                }
            }
        }
        Ok(())
    }

    fn clear_mask(&self, mask: u32, bank: u32) -> HalResult<()> {
        let base_pin = bank * 32;
        for bit in 0..32u32 {
            if (mask & (1 << bit)) != 0 {
                let pin = base_pin + bit;
                if pin <= regs::GPIO_MAX_PIN {
                    gpio_write(pin, false);
                }
            }
        }
        Ok(())
    }

    fn read_mask(&self, bank: u32) -> u32 {
        let base_pin = bank * 32;
        let mut result = 0u32;
        for bit in 0..32u32 {
            let pin = base_pin + bit;
            if pin <= regs::GPIO_MAX_PIN && gpio_read(pin) {
                result |= 1 << bit;
            }
        }
        result
    }

    fn configure_peripheral(&self, peripheral: Peripheral) -> HalResult<()> {
        match peripheral {
            // UART0 pinmux is already configured by U-Boot.
            Peripheral::Uart(0) => Ok(()),
            // HDMI is fully configured by U-Boot via DC8200 + HDMI TX.
            Peripheral::Hdmi => Ok(()),
            // DPI is not used on JH7110 (HDMI output via DC8200).
            Peripheral::Dpi => Err(HalError::NotSupported),
            _ => Err(HalError::NotSupported),
        }
    }
}
