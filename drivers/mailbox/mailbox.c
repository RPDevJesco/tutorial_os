/*
 * mailbox.c - VideoCore Mailbox Implementation
 * ============================================
 *
 * This implements the mailbox interface for communicating with the
 * VideoCore GPU on the Raspberry Pi.
 *
 * See mailbox.h for API documentation and message format details.
 */

#include "mailbox.h"


/* =============================================================================
 * CORE MAILBOX IMPLEMENTATION
 * =============================================================================
 */

bool mailbox_call(mailbox_buffer_t *buffer, uint8_t channel)
{
    uint32_t addr = (uint32_t)(uintptr_t)buffer;

    /* Convert ARM physical address to VC bus address (same as boot.S)
    *  The VideoCore sees memory through a different mapping than the ARM core.
    * 0xC0000000 = L2 cache coherent alias (required for mailbox DMA). */
    addr |= 0xC0000000;

    /* Wait for mailbox to not be full */
    while ((mmio_read(MBOX_STATUS) & MBOX_FULL) != 0) {
        __asm__ volatile("yield");
    }

    mmio_write(MBOX_WRITE, (addr & ~0xF) | (channel & 0xF));

    while (1) {
        while ((mmio_read(MBOX_STATUS) & MBOX_EMPTY) != 0) {
            __asm__ volatile("yield");
        }

        uint32_t response = mmio_read(MBOX_READ);

        if ((response & 0xF) == channel) {
            return buffer->data[1] == MBOX_RESPONSE_SUCCESS;
        }
    }
}

/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 */

bool mailbox_set_power_state(uint32_t device_id, bool on)
{
    mailbox_buffer_t mbox = { .data = {0} };

    /*
     * Build the SET_POWER_STATE message
     *
     * Structure:
     *   [0] Total size = 8 words x 4 bytes = 32 bytes
     *   [1] Request code = 0
     *   [2] Tag ID = SET_POWER_STATE (0x28001)
     *   [3] Value buffer size = 8 bytes (2 words)
     *   [4] Request/response indicator = 8 (request size)
     *   [5] Device ID
     *   [6] State (bit 0 = on, bit 1 = wait for stable)
     *   [7] End tag = 0
         ┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
         │ 32      │ 0       │ 0x28001 │ 8       │ 8       │ dev_id  │ state   │ 0       │
         ├─────────┼─────────┼─────────┼─────────┼─────────┼─────────┼─────────┼─────────┤
         │ size    │ request │ tag     │ val_sz  │ req_sz  │ input   │ input   │ end     │
         └─────────┴─────────┴─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
     */

    mbox.data[0] = 8 * 4;           /* Total buffer size */
    mbox.data[1] = MBOX_REQUEST;    /* Request code */
    mbox.data[2] = TAG_SET_POWER_STATE;
    mbox.data[3] = 8;               /* Value buffer size */
    mbox.data[4] = 8;               /* Request size */
    mbox.data[5] = device_id;
    mbox.data[6] = on ? 3 : 0;      /* 3 = on + wait, 0 = off */
    mbox.data[7] = TAG_END;

    return mailbox_call(&mbox, MBOX_CHANNEL_PROP) && (mbox.data[6] & 1) != 0;
}

bool mailbox_get_power_state(uint32_t device_id, bool *exists, bool *is_on)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_POWER_STATE;
    mbox.data[3] = 8;
    mbox.data[4] = 0;               /* Request size = 0 for GET */
    mbox.data[5] = device_id;
    mbox.data[6] = 0;
    mbox.data[7] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        if (exists) *exists = false;
        if (is_on) *is_on = false;
        return false;
    }

    /*
     * Response in data[6]:
     *   Bit 0 = Device is on
     *   Bit 1 = Device does not exist
     */
    if (exists) *exists = (mbox.data[6] & 2) == 0;
    if (is_on) *is_on = (mbox.data[6] & 1) != 0;

    return true;
}


/* =============================================================================
 * MEMORY INFO
 * =============================================================================
 */

