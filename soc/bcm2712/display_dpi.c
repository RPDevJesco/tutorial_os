/*
 * soc/bcm2712/display_dpi.c - BCM2712 Display Driver (VideoCore Mailbox)
 * ========================================================================
 *
 * Tutorial-OS: BCM2712 HAL Implementation
 *
 * IMPORTANT: "DPI" in the filename is a misnomer inherited from bcm2710/bcm2711.
 * On BCM2712 (Pi 5 / CM5), display output is HDMI routed through the RP1
 * south bridge — NOT DPI GPIO pins. The framebuffer is still allocated via the
 * same VideoCore mailbox property tag protocol as BCM2710/2711.
 *
 * KEY BCM2712 DIFFERENCES vs BCM2711:
 *
 *  1. DO NOT call hal_gpio_configure_dpi().
 *     On BCM2710/2711 with the GPi Case, DPI output uses GPIO 0-27 → ALT2.
 *     On BCM2712, HDMI is handled entirely by the RP1 chip. Setting GPIO 0-27
 *     to ALT2 is wrong and can corrupt RP1 peripheral pin assignments.
 *
 *  2. Send SET_DEPTH in a SEPARATE mailbox call before the full allocation.
 *     The BCM2712 VideoCore 7 firmware has a known quirk: when SET_DEPTH is
 *     bundled with SET_PHYSICAL_SIZE in a combined property tag message it may
 *     be silently ignored and the framebuffer allocated at 16bpp regardless.
 *
 *     Symptom of the bug: uniform 1-pixel-wide alternating vertical stripes
 *     across the entire display. Each 32-bit pixel we write gets read back by
 *     the GPU as two 16-bit pixels (low word = B,G columns; high word = R,A).
 *
 *     Fix: send SET_DEPTH=32 alone first, then the full allocation message.
 *
 *  3. Verify the returned pitch == width * 4.
 *     If pitch == width * 2 the firmware still allocated 16bpp. The code
 *     returns HAL_ERROR_DISPLAY_MODE in that case rather than letting the
 *     kernel write 32bpp pixels into a 16bpp buffer silently.
 *
 * PORTABILITY NOTE:
 *   Drawing functions (fb_clear, fb_draw_rect, fb_draw_string, etc.) live in
 *   drivers/framebuffer/framebuffer.c — unchanged across all platforms.
 *   This file only handles platform-specific allocation and buffer swap.
 */

#include "hal/hal_display.h"
#include "hal/hal_gpio.h"
#include "bcm2712_regs.h"
#include "bcm2712_mailbox.h"
#include "framebuffer.h"

/* =============================================================================
 * CONFIGURATION
 *
 * framebuffer.h defines FB_DEFAULT_WIDTH (640), FB_DEFAULT_HEIGHT (480), and
 * FB_BITS_PER_PIXEL (32) for the generic case. We #undef before redefining to
 * avoid -Wredefinition warnings from the compiler.
 *
 * BCM2712 targets 1080p HDMI. Using native resolution avoids the GPU upscaler,
 * which can add artifacts on top of any pre-existing issues.
 * =============================================================================
 */
#undef  FB_BITS_PER_PIXEL
#define FB_BITS_PER_PIXEL   32

#undef  FB_DEFAULT_WIDTH
#define FB_DEFAULT_WIDTH    1920

#undef  FB_DEFAULT_HEIGHT
#define FB_DEFAULT_HEIGHT   1080

#define FB_BUFFER_COUNT     2       /* Double buffering via virtual Y offset */

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static framebuffer_t g_framebuffer;
static bool          g_display_initialized = false;

/* =============================================================================
 * INTERNAL: SET DEPTH (separate mailbox call)
 *
 * Send ONLY the SET_DEPTH tag in its own message before the full allocation.
 * This primes the VC7 firmware so it honours 32bpp in the subsequent call.
 * Without this, the firmware may default to 16bpp (stripe artifact).
 * =============================================================================
 */
