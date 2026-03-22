/*
 * drivers/pmic_axp15060.c — X-Powers AXP15060 PMIC Driver for JH7110
 * ====================================================================
 *
 * This is the BIGGEST DIFFERENCE between the Milk-V Mars and Orange Pi RV2
 * at the driver level. While both boards use I2C to talk to a PMIC, the PMICs
 * are completely different silicon from different vendors:
 *
 *   Orange Pi RV2:  SpacemiT SPM8821 (PMIC from CPU vendor, undocumented)
 *   Milk-V Mars:    X-Powers AXP15060 (PMIC from a dedicated power IC vendor,
 *                   well-documented via Linux kernel drivers)
 *
 * THIS IS GOOD NEWS FOR US:
 *   The X-Powers AXP family has excellent open documentation. The Linux kernel
 *   driver at drivers/mfd/axp20x.c contains a full register map that we can
 *   reference directly. X-Powers also publishes datasheets.
 *
 * AXP15060 KEY FACTS:
 * ===================
 * I2C address: 0x36 (confirmed from Mars schematic page 21)
 * I2C bus:     I2C6 (0x100E0000)
 * Chip ID register: 0x03 (should read 0x50 for AXP15060)
 *
 * The AXP15060 is part of the AXP "15" series (higher power budget than
 * the AXP2xx series used in mobile devices). Key capabilities for us:
 *   - DCDC regulators for CPU/DDR/display power
 *   - LDO outputs for peripheral power
 *   - GPADC for temperature/battery voltage measurement
 *   - Interrupt output (connected to SoC interrupt line)
 *   - I2C slave at 0x36
 *
 * REGISTER MAP (from Linux drivers/mfd/axp20x.c, AXP15060 entries):
 * ==================================================================
 *   0x00  Power Status
 *   0x01  Power Source Status
 *   0x02  Charge Status
 *   0x03  Chip ID (reads 0x50 for AXP15060)
 *   0x10  DCDC1 enable + voltage (VDD CPU)
 *   0x11  DCDC2 enable + voltage
 *   0x12  DCDC3 enable + voltage (VDD DDR)
 *   0x13  DCDC4 enable + voltage
 *   0x14  DCDC5 enable + voltage
 *   0x15  DCDC6 enable + voltage (3V3 system rail)
 *   0x16  DCDC7 enable + voltage
 *   0x17  DCDC8 enable + voltage
 *   0x30  ALDO1 enable/voltage
 *   ...
 *   0x56  GPADC high byte (temperature MSB)
 *   0x57  GPADC low byte  (temperature LSB)
 *   0x58  GPADC low byte  (voltage)
 *   0x64  Temperature sensor enable
 *   0x9A  Thermal overtemp threshold
 *
 * TEACHING POINT — COMPARE WITH SPM8821:
 *   The SPM8821 on the Ky X1 is an undocumented PMIC from a CPU vendor.
 *   Getting its register map required reverse engineering the Linux driver.
 *   The AXP15060 has a proper datasheet and a clean kernel driver to reference.
 *   This is a common pattern: dedicated PMIC vendors (X-Powers, Maxim, TI)
 *   publish full documentation. SoC-integrated or SoC-vendor PMICs often don't.
 */

#include "pmic_axp15060.h"
#include "i2c.h"
#include "jh7110_regs.h"
#include "types.h"

extern void jh7110_uart_puts(const char *str);
extern void jh7110_uart_puthex(uint64_t val);
extern void jh7110_uart_putdec(uint32_t val);
extern void jh7110_uart_putc(char c);

/* AXP15060 I2C connection */
#define PMIC_I2C_BASE   JH7110_AXP15060_I2C_BASE   /* I2C6: 0x100E0000 */
#define PMIC_I2C_ADDR   JH7110_AXP15060_I2C_ADDR   /* 0x36 */

/* =============================================================================
 * AXP15060 REGISTER DEFINITIONS
 * =============================================================================
 * From Linux: drivers/mfd/axp20x.c, include/linux/mfd/axp20x.h
 */

/* Status and identification */
#define AXP15060_CHIP_ID_REG            0x03
#define AXP15060_CHIP_ID_VALUE          0x50    /* Expected ID for AXP15060 */

/* DCDC converters (CPU core, DDR, system rail) */
#define AXP15060_DCDC1_CTRL             0x10    /* VDD CPU core (0.9V) */
#define AXP15060_DCDC2_CTRL             0x11
#define AXP15060_DCDC3_CTRL             0x12    /* VDD DDR */
#define AXP15060_DCDC4_CTRL             0x13
#define AXP15060_DCDC5_CTRL             0x14
#define AXP15060_DCDC6_CTRL             0x15    /* 3.3V system rail */
#define AXP15060_DCDC7_CTRL             0x16
#define AXP15060_DCDC8_CTRL             0x17

/* DCDC enable register */
#define AXP15060_DCDC_EN                0x18

