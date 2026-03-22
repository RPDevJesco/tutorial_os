/*
 * drivers/i2c.c — I2C Driver for the StarFive JH-7110 SoC
 * =========================================================
 *
 * This implements a polled I2C master driver for the Synopsys DesignWare
 * I2C controller embedded in the JH7110. It's used to communicate with
 * the AXP15060 PMIC on I2C6 (base 0x100E0000, PMIC at address 0x36).
 *
 * COMPARISON WITH KYX1:
 *   The Ky X1 uses I2C8 (0xD401D800) with the PXA/MMP I2C variant.
 *   The JH7110 uses the DesignWare APB I2C, which is more standard and
 *   better documented. The register layout is textbook DW I2C.
 *
 * DESIGNWARE I2C OPERATION:
 * =========================
 *
 * To write a byte to a register:
 *   1. Disable I2C controller (IC_ENABLE = 0)
 *   2. Set target address in IC_TAR
 *   3. Re-enable controller (IC_ENABLE = 1)
 *   4. Write data bytes to IC_DATA_CMD (command = 0 for write)
 *   5. For the last byte, set STOP bit (IC_DATA_CMD bit 9)
 *   6. Poll IC_STATUS until MST_ACTIVITY goes low (transfer complete)
 *
 * To read a byte from a register:
 *   1. Disable + set target + re-enable (same as write)
 *   2. Write the register address byte (write command, no STOP)
 *   3. Write a read command byte (IC_DATA_CMD bit 8 set, with STOP)
 *   4. Poll until RX FIFO has data (IC_STATUS.RFNE = 1)
 *   5. Read the result from IC_DATA_CMD
 *
 * ERROR HANDLING:
 *   A TX abort (IC_RAW_INTR_STAT.TX_ABRT) means the target didn't ACK.
 *   We must read IC_CLR_TX_ABRT to clear it before the next transfer.
 *   This happens if the PMIC address is wrong or the I2C bus is busy.
 *
 * POLLED VS INTERRUPT-DRIVEN:
 *   We use polled I2C (spin-wait on status registers). This is simpler
 *   for bare-metal and avoids interrupt setup complexity. The PMIC read
 *   during boot takes ~100µs — negligible compared to display init.
 *
 * CLOCK CONFIGURATION:
 *   The JH7110 APB bus clock is ~100 MHz (from SYS_CRG).
 *   For standard mode I2C (100 kHz):
 *     SCL high count = APB_CLK / (2 * SCL_FREQ) - 7 = 500 - 7 = 493
 *     SCL low  count = APB_CLK / (2 * SCL_FREQ) - 1 = 500 - 1 = 499
 */

#include "i2c.h"
#include "jh7110_regs.h"
#include "types.h"

extern void jh7110_uart_puts(const char *str);
extern void jh7110_uart_puthex(uint64_t val);
extern void delay_us(uint32_t us);

/* Convenience macro: read/write I2C registers */
#define I2C_REG(base, offset)   (*((volatile uint32_t *)((base) + (offset))))

/* Timeout for polling operations (in microseconds) */
#define I2C_TIMEOUT_US          10000   /* 10 ms */

/* =============================================================================
 * LOW-LEVEL I2C CONTROL
 * =============================================================================
 */

static void i2c_enable(uintptr_t base, bool enable)
{
    I2C_REG(base, DWI2C_IC_ENABLE) = enable ? 1 : 0;
    /* Wait for enable/disable to take effect */
    uint32_t timeout = 100;
    while (timeout--) {
        uint32_t status = I2C_REG(base, DWI2C_IC_STATUS);
        if (enable) {
            if (status & DWI2C_STATUS_ACTIVITY) break;
        } else {
            /* Wait until no longer active */
            uint32_t en_status = I2C_REG(base, DWI2C_IC_ENABLE);
            if (!(en_status & 1)) break;
        }
        delay_us(10);
    }
}

static bool i2c_wait_not_busy(uintptr_t base)
{
    uint32_t timeout = I2C_TIMEOUT_US;
    while (timeout--) {
        uint32_t status = I2C_REG(base, DWI2C_IC_STATUS);
        if (!(status & DWI2C_STATUS_MST_ACTIVITY)) return true;
        delay_us(1);
    }
    jh7110_uart_puts("[i2c] Timeout waiting for bus idle\n");
    return false;
}

