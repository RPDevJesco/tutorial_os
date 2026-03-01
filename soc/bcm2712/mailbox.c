/*
 * soc/bcm2712/bcm2712_mailbox.c - BCM2712 VideoCore Mailbox
 * ==========================================================
 *
 * BCM2712 mailbox at 0x107c013880 - same protocol as BCM2711
 */

#include "bcm2712_mailbox.h"
#include "bcm2712_regs.h"

/* Mailbox registers */
#define MBOX_READ_REG    ((volatile uint32_t *)(BCM2712_MBOX_BASE + MBOX_READ))
#define MBOX_STATUS_REG  ((volatile uint32_t *)(BCM2712_MBOX_BASE + MBOX_STATUS))
#define MBOX_WRITE_REG   ((volatile uint32_t *)(BCM2712_MBOX_BASE + MBOX_WRITE))

/* Aligned buffer for mailbox messages */

/* =============================================================================
 * LOW-LEVEL MAILBOX
 * =============================================================================
 */

bool bcm_mailbox_call(bcm_mailbox_buffer_t *buffer, uint8_t channel)
{
    /*
     * BCM2712 uses ARM physical addresses directly — no L2 coherent alias.
     *
     * BCM2711 required ORing 0xC0000000 to give the GPU a coherent view of
     * ARM memory. BCM2712 (VC7) uses the same physical address space as the
     * ARM cores. Applying the BCM2711 alias here sends the GPU to the wrong
     * location entirely, causing it to read a garbage message and return
     * whatever its internal default state is — 16bpp, producing the stripe
     * artifact.
     *
     * BCM_ARM_TO_BUS is defined in bcm2712_regs.h as a no-op identity macro:
     *   #define BCM_ARM_TO_BUS(addr)  (addr)
     *
     * Using the macro (rather than a raw cast) keeps the intent explicit and
     * means this file automatically picks up any future change to the address
     * translation policy without needing to be touched again.
     *
     * We pass the struct pointer itself, not buffer->data, matching the
     * BCM2711 convention. The struct is typedef'd with HAL_ALIGNED(16) so
     * the low 4 bits are always zero and safe to OR with the channel number.
     */
    uint64_t addr = (uint64_t)(uintptr_t)buffer;

    /* Mailbox only supports 32-bit bus addresses. The buffer lives in .data
     * or on the stack inside the first 1GB, so this should never fire.
     * If it does, something is badly wrong with the memory layout.         */
    if (addr >= 0x40000000ULL) {
        return false;
    }

    uint32_t bus_addr = BCM_ARM_TO_BUS((uint32_t)addr);

    /*
     * DSB ST — flush all pending ARM stores to DRAM before handing the
     * buffer address to the GPU. The Cortex-A76 is aggressively
     * out-of-order; without this barrier the GPU may read the buffer before
     * the ARM's writes have left the L1/L2 cache.
     *
     * "memory" in the clobber list is a compiler barrier — it prevents the
     * compiler from reordering buffer writes across this asm statement.
     * DSB ST is store-only (lighter than DSB SY) which is sufficient here
     * because we only need to drain our own writes, not incoming loads.
     */
    __asm__ volatile ("dsb st" ::: "memory");

    /* Wait until the WRITE register is not full */
    int timeout = 0x100000;
    while ((*MBOX_STATUS_REG & MBOX_FULL) && --timeout) {
        HAL_NOP();
    }
    if (timeout == 0) return false;

    /* Write the buffer bus address with the channel number in the low 4 bits.
     * The alignment guarantee on bcm_mailbox_buffer_t means bus_addr[3:0]
     * are always 0, so the OR does not corrupt the address.               */
    *MBOX_WRITE_REG = bus_addr | channel;

    /* Wait for the GPU's response on our channel */
    timeout = 0x100000;
    while (1) {
        while ((*MBOX_STATUS_REG & MBOX_EMPTY) && --timeout) {
            HAL_NOP();
        }
        if (timeout == 0) return false;

        uint32_t response = *MBOX_READ_REG;
        if ((response & 0xF) == channel) {
            break;
        }
        /* Response was for a different channel — discard and keep waiting */
    }

    /*
     * DMB SY — ensure the GPU's writes to the response buffer are visible
     * to the ARM before we read buffer->data[1]. Without this, the A76's
     * cache may return a stale pre-response value.
     *
     * DMB (not DSB) is correct here — we need memory access ordering, not
     * a full pipeline drain.
     */
    __asm__ volatile ("dmb sy" ::: "memory");

    /* Response code 0x80000000 = success */
    return (buffer->data[1] == 0x80000000);
}