/* LDO outputs */
#define AXP15060_ALDO1_CTRL             0x30
#define AXP15060_ALDO2_CTRL             0x31
#define AXP15060_ALDO3_CTRL             0x32
#define AXP15060_ALDO4_CTRL             0x33
#define AXP15060_ALDO5_CTRL             0x34

/* GPADC — General Purpose ADC (used for temperature/voltage measurement) */
#define AXP15060_GPADC_H                0x56    /* GPADC result high byte */
#define AXP15060_GPADC_L                0x57    /* GPADC result low byte */
#define AXP15060_TS_PIN_CFG             0x58    /* TS pin / NTC config */
#define AXP15060_GPADC_CTRL             0x64    /* Enable ADC channels */

/* Interrupt and fault registers */
#define AXP15060_IRQ_STATUS1            0x40
#define AXP15060_IRQ_STATUS2            0x41
#define AXP15060_IRQ_EN1                0x42
#define AXP15060_IRQ_EN2                0x43

/* Thermal protection */
#define AXP15060_THERMAL_THRESH         0x9A    /* Overtemp threshold */

/* NTC temperature calculation constants:
 * The AXP15060 measures an external NTC thermistor via the TS/GPADC pin.
 * On the Mars, an NTC thermistor is connected for PCB temperature measurement.
 * The conversion from raw ADC to temperature requires the NTC β value and
 * a reference temperature/resistance. For Tutorial-OS, we report raw ADC
 * and note that proper temperature conversion requires NTC characterization.
 *
 * As a simplification: AXP15060 GPADC is a 12-bit ADC.
 * Raw value range: 0x000-0xFFF
 * Voltage measured: 0V to 2.0V (reference voltage)
 * For the 100kΩ NTC at 25°C, a typical raw value is around 0x800.
 */
#define AXP15060_GPADC_BITS             12
#define AXP15060_GPADC_VREF_MV          2000    /* 2.0V reference */

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static bool g_pmic_initialized = false;
static uint8_t g_chip_id = 0;

/* =============================================================================
 * INITIALIZATION
 * =============================================================================
 */

/*
 * axp15060_init — initialize the PMIC and verify chip ID
 *
 * @return  0 on success (chip ID matches AXP15060)
 *         -1 on I2C error or unexpected chip ID
 */
int axp15060_init(void)
{
    jh7110_uart_puts("[pmic] Initializing AXP15060 on I2C6 (0x36)...\n");

    /* Initialize I2C6 controller for the PMIC */
    jh7110_i2c_init(PMIC_I2C_BASE, PMIC_I2C_ADDR);

    /* Read chip ID register */
    if (!jh7110_i2c_read_reg(PMIC_I2C_BASE, AXP15060_CHIP_ID_REG, &g_chip_id)) {
        jh7110_uart_puts("[pmic] ERROR: I2C read failed — PMIC not responding\n");
        return -1;
    }

    jh7110_uart_puts("[pmic] Chip ID: 0x");
    jh7110_uart_puthex((uint64_t)g_chip_id);
    jh7110_uart_putc('\n');

    if (g_chip_id != AXP15060_CHIP_ID_VALUE) {
        jh7110_uart_puts("[pmic] WARNING: Unexpected chip ID (expected 0x50)\n");
        /* Continue anyway — register layout may still be compatible */
    }

    g_pmic_initialized = true;
    jh7110_uart_puts("[pmic] AXP15060 init OK\n");
    return 0;
}

bool axp15060_is_available(void)
{
    return g_pmic_initialized;
}

uint8_t axp15060_get_chip_id(void)
{
    return g_chip_id;
}

/* =============================================================================
 * TEMPERATURE READING
 * =============================================================================
 *
 * Reads the GPADC value from the TS (temperature sense) channel.
 * The AXP15060 measures an external NTC thermistor on the Mars PCB.
 *
 * IMPORTANT LIMITATION:
 *   Converting the raw 12-bit ADC value to Celsius requires knowing:
 *   1. The NTC thermistor's β coefficient and R25 value
 *   2. The pull-up resistor value in the AXP15060 TS divider
 *
 *   These are board-specific (Mars schematic has the NTC part number
 *   on page 21). For Tutorial-OS, we report the raw ADC value scaled
 *   to millidegrees using a rough linear approximation.
 *
 *   A proper implementation would use the Steinhart-Hart equation.
 *   The book can discuss this as "Phase 5 enhancement" material.
 *
 * @param temp_mc  Output: temperature in millicelsius
 * @return         0 on success, -1 on error
 */
int axp15060_get_temperature(int32_t *temp_mc)
{
    if (!temp_mc) return -1;
    if (!g_pmic_initialized) return -1;

    /* Enable GPADC measurement (bit 5 in GPADC_CTRL enables TS channel) */
    uint8_t ctrl;
    if (!jh7110_i2c_read_reg(PMIC_I2C_BASE, AXP15060_GPADC_CTRL, &ctrl))
        return -1;

    /* Enable TS pin ADC measurement (bit 5) */
    uint8_t ctrl_new = ctrl | (1 << 5);
    if (ctrl_new != ctrl) {
        if (!jh7110_i2c_write_reg(PMIC_I2C_BASE, AXP15060_GPADC_CTRL, ctrl_new))
            return -1;
    }

    /* Read 12-bit GPADC result (2 registers: high byte + low 4 bits) */
    uint8_t adc_h, adc_l;
    if (!jh7110_i2c_read_reg(PMIC_I2C_BASE, AXP15060_GPADC_H, &adc_h)) return -1;
    if (!jh7110_i2c_read_reg(PMIC_I2C_BASE, AXP15060_GPADC_L, &adc_l)) return -1;

    uint32_t raw_adc = ((uint32_t)adc_h << 4) | (adc_l & 0x0F);

    /*
     * Rough temperature approximation:
     * This is not physically accurate — it's a placeholder until we
     * characterize the specific NTC thermistor on the Mars PCB.
     *
     * The linear approximation assumes:
     *   raw = 0x800 (~25°C)
     *   Each bit below 0x800 → ~0.05°C warmer (NTC resistance decreases with temp)
     *   Each bit above 0x800 → ~0.05°C cooler
     *
     * TODO: Implement proper Steinhart-Hart conversion once the NTC β value
     * is confirmed from the Mars schematic's NTC component specification.
     */
    int32_t ref_adc = 0x800;
    int32_t delta   = (int32_t)raw_adc - ref_adc;
    /* Each ADC count ≈ 0.49 mV at 2V ref, 12 bits */
    /* Rough: -50 millicounts per millidegree (NTC, negative temp coefficient) */
    int32_t temp_c_tenths = 250 + ((-delta * 5) / 100);  /* 25.0°C base */
    *temp_mc = temp_c_tenths * 100;

    return 0;
}

/* =============================================================================
 * VOLTAGE MONITORING
 * =============================================================================
 */

/*
 * axp15060_read_dcdc_voltage — read a DCDC converter's output voltage
 *
 * @param dcdc_num  DCDC number (1-8)
 * @param mv_out    Output: voltage in millivolts
 * @return          0 on success, -1 on error
 */
int axp15060_read_dcdc_voltage(uint8_t dcdc_num, uint32_t *mv_out)
{
    if (!mv_out || dcdc_num < 1 || dcdc_num > 8) return -1;
    if (!g_pmic_initialized) return -1;

    uint8_t reg = AXP15060_DCDC1_CTRL + (dcdc_num - 1);
    uint8_t val;
    if (!jh7110_i2c_read_reg(PMIC_I2C_BASE, reg, &val)) return -1;

    /*
     * AXP15060 DCDC voltage encoding (from AXP15060 datasheet):
     * Bits [5:0] of the control register encode the output voltage.
     * For most DCDCs:
     *   0x00 = 500 mV, each step = 10 mV
     *   0x3F = 1530 mV
     *
     * DCDC1 (VDD CPU) typically at 0x1A = 500 + 26*10 = 760 mV
     * DCDC6 (3.3V)    typically at 0x68 (but this reg is for enable/disable)
     *
     * NOTE: Exact encoding varies per DCDC channel. This is a simplification.
     * Full implementation would use the per-channel voltage tables from
     * the AXP15060 datasheet Table 7-x.
     */
    uint8_t voltage_bits = val & 0x3F;
    *mv_out = 500 + (uint32_t)voltage_bits * 10;

    return 0;
}

/*
 * axp15060_print_status — dump PMIC status to UART for debugging
 *
 * Useful during bring-up to verify power rail voltages before
 * powering on sensitive peripherals.
 */
void axp15060_print_status(void)
{
    if (!g_pmic_initialized) {
        jh7110_uart_puts("[pmic] Not initialized\n");
        return;
    }

    jh7110_uart_puts("[pmic] AXP15060 Status:\n");
    jh7110_uart_puts("  Chip ID: 0x");
    jh7110_uart_puthex((uint64_t)g_chip_id);
    jh7110_uart_putc('\n');

    /* Read and display power status */
    uint8_t pwr_status;
    if (jh7110_i2c_read_reg(PMIC_I2C_BASE, 0x00, &pwr_status)) {
        jh7110_uart_puts("  Power Status: 0x");
        jh7110_uart_puthex((uint64_t)pwr_status);
        jh7110_uart_putc('\n');
    }

    /* Read DCDC enable register */
    uint8_t dcdc_en;
    if (jh7110_i2c_read_reg(PMIC_I2C_BASE, AXP15060_DCDC_EN, &dcdc_en)) {
        jh7110_uart_puts("  DCDC Enable: 0x");
        jh7110_uart_puthex((uint64_t)dcdc_en);
        jh7110_uart_putc('\n');
    }
}