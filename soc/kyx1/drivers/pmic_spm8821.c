/*
 * pmic_spm8821.c — SpacemiT SPM8821 (P1) PMIC Driver
 * =====================================================
 *
 * Bare-metal driver for the SPM8821 PMIC on the Orange Pi RV2.
 * Communicates via I2C8 at address 0x41 using our polled I2C driver.
 *
 * WHAT THIS GIVES US:
 *   ✓ Real regulator voltages for Hardware Inspector display
 *   ✓ PMIC chip ID for board identification
 *   ✓ Register dump for reverse-engineering unknown registers
 *   ✓ Foundation for temperature, reboot, shutdown
 *
 * REGISTER MAP STATUS:
 * --------------------
 * The SPM8821 register map is partially documented through open-source
 * kernel and U-Boot drivers. The buck/LDO register offsets are derived
 * from the U-Boot spacemit_p1_regulator.c driver and the Linux
 * spacemit-p1.c regulator driver.
 *
 * IMPORTANT: The register base offsets in pmic_spm8821.h are educated
 * guesses based on common PMIC patterns. The spm8821_dump_registers()
 * function exists specifically to verify these offsets against real
 * hardware. Run it at boot and compare the VSEL register reads against
 * known output voltages from the DTS.
 *
 * VERIFICATION STRATEGY:
 * ----------------------
 * From the Orange Pi RV2 device tree, we know:
 *   BUCK1 (dcdc1) = 950 mV  (CPU cluster supply)
 *   BUCK3 (dcdc3) = configured, boot-on
 *   ALDO1-4 = various rails
 *   DLDO1-7 = various rails
 *
 * After dumping registers 0x00-0xFF, we look for:
 *   - A byte that decodes to ~950 mV via buck formula → that's BUCK1 VSEL
 *   - Enable bits near the VSEL registers
 *   - Chip ID at register 0x00
 *
 * This approach is standard practice for PMIC bring-up when the full
 * datasheet isn't available.
 */

#include "pmic_spm8821.h"
#include "i2c.h"

/* From soc/kyx1/uart.c — debug output */
extern void kyx1_uart_puts(const char *str);
extern void kyx1_uart_puthex(uint64_t val);
extern void kyx1_uart_putc(char c);

/* Module state */
static bool g_pmic_initialized = false;


/* =============================================================================
 * LOW-LEVEL REGISTER ACCESS
 * =============================================================================
 */

int spm8821_read_reg(uint8_t reg, uint8_t *out)
{
    return kyx1_i2c_read_reg(SPM8821_I2C_BASE, SPM8821_I2C_ADDR, reg, out);
}

int spm8821_write_reg(uint8_t reg, uint8_t val)
{
    return kyx1_i2c_write_reg(SPM8821_I2C_BASE, SPM8821_I2C_ADDR, reg, val);
}


/* =============================================================================
 * INITIALIZATION
 * =============================================================================
 */

int spm8821_init(void)
{
    if (g_pmic_initialized)
        return 0;

    kyx1_uart_puts("[pmic] Initializing I2C8 for SPM8821...\n");

    /* Initialize the I2C8 controller */
    if (kyx1_i2c_init(SPM8821_I2C_BASE) < 0) {
        kyx1_uart_puts("[pmic] ERROR: I2C8 init failed (bus stuck?)\n");
        return -1;
    }

    /* Probe: try to read chip ID register */
    uint8_t chip_id = 0;
    if (spm8821_read_reg(SPM8821_REG_CHIP_ID, &chip_id) < 0) {
        kyx1_uart_puts("[pmic] ERROR: No response from SPM8821 at 0x41\n");
        return -1;
    }

    kyx1_uart_puts("[pmic] SPM8821 found! Chip ID: 0x");
    kyx1_uart_puthex(chip_id);
    kyx1_uart_putc('\n');

    g_pmic_initialized = true;
    return 0;
}


/* =============================================================================
 * REGULATOR VOLTAGE READBACK
 * =============================================================================
 */

/*
 * Read a single buck regulator's output voltage.
 * Returns voltage in millivolts, or 0 on error.
 */
static uint32_t read_buck_mv(int buck_num)
{
    if (buck_num < 1 || buck_num > SPM8821_NUM_BUCKS)
        return 0;

    uint8_t vsel = 0;
    if (spm8821_read_reg(SPM8821_BUCK_VSEL(buck_num), &vsel) < 0)
        return 0;

    uint32_t uv = spm8821_buck_sel_to_uv(vsel);
    return uv / 1000;  /* Convert µV to mV */
}