/* =============================================================================
 * MEMORY QUERIES
 * =============================================================================
 */

bool bcm_mailbox_get_arm_memory(uint32_t *base, uint32_t *size)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_ARM_MEMORY;
    mbox.data[3] = 8;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    *base = mbox.data[5];
    *size = mbox.data[6];
    return true;
}

bool bcm_mailbox_get_vc_memory(uint32_t *base, uint32_t *size)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
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

    *base = mbox.data[5];
    *size = mbox.data[6];
    return true;
}

/* =============================================================================
 * CLOCK QUERIES
 * =============================================================================
 */

bool bcm_mailbox_get_clock_rate(uint32_t clock_id, uint32_t *rate_hz)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_CLOCK_RATE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = clock_id;
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    *rate_hz = mbox.data[6];
    return true;
}

bool bcm_mailbox_get_clock_measured(uint32_t clock_id, uint32_t *rate_hz)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
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

    *rate_hz = mbox.data[6];
    return true;
}

/* =============================================================================
 * TEMPERATURE
 * =============================================================================
 */

bool bcm_mailbox_get_temperature(uint32_t *temp_mc)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_TEMPERATURE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = 0;  /* Temperature ID (0 = SoC) */
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    *temp_mc = mbox.data[6];
    return true;
}

bool bcm_mailbox_get_max_temperature(uint32_t *temp_mc)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_MAX_TEMPERATURE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = 0;  /* Temperature ID */
    mbox.data[6] = 0;
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    *temp_mc = mbox.data[6];
    return true;
}

/* =============================================================================
 * THROTTLE STATUS
 * =============================================================================
 */

bool bcm_mailbox_get_throttled(uint32_t *status)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_THROTTLED;
    mbox.data[3] = 4;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    *status = mbox.data[5];
    return true;
}

/* =============================================================================
 * BOARD INFO
 * =============================================================================
 */

bool bcm_mailbox_get_board_revision(uint32_t *revision)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_GET_BOARD_REVISION;
    mbox.data[3] = 4;
    mbox.data[4] = 0;
    mbox.data[5] = 0;
    mbox.data[6] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    *revision = mbox.data[5];
    return true;
}

bool bcm_mailbox_get_board_serial(uint64_t *serial)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
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

    *serial = ((uint64_t)mbox.data[6] << 32) | mbox.data[5];
    return true;
}

/* =============================================================================
 * POWER MANAGEMENT
 * =============================================================================
 */

bool bcm_mailbox_set_power_state(uint32_t device_id, bool on)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_SET_POWER_STATE;
    mbox.data[3] = 8;
    mbox.data[4] = 8;
    mbox.data[5] = device_id;
    mbox.data[6] = on ? 3 : 2;  /* Bit 0 = on, Bit 1 = wait */
    mbox.data[7] = BCM_TAG_END;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    /* Check device exists (bit 1 of response) */
    return (mbox.data[6] & 2) == 0;
}

bool bcm_mailbox_get_power_state(uint32_t device_id, bool *on)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
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

    *on = (mbox.data[6] & 1) != 0;
    return true;
}

/* =============================================================================
 * DISPLAY CONTROL
 * =============================================================================
 */

bool bcm_mailbox_wait_vsync(void)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_WAIT_VSYNC;
    mbox.data[3] = 4;
    mbox.data[4] = 0;
    mbox.data[5] = 0;  /* Display ID */
    mbox.data[6] = BCM_TAG_END;

    return bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP);
}

bool bcm_mailbox_set_virtual_offset(uint32_t x, uint32_t y)
{
    bcm_mailbox_buffer_t mbox HAL_ALIGNED(16);

    mbox.data[0] = 32;
    mbox.data[1] = BCM_MBOX_REQUEST;
    mbox.data[2] = BCM_TAG_SET_VIRTUAL_OFFSET;
    mbox.data[3] = 8;
    mbox.data[4] = 8;
    mbox.data[5] = x;
    mbox.data[6] = y;
    mbox.data[7] = BCM_TAG_END;

    return bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP);
}