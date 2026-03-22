//! AXP15060 PMIC driver for the Milk-V Mars.
//!
//! Port of `drivers/pmic_axp15060.c`.  The X-Powers AXP15060 is connected
//! to I2C6 at address 0x36.
//!
//! # Capabilities
//!
//! - Power rail management (DCDC1–DCDC8, ALDO1–5)
//! - NTC thermistor temperature reading via GPADC
//! - Chip identification (chip ID register 0x03 = 0x50)
//!
//! # Temperature Note
//!
//! The AXP15060 measures an external NTC thermistor on the Mars PCB.
//! Converting the raw 12-bit ADC value to Celsius requires the NTC β
//! value and R25 from the board schematic.  We use a rough linear
//! approximation (same as the C implementation).

use crate::regs;
use crate::drivers::i2c;

// ============================================================================
// AXP15060 Register Definitions
// (from Linux: drivers/mfd/axp20x.c, include/linux/mfd/axp20x.h)
// ============================================================================

// Status and identification
const CHIP_ID_REG: u8    = 0x03;
const CHIP_ID_VALUE: u8  = 0x50; // Expected for AXP15060

// DCDC converters
const DCDC1_CTRL: u8     = 0x10; // VDD CPU core
const DCDC_EN: u8        = 0x18; // DCDC enable register

// GPADC — General Purpose ADC (temperature/voltage measurement)
const GPADC_H: u8        = 0x56; // GPADC result high byte
const GPADC_L: u8        = 0x57; // GPADC result low byte
const GPADC_CTRL: u8     = 0x64; // Enable ADC channels

// Power status
const POWER_STATUS: u8   = 0x00;

// ============================================================================
// Driver State
// ============================================================================

static mut PMIC_INITIALIZED: bool = false;
static mut CHIP_ID: u8 = 0;

const I2C_BASE: usize = regs::AXP15060_I2C_BASE;
const I2C_ADDR: u8    = regs::AXP15060_I2C_ADDR;

// ============================================================================
// Initialization
// ============================================================================

/// Initialize the AXP15060 PMIC and verify chip ID.
///
/// Returns `Ok(())` if chip ID matches (or is close enough), `Err` otherwise.
/// Matches the C `axp15060_init()`.
pub fn init() -> Result<(), &'static str> {
    crate::uart::puts(b"[pmic] Initializing AXP15060 on I2C6 (0x36)...\n");

    // Initialize I2C6 controller for the PMIC (sets target address once)
    i2c::init(I2C_BASE, I2C_ADDR);

    // Read chip ID register
    let chip_id = i2c::read_reg(I2C_BASE, CHIP_ID_REG)
        .map_err(|_| "I2C read failed - PMIC not responding")?;

    crate::uart::puts(b"[pmic] Chip ID: 0x");
    crate::uart::puthex(chip_id as u64);
    crate::uart::putc(b'\n');

    if chip_id != CHIP_ID_VALUE {
        crate::uart::puts(b"[pmic] WARNING: Unexpected chip ID (expected 0x50)\n");
        // Continue anyway — register layout may still be compatible
    }

    unsafe {
        CHIP_ID = chip_id;
        PMIC_INITIALIZED = true;
    }

    crate::uart::puts(b"[pmic] AXP15060 init OK\n");
    Ok(())
}

/// Returns `true` if the PMIC was successfully initialized.
pub fn is_available() -> bool {
    unsafe { PMIC_INITIALIZED }
}

/// Returns the chip ID read during init.
pub fn get_chip_id() -> u8 {
    unsafe { CHIP_ID }
}

// ============================================================================
// Temperature Reading
// ============================================================================

