//! JH7110 platform initialization and HAL implementation.
//!
//! Port of `soc_init.c` + `hal_platform_jh7110.c`.  Implements
//! [`hal::platform::Platform`] for the StarFive JH7110 / Milk-V Mars.
//!
//! # Init Phases (matching C implementation)
//!
//! 1. **UART**: SBI console already works from `entry.S`; switch to direct
//!    DW 8250 for faster output.
//! 2. **CPU Frequency**: Measure via `rdcycle`/`rdtime` calibration (same
//!    algorithm as KyX1, same 24 MHz reference).
//! 3. **GPIO**: No user LED on Mars SoM — log to UART instead.
//! 4. **PMIC**: AXP15060 via I2C6 (new vs KyX1 which has SPM8821).
//! 5. **Display**: SimpleFB from DTB (same strategy as KyX1).
//!
//! # What Stays Identical vs KyX1
//!
//! - ✓ Entry point pattern: `Platform::init()`
//! - ✓ DTB pointer from `__dtb_ptr` (set by `common_init.S`)
//! - ✓ CPU freq measurement using rdcycle/rdtime ratio
//! - ✓ Display via SimpleFB
//! - ✗ UART IP differs (PXA vs DW 8250)
//! - ✗ GPIO controller differs (MMP banks vs per-pin dout/doen)
//! - ✗ PMIC differs (SPM8821 vs AXP15060)
//! - ✗ No cache.S (U74 = no Zicbom)

use hal::platform::*;
use crate::{regs, uart, timer, gpio, cpu, Jh7110Timer, Jh7110Display};
use crate::drivers::{sbi, pmic_axp15060};

// ============================================================================
// External Symbols (set by boot/riscv64/common_init.S)
// ============================================================================

extern "C" {
    static __dtb_ptr: u64;
    static __boot_hart_id: u64;
}

// ============================================================================
// Platform Struct
// ============================================================================

/// JH7110 platform instance.
pub struct Jh7110Platform {
    initialized: bool,
    cpu_info: sbi::CpuInfo,
    measured_cpu_freq_hz: u64,
}

impl Jh7110Platform {
    pub const fn new() -> Self {
        Self {
            initialized: false,
            cpu_info: sbi::CpuInfo {
                mvendorid: 0,
                marchid: 0,
                mimpid: 0,
                core_name: "Unknown",
                isa: "RV64GC",
                sbi_spec_major: 0,
                sbi_spec_minor: 0,
            },
            measured_cpu_freq_hz: 0,
        }
    }

    /// Get the DTB pointer saved by common_init.S.
    fn dtb_ptr(&self) -> *const u8 {
        unsafe { __dtb_ptr as *const u8 }
    }
}

// ============================================================================
// hal::Platform Implementation
// ============================================================================

impl Platform for Jh7110Platform {
    type Disp = Jh7110Display;
    type Tmr  = Jh7110Timer;

    fn create_display(&self) -> Self::Disp {
        // self already stores the DTB pointer from init()
        Jh7110Display::new(self.dtb_ptr())
    }
    fn timer(&self) -> Self::Tmr { Jh7110Timer }

    // ---- Lifecycle ----

    fn init(&mut self) -> HalResult<()> {
        if self.initialized {
            return Err(HalError::AlreadyInitialized);
        }

        // ================================================================
        // Phase 1: Console
        // ================================================================
        // SBI console already works (entry.S printed 'T').
        // Switch to direct DW 8250 UART for faster output.
        uart::init_hw();

        uart::puts(b"\n[jh7110] Boot hart: ");
        uart::putdec(unsafe { __boot_hart_id } as u32);
        uart::puts(b"  DTB @ ");
        uart::puthex(unsafe { __dtb_ptr });
        uart::putc(b'\n');

        // ================================================================
        // Phase 2: CPU Identification & Frequency
        // ================================================================
        self.cpu_info = sbi::get_cpu_info();

        uart::puts(b"[jh7110] mvendorid=");
        uart::puthex(self.cpu_info.mvendorid);
        uart::puts(b" marchid=");
        uart::puthex(self.cpu_info.marchid);
        uart::puts_str(" -> ");
        uart::puts_str(self.cpu_info.core_name);
        uart::putc(b'\n');

        let tmr = timer::Jh7110Timer;
        let freq_mhz = tmr.measure_cpu_freq_mhz(10);
        self.measured_cpu_freq_hz = freq_mhz as u64 * 1_000_000;

        uart::puts(b"[jh7110] CPU: ~");
        uart::putdec(freq_mhz);
        uart::puts(b" MHz\n");

        // ================================================================
        // Phase 3: GPIO — Heartbeat LED
        // ================================================================
        // Mars has no standardized user LED.  Call anyway for API uniformity.
        gpio::init_heartbeat_led();

        // ================================================================
        // Phase 4: PMIC (AXP15060)
        // ================================================================
        uart::puts(b"[jh7110] Initializing PMIC (AXP15060 @ I2C6/0x36)...\n");
        match pmic_axp15060::init() {
            Ok(()) => {
                uart::puts(b"[jh7110] PMIC OK\n");
                pmic_axp15060::print_status();
            }
            Err(_) => {
                uart::puts(b"[jh7110] PMIC init failed - temperature N/A\n");
            }
        }

        self.initialized = true;
        uart::puts(b"[jh7110] Platform init complete\n");
        Ok(())
    }

