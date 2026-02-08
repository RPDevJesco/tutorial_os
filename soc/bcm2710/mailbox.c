/*
 * soc/bcm2710/mailbox.c - BCM2710 Mailbox Implementation
 *
 * Tutorial-OS: BCM2710 HAL Implementation
 *
 * The mailbox interface is used for ARM <-> VideoCore GPU communication.
 *
 * This is an INTERNAL module - not directly exposed in the HAL API.
 * Instead, hal_platform_*() and hal_display_*() call these functions.
 */

#include "bcm2710_mailbox.h"
#include "hal/hal_types.h"

/* =============================================================================
 * CORE MAILBOX CALL
 * =============================================================================.
 */

bool bcm_mailbox_call(bcm_mailbox_buffer_t *buffer, uint8_t channel)
{
    uint32_t addr = (uint32_t)(uintptr_t)buffer;

    /*
     * Convert ARM physical address to VC bus address.
     * The VideoCore sees memory through a different mapping than the ARM core.
     * 0xC0000000 = L2 cache coherent alias (required for mailbox DMA).
     */
    addr = BCM_ARM_TO_BUS(addr);

    /* Wait for mailbox to not be full */
    while ((hal_mmio_read32(BCM_MBOX_STATUS) & BCM_MBOX_FULL) != 0) {
        HAL_NOP();
    }

    /* Write address with channel in low 4 bits */
    hal_mmio_write32(BCM_MBOX_WRITE, (addr & ~0xF) | (channel & 0xF));

    /* Wait for response */
    while (1) {
        /* Wait for mailbox to not be empty */
        while ((hal_mmio_read32(BCM_MBOX_STATUS) & BCM_MBOX_EMPTY) != 0) {
            HAL_NOP();
        }

        /* Read response */
        uint32_t response = hal_mmio_read32(BCM_MBOX_READ);

        /* Check if it's for our channel */
        if ((response & 0xF) == channel) {
            return buffer->data[1] == BCM_MBOX_RESPONSE_OK;
        }
    }
}

/* =============================================================================
 * MEMORY INFORMATION
 * =============================================================================
 */

bool bcm_mailbox_get_arm_memory(uint32_t *base, uint32_t *size)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;               /* Total size */
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_ARM_MEMORY;
    mbox.data[3] = 8;                   /* Value buffer size */
    mbox.data[4] = 0;                   /* Request */
    mbox.data[5] = 0;                   /* Base (output) */
    mbox.data[6] = 0;                   /* Size (output) */
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (base) *base = mbox.data[5];
    if (size) *size = mbox.data[6];
    return true;
}

bool bcm_mailbox_get_vc_memory(uint32_t *base, uint32_t *size)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_VC_MEMORY;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (base) *base = mbox.data[5];
    if (size) *size = mbox.data[6];
    return true;
}

/* =============================================================================
 * CLOCK INFORMATION
 * =============================================================================
 */

bool bcm_mailbox_get_clock_rate(uint32_t clock_id, uint32_t *rate_hz)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_CLOCK_RATE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;                   /* Request size */
    mbox.data[5] = clock_id;
    mbox.data[6] = 0;                   /* Rate (output) */
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (rate_hz) *rate_hz = mbox.data[6];
    return true;
}

bool bcm_mailbox_get_clock_measured(uint32_t clock_id, uint32_t *rate_hz)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_CLOCK_MEASURED;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = clock_id;
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (rate_hz) *rate_hz = mbox.data[6];
    return true;
}

/* =============================================================================
 * TEMPERATURE
 * =============================================================================
 */

bool bcm_mailbox_get_temperature(uint32_t *temp_mc)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_TEMPERATURE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = 0;                   /* Temperature ID (0 = SOC) */
    mbox.data[6] = 0;                   /* Temperature (output) */
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (temp_mc) *temp_mc = mbox.data[6];
    return true;
}

bool bcm_mailbox_get_max_temperature(uint32_t *temp_mc)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_MAX_TEMP;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = 0;
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (temp_mc) *temp_mc = mbox.data[6];
    return true;
}

/* =============================================================================
 * THROTTLE STATUS
 * =============================================================================
 */

bool bcm_mailbox_get_throttled(uint32_t *status)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_THROTTLED;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = 0;
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (status) *status = mbox.data[6];
    return true;
}

/* =============================================================================
 * BOARD INFORMATION
 * =============================================================================
 */

bool bcm_mailbox_get_board_revision(uint32_t *revision)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 7 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_BOARD_REV;
    mbox.data[3] = 4;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (revision) *revision = mbox.data[5];
    return true;
}

bool bcm_mailbox_get_board_serial(uint64_t *serial)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_BOARD_SERIAL;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (serial) {
        *serial = ((uint64_t)mbox.data[6] << 32) | mbox.data[5];
    }
    return true;
}

/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 */

bool bcm_mailbox_set_power_state(uint32_t device_id, bool on)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_SET_POWER_STATE;
    mbox.data[3] = 8;
    mbox.data[4] = 8;
    mbox.data[5] = device_id;
    mbox.data[6] = on ? 3 : 2;          /* Bit 0 = on, Bit 1 = wait for stable */
    mbox.data[7] = BCM_TAG_END;

    return bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP);
}

bool bcm_mailbox_get_power_state(uint32_t device_id, bool *on)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_POWER_STATE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = device_id;
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    if (on) *on = (mbox.data[6] & 1) != 0;
    return true;
}

/* =============================================================================
 * DISPLAY HELPERS
 * =============================================================================
 */

bool bcm_mailbox_wait_vsync(void)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 7 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_WAIT_FOR_VSYNC;
    mbox.data[3] = 4;
    mbox.data[4] = 4;
    mbox.data[5] = 0;
    mbox.data[6] = BCM_TAG_END;

    return bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP);
}

bool bcm_mailbox_set_virtual_offset(uint32_t x, uint32_t y)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_SET_VIRTUAL_OFFSET;
    mbox.data[3] = 8;
    mbox.data[4] = 8;
    mbox.data[5] = x;
    mbox.data[6] = y;
    mbox.data[7] = BCM_TAG_END;

    return bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP);
}