/// Read temperature in millidegrees Celsius via the GPADC NTC channel.
///
/// The AXP15060 measures an external NTC thermistor on the Mars PCB.
/// We use a rough linear approximation (same as C implementation):
///
///   raw = 0x800 ≈ 25°C
///   Each ADC count below 0x800 → warmer (NTC resistance decreases)
///   Each ADC count above 0x800 → cooler
///
/// **Not physically accurate** — proper conversion requires the NTC β
/// value from the Mars schematic.  This is a placeholder for Tutorial-OS.
pub fn get_temperature_mc() -> Result<i32, &'static str> {
    if !is_available() {
        return Err("PMIC not initialized");
    }

    // Enable GPADC TS channel measurement (bit 5 in GPADC_CTRL)
    let ctrl = i2c::read_reg(I2C_BASE, GPADC_CTRL)
        .map_err(|_| "GPADC ctrl read failed")?;

    let ctrl_new = ctrl | (1 << 5);
    if ctrl_new != ctrl {
        i2c::write_reg(I2C_BASE, GPADC_CTRL, ctrl_new)
            .map_err(|_| "GPADC ctrl write failed")?;
    }

    // Read 12-bit GPADC result: high byte [7:0] + low byte [3:0]
    let adc_h = i2c::read_reg(I2C_BASE, GPADC_H)
        .map_err(|_| "GPADC H read failed")?;
    let adc_l = i2c::read_reg(I2C_BASE, GPADC_L)
        .map_err(|_| "GPADC L read failed")?;

    let raw_adc = ((adc_h as u32) << 4) | ((adc_l as u32) & 0x0F);

    // Rough linear approximation:
    //   ref_adc = 0x800 (~25°C)
    //   temp_c_tenths = 250 + ((-delta * 5) / 100)
    let ref_adc: i32 = 0x800;
    let delta = raw_adc as i32 - ref_adc;
    let temp_c_tenths = 250 + ((-delta * 5) / 100);
    let temp_mc = temp_c_tenths * 100;

    Ok(temp_mc)
}

// ============================================================================
// Voltage Monitoring
// ============================================================================

/// Read a DCDC converter's output voltage in millivolts.
///
/// `dcdc_num` is 1–8.  Voltage encoding (simplified):
///   bits [5:0] × 10mV + 500mV.
///
/// Matches the C `axp15060_read_dcdc_voltage()`.
pub fn read_dcdc_voltage(dcdc_num: u8) -> Result<u32, &'static str> {
    if dcdc_num < 1 || dcdc_num > 8 {
        return Err("Invalid DCDC number");
    }
    if !is_available() {
        return Err("PMIC not initialized");
    }

    let reg = DCDC1_CTRL + (dcdc_num - 1);
    let val = i2c::read_reg(I2C_BASE, reg)
        .map_err(|_| "DCDC read failed")?;

    let voltage_bits = (val & 0x3F) as u32;
    let mv = 500 + voltage_bits * 10;
    Ok(mv)
}

// ============================================================================
// Status Reporting
// ============================================================================

/// Print PMIC status to UART for boot diagnostics.
///
/// Matches the C `axp15060_print_status()`.
pub fn print_status() {
    if !is_available() {
        crate::uart::puts(b"[pmic] Not initialized\n");
        return;
    }

    crate::uart::puts(b"[pmic] AXP15060 Status:\n");
    crate::uart::puts(b"  Chip ID: 0x");
    crate::uart::puthex(get_chip_id() as u64);
    crate::uart::putc(b'\n');

    // Read and display power status register (0x00)
    if let Ok(pwr_status) = i2c::read_reg(I2C_BASE, POWER_STATUS) {
        crate::uart::puts(b"  Power Status: 0x");
        crate::uart::puthex(pwr_status as u64);
        crate::uart::putc(b'\n');
    }

    // Read DCDC enable register (0x18)
    if let Ok(dcdc_en) = i2c::read_reg(I2C_BASE, DCDC_EN) {
        crate::uart::puts(b"  DCDC Enable: 0x");
        crate::uart::puthex(dcdc_en as u64);
        crate::uart::putc(b'\n');
    }
}