    fn is_initialized(&self) -> bool {
        self.initialized
    }

    // ---- Identification ----

    fn info(&self) -> PlatformInfo {
        PlatformInfo {
            platform_id: PlatformId::MilkVMars,
            arch: Arch::RiscV64,
            board_name: "Milk-V Mars",
            soc_name: "StarFive JH7110",
            board_revision: self.cpu_info.marchid as u32,
            serial_number: ((self.cpu_info.mvendorid as u64) << 32)
                | (self.cpu_info.mimpid as u64),
        }
    }

    fn platform_id(&self) -> PlatformId {
        PlatformId::MilkVMars
    }

    fn board_name(&self) -> &'static str {
        "Milk-V Mars"
    }

    fn soc_name(&self) -> &'static str {
        "StarFive JH7110"
    }

    // ---- Memory ----

    fn memory_info(&self) -> HalResult<MemoryInfo> {
        Ok(MemoryInfo {
            arm_base: regs::DRAM_BASE,
            arm_size: regs::TOTAL_RAM as usize,
            peripheral_base: regs::PERI_BASE,
            gpu_base: 0,   // No dedicated GPU memory on JH7110
            gpu_size: 0,
        })
    }

    fn arm_memory(&self) -> usize {
        regs::TOTAL_RAM as usize
    }

    fn total_memory(&self) -> usize {
        regs::TOTAL_RAM as usize
    }

    // ---- Clocks ----

    fn clock_info(&self) -> HalResult<ClockInfo> {
        let arm_freq = if self.measured_cpu_freq_hz > 0 {
            self.measured_cpu_freq_hz as u32
        } else {
            regs::CPU_FREQ_HZ as u32
        };

        Ok(ClockInfo {
            arm_freq_hz: arm_freq,
            core_freq_hz: 0,                       // No separate "core" clock
            uart_freq_hz: regs::TIMER_FREQ_HZ as u32, // UART ref = 24 MHz
            emmc_freq_hz: 0,
        })
    }

    fn arm_freq_hz(&self) -> u32 {
        if self.measured_cpu_freq_hz > 0 {
            self.measured_cpu_freq_hz as u32
        } else {
            regs::CPU_FREQ_HZ as u32
        }
    }

    fn arm_freq_measured_hz(&self) -> u32 {
        if self.measured_cpu_freq_hz > 0 {
            self.measured_cpu_freq_hz as u32
        } else {
            let tmr = timer::Jh7110Timer;
            let mhz = tmr.measure_cpu_freq_mhz(10);
            mhz * 1_000_000
        }
    }

    fn clock_rate(&self, clock: ClockId) -> u32 {
        match clock {
            ClockId::Arm => self.arm_freq_hz(),
            ClockId::Core => 100_000_000,            // APB bus ~100 MHz
            ClockId::Uart => regs::TIMER_FREQ_HZ as u32, // 24 MHz OSC
            ClockId::Emmc => 100_000_000,            // Set by U-Boot
            ClockId::Pwm | ClockId::Pixel => 0,
        }
    }

    // ---- Temperature ----

    fn temperature_mc(&self) -> HalResult<i32> {
        if !pmic_axp15060::is_available() {
            return Err(HalError::NotSupported);
        }
        pmic_axp15060::get_temperature_mc().map_err(|_| HalError::HardwareFault)
    }

    fn max_temperature_mc(&self) -> HalResult<i32> {
        Ok(regs::THERMAL_MAX_MC)
    }

    // ---- Throttling ----

    fn throttle_status(&self) -> HalResult<ThrottleFlags> {
        // JH7110 has no VideoCore-style throttle reporting.
        // AXP15060 handles thermal shutdown independently.
        Ok(ThrottleFlags(0))
    }

    // ---- Power Management ----

    fn set_power(&self, _device: DeviceId, _on: bool) -> HalResult<()> {
        // Clock gating via SYS_CRG is possible but not yet implemented.
        Err(HalError::NotSupported)
    }

    fn get_power(&self, device: DeviceId) -> HalResult<bool> {
        // U-Boot initializes standard peripherals before handoff.
        match device {
            DeviceId::SdCard | DeviceId::Uart0 | DeviceId::Usb
            | DeviceId::I2c0 | DeviceId::I2c1 | DeviceId::Spi => Ok(true),

            DeviceId::Uart1 | DeviceId::I2c2 | DeviceId::Pwm => Ok(false),
        }
    }

    // ---- System Control ----

    fn reboot(&self) -> HalResult<()> {
        sbi::reboot();
        // Never returns
    }

    fn shutdown(&self) -> HalResult<()> {
        sbi::shutdown();
        // Never returns
    }

    fn panic(&self, msg: &str) -> ! {
        uart::puts(b"\n[PANIC] ");
        uart::puts_str(msg);
        uart::puts(b"\n[PANIC] System halted\n");
        loop {
            cpu::wfi();
        }
    }

    // ---- Debug Output ----

    fn debug_putc(&self, c: u8) {
        uart::putc(c);
    }

    fn debug_puts(&self, s: &str) {
        uart::puts_str(s);
    }
}