bool mailbox_get_arm_memory(uint32_t *base, uint32_t *size)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_ARM_MEMORY;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = 0;
    mbox.data[7] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        return false;
    }

    if (base) *base = mbox.data[5];
    if (size) *size = mbox.data[6];

    return true;
}

bool mailbox_get_vc_memory(uint32_t *base, uint32_t *size)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_VC_MEMORY;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = 0;
    mbox.data[7] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        return false;
    }

    if (base) *base = mbox.data[5];
    if (size) *size = mbox.data[6];

    return true;
}


/* =============================================================================
 * BOARD INFO
 * =============================================================================
 */

bool mailbox_get_board_serial(uint64_t *serial)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_BOARD_SERIAL;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = 0;
    mbox.data[7] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        return false;
    }

    if (serial) {
        *serial = ((uint64_t)mbox.data[6] << 32) | mbox.data[5];
    }

    return true;
}


/* =============================================================================
 * TEMPERATURE
 * =============================================================================
 */

bool mailbox_get_temperature(uint32_t *temp_millicelsius)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_TEMPERATURE;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = 0;               /* Temperature ID (0 = SoC) */
    mbox.data[6] = 0;               /* Response: temperature */
    mbox.data[7] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        return false;
    }

    /* Check that response indicator shows success */
    if ((mbox.data[4] & 0x80000000) == 0) {
        return false;
    }

    if (temp_millicelsius) *temp_millicelsius = mbox.data[6];

    return true;
}

bool mailbox_get_max_temperature(uint32_t *temp_millicelsius)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_MAX_TEMPERATURE;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = 0;               /* Temperature ID (0 = SoC) */
    mbox.data[6] = 0;               /* Response: max temperature */
    mbox.data[7] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        return false;
    }

    if ((mbox.data[4] & 0x80000000) == 0) {
        return false;
    }

    if (temp_millicelsius) *temp_millicelsius = mbox.data[6];

    return true;
}


/* =============================================================================
 * CLOCKS
 * =============================================================================
 */

bool mailbox_get_clock_rate(uint32_t clock_id, uint32_t *rate_hz)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_CLOCK_RATE;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = clock_id;
    mbox.data[6] = 0;               /* Response: rate */
    mbox.data[7] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        return false;
    }

    if ((mbox.data[4] & 0x80000000) == 0) {
        return false;
    }

    if (rate_hz) *rate_hz = mbox.data[6];

    return true;
}

bool mailbox_get_max_clock_rate(uint32_t clock_id, uint32_t *rate_hz)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 8 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_MAX_CLOCK_RATE;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = clock_id;
    mbox.data[6] = 0;               /* Response: max rate */
    mbox.data[7] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        return false;
    }

    if ((mbox.data[4] & 0x80000000) == 0) {
        return false;
    }

    if (rate_hz) *rate_hz = mbox.data[6];

    return true;
}


/* =============================================================================
 * THROTTLING
 * =============================================================================
 */

bool mailbox_get_throttled(uint32_t *flags)
{
    mailbox_buffer_t mbox = { .data = {0} };

    mbox.data[0] = 7 * 4;
    mbox.data[1] = MBOX_REQUEST;
    mbox.data[2] = TAG_GET_THROTTLED;
    mbox.data[3] = 4;
    mbox.data[4] = 0;
    mbox.data[5] = 0;               /* Response: throttle flags */
    mbox.data[6] = TAG_END;

    if (!mailbox_call(&mbox, MBOX_CHANNEL_PROP)) {
        return false;
    }

    if ((mbox.data[4] & 0x80000000) == 0) {
        return false;
    }

    if (flags) *flags = mbox.data[5];

    return true;
}

bool mailbox_is_throttled(void)
{
    uint32_t flags = 0;
    if (!mailbox_get_throttled(&flags)) {
        return false;
    }
    return (flags & THROTTLE_THROTTLED) != 0;
}

bool mailbox_throttling_occurred(void)
{
    uint32_t flags = 0;
    if (!mailbox_get_throttled(&flags)) {
        return false;
    }
    return (flags & THROTTLE_THROTTLE_OCCURRED) != 0;
}