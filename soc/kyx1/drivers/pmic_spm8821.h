/*
 * pmic_spm8821.h — SpacemiT SPM8821 (P1) PMIC Register Map & Driver API
 * ========================================================================
 *
 * The SPM8821 is the PMIC used on the Orange Pi RV2 and other K1-based
 * boards (Banana Pi F3, Milk-V Jupiter, etc.). SpacemiT markets it as
 * the "P1" PMIC. It connects via I2C8 at address 0x41.
 *
 * FEATURES:
 *   - 6 DC-DC buck converters (500 mV to 3.45 V)
 *   - 4 ALDO (analog LDO) regulators (500 mV to 3.4 V)
 *   - 7 DLDO (digital LDO) regulators (500 mV to 3.4 V)
 *   - Load switch
 *   - 6-channel 12-bit GPADC (general purpose ADC)
 *   - 6 GPIO/function pins
 *   - RTC with alarm
 *   - Watchdog timer
 *   - Thermal monitoring
 *
 * REGISTER MAP SOURCES:
 *   - Linux upstream: drivers/regulator/spacemit-p1.c (Alex Elder's patches)
 *   - Linux upstream: drivers/mfd/simple-mfd-i2c.c (max_register)
 *   - U-Boot upstream: include/power/spacemit_p1.h (Raymond Mao's patches)
 *   - U-Boot upstream: drivers/power/regulator/spacemit_p1_regulator.c
 *   - Vendor BSP: linux-6.6 spacemit-pmic driver
 *   - Boot log evidence: "Read PMIC reg ab value f0"
 *
 * NOTE: The complete register map is not publicly documented. These
 * offsets are derived from open-source kernel/U-Boot driver code.
 * Unknown registers are marked TODO.
 */

#ifndef KYX1_PMIC_SPM8821_H
#define KYX1_PMIC_SPM8821_H

#include "types.h"

/* =============================================================================
 * I2C CONFIGURATION
 * =============================================================================
 */
#define SPM8821_I2C_ADDR            0x41
#define SPM8821_I2C_BASE            0xD401D800UL    /* I2C8 */


/* =============================================================================
 * CHIP IDENTIFICATION
 * =============================================================================
 * Register 0x00 typically holds the chip ID in most PMICs.
 * The boot log shows "Read PMIC reg ab value f0" during U-Boot init,
 * suggesting register 0xAB holds some status/ID-related data.
 */
#define SPM8821_REG_CHIP_ID         0x00


/* =============================================================================
 * BUCK REGULATOR REGISTERS
 * =============================================================================
 * 6 buck converters. Each has 3 registers:
 *   - CTRL: enable/disable, mode selection
 *   - VSEL: active voltage selection (0-254 → 500mV to 3.45V)
 *   - SVSEL: sleep/standby voltage selection
 *
 * Voltage formula (from U-Boot driver):
 *   sel 0x00-0xAA: voltage = 500000 + sel * 5000      (500 mV - 1.35 V, 5 mV steps)
 *   sel 0xAB-0xFE: voltage = 1375000 + (sel-0xAB) * 25000  (1.375 V - 3.45 V, 25 mV steps)
 *
 * Register layout derived from U-Boot P1_BUCK_VSEL(n), P1_BUCK_CTRL(n) macros.
 * The exact base offset needs verification — common patterns place bucks
 * at register 0x20+ with 0x10 spacing, or packed sequential.
 */

/* Buck register macros — these are the most common PMIC layout patterns.
 * The actual offsets will be verified by probing and comparing with
 * the U-Boot boot log and regulator output voltages.
 *
 * Layout assumption (per buck, 3 regs each, starting at 0x20):
 *   BUCK1: CTRL=0x20, VSEL=0x21, SVSEL=0x22
 *   BUCK2: CTRL=0x23, VSEL=0x24, SVSEL=0x25
 *   ... etc.
 */
#define SPM8821_BUCK_BASE           0x20
#define SPM8821_BUCK_STRIDE         3

