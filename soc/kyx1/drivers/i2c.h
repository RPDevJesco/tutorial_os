/*
 * i2c.h — SpacemiT K1 I2C Controller Driver (Polled Mode)
 * =========================================================
 *
 * The K1 SoC has 9 I2C controllers (I2C0-I2C8). They are PXA-compatible
 * I2C units, the same IP block found in Marvell/Intel PXA SoCs.
 *
 * For Tutorial-OS, we only need I2C8 (the "power I2C") which connects
 * to the SPM8821 PMIC at address 0x41. U-Boot has already configured
 * the clock gating and pin mux for I2C8 during boot, so we can just
 * drive the registers directly.
 *
 * REGISTER MAP (from Linux i2c-k1.c / PXA I2C documentation):
 *   Offset  Name   Description
 *   0x00    ICR    I2C Control Register
 *   0x04    ISR    I2C Status Register
 *   0x0C    IDBR   I2C Data Buffer Register
 *   0x10    ILCR   I2C Load Count Register (clock divider)
 *   0x18    IRCR   I2C Reset Cycle Counter
 *   0x1C    IBMR   I2C Bus Monitor Register
 *
 * WHY POLLED MODE?
 * -----------------
 * We're bare-metal with no interrupt infrastructure yet. The PXA I2C
 * controller works fine in polled mode — just write ICR, poll ISR
 * for completion flags, read IDBR. Each transaction is a few hundred
 * microseconds at 100 kHz, which is fine for reading PMIC registers
 * during init or periodic status checks.
 */

#ifndef KYX1_I2C_H
#define KYX1_I2C_H

#include "types.h"

/* =============================================================================
 * I2C CONTROLLER BASE ADDRESSES
 * =============================================================================
 * From K1 device tree (k1.dtsi). Each controller is 0x38 bytes.
 */
#define KYX1_I2C0_BASE      0xD4010800UL
#define KYX1_I2C1_BASE      0xD4011000UL
#define KYX1_I2C2_BASE      0xD4012000UL
/* I2C3-I2C7: 0xD4013800, 0xD4018000, 0xD4018800, 0xD401D000, 0xD401D000 */
#define KYX1_I2C8_BASE      0xD401D800UL    /* Power I2C — connects to PMIC */


/* =============================================================================
 * I2C REGISTER OFFSETS
 * =============================================================================
 * PXA-compatible I2C controller registers.
 * Source: Linux drivers/i2c/busses/i2c-k1.c
 */
#define I2C_ICR             0x00    /* Control Register */
#define I2C_ISR             0x04    /* Status Register */
#define I2C_IDBR            0x0C    /* Data Buffer Register */
#define I2C_ILCR            0x10    /* Load Count Register */
#define I2C_IRCR            0x18    /* Reset Cycle Counter */
#define I2C_IBMR            0x1C    /* Bus Monitor Register */


/* =============================================================================
 * ICR — I2C CONTROL REGISTER (offset 0x00)
 * =============================================================================
 *
 * Bit layout (from Linux i2c-k1.c):
 *   [0]  START    — Generate START condition
 *   [1]  STOP     — Generate STOP condition
 *   [2]  ACKNAK   — Send ACK (0) or NAK (1) after receiving
 *   [3]  TB       — Transfer Byte — start a byte transfer
 *   [4]  MA       — Master Abort
 *   [5]  SCLE     — SCL Enable (master drives clock)
 *   [6]  IUE      — I2C Unit Enable
 *   [7]  GCD      — General Call Disable
 *   [8]  ITEIE    — Interrupt on Transmit Empty Enable
 *   [9]  IRFIE    — Interrupt on Receive Full Enable
 *   [10] BEIE     — Bus Error Interrupt Enable
 *   [11] SSDIE    — Slave Stop Detected Interrupt Enable
 *   [12] ALDIE    — Arbitration Loss Interrupt Enable
 *   [13] SADIE    — Slave Address Detected Interrupt Enable
 *   [14] UR       — Unit Reset
 *   [15] MODE     — Fast Mode (0=standard 100kHz, 1=fast 400kHz)
 *   [16] DRFIE    — DMA Receive Full Interrupt Enable
 */
#define ICR_START           (1 << 0)
#define ICR_STOP            (1 << 1)
#define ICR_ACKNAK          (1 << 2)
#define ICR_TB              (1 << 3)
#define ICR_MA              (1 << 4)
#define ICR_SCLE            (1 << 5)
#define ICR_IUE             (1 << 6)
#define ICR_GCD             (1 << 7)
#define ICR_ITEIE           (1 << 8)
#define ICR_IRFIE           (1 << 9)
#define ICR_BEIE            (1 << 10)
#define ICR_SSDIE           (1 << 11)
#define ICR_ALDIE           (1 << 12)
#define ICR_SADIE           (1 << 13)
#define ICR_UR              (1 << 14)
#define ICR_MODE_FAST       (1 << 15)
#define ICR_DRFIE           (1 << 16)


