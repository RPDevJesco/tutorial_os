/*
 * soc/bcm2711/bcm2711_mailbox.h - BCM2711 Mailbox Interface (Internal)
 *
 * Tutorial-OS: BCM2711 HAL Implementation
 *
 * This is an INTERNAL header - not part of the public HAL API.
 * It provides the mailbox communication used by:
 *   - soc_init.c (platform info queries)
 *   - display_dpi.c (framebuffer allocation)
 *
 * Based on the existing drivers/mailbox/mailbox.h
 */

#ifndef BCM2711_MAILBOX_H
#define BCM2711_MAILBOX_H

#include "hal/hal_types.h"
#include "bcm2711_regs.h"

/* =============================================================================
 * MAILBOX BUFFER
 * =============================================================================
 * Must be 16-byte aligned because low 4 bits are used for channel number.
 */

typedef struct {
    uint32_t data[36];  /* Buffer for property tags */
} HAL_ALIGNED(16) bcm_mailbox_buffer_t;

/* =============================================================================
 * MAILBOX FUNCTIONS
 * =============================================================================
 */

/*
 * Send a mailbox message and wait for response
 *
 * @param buffer    Message buffer (must be 16-byte aligned)
 * @param channel   Mailbox channel (usually BCM_MBOX_CH_PROP = 8)
 * @return          true on success
 */
bool bcm_mailbox_call(bcm_mailbox_buffer_t *buffer, uint8_t channel);

/* =============================================================================
 * CONVENIENCE FUNCTIONS
 * =============================================================================
 */

/*
 * Get ARM memory info
 */
bool bcm_mailbox_get_arm_memory(uint32_t *base, uint32_t *size);

/*
 * Get VideoCore memory info
 */
bool bcm_mailbox_get_vc_memory(uint32_t *base, uint32_t *size);

/*
 * Get clock rate
 */
bool bcm_mailbox_get_clock_rate(uint32_t clock_id, uint32_t *rate_hz);

/*
 * Get measured clock rate
 */
bool bcm_mailbox_get_clock_measured(uint32_t clock_id, uint32_t *rate_hz);

/*
 * Get temperature in millicelsius
 */
bool bcm_mailbox_get_temperature(uint32_t *temp_mc);

/*
 * Get max temperature before throttling
 */
bool bcm_mailbox_get_max_temperature(uint32_t *temp_mc);

/*
 * Get throttle status flags
 */
bool bcm_mailbox_get_throttled(uint32_t *status);

/*
 * Get board revision
 */
bool bcm_mailbox_get_board_revision(uint32_t *revision);

/*
 * Get board serial number
 */
bool bcm_mailbox_get_board_serial(uint64_t *serial);

/*
 * Set device power state
 */
bool bcm_mailbox_set_power_state(uint32_t device_id, bool on);

/*
 * Get device power state
 */
bool bcm_mailbox_get_power_state(uint32_t device_id, bool *on);

/*
 * Wait for vsync
 */
bool bcm_mailbox_wait_vsync(void);

/*
 * Set virtual offset (for double buffering)
 */
bool bcm_mailbox_set_virtual_offset(uint32_t x, uint32_t y);

#endif /* BCM2711_MAILBOX_H */
