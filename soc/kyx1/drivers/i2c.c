/*
 * i2c.c — SpacemiT K1 I2C Controller Driver (Polled Mode)
 * =========================================================
 *
 * Bare-metal polled I2C driver for the PXA-compatible I2C controllers
 * in the SpacemiT K1 SoC. Used to communicate with the SPM8821 PMIC
 * on I2C8 (base 0xD401D800, device address 0x41).
 *
 * PROTOCOL:
 * ---------
 * PXA I2C uses a state-machine approach:
 *   1. Load data byte into IDBR
 *   2. Set control bits in ICR (START, STOP, TB, ACKNAK)
 *   3. Wait for status in ISR (ITE for tx done, IRF for rx done)
 *   4. Clear status flags by writing 1 to ISR
 *   5. Repeat for each byte
 *
 * A register read is a "combined" I2C transaction:
 *   START → [addr+W] → [reg] → RESTART → [addr+R] → [data] → STOP
 *
 * ASSUMPTIONS:
 * - U-Boot has already configured I2C8 clock gating and pinmux
 * - We operate at 100 kHz standard mode (ILCR default values)
 * - No DMA, no interrupts — pure polling
 */

#include "i2c.h"

/* ---- Low-level MMIO access ---- */

static inline uint32_t i2c_read32(uintptr_t base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static inline void i2c_write32(uintptr_t base, uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(base + offset) = val;
}


/* ---- Timer (from rdtime CSR) ---- */

static inline uint64_t rdtime(void)
{
    uint64_t t;
    __asm__ volatile("rdtime %0" : "=r"(t));
    return t;
}

/* K1 timebase is 24 MHz */
#define TICKS_PER_US    24

static inline void delay_us(uint32_t us)
{
    uint64_t start = rdtime();
    uint64_t target = (uint64_t)us * TICKS_PER_US;
    while ((rdtime() - start) < target)
        ;
}


/* ---- Internal helpers ---- */

/*
 * Wait for a specific ISR flag to be set, with timeout.
 * Returns 0 on success, -1 on timeout.
 */
static int i2c_wait_flag(uintptr_t base, uint32_t flag, uint32_t timeout_us)
{
    uint64_t start = rdtime();
    uint64_t deadline = (uint64_t)timeout_us * TICKS_PER_US;

    while (!(i2c_read32(base, I2C_ISR) & flag)) {
        if ((rdtime() - start) > deadline)
            return -1;
    }
    return 0;
}

/*
 * Check if NAK was received after a transfer.
 * Returns true if NAK detected.
 */
static bool i2c_got_nak(uintptr_t base)
{
    return (i2c_read32(base, I2C_ISR) & ISR_ACKNAK) != 0;
}

/*
 * Clear all pending status flags.
 */
static void i2c_clear_status(uintptr_t base)
{
    i2c_write32(base, I2C_ISR, ISR_CLEAR_ALL);
}

/*
 * Wait for bus to become not busy.
 * Returns 0 on success, -1 on timeout.
 */
static int i2c_wait_bus_free(uintptr_t base)
{
    uint64_t start = rdtime();
    uint64_t deadline = (uint64_t)I2C_TIMEOUT_US * TICKS_PER_US;

    while (i2c_read32(base, I2C_ISR) & ISR_IBB) {
        if ((rdtime() - start) > deadline)
            return -1;
    }
    return 0;
}


/* =============================================================================
 * PUBLIC API
 * =============================================================================
 */

int kyx1_i2c_init(uintptr_t base)
{
    /*
     * Step 1: Unit reset
     * Writing UR bit resets the controller state machine.
     * Must clear UR afterward to allow normal operation.
     */
    i2c_write32(base, I2C_ICR, ICR_UR);
    delay_us(10);
    i2c_write32(base, I2C_ICR, 0);
    delay_us(10);

    /* Step 2: Clear any pending status */
    i2c_clear_status(base);

    /*
     * Step 3: Enable I2C unit
     *   IUE  = 1  (unit enable)
     *   SCLE = 1  (master mode, drive SCL)
     *   GCD  = 1  (disable general call — we don't need it)
     *
     * We do NOT set MODE_FAST — U-Boot configured ILCR for 100 kHz
     * standard mode, which is safe for PMIC communication.
     */
    i2c_write32(base, I2C_ICR, ICR_IUE | ICR_SCLE | ICR_GCD);

    /*
     * Step 4: Verify bus is free
     * Check IBMR — both SDA and SCL should be high (pulled up).
     */
    uint32_t bmr = i2c_read32(base, I2C_IBMR);
    if (!(bmr & IBMR_SDA) || !(bmr & IBMR_SCL)) {
        /*
         * Bus is stuck. Try sending 9 clock pulses to unstick.
         * This is a standard I2C recovery procedure.
         */
        for (int i = 0; i < 9; i++) {
            i2c_write32(base, I2C_ICR, ICR_IUE | ICR_SCLE | ICR_GCD |
                         ICR_MA | ICR_TB);
            delay_us(100);
        }
        i2c_write32(base, I2C_ICR, ICR_IUE | ICR_SCLE | ICR_GCD);
        delay_us(100);

        bmr = i2c_read32(base, I2C_IBMR);
        if (!(bmr & IBMR_SDA) || !(bmr & IBMR_SCL)) {
            return -1;  /* Bus still stuck */
        }
    }

    return 0;
}


int kyx1_i2c_read_reg(uintptr_t base, uint8_t dev_addr,
                       uint8_t reg, uint8_t *out)
{
    uint32_t icr_base = ICR_IUE | ICR_SCLE | ICR_GCD;

    /* Ensure bus is free */
    if (i2c_wait_bus_free(base) < 0)
        return -1;

    i2c_clear_status(base);

    /*
     * Phase 1: Write register address
     * START → [dev_addr << 1 | 0 (write)] → wait ITE
     */
    i2c_write32(base, I2C_IDBR, (dev_addr << 1) | 0);  /* Write mode */
    i2c_write32(base, I2C_ICR, icr_base | ICR_START | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0)
        return -1;
    if (i2c_got_nak(base)) {
        /* Device didn't ACK — send STOP and bail */
        i2c_write32(base, I2C_ICR, icr_base | ICR_STOP | ICR_TB);
        i2c_clear_status(base);
        return -1;
    }
    i2c_clear_status(base);

    /*
     * Phase 2: Send register address byte
     * [reg] → wait ITE
     */
    i2c_write32(base, I2C_IDBR, reg);
    i2c_write32(base, I2C_ICR, icr_base | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0)
        return -1;
    if (i2c_got_nak(base)) {
        i2c_write32(base, I2C_ICR, icr_base | ICR_STOP | ICR_TB);
        i2c_clear_status(base);
        return -1;
    }
    i2c_clear_status(base);

    /*
     * Phase 3: Repeated START for read
     * RESTART → [dev_addr << 1 | 1 (read)] → wait ITE
     */
    i2c_write32(base, I2C_IDBR, (dev_addr << 1) | 1);  /* Read mode */
    i2c_write32(base, I2C_ICR, icr_base | ICR_START | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0)
        return -1;
    if (i2c_got_nak(base)) {
        i2c_write32(base, I2C_ICR, icr_base | ICR_STOP | ICR_TB);
        i2c_clear_status(base);
        return -1;
    }
    i2c_clear_status(base);

    /*
     * Phase 4: Read data byte
     * Set STOP + ACKNAK (NAK the last/only byte) + TB
     * Wait for IRF (receive full), then read IDBR
     */
    i2c_write32(base, I2C_ICR, icr_base | ICR_STOP | ICR_ACKNAK | ICR_TB);

    if (i2c_wait_flag(base, ISR_IRF, I2C_TIMEOUT_US) < 0)
        return -1;

    *out = (uint8_t)(i2c_read32(base, I2C_IDBR) & 0xFF);
    i2c_clear_status(base);

    return 0;
}


int kyx1_i2c_write_reg(uintptr_t base, uint8_t dev_addr,
                        uint8_t reg, uint8_t val)
{
    uint32_t icr_base = ICR_IUE | ICR_SCLE | ICR_GCD;

    if (i2c_wait_bus_free(base) < 0)
        return -1;

    i2c_clear_status(base);

    /*
     * Phase 1: START → [dev_addr + W]
     */
    i2c_write32(base, I2C_IDBR, (dev_addr << 1) | 0);
    i2c_write32(base, I2C_ICR, icr_base | ICR_START | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0)
        return -1;
    if (i2c_got_nak(base)) {
        i2c_write32(base, I2C_ICR, icr_base | ICR_STOP | ICR_TB);
        i2c_clear_status(base);
        return -1;
    }
    i2c_clear_status(base);

    /*
     * Phase 2: [register address]
     */
    i2c_write32(base, I2C_IDBR, reg);
    i2c_write32(base, I2C_ICR, icr_base | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0)
        return -1;
    if (i2c_got_nak(base)) {
        i2c_write32(base, I2C_ICR, icr_base | ICR_STOP | ICR_TB);
        i2c_clear_status(base);
        return -1;
    }
    i2c_clear_status(base);

    /*
     * Phase 3: [data byte] + STOP
     */
    i2c_write32(base, I2C_IDBR, val);
    i2c_write32(base, I2C_ICR, icr_base | ICR_STOP | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0)
        return -1;
    i2c_clear_status(base);

    return 0;
}


int kyx1_i2c_read_bulk(uintptr_t base, uint8_t dev_addr,
                        uint8_t reg, uint8_t *buf, uint32_t len)
{
    if (len == 0) return 0;
    if (len == 1) return kyx1_i2c_read_reg(base, dev_addr, reg, buf);

    uint32_t icr_base = ICR_IUE | ICR_SCLE | ICR_GCD;

    if (i2c_wait_bus_free(base) < 0)
        return -1;

    i2c_clear_status(base);

    /* Phase 1: START → [addr+W] */
    i2c_write32(base, I2C_IDBR, (dev_addr << 1) | 0);
    i2c_write32(base, I2C_ICR, icr_base | ICR_START | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0) return -1;
    if (i2c_got_nak(base)) goto fail;
    i2c_clear_status(base);

    /* Phase 2: [register address] */
    i2c_write32(base, I2C_IDBR, reg);
    i2c_write32(base, I2C_ICR, icr_base | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0) return -1;
    if (i2c_got_nak(base)) goto fail;
    i2c_clear_status(base);

    /* Phase 3: RESTART → [addr+R] */
    i2c_write32(base, I2C_IDBR, (dev_addr << 1) | 1);
    i2c_write32(base, I2C_ICR, icr_base | ICR_START | ICR_TB);

    if (i2c_wait_flag(base, ISR_ITE, I2C_TIMEOUT_US) < 0) return -1;
    if (i2c_got_nak(base)) goto fail;
    i2c_clear_status(base);

    /* Phase 4: Read bytes — ACK all except last, which gets NAK+STOP */
    for (uint32_t i = 0; i < len; i++) {
        bool last = (i == len - 1);

        if (last) {
            /* Last byte: NAK + STOP */
            i2c_write32(base, I2C_ICR,
                         icr_base | ICR_STOP | ICR_ACKNAK | ICR_TB);
        } else {
            /* Not last: ACK (ACKNAK=0) */
            i2c_write32(base, I2C_ICR, icr_base | ICR_TB);
        }

        if (i2c_wait_flag(base, ISR_IRF, I2C_TIMEOUT_US) < 0)
            return -1;

        buf[i] = (uint8_t)(i2c_read32(base, I2C_IDBR) & 0xFF);
        i2c_clear_status(base);
    }

    return 0;

fail:
    i2c_write32(base, I2C_ICR, icr_base | ICR_STOP | ICR_TB);
    i2c_clear_status(base);
    return -1;
}