/* =============================================================================
 * ISR — I2C STATUS REGISTER (offset 0x04)
 * =============================================================================
 *
 * Bit layout:
 *   [0]  RWM      — Read/Write Mode (1=read, 0=write)
 *   [1]  ACKNAK   — ACK/NAK status (1=NAK received)
 *   [2]  UB       — Unit Busy
 *   [3]  IBB      — I2C Bus Busy
 *   [4]  SSD      — Slave STOP Detected (W1C)
 *   [5]  ALD      — Arbitration Loss Detected (W1C)
 *   [6]  ITE      — I2C Transmit Empty (W1C)
 *   [7]  IRF      — I2C Receive Full (W1C)
 *   [9]  BED      — Bus Error Detected (W1C)
 *   [10] SAD      — Slave Address Detected (W1C)
 */
#define ISR_RWM             (1 << 0)
#define ISR_ACKNAK          (1 << 1)
#define ISR_UB              (1 << 2)
#define ISR_IBB             (1 << 3)
#define ISR_SSD             (1 << 4)
#define ISR_ALD             (1 << 5)
#define ISR_ITE             (1 << 6)
#define ISR_IRF             (1 << 7)
#define ISR_BED             (1 << 9)
#define ISR_SAD             (1 << 10)

/* W1C (write-1-to-clear) mask for all clearable status bits */
#define ISR_CLEAR_ALL       (ISR_SSD | ISR_ALD | ISR_ITE | ISR_IRF | \
                             ISR_BED | ISR_SAD)


/* =============================================================================
 * IBMR — I2C BUS MONITOR REGISTER (offset 0x1C)
 * =============================================================================
 */
#define IBMR_SDA            (1 << 0)    /* SDA line level */
#define IBMR_SCL            (1 << 1)    /* SCL line level */


/* =============================================================================
 * I2C DRIVER CONFIGURATION
 * =============================================================================
 */
#define I2C_TIMEOUT_US      100000      /* 100ms timeout for bus operations */


/* =============================================================================
 * PUBLIC API
 * =============================================================================
 *
 * Usage pattern for PMIC register access:
 *
 *   kyx1_i2c_init(KYX1_I2C8_BASE);
 *
 *   // Read register 0xAB from PMIC at address 0x41:
 *   uint8_t val;
 *   kyx1_i2c_read_reg(KYX1_I2C8_BASE, 0x41, 0xAB, &val);
 *
 *   // Write register 0xAB on PMIC:
 *   kyx1_i2c_write_reg(KYX1_I2C8_BASE, 0x41, 0xAB, 0xF0);
 */

/**
 * kyx1_i2c_init() — Initialize an I2C controller
 *
 * Performs a unit reset, enables the I2C unit in standard mode (100 kHz).
 * U-Boot has already configured the clock gating and pinmux, so we just
 * need to reset and enable the hardware.
 *
 * @base: Controller base address (e.g., KYX1_I2C8_BASE)
 * @return: 0 on success, -1 on error (bus stuck)
 */
int kyx1_i2c_init(uintptr_t base);

/**
 * kyx1_i2c_read_reg() — Read a single register from an I2C device
 *
 * Performs: START → addr+W → reg_addr → RESTART → addr+R → data → STOP
 *
 * @base:     Controller base address
 * @dev_addr: 7-bit I2C device address (e.g., 0x41 for SPM8821)
 * @reg:      Register address to read
 * @out:      Pointer to store the read byte
 * @return:   0 on success, -1 on NAK/timeout
 */
int kyx1_i2c_read_reg(uintptr_t base, uint8_t dev_addr,
                       uint8_t reg, uint8_t *out);

/**
 * kyx1_i2c_write_reg() — Write a single register on an I2C device
 *
 * Performs: START → addr+W → reg_addr → data → STOP
 *
 * @base:     Controller base address
 * @dev_addr: 7-bit I2C device address
 * @reg:      Register address to write
 * @val:      Value to write
 * @return:   0 on success, -1 on NAK/timeout
 */
int kyx1_i2c_write_reg(uintptr_t base, uint8_t dev_addr,
                        uint8_t reg, uint8_t val);

/**
 * kyx1_i2c_read_bulk() — Read multiple consecutive registers
 *
 * @base:     Controller base address
 * @dev_addr: 7-bit I2C device address
 * @reg:      Starting register address
 * @buf:      Buffer to store read bytes
 * @len:      Number of bytes to read
 * @return:   0 on success, -1 on error
 */
int kyx1_i2c_read_bulk(uintptr_t base, uint8_t dev_addr,
                        uint8_t reg, uint8_t *buf, uint32_t len);

#endif /* KYX1_I2C_H */