/*
 * Read a single ALDO regulator's output voltage.
 */
static uint32_t read_aldo_mv(int aldo_num)
{
    if (aldo_num < 1 || aldo_num > SPM8821_NUM_ALDOS)
        return 0;

    uint8_t vsel = 0;
    if (spm8821_read_reg(SPM8821_ALDO_VOLT(aldo_num), &vsel) < 0)
        return 0;

    uint32_t uv = spm8821_ldo_sel_to_uv(vsel & SPM8821_ALDO_VSEL_MASK);
    return uv / 1000;
}

/*
 * Read a single DLDO regulator's output voltage.
 */
static uint32_t read_dldo_mv(int dldo_num)
{
    if (dldo_num < 1 || dldo_num > SPM8821_NUM_DLDOS)
        return 0;

    uint8_t vsel = 0;
    if (spm8821_read_reg(SPM8821_DLDO_VOLT(dldo_num), &vsel) < 0)
        return 0;

    uint32_t uv = spm8821_ldo_sel_to_uv(vsel & SPM8821_DLDO_VSEL_MASK);
    return uv / 1000;
}


/* =============================================================================
 * COMPREHENSIVE STATUS READ
 * =============================================================================
 */

int spm8821_get_info(spm8821_info_t *info)
{
    if (!info) return -1;

    /* Zero the struct */
    for (uint32_t i = 0; i < sizeof(spm8821_info_t); i++)
        ((uint8_t *)info)[i] = 0;

    /* Ensure PMIC is initialized */
    if (!g_pmic_initialized) {
        if (spm8821_init() < 0) {
            info->present = false;
            return -1;
        }
    }

    info->present = true;

    /* Read chip ID */
    spm8821_read_reg(SPM8821_REG_CHIP_ID, &info->chip_id);

    /* Read all buck voltages */
    for (int i = 0; i < SPM8821_NUM_BUCKS; i++) {
        info->buck_mv[i] = read_buck_mv(i + 1);
    }

    /* Read all ALDO voltages */
    for (int i = 0; i < SPM8821_NUM_ALDOS; i++) {
        info->aldo_mv[i] = read_aldo_mv(i + 1);
    }

    /* Read all DLDO voltages */
    for (int i = 0; i < SPM8821_NUM_DLDOS; i++) {
        info->dldo_mv[i] = read_dldo_mv(i + 1);
    }

    /* Temperature — not yet implemented */
    info->temperature_mc = -1;

    return 0;
}


/* =============================================================================
 * TEMPERATURE (STUB)
 * =============================================================================
 */

int spm8821_get_temperature(int32_t *temp_mc)
{
    /*
     * The SPM8821 GPADC can measure temperature via an internal sensor
     * or external NTC thermistor. The register addresses for GPADC
     * control and data are not yet confirmed.
     *
     * TODO: After register dump, identify:
     *   1. GPADC enable/channel select register
     *   2. GPADC data registers (12-bit, likely 2 bytes)
     *   3. Conversion formula (ADC counts to temperature)
     *
     * For now, return not-supported.
     */
    if (temp_mc) *temp_mc = 0;
    return -1;
}


/* =============================================================================
 * POWER CONTROL (STUBS)
 * =============================================================================
 */

int spm8821_reboot(void)
{
    /*
     * TODO: Most PMICs have a reset register where writing a magic
     * value triggers a full power cycle. Discover via register dump
     * or SBI SRST extension.
     */
    kyx1_uart_puts("[pmic] Reboot not yet implemented\n");
    return -1;
}

int spm8821_shutdown(void)
{
    kyx1_uart_puts("[pmic] Shutdown not yet implemented\n");
    return -1;
}


/* =============================================================================
 * REGISTER DUMP — THE KEY TOOL FOR REVERSE ENGINEERING
 * =============================================================================
 *
 * This function is the most important debugging tool for PMIC bring-up.
 * It reads every register in a range and dumps them in a hex table
 * format to UART, like this:
 *
 *   [pmic] Register dump 0x00-0xFF:
 *        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
 *   00: 21 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *   10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *   20: 01 5A 5A 01 5A 5A 01 38 38 01 5A 5A 01 5A 5A 01
 *   ...
 *
 * From this output, we can:
 *   1. Find the chip ID (first non-zero byte, usually reg 0x00)
 *   2. Match VSEL values to expected voltages
 *      e.g., BUCK1=950mV → sel = (950000-500000)/5000 = 0x5A → find 0x5A
 *   3. Identify control registers (bytes with bit 0 set = enabled)
 *   4. Find power control and status registers
 *
 * CALL THIS FROM main.c DURING INITIAL BRING-UP, THEN USE THE OUTPUT
 * TO CORRECT THE REGISTER OFFSETS IN pmic_spm8821.h.
 */

