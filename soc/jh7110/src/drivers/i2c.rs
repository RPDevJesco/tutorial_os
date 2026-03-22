//! DesignWare I2C driver for the JH7110.
//!
//! Port of `drivers/i2c.c`.  Provides polled byte-level I2C transfers
//! on any of the 7 DW I2C controllers (I2C0–I2C6).  Used to communicate
//! with the AXP15060 PMIC on I2C6 (base 0x100E0000, address 0x36).
//!
//! # Comparison with KyX1
//!
//! The KyX1 uses I2C8 (0xD401D800) with the PXA/MMP I2C variant.
//! The JH7110 uses the standard DesignWare APB I2C, which is better
//! documented and found in hundreds of SoCs.
//!
//! # Operation
//!
//! - **Write**: send register address + data byte with STOP
//! - **Read**: send register address, then read command + STOP, poll RX FIFO
//! - **Polled mode**: no interrupts, spin on status registers with `delay_us()`

use crate::regs::dwi2c::*;
use hal::types::HalError;

/// Timeout for polling operations (microseconds).
const I2C_TIMEOUT_US: u32 = 10_000; // 10 ms

// ============================================================================
// Register Access
// ============================================================================

#[inline]
fn i2c_read(base: usize, offset: usize) -> u32 {
    unsafe { common::mmio::read32(base + offset) }
}

#[inline]
fn i2c_write(base: usize, offset: usize, val: u32) {
    unsafe { common::mmio::write32(base + offset, val) };
}

// ============================================================================
// Low-Level I2C Control
// ============================================================================

/// Enable or disable the I2C controller, waiting for the change to take effect.
fn i2c_enable(base: usize, enable: bool) {
    i2c_write(base, IC_ENABLE, if enable { 1 } else { 0 });

    // Wait for enable/disable to take effect
    let timer = crate::timer::Jh7110Timer;
    let mut timeout = 100u32;
    while timeout > 0 {
        if enable {
            let status = i2c_read(base, IC_STATUS);
            if (status & STATUS_ACTIVITY) != 0 {
                break;
            }
        } else {
            let en_status = i2c_read(base, IC_ENABLE);
            if (en_status & 1) == 0 {
                break;
            }
        }
        hal::timer::Timer::delay_us(&timer, 10);
        timeout -= 1;
    }
}

/// Wait until the I2C bus is idle (master not active).
fn i2c_wait_not_busy(base: usize) -> bool {
    let timer = crate::timer::Jh7110Timer;
    let mut timeout = I2C_TIMEOUT_US;
    while timeout > 0 {
        let status = i2c_read(base, IC_STATUS);
        if (status & STATUS_MST_ACTIVITY) == 0 {
            return true;
        }
        hal::timer::Timer::delay_us(&timer, 1);
        timeout -= 1;
    }
    crate::uart::puts(b"[i2c] Timeout waiting for bus idle\n");
    false
}

/// Check (and clear) a TX abort condition.  Returns `true` if abort occurred.
fn i2c_check_abort(base: usize) -> bool {
    let raw = i2c_read(base, IC_RAW_INTR_STAT);
    if (raw & INTR_TX_ABRT) != 0 {
        // Clear the abort by reading IC_CLR_TX_ABRT
        let _ = i2c_read(base, IC_CLR_TX_ABRT);
        return true;
    }
    false
}

// ============================================================================
// Public I2C API
// ============================================================================

/// Initialize a DesignWare I2C controller in master mode at standard speed.
///
/// Sets the target slave address once — all subsequent `read_reg` / `write_reg`
/// calls on this base address communicate with this slave.
///
/// Matches the C `jh7110_i2c_init(base, addr_7bit)`.
pub fn init(base: usize, addr_7bit: u8) {
    // Disable controller to allow configuration
    i2c_enable(base, false);

    // Configure: master mode, standard speed (100 kHz), slave disabled
    i2c_write(
        base,
        IC_CON,
        CON_MASTER_MODE | CON_SPEED_STD | CON_RESTART_EN | CON_SLAVE_DISABLE,
    );

    // Set target (PMIC) address — 7-bit mode
    i2c_write(base, IC_TAR, (addr_7bit & 0x7F) as u32);

    // SCL timing for standard mode @ ~100 MHz APB clock
    i2c_write(base, IC_SS_SCL_HCNT, SS_SCL_HCNT_100MHZ);
    i2c_write(base, IC_SS_SCL_LCNT, SS_SCL_LCNT_100MHZ);

    // Mask all interrupts (polled mode)
    i2c_write(base, IC_INTR_MASK, 0x0000);

    // Re-enable controller
    i2c_enable(base, true);
}

/// Write a single byte to a device register.
///
/// Matches the C `jh7110_i2c_write_reg(base, reg, data)`.
pub fn write_reg(base: usize, reg: u8, data: u8) -> Result<(), HalError> {
    if !i2c_wait_not_busy(base) {
        return Err(HalError::Timeout);
    }

    // Write register address (no STOP — more data follows)
    i2c_write(base, IC_DATA_CMD, reg as u32);

    // Write data byte with STOP to end the transfer
    i2c_write(base, IC_DATA_CMD, (data as u32) | DATA_CMD_STOP);

    // Wait for transfer to complete
    if !i2c_wait_not_busy(base) {
        return Err(HalError::Timeout);
    }

    if i2c_check_abort(base) {
        return Err(HalError::I2cNack);
    }

    Ok(())
}

/// Read a single byte from a device register.
///
/// Matches the C `jh7110_i2c_read_reg(base, reg, &data)`.
pub fn read_reg(base: usize, reg: u8) -> Result<u8, HalError> {
    if !i2c_wait_not_busy(base) {
        return Err(HalError::Timeout);
    }

    // Write register address (no STOP — RESTART will follow for read)
    i2c_write(base, IC_DATA_CMD, reg as u32);

    // Issue read command with STOP
    i2c_write(base, IC_DATA_CMD, DATA_CMD_READ | DATA_CMD_STOP);

    // Wait for RX data to arrive
    let timer = crate::timer::Jh7110Timer;
    let mut timeout = I2C_TIMEOUT_US;
    while timeout > 0 {
        if i2c_check_abort(base) {
            return Err(HalError::I2cNack);
        }
        if (i2c_read(base, IC_STATUS) & STATUS_RFNE) != 0 {
            break;
        }
        hal::timer::Timer::delay_us(&timer, 1);
        timeout -= 1;
    }
    if timeout == 0 {
        crate::uart::puts(b"[i2c] Read timeout\n");
        return Err(HalError::Timeout);
    }

    let val = i2c_read(base, IC_DATA_CMD) & 0xFF;
    Ok(val as u8)
}

/// Read multiple consecutive bytes starting from a register address.
///
/// Matches the C `jh7110_i2c_read_regs(base, reg, buf, count)`.
pub fn read_regs(base: usize, start_reg: u8, buf: &mut [u8]) -> Result<(), HalError> {
    for (i, slot) in buf.iter_mut().enumerate() {
        *slot = read_reg(base, start_reg.wrapping_add(i as u8))?;
    }
    Ok(())
}