#define SPM8821_BUCK_CTRL(n)        (SPM8821_BUCK_BASE + ((n)-1) * SPM8821_BUCK_STRIDE + 0)
#define SPM8821_BUCK_VSEL(n)        (SPM8821_BUCK_BASE + ((n)-1) * SPM8821_BUCK_STRIDE + 1)
#define SPM8821_BUCK_SVSEL(n)       (SPM8821_BUCK_BASE + ((n)-1) * SPM8821_BUCK_STRIDE + 2)

#define SPM8821_BUCK_EN_MASK        0x01    /* Bit 0 = enable */
#define SPM8821_BUCK_VSEL_MASK      0xFF    /* Full byte = voltage select */

/* Number of buck regulators */
#define SPM8821_NUM_BUCKS           6


/* =============================================================================
 * LDO REGULATOR REGISTERS
 * =============================================================================
 * 4 ALDOs + 7 DLDOs = 11 LDOs total.
 *
 * LDO voltage formula:
 *   sel 0x0B-0x7F: voltage = 500000 + (sel - 0x0B) * 25000
 *   Range: ~775 mV to 3.4 V in 25 mV steps
 */

/* ALDO registers (4 analog LDOs) */
#define SPM8821_ALDO_BASE           0x38
#define SPM8821_ALDO_STRIDE         3

#define SPM8821_ALDO_CTRL(n)        (SPM8821_ALDO_BASE + ((n)-1) * SPM8821_ALDO_STRIDE + 0)
#define SPM8821_ALDO_VOLT(n)        (SPM8821_ALDO_BASE + ((n)-1) * SPM8821_ALDO_STRIDE + 1)
#define SPM8821_ALDO_SVOLT(n)       (SPM8821_ALDO_BASE + ((n)-1) * SPM8821_ALDO_STRIDE + 2)

#define SPM8821_ALDO_EN_MASK        0x01
#define SPM8821_ALDO_VSEL_MASK      0x7F
#define SPM8821_NUM_ALDOS           4

/* DLDO registers (7 digital LDOs) */
#define SPM8821_DLDO_BASE           0x44
#define SPM8821_DLDO_STRIDE         3

#define SPM8821_DLDO_CTRL(n)        (SPM8821_DLDO_BASE + ((n)-1) * SPM8821_DLDO_STRIDE + 0)
#define SPM8821_DLDO_VOLT(n)        (SPM8821_DLDO_BASE + ((n)-1) * SPM8821_DLDO_STRIDE + 1)
#define SPM8821_DLDO_SVOLT(n)       (SPM8821_DLDO_BASE + ((n)-1) * SPM8821_DLDO_STRIDE + 2)

#define SPM8821_DLDO_EN_MASK        0x01
#define SPM8821_DLDO_VSEL_MASK      0x7F
#define SPM8821_NUM_DLDOS           7


/* =============================================================================
 * POWER CONTROL
 * =============================================================================
 * Shutdown and reset control — needed for hal_platform_reboot/shutdown.
 * Register addresses TBD — will be discovered via register probing.
 */
#define SPM8821_REG_PWR_CTRL        0xAB    /* Boot log: "Read PMIC reg ab" */


/* =============================================================================
 * GPADC (GENERAL PURPOSE ADC)
 * =============================================================================
 * 12-bit ADC with 6 channels. Used for temperature measurement and
 * voltage monitoring. Accessed via PMIC registers.
 *
 * Channel assignments vary by board design:
 *   - Channel 0: typically VBAT
 *   - Channel 1: typically VBUS/USB
 *   - Channel 2-5: board-specific (temperature NTC, etc.)
 *
 * TODO: Discover GPADC register addresses.
 */
#define SPM8821_GPADC_CTRL         0x80    /* Estimated — needs verification */
#define SPM8821_GPADC_DATA_H       0x81
#define SPM8821_GPADC_DATA_L       0x82


/* =============================================================================
 * STATUS REGISTERS
 * =============================================================================
 * Interrupt status, power-on reason, etc.
 */
#define SPM8821_REG_STATUS          0xA0    /* Estimated */
#define SPM8821_REG_IRQ_STATUS1     0xA1
#define SPM8821_REG_IRQ_STATUS2     0xA2