static bool i2c_check_abort(uintptr_t base)
{
    uint32_t raw = I2C_REG(base, DWI2C_IC_RAW_INTR_STAT);
    if (raw & DWI2C_INTR_TX_ABRT) {
        /* Clear the abort by reading IC_CLR_TX_ABRT */
        (void)I2C_REG(base, DWI2C_IC_CLR_TX_ABRT);
        return true;    /* Abort occurred */
    }
    return false;
}

/* =============================================================================
 * PUBLIC I2C API
 * =============================================================================
 */

/*
 * jh7110_i2c_init — initialize a DesignWare I2C controller
 *
 * @param base      I2C controller base address (e.g. JH7110_I2C6_BASE)
 * @param addr_7bit 7-bit I2C target address
 */
void jh7110_i2c_init(uintptr_t base, uint8_t addr_7bit)
{
    /* Disable controller to allow configuration */
    i2c_enable(base, false);

    /* Configure as master, standard speed (100 kHz), slave disabled */
    I2C_REG(base, DWI2C_IC_CON) = DWI2C_CON_MASTER_MODE  |
                                   DWI2C_CON_SPEED_STD    |
                                   DWI2C_CON_RESTART_EN   |
                                   DWI2C_CON_SLAVE_DISABLE;

    /* Set target (PMIC) address — 7-bit mode */
    I2C_REG(base, DWI2C_IC_TAR) = addr_7bit & 0x7F;

    /* Clock configuration for 100 kHz @ ~100 MHz APB clock */
    I2C_REG(base, DWI2C_IC_SS_SCL_HCNT) = DWI2C_SS_SCL_HCNT_100MHZ;
    I2C_REG(base, DWI2C_IC_SS_SCL_LCNT) = DWI2C_SS_SCL_LCNT_100MHZ;

    /* Mask all interrupts (polled mode — we don't use IRQs) */
    I2C_REG(base, DWI2C_IC_INTR_MASK) = 0x0000;

    /* Re-enable controller */
    i2c_enable(base, true);
}

/*
 * jh7110_i2c_write_reg — write a single byte to a device register
 *
 * @param base  I2C controller base address
 * @param reg   Register address on the I2C device
 * @param data  Byte to write
 * @return      true on success, false on NACK or timeout
 */
bool jh7110_i2c_write_reg(uintptr_t base, uint8_t reg, uint8_t data)
{
    if (!i2c_wait_not_busy(base)) return false;

    /* Write register address (no STOP — more data follows) */
    I2C_REG(base, DWI2C_IC_DATA_CMD) = (uint32_t)reg;

    /* Write data byte with STOP to end the transfer */
    I2C_REG(base, DWI2C_IC_DATA_CMD) = (uint32_t)data | DWI2C_DATA_CMD_STOP;

    /* Wait for transfer to complete */
    if (!i2c_wait_not_busy(base)) return false;

    return !i2c_check_abort(base);
}

/*
 * jh7110_i2c_read_reg — read a single byte from a device register
 *
 * @param base  I2C controller base address
 * @param reg   Register address on the I2C device
 * @param data  Output: received byte
 * @return      true on success, false on NACK or timeout
 */
bool jh7110_i2c_read_reg(uintptr_t base, uint8_t reg, uint8_t *data)
{
    if (!data) return false;
    if (!i2c_wait_not_busy(base)) return false;

    /* Write register address (no STOP — RESTART will follow for read) */
    I2C_REG(base, DWI2C_IC_DATA_CMD) = (uint32_t)reg;

    /* Issue read command with STOP */
    I2C_REG(base, DWI2C_IC_DATA_CMD) = DWI2C_DATA_CMD_READ |
                                        DWI2C_DATA_CMD_STOP;

    /* Wait for RX data to arrive */
    uint32_t timeout = I2C_TIMEOUT_US;
    while (timeout--) {
        if (i2c_check_abort(base)) return false;
        if (I2C_REG(base, DWI2C_IC_STATUS) & DWI2C_STATUS_RFNE) break;
        delay_us(1);
    }
    if (timeout == 0) {
        jh7110_uart_puts("[i2c] Read timeout\n");
        return false;
    }

    *data = (uint8_t)(I2C_REG(base, DWI2C_IC_DATA_CMD) & 0xFF);
    return true;
}

/*
 * jh7110_i2c_read_regs — read multiple consecutive bytes
 *
 * @param base   I2C controller base address
 * @param reg    Starting register address
 * @param buf    Output buffer
 * @param count  Number of bytes to read
 * @return       true on success
 */
bool jh7110_i2c_read_regs(uintptr_t base, uint8_t reg,
                           uint8_t *buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (!jh7110_i2c_read_reg(base, reg + i, &buf[i])) return false;
    }
    return true;
}