/*
 * mailbox.h - VideoCore Mailbox Interface
 * ========================================
 *
 * The Raspberry Pi uses a mailbox interface for ARM <-> VideoCore GPU
 * communication. This is used for:
 *   - Framebuffer allocation
 *   - Power management
 *   - Clock configuration
 *   - Hardware queries
 *   - Temperature monitoring
 *   - Throttle detection
 *
 * The mailbox buffer must be 16-byte aligned because the low 4 bits
 * of the address are used for the channel number.
 *
 * MESSAGE FORMAT:
 * ---------------
 * The property tag interface uses a buffer with this format:
 *   [0] Total buffer size in bytes
 *   [1] Request/response code (0 = request, 0x80000000 = success)
 *   [2] Tag ID
 *   [3] Value buffer size
 *   [4] Request/response indicator
 *   [5...] Tag value(s)
 *   [...] More tags...
 *   [n] End tag (0)
 */

#ifndef MAILBOX_H
#define MAILBOX_H

#include "types.h"
#include "mmio.h"

/* =============================================================================
 * MAILBOX REGISTER ADDRESSES
 * =============================================================================
 */

#define MBOX_BASE           (PERIPHERAL_BASE + 0x0000B880)

#define MBOX_READ           (MBOX_BASE + 0x00)
#define MBOX_STATUS         (MBOX_BASE + 0x18)
#define MBOX_WRITE          (MBOX_BASE + 0x20)

/* Status flags */
#define MBOX_FULL           0x80000000
#define MBOX_EMPTY          0x40000000

/* Response codes */
#define MBOX_REQUEST            0x00000000
#define MBOX_RESPONSE_SUCCESS   0x80000000
#define MBOX_RESPONSE_ERROR     0x80000001

/* Channel for property tags */
#define MBOX_CHANNEL_PROP   8


/* =============================================================================
 * DEVICE IDS (for power management)
 * =============================================================================
 */

#define DEVICE_SD_CARD      0
#define DEVICE_UART0        1
#define DEVICE_UART1        2
#define DEVICE_USB_HCD      3
#define DEVICE_I2C0         4
#define DEVICE_I2C1         5
#define DEVICE_I2C2         6
#define DEVICE_SPI          7
#define DEVICE_CCP2TX       8


/* =============================================================================
 * CLOCK IDS
 * =============================================================================
 */

#define CLOCK_ID_EMMC       1
#define CLOCK_ID_UART       2
#define CLOCK_ID_ARM        3
#define CLOCK_ID_CORE       4
#define CLOCK_ID_V3D        5
#define CLOCK_ID_H264       6
#define CLOCK_ID_ISP        7
#define CLOCK_ID_SDRAM      8
#define CLOCK_ID_PIXEL      9
#define CLOCK_ID_PWM        10


/* =============================================================================
 * PROPERTY TAGS
 * =============================================================================
 */

/* VideoCore info */
#define TAG_GET_FIRMWARE_REV    0x00000001

/* Hardware info */
#define TAG_GET_BOARD_MODEL     0x00010001
#define TAG_GET_BOARD_REV       0x00010002
#define TAG_GET_BOARD_MAC       0x00010003
#define TAG_GET_BOARD_SERIAL    0x00010004
#define TAG_GET_ARM_MEMORY      0x00010005
#define TAG_GET_VC_MEMORY       0x00010006

/* Power management */
#define TAG_GET_POWER_STATE     0x00020001
#define TAG_SET_POWER_STATE     0x00028001
#define TAG_WAIT_FOR_VSYNC      0x00020014

/* Clocks */
#define TAG_GET_CLOCK_STATE     0x00030001
#define TAG_SET_CLOCK_STATE     0x00038001
#define TAG_GET_CLOCK_RATE      0x00030002
#define TAG_SET_CLOCK_RATE      0x00038002
#define TAG_GET_MAX_CLOCK_RATE  0x00030004
#define TAG_GET_MIN_CLOCK_RATE  0x00030007

/* Temperature */
#define TAG_GET_TEMPERATURE     0x00030006
#define TAG_GET_MAX_TEMPERATURE 0x0003000A

/* Throttling */
#define TAG_GET_THROTTLED       0x00030046

/* Framebuffer */
#define TAG_ALLOCATE_BUFFER     0x00040001
#define TAG_RELEASE_BUFFER      0x00048001
#define TAG_BLANK_SCREEN        0x00040002
#define TAG_GET_PHYSICAL_SIZE   0x00040003
#define TAG_SET_PHYSICAL_SIZE   0x00048003
#define TAG_GET_VIRTUAL_SIZE    0x00040004
#define TAG_SET_VIRTUAL_SIZE    0x00048004
#define TAG_GET_DEPTH           0x00040005
#define TAG_SET_DEPTH           0x00048005
#define TAG_GET_PIXEL_ORDER     0x00040006
#define TAG_SET_PIXEL_ORDER     0x00048006
#define TAG_GET_PITCH           0x00040008
#define TAG_SET_VIRTUAL_OFFSET  0x00048009

/* End tag marker */
#define TAG_END                 0x00000000


/* =============================================================================
 * THROTTLE FLAGS
 * =============================================================================
 *
 * Bit flags returned by TAG_GET_THROTTLED:
 */