static bool set_depth_first(void)
{
    bcm_mailbox_buffer_t mbox;
    uint32_t i;

    for (i = 0; i < 36; i++) mbox.data[i] = 0;

    i = 0;
    mbox.data[i++] = 0;                     /* total size (set below) */
    mbox.data[i++] = BCM_MBOX_REQUEST;
    mbox.data[i++] = BCM_TAG_SET_DEPTH;
    mbox.data[i++] = 4;                     /* value buffer: 4 bytes */
    mbox.data[i++] = 0;                     /* request indicator */
    mbox.data[i++] = FB_BITS_PER_PIXEL;     /* 32 */
    mbox.data[i++] = BCM_TAG_END;
    mbox.data[0]   = i * 4;

    /* Not fatal if this fails; pitch verification below catches any mismatch */
    return bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP);
}

/* =============================================================================
 * INTERNAL: ALLOCATE FRAMEBUFFER
 * =============================================================================
 */
static hal_error_t allocate_framebuffer(uint32_t width, uint32_t height)
{
    bcm_mailbox_buffer_t mbox;
    uint32_t i;
    uint32_t virtual_height = height * FB_BUFFER_COUNT;
    uint32_t alloc_addr_idx;
    uint32_t pitch_idx;
    uint32_t fb_bus_addr;
    uint32_t fb_size_bytes;
    uint32_t pitch;
    uint32_t expected_pitch;
    uint32_t fb_arm_addr;

    /*
     * Step 1: prime the VC7 firmware with the desired bit depth.
     * See header comment for why this must be a separate call on BCM2712.
     */
    set_depth_first();

    /*
     * Step 2: full framebuffer allocation request.
     *
     * Tag ordering that works reliably across BCM2710/2711/2712:
     *   SET_PHYSICAL_SIZE -> SET_VIRTUAL_SIZE -> SET_VIRTUAL_OFFSET ->
     *   SET_DEPTH -> SET_PIXEL_ORDER -> ALLOCATE_BUFFER -> GET_PITCH
     */
    for (i = 0; i < 36; i++) mbox.data[i] = 0;

    i = 0;
    mbox.data[i++] = 0;                     /* total size (set below) */
    mbox.data[i++] = BCM_MBOX_REQUEST;

    mbox.data[i++] = BCM_TAG_SET_PHYSICAL_SIZE;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = width;
    mbox.data[i++] = height;

    mbox.data[i++] = BCM_TAG_SET_VIRTUAL_SIZE;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = width;
    mbox.data[i++] = virtual_height;

    mbox.data[i++] = BCM_TAG_SET_VIRTUAL_OFFSET;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;

    mbox.data[i++] = BCM_TAG_SET_DEPTH;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    mbox.data[i++] = FB_BITS_PER_PIXEL;

    /*
     * Pixel order: 0 = BGR (bytes in memory: B G R A).
     * The drawing code writes pixels as 0xAARRGGBB packed uint32 on a
     * little-endian CPU, which stores B G R A in memory — matching BGR order.
     * If colors appear red/blue swapped on your display, change this to 1.
     */
    mbox.data[i++] = BCM_TAG_SET_PIXEL_ORDER;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;                     /* 0 = BGR */

    mbox.data[i++] = BCM_TAG_ALLOCATE_BUFFER;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    alloc_addr_idx  = i;
    mbox.data[i++] = 16;                    /* alignment -> response: bus addr */
    mbox.data[i++] = 0;                     /*           -> response: size */

    mbox.data[i++] = BCM_TAG_GET_PITCH;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    pitch_idx       = i;
    mbox.data[i++] = 0;

    mbox.data[i++] = BCM_TAG_END;
    mbox.data[0]   = i * 4;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return HAL_ERROR_DISPLAY_MAILBOX;
    }

    if ((mbox.data[1] & 0x80000000) == 0) {
        return HAL_ERROR_DISPLAY_MAILBOX;
    }

    fb_bus_addr   = mbox.data[alloc_addr_idx];
    fb_size_bytes = mbox.data[alloc_addr_idx + 1];
    pitch         = mbox.data[pitch_idx];

    if (fb_bus_addr == 0 || fb_size_bytes == 0) {
        return HAL_ERROR_DISPLAY_NO_FB;
    }

    /*
     * Verify the firmware honoured our 32bpp request.
     *
     * If pitch == width * 2, the firmware allocated 16bpp despite SET_DEPTH.
     * This produces the 1-pixel-wide vertical stripe artifact described in the
     * file header. HAL_ERROR_DISPLAY_MODE is the closest existing error code.
     * Inspect g_framebuffer.pitch after this returns: width*2 = 16bpp mismatch.
     *
     * Possible causes if this fires:
     *   - set_depth_first() was ineffective (firmware version issue)
     *   - config.txt has a conflicting framebuffer_depth= setting
     *   - The mailbox buffer was not 16-byte aligned
     */
    expected_pitch = width * (FB_BITS_PER_PIXEL / 8);
    if (pitch != expected_pitch) {
        g_framebuffer.pitch = pitch;
        return HAL_ERROR_DISPLAY_MODE;
    }

    /*
     * Convert bus address to ARM physical address.
     * BCM_BUS_TO_ARM clears bits [31:30] (L2 coherent alias).
     * The framebuffer is always in the first 1GB on BCM2712.
     */
    fb_arm_addr = BCM_BUS_TO_ARM(fb_bus_addr);

    g_framebuffer.width          = width;
    g_framebuffer.height         = height;
    g_framebuffer.pitch          = pitch;
    g_framebuffer.virtual_height = virtual_height;
    g_framebuffer.buffer_size    = pitch * height;

    g_framebuffer.buffers[0] = (uint32_t *)(uintptr_t)fb_arm_addr;
    g_framebuffer.buffers[1] = (uint32_t *)(uintptr_t)(fb_arm_addr + g_framebuffer.buffer_size);

    g_framebuffer.front_buffer = 0;
    g_framebuffer.back_buffer  = 1;
    g_framebuffer.addr         = g_framebuffer.buffers[g_framebuffer.back_buffer];

    g_framebuffer.clip_depth      = 0;
    g_framebuffer.clip_stack[0].x = 0;
    g_framebuffer.clip_stack[0].y = 0;
    g_framebuffer.clip_stack[0].w = width;
    g_framebuffer.clip_stack[0].h = height;

    g_framebuffer.dirty_count   = 0;
    g_framebuffer.full_dirty    = true;
    g_framebuffer.frame_count   = 0;
    g_framebuffer.vsync_enabled = true;
    g_framebuffer.initialized   = true;

    return HAL_SUCCESS;
}