/* Helper: print a 2-digit hex byte */
static void put_hex8(uint8_t val)
{
    const char hex[] = "0123456789ABCDEF";
    kyx1_uart_putc(hex[(val >> 4) & 0xF]);
    kyx1_uart_putc(hex[val & 0xF]);
}

void spm8821_dump_registers(uint8_t start, uint8_t end)
{
    if (!g_pmic_initialized) {
        kyx1_uart_puts("[pmic] Not initialized — call spm8821_init() first\n");
        return;
    }

    kyx1_uart_puts("\n[pmic] SPM8821 register dump 0x");
    put_hex8(start);
    kyx1_uart_puts("-0x");
    put_hex8(end);
    kyx1_uart_puts(":\n");

    /* Print column header */
    kyx1_uart_puts("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    /* Align start to 16-byte boundary for clean display */
    uint8_t row_start = start & 0xF0;

    for (uint16_t row = row_start; row <= (uint16_t)(end | 0x0F); row += 16) {
        /* Row label */
        put_hex8((uint8_t)(row >> 4));
        kyx1_uart_putc('0');
        kyx1_uart_putc(':');

        for (uint8_t col = 0; col < 16; col++) {
            uint8_t addr = (uint8_t)(row + col);
            kyx1_uart_putc(' ');

            if (addr < start || addr > end) {
                /* Outside requested range */
                kyx1_uart_puts("--");
            } else {
                uint8_t val = 0;
                if (spm8821_read_reg(addr, &val) == 0) {
                    put_hex8(val);
                } else {
                    kyx1_uart_puts("??");  /* Read failed */
                }
            }
        }
        kyx1_uart_putc('\n');
    }

    kyx1_uart_puts("[pmic] Dump complete\n\n");
}


/* =============================================================================
 * PRETTY-PRINT STATUS (FOR UART DEBUG)
 * =============================================================================
 */

/* Helper: print decimal number */
static void put_dec(uint32_t val)
{
    char buf[12];
    int i = 0;

    if (val == 0) {
        kyx1_uart_putc('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    /* Print in reverse */
    while (i > 0) {
        kyx1_uart_putc(buf[--i]);
    }
}

void spm8821_print_status(void)
{
    spm8821_info_t info;

    if (spm8821_get_info(&info) < 0) {
        kyx1_uart_puts("[pmic] Failed to read PMIC status\n");
        return;
    }

    kyx1_uart_puts("\n=== SPM8821 PMIC Status ===\n");
    kyx1_uart_puts("Chip ID: 0x");
    put_hex8(info.chip_id);
    kyx1_uart_putc('\n');

    /* Buck regulators */
    for (int i = 0; i < SPM8821_NUM_BUCKS; i++) {
        kyx1_uart_puts("  BUCK");
        kyx1_uart_putc('1' + i);
        kyx1_uart_puts(": ");
        if (info.buck_mv[i] > 0) {
            put_dec(info.buck_mv[i]);
            kyx1_uart_puts(" mV\n");
        } else {
            kyx1_uart_puts("OFF/unknown\n");
        }
    }

    /* ALDO regulators */
    for (int i = 0; i < SPM8821_NUM_ALDOS; i++) {
        kyx1_uart_puts("  ALDO");
        kyx1_uart_putc('1' + i);
        kyx1_uart_puts(": ");
        if (info.aldo_mv[i] > 0) {
            put_dec(info.aldo_mv[i]);
            kyx1_uart_puts(" mV\n");
        } else {
            kyx1_uart_puts("OFF/unknown\n");
        }
    }

    /* DLDO regulators */
    for (int i = 0; i < SPM8821_NUM_DLDOS; i++) {
        kyx1_uart_puts("  DLDO");
        kyx1_uart_putc('1' + i);
        kyx1_uart_puts(": ");
        if (info.dldo_mv[i] > 0) {
            put_dec(info.dldo_mv[i]);
            kyx1_uart_puts(" mV\n");
        } else {
            kyx1_uart_puts("OFF/unknown\n");
        }
    }

    kyx1_uart_puts("===========================\n\n");
}