#define THROTTLE_UNDERVOLT          (1 << 0)   /* Currently under-voltage */
#define THROTTLE_FREQ_CAPPED        (1 << 1)   /* Currently frequency capped */
#define THROTTLE_THROTTLED          (1 << 2)   /* Currently throttled */
#define THROTTLE_SOFT_TEMP_LIMIT    (1 << 3)   /* Soft temperature limit active */
#define THROTTLE_UNDERVOLT_OCCURRED (1 << 16)  /* Under-voltage has occurred */
#define THROTTLE_FREQ_CAP_OCCURRED  (1 << 17)  /* Frequency cap has occurred */
#define THROTTLE_THROTTLE_OCCURRED  (1 << 18)  /* Throttling has occurred */
#define THROTTLE_SOFT_TEMP_OCCURRED (1 << 19)  /* Soft temp limit has occurred */


/* =============================================================================
 * MAILBOX BUFFER
 * =============================================================================
 */

/*
 * Mailbox buffer for property tag messages.
 * Must be 16-byte aligned.
 */
typedef struct __attribute__((aligned(16))) {
    uint32_t data[64];
} mailbox_buffer_t;


/* =============================================================================
 * CORE MAILBOX API
 * =============================================================================
 */

/*
 * mailbox_call() - Send a mailbox message and wait for response
 *
 * @param buffer    Pointer to 16-byte aligned message buffer
 * @param channel   Mailbox channel (usually MBOX_CHANNEL_PROP = 8)
 *
 * Returns: true on success, false on error
 *
 * The buffer should be populated with the message before calling.
 * On return, the buffer contains the response.
 */
bool mailbox_call(mailbox_buffer_t *buffer, uint8_t channel);


/* =============================================================================
 * POWER MANAGEMENT API
 * =============================================================================
 */

/*
 * mailbox_set_power_state() - Turn a device on or off
 *
 * @param device_id  Device ID (DEVICE_SD_CARD, DEVICE_USB_HCD, etc.)
 * @param on         true to power on, false to power off
 *
 * Returns: true if device is now in the requested state
 */
bool mailbox_set_power_state(uint32_t device_id, bool on);

/*
 * mailbox_get_power_state() - Get device power state
 *
 * @param device_id  Device ID
 * @param exists     Output: true if device exists (can be NULL)
 * @param is_on      Output: true if device is powered on (can be NULL)
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_power_state(uint32_t device_id, bool *exists, bool *is_on);


/* =============================================================================
 * MEMORY INFO API
 * =============================================================================
 */

/*
 * mailbox_get_arm_memory() - Get ARM memory base and size
 *
 * @param base  Output: Memory base address
 * @param size  Output: Memory size in bytes
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_arm_memory(uint32_t *base, uint32_t *size);

/*
 * mailbox_get_vc_memory() - Get VideoCore memory base and size
 *
 * @param base  Output: Memory base address
 * @param size  Output: Memory size in bytes
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_vc_memory(uint32_t *base, uint32_t *size);


/* =============================================================================
 * BOARD INFO API
 * =============================================================================
 */

/*
 * mailbox_get_board_serial() - Get board serial number
 *
 * @param serial  Output: 64-bit serial number
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_board_serial(uint64_t *serial);


/* =============================================================================
 * TEMPERATURE API
 * =============================================================================
 */

/*
 * mailbox_get_temperature() - Get SoC temperature
 *
 * @param temp_millicelsius  Output: Temperature in millidegrees Celsius
 *                           (e.g., 45000 = 45.0°C)
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_temperature(uint32_t *temp_millicelsius);

/*
 * mailbox_get_max_temperature() - Get maximum safe temperature
 *
 * This is the thermal throttling threshold (typically around 85°C).
 *
 * @param temp_millicelsius  Output: Max temperature in millidegrees Celsius
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_max_temperature(uint32_t *temp_millicelsius);


/* =============================================================================
 * CLOCK API
 * =============================================================================
 */

/*
 * mailbox_get_clock_rate() - Get current clock rate
 *
 * @param clock_id  Clock ID (CLOCK_ID_ARM, CLOCK_ID_CORE, etc.)
 * @param rate_hz   Output: Clock rate in Hz
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_clock_rate(uint32_t clock_id, uint32_t *rate_hz);

/*
 * mailbox_get_max_clock_rate() - Get maximum clock rate
 *
 * @param clock_id  Clock ID
 * @param rate_hz   Output: Maximum clock rate in Hz
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_max_clock_rate(uint32_t clock_id, uint32_t *rate_hz);

/* Convenience functions for common clocks */
static inline bool mailbox_get_arm_clock(uint32_t *rate_hz)
{
    return mailbox_get_clock_rate(CLOCK_ID_ARM, rate_hz);
}

static inline bool mailbox_get_core_clock(uint32_t *rate_hz)
{
    return mailbox_get_clock_rate(CLOCK_ID_CORE, rate_hz);
}

static inline bool mailbox_get_max_arm_clock(uint32_t *rate_hz)
{
    return mailbox_get_max_clock_rate(CLOCK_ID_ARM, rate_hz);
}


/* =============================================================================
 * THROTTLING API
 * =============================================================================
 */

/*
 * mailbox_get_throttled() - Get throttling status flags
 *
 * @param flags  Output: Throttle flags (see THROTTLE_* defines)
 *
 * Returns: true if query succeeded
 */
bool mailbox_get_throttled(uint32_t *flags);

/*
 * mailbox_is_throttled() - Check if currently being throttled
 *
 * Returns: true if CPU is currently being throttled due to temperature
 */
bool mailbox_is_throttled(void);

/*
 * mailbox_throttling_occurred() - Check if throttling has occurred since boot
 *
 * Returns: true if throttling has occurred at any point since boot
 */
bool mailbox_throttling_occurred(void);


#endif /* MAILBOX_H */