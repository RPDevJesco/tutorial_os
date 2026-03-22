//! BCM2710 platform initialization and information queries.
//!
//! Implements [`hal::platform::Platform`] for BCM2710/BCM2837.
//! All hardware queries go through the VideoCore mailbox.

use hal::platform::*;
use crate::{mailbox, regs, Bcm2710Display, Bcm2710Timer};

/// BCM2710 platform instance.
pub struct Bcm2710Platform {
    initialized: bool,
    board_name: &'static str,
    board_revision: u32,
    board_serial: u64,
}

impl Bcm2710Platform {
    pub const fn new() -> Self {
        Self {
            initialized: false,
            board_name: "Raspberry Pi (BCM2710)",
            board_revision: 0,
            board_serial: 0,
        }
    }
}

/// Decode the Raspberry Pi board name from the revision code.
fn decode_board_name(revision: u32) -> &'static str {
    // New-style revision codes have bit 23 set; bits 4–11 encode the type.
    if revision & (1 << 23) != 0 {
        match (revision >> 4) & 0xFF {
            0x04 => "Raspberry Pi 2B",
            0x08 => "Raspberry Pi 3B",
            0x0D => "Raspberry Pi 3B+",
            0x0E => "Raspberry Pi 3A+",
            0x10 => "Raspberry Pi CM3+",
            0x12 => "Raspberry Pi Zero 2W",
            _    => "Raspberry Pi (BCM2710)",
        }
    } else {
        "Raspberry Pi"
    }
}

/// Map a HAL [`ClockId`] to the BCM mailbox clock constant.
fn hal_to_bcm_clock(id: ClockId) -> Option<u32> {
    match id {
        ClockId::Arm   => Some(regs::clock::ARM),
        ClockId::Core  => Some(regs::clock::CORE),
        ClockId::Uart  => Some(regs::clock::UART),
        ClockId::Emmc  => Some(regs::clock::EMMC),
        ClockId::Pwm   => Some(regs::clock::PWM),
        ClockId::Pixel => Some(regs::clock::PIXEL),
    }
}

/// Map a HAL [`DeviceId`] to the BCM mailbox device constant.
fn hal_to_bcm_device(id: DeviceId) -> Option<u32> {
    match id {
        DeviceId::SdCard => Some(regs::device::SD),
        DeviceId::Uart0  => Some(regs::device::UART0),
        DeviceId::Uart1  => Some(regs::device::UART1),
        DeviceId::Usb    => Some(regs::device::USB),
        _                => None,
    }
}

impl Platform for Bcm2710Platform {
    type Disp = Bcm2710Display;
    type Tmr  = Bcm2710Timer;

    fn create_display(&self) -> Self::Disp { Bcm2710Display::new() }
    fn timer(&self) -> Self::Tmr { Bcm2710Timer }

    // ---- Lifecycle ----

    fn init(&mut self) -> HalResult<()> {
        if self.initialized {
            return Err(HalError::AlreadyInitialized);
        }

        // Timer — runs from boot, nothing to do.
        let timer = crate::timer::Bcm2710Timer;
        hal::timer::Timer::init(&timer)?;

        // GPIO — no explicit init on BCM2710.

        // Query board information via mailbox.
        self.board_revision = mailbox::get_board_revision().unwrap_or(0);
        self.board_serial   = mailbox::get_board_serial().unwrap_or(0);
        self.board_name     = decode_board_name(self.board_revision);

        self.initialized = true;
        Ok(())
    }

    fn is_initialized(&self) -> bool { self.initialized }

    // ---- Identification ----

    fn info(&self) -> PlatformInfo {
        PlatformInfo {
            platform_id: PlatformId::RpiZero2W,
            arch: Arch::Arm64,
            board_name: self.board_name,
            soc_name: "BCM2710",
            board_revision: self.board_revision,
            serial_number: self.board_serial,
        }
    }

    fn platform_id(&self) -> PlatformId { PlatformId::RpiZero2W }
    fn board_name(&self) -> &'static str { self.board_name }
    fn soc_name(&self) -> &'static str { "BCM2710" }

    // ---- Memory ----

    fn memory_info(&self) -> HalResult<MemoryInfo> {
        let (arm_base, arm_size) = mailbox::get_arm_memory()
            .ok_or(HalError::HardwareFault)?;
        let (gpu_base, gpu_size) = mailbox::get_vc_memory()
            .unwrap_or((0, 0));

        Ok(MemoryInfo {
            arm_base: arm_base as usize,
            arm_size: arm_size as usize,
            peripheral_base: regs::PERIPHERAL_BASE,
            gpu_base: gpu_base as usize,
            gpu_size: gpu_size as usize,
        })
    }

    fn arm_memory(&self) -> usize {
        mailbox::get_arm_memory().map(|(_, s)| s as usize).unwrap_or(0)
    }

    fn total_memory(&self) -> usize {
        let arm = mailbox::get_arm_memory().map(|(_, s)| s as usize).unwrap_or(0);
        let vc  = mailbox::get_vc_memory().map(|(_, s)| s as usize).unwrap_or(0);
        arm + vc
    }

    // ---- Clocks ----

    fn clock_info(&self) -> HalResult<ClockInfo> {
        Ok(ClockInfo {
            arm_freq_hz:  mailbox::get_clock_rate(regs::clock::ARM).unwrap_or(0),
            core_freq_hz: mailbox::get_clock_rate(regs::clock::CORE).unwrap_or(0),
            uart_freq_hz: mailbox::get_clock_rate(regs::clock::UART).unwrap_or(0),
            emmc_freq_hz: mailbox::get_clock_rate(regs::clock::EMMC).unwrap_or(0),
        })
    }

    fn arm_freq_hz(&self) -> u32 {
        mailbox::get_clock_rate(regs::clock::ARM).unwrap_or(0)
    }

    fn arm_freq_measured_hz(&self) -> u32 {
        mailbox::get_clock_measured(regs::clock::ARM).unwrap_or(0)
    }

    fn clock_rate(&self, clock: ClockId) -> u32 {
        hal_to_bcm_clock(clock)
            .and_then(mailbox::get_clock_rate)
            .unwrap_or(0)
    }

    // ---- Temperature ----

    fn temperature_mc(&self) -> HalResult<i32> {
        mailbox::get_temperature()
            .map(|t| t as i32)
            .ok_or(HalError::HardwareFault)
    }

    fn max_temperature_mc(&self) -> HalResult<i32> {
        mailbox::get_max_temperature()
            .map(|t| t as i32)
            .ok_or(HalError::HardwareFault)
    }

    // ---- Throttling ----

    fn throttle_status(&self) -> HalResult<ThrottleFlags> {
        mailbox::get_throttled()
            .map(ThrottleFlags)
            .ok_or(HalError::HardwareFault)
    }

    // ---- Power ----

    fn set_power(&self, device: DeviceId, on: bool) -> HalResult<()> {
        let bcm_id = hal_to_bcm_device(device).ok_or(HalError::InvalidArgument)?;
        if mailbox::set_power_state(bcm_id, on) { Ok(()) } else { Err(HalError::HardwareFault) }
    }

    fn get_power(&self, device: DeviceId) -> HalResult<bool> {
        let bcm_id = hal_to_bcm_device(device).ok_or(HalError::InvalidArgument)?;
        mailbox::get_power_state(bcm_id).ok_or(HalError::HardwareFault)
    }

    // ---- System Control ----

    fn reboot(&self) -> HalResult<()> { Err(HalError::NotSupported) }
    fn shutdown(&self) -> HalResult<()> { Err(HalError::NotSupported) }

    fn panic(&self, _message: &str) -> ! {
        loop { hal::cpu::wfi(); }
    }

    // ---- Debug ----

    fn debug_putc(&self, _c: u8) {
        // TODO: UART output
    }
}