/* =============================================================================
 * VOLTAGE CONVERSION HELPERS
 * =============================================================================
 */

/**
 * spm8821_buck_sel_to_uv() — Convert buck VSEL register value to microvolts
 */
static inline uint32_t spm8821_buck_sel_to_uv(uint8_t sel)
{
    if (sel <= 0xAA) {
        /* Range 1: 500 mV + sel * 5 mV */
        return 500000 + (uint32_t)sel * 5000;
    } else if (sel <= 0xFE) {
        /* Range 2: 1.375 V + (sel - 0xAB) * 25 mV */
        return 1375000 + (uint32_t)(sel - 0xAB) * 25000;
    }
    return 0;  /* 0xFF = invalid/disabled */
}

/**
 * spm8821_ldo_sel_to_uv() — Convert LDO VSEL register value to microvolts
 */
static inline uint32_t spm8821_ldo_sel_to_uv(uint8_t sel)
{
    if (sel < 0x0B) return 0;  /* Below minimum */
    if (sel > 0x7F) return 0;  /* Above maximum */
    return 500000 + (uint32_t)(sel - 0x0B) * 25000;
}


/* =============================================================================
 * PMIC DRIVER API
 * =============================================================================
 */

/**
 * PMIC information structure — filled by spm8821_init()
 */
typedef struct {
    uint8_t chip_id;            /* Chip identification byte */
    bool    present;            /* true if PMIC responded on I2C */
    int32_t temperature_mc;     /* Temperature in millidegrees C (-1 = N/A) */

    /* Buck voltages in millivolts (0 = disabled/unknown) */
    uint32_t buck_mv[SPM8821_NUM_BUCKS];

    /* LDO voltages in millivolts */
    uint32_t aldo_mv[SPM8821_NUM_ALDOS];
    uint32_t dldo_mv[SPM8821_NUM_DLDOS];
} spm8821_info_t;

/**
 * spm8821_init() — Initialize I2C and probe the PMIC
 *
 * Sets up I2C8, reads chip ID, verifies communication.
 * Must be called before any other spm8821_* functions.
 *
 * @return: 0 on success, -1 if PMIC not responding
 */
int spm8821_init(void);

/**
 * spm8821_read_reg() — Read a single PMIC register
 *
 * @reg:  Register address (0x00-0xFF)
 * @out:  Pointer to store the read value
 * @return: 0 on success, -1 on error
 */
int spm8821_read_reg(uint8_t reg, uint8_t *out);

/**
 * spm8821_write_reg() — Write a single PMIC register
 *
 * @reg:  Register address
 * @val:  Value to write
 * @return: 0 on success, -1 on error
 */
int spm8821_write_reg(uint8_t reg, uint8_t val);

/**
 * spm8821_get_info() — Read all PMIC status into info struct
 *
 * Reads chip ID, all regulator voltages, temperature if available.
 *
 * @info: Pointer to info struct to fill
 * @return: 0 on success
 */
int spm8821_get_info(spm8821_info_t *info);

/**
 * spm8821_get_temperature() — Read temperature from PMIC thermal sensor
 *
 * @temp_mc: Pointer to store temperature in millidegrees Celsius
 * @return: 0 on success, -1 if not available
 */
int spm8821_get_temperature(int32_t *temp_mc);

/**
 * spm8821_dump_registers() — Dump register range to UART for debugging
 *
 * Reads registers from start to end and prints hex values.
 * Essential for reverse-engineering the register map.
 *
 * @start: First register address
 * @end:   Last register address (inclusive)
 */
void spm8821_dump_registers(uint8_t start, uint8_t end);

/**
 * spm8821_reboot() — Trigger system reboot via PMIC
 * @return: 0 on success, -1 if not supported
 */
int spm8821_reboot(void);

/**
 * spm8821_shutdown() — Trigger system power-off via PMIC
 * @return: 0 on success, -1 if not supported
 */
int spm8821_shutdown(void);

#endif /* KYX1_PMIC_SPM8821_H */