/* =============================================================================
 * HAL DISPLAY INITIALIZATION
 * =============================================================================
 */

hal_error_t hal_display_init(framebuffer_t **fb_out)
{
    return hal_display_init_with_size(FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT, fb_out);
}

hal_error_t hal_display_init_with_size(uint32_t width, uint32_t height,
                                        framebuffer_t **fb_out)
{
    hal_error_t err;

    if (fb_out == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    if (g_display_initialized) {
        *fb_out = &g_framebuffer;
        return HAL_SUCCESS;
    }

    /*
     * DO NOT call hal_gpio_configure_dpi() here.
     *
     * On BCM2710/BCM2711 + GPi Case, display uses GPIO 0-27 in DPI mode.
     * On BCM2712 (CM5), display is HDMI through RP1. The GPU firmware handles
     * HDMI before our code runs. Calling configure_dpi() would forcibly
     * reassign GPIO 0-27 to ALT2, corrupting RP1 UART/SPI/I2C pin functions.
     */

    err = allocate_framebuffer(width, height);
    if (HAL_FAILED(err)) {
        return err;
    }

    g_display_initialized = true;
    *fb_out = &g_framebuffer;

    return HAL_SUCCESS;
}

hal_error_t hal_display_init_with_config(const hal_display_config_t *config,
                                          framebuffer_t **fb_out)
{
    if (config == NULL || fb_out == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    return hal_display_init_with_size(config->width, config->height, fb_out);
}

hal_error_t hal_display_shutdown(void)
{
    if (!g_display_initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    g_display_initialized     = false;
    g_framebuffer.initialized = false;

    return HAL_SUCCESS;
}

bool hal_display_is_initialized(void)
{
    return g_display_initialized;
}

/* =============================================================================
 * DISPLAY INFORMATION
 * =============================================================================
 */

hal_error_t hal_display_get_info(hal_display_info_t *info)
{
    if (info == NULL)            return HAL_ERROR_NULL_PTR;
    if (!g_display_initialized) return HAL_ERROR_NOT_INIT;

    info->width            = g_framebuffer.width;
    info->height           = g_framebuffer.height;
    info->pitch            = g_framebuffer.pitch;
    info->bits_per_pixel   = FB_BITS_PER_PIXEL;
    info->type             = HAL_DISPLAY_TYPE_HDMI;
    info->format           = HAL_PIXEL_FORMAT_ARGB8888;
    info->framebuffer_base = g_framebuffer.buffers[0];
    info->framebuffer_size = g_framebuffer.buffer_size * FB_BUFFER_COUNT;
    info->buffer_count     = FB_BUFFER_COUNT;

    return HAL_SUCCESS;
}

uint32_t hal_display_get_width(void)  { return g_display_initialized ? g_framebuffer.width  : 0; }
uint32_t hal_display_get_height(void) { return g_display_initialized ? g_framebuffer.height : 0; }
uint32_t hal_display_get_pitch(void)  { return g_display_initialized ? g_framebuffer.pitch  : 0; }

/* =============================================================================
 * BUFFER SWAP (double buffering via virtual Y offset)
 *
 * Virtual framebuffer is 2x physical height. Buffer 0 = rows [0, h-1],
 * buffer 1 = rows [h, 2h-1]. Virtual Y offset tells GPU which to display.
 * =============================================================================
 */

hal_error_t hal_display_present(framebuffer_t *fb)
{
    uint32_t temp;

    if (fb == NULL || !fb->initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    if (fb->vsync_enabled) {
        bcm_mailbox_wait_vsync();
    }

    temp             = fb->front_buffer;
    fb->front_buffer = fb->back_buffer;
    fb->back_buffer  = temp;
    fb->addr         = fb->buffers[fb->back_buffer];

    bcm_mailbox_set_virtual_offset(0, fb->front_buffer * fb->height);

    fb->frame_count++;
    fb->full_dirty  = false;
    fb->dirty_count = 0;

    return HAL_SUCCESS;
}

hal_error_t hal_display_present_immediate(framebuffer_t *fb)
{
    uint32_t temp;

    if (fb == NULL || !fb->initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    temp             = fb->front_buffer;
    fb->front_buffer = fb->back_buffer;
    fb->back_buffer  = temp;
    fb->addr         = fb->buffers[fb->back_buffer];

    bcm_mailbox_set_virtual_offset(0, fb->front_buffer * fb->height);

    fb->frame_count++;
    fb->full_dirty  = false;
    fb->dirty_count = 0;

    return HAL_SUCCESS;
}

hal_error_t hal_display_set_vsync(framebuffer_t *fb, bool enabled)
{
    if (fb == NULL || !fb->initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    fb->vsync_enabled = enabled;
    return HAL_SUCCESS;
}

hal_error_t hal_display_wait_vsync(void)
{
    return bcm_mailbox_wait_vsync() ? HAL_SUCCESS : HAL_ERROR_HARDWARE;
}

/* =============================================================================
 * DEFAULT CONFIGURATION
 * =============================================================================
 */

hal_error_t hal_display_get_default_config(hal_display_config_t *config)
{
    if (config == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    config->width         = FB_DEFAULT_WIDTH;
    config->height        = FB_DEFAULT_HEIGHT;
    config->type          = HAL_DISPLAY_TYPE_HDMI;
    config->format        = HAL_PIXEL_FORMAT_ARGB8888;
    config->buffer_count  = FB_BUFFER_COUNT;
    config->vsync_enabled = true;

    return HAL_SUCCESS;
}