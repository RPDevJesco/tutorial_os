/*
 * soc/s905x/display_vpu.c - Amlogic S905X Display Implementation
 *
 * Tutorial-OS: S905X HAL Implementation
 *
 * COMPLETELY DIFFERENT FROM BOTH BCM AND ROCKCHIP:
 *
 * BCM2710/BCM2711:
 *   VideoCore mailbox -> request framebuffer -> get address -> done!
 *   Simple and well-documented. The GPU firmware handles everything.
 *
 * RK3528A:
 *   VOP2 (Video Output Processor 2) - must configure window layers,
 *   connect to HDMI TX. Complex but somewhat documented.
 *
 * S905X (Amlogic Meson GXL):
 *   VPU (Video Processing Unit) with OSD (On-Screen Display) layers.
 *   The VPU uses a "canvas" system for memory addressing — framebuffer
 *   addresses are registered as numbered canvas entries, and the OSD
 *   references them by canvas number rather than direct address.
 *
 *   This is the most complex display setup of the three platforms.
 *
 * SIMPLIFIED APPROACH (same strategy as Rockchip):
 *   - Use a pre-allocated framebuffer region in high DRAM
 *   - Assume U-Boot has already initialized HDMI and VPU
 *   - Just update the OSD canvas to point at our framebuffer
 *   - For full VPU initialization, see Linux meson-drm driver
 */

#include "hal/hal_display.h"
#include "hal/hal_gpio.h"
#include "hal/hal_timer.h"
#include "s905x_regs.h"

/* Include the portable framebuffer header */
#include "framebuffer.h"

/* =============================================================================
 * CONFIGURATION
 * =============================================================================
 */

#define FB_BITS_PER_PIXEL   32
#define FB_BUFFER_COUNT     2

/* Default for La Potato with HDMI */
#undef FB_DEFAULT_WIDTH
#undef FB_DEFAULT_HEIGHT
#define FB_DEFAULT_WIDTH    1280
#define FB_DEFAULT_HEIGHT   720

/*
 * Reserved framebuffer region
 *
 * La Potato has 1GB or 2GB. We place the framebuffer near the top
 * of the first 1GB to be safe for all variants.
 *
 * NOTE: This is at a lower address than Rockchip's 0x7F000000 because
 * S905X DRAM starts at 0x00000000 vs 0x40000000, so "top of 1GB" is
 * at 0x3F000000 rather than 0x7F000000.
 */
#define FB_RESERVED_BASE    0x3F000000  /* Near top of 1GB DRAM */
#define FB_RESERVED_SIZE    (16 * 1024 * 1024)  /* 16MB for framebuffer */

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static framebuffer_t g_framebuffer;
static bool g_display_initialized = false;

/* =============================================================================
 * VPU CONFIGURATION (Simplified)
 * =============================================================================
 */

static hal_error_t vpu_configure(uint32_t width, uint32_t height, uintptr_t fb_addr)
{
    /*
     * Full VPU/OSD configuration is very complex and requires:
     *
     * 1. Enable VPU clocks via HIU/HHI registers
     * 2. Configure OSD1 layer (format, dimensions, position)
     * 3. Set up canvas entry mapping fb_addr to a canvas number
     * 4. Configure VPP (Video Post-Processor) blending
     * 5. Set up HDMI TX output timing
     *
     * The "canvas" system is unique to Amlogic:
     *   - You register a memory region as canvas entry N
     *   - OSD hardware references canvas N instead of raw address
     *   - This adds an indirection layer not present in BCM or Rockchip
     *
     * For now, assume U-Boot has done basic setup and we just
     * update the OSD1 canvas with our framebuffer address.
     *
     * TODO: Implement canvas configuration via AML_DMC_CAV_LUT_* registers
     */

    (void)width;
    (void)height;
    (void)fb_addr;

    return HAL_SUCCESS;
}

/* =============================================================================
 * HAL DISPLAY INTERFACE
 * =============================================================================
 */

hal_error_t hal_display_init(framebuffer_t **fb_out)
{
    return hal_display_init_with_size(FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT, fb_out);
}

hal_error_t hal_display_init_with_size(uint32_t width, uint32_t height,
                                        framebuffer_t **fb_out)
{
    if (fb_out == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    if (g_display_initialized) {
        *fb_out = &g_framebuffer;
        return HAL_SUCCESS;
    }

    /* Calculate framebuffer dimensions */
    uint32_t pitch = width * (FB_BITS_PER_PIXEL / 8);
    uint32_t buffer_size = pitch * height;

    /* Verify we have enough reserved space */
    if (buffer_size * FB_BUFFER_COUNT > FB_RESERVED_SIZE) {
        return HAL_ERROR_NO_MEMORY;
    }

    /* Configure VPU (simplified) */
    hal_error_t err = vpu_configure(width, height, FB_RESERVED_BASE);
    if (HAL_FAILED(err)) {
        return err;
    }

    /* Set up framebuffer structure */
    g_framebuffer.width = width;
    g_framebuffer.height = height;
    g_framebuffer.pitch = pitch;
    g_framebuffer.buffer_size = buffer_size;

    uintptr_t fb_addr = FB_RESERVED_BASE;
    g_framebuffer.buffers[0] = (uint32_t *)fb_addr;
    if (FB_BUFFER_COUNT > 1) {
        g_framebuffer.buffers[1] = (uint32_t *)(fb_addr + buffer_size);
    }
    g_framebuffer.front_buffer = 0;
    g_framebuffer.back_buffer = (FB_BUFFER_COUNT > 1) ? 1 : 0;
    g_framebuffer.addr = g_framebuffer.buffers[g_framebuffer.back_buffer];

    /* Initialize clipping */
    g_framebuffer.clip_depth = 0;
    g_framebuffer.clip_stack[0].x = 0;
    g_framebuffer.clip_stack[0].y = 0;
    g_framebuffer.clip_stack[0].w = width;
    g_framebuffer.clip_stack[0].h = height;

    /* Initialize dirty tracking */
    g_framebuffer.dirty_count = 0;
    g_framebuffer.full_dirty = true;
    g_framebuffer.frame_count = 0;
    g_framebuffer.vsync_enabled = true;
    g_framebuffer.initialized = true;

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

    g_display_initialized = false;
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
    if (info == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    if (!g_display_initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    info->width = g_framebuffer.width;
    info->height = g_framebuffer.height;
    info->pitch = g_framebuffer.pitch;
    info->bits_per_pixel = FB_BITS_PER_PIXEL;
    info->type = HAL_DISPLAY_TYPE_HDMI;     /* La Potato has HDMI */
    info->format = HAL_PIXEL_FORMAT_ARGB8888;
    info->framebuffer_base = g_framebuffer.buffers[0];
    info->framebuffer_size = g_framebuffer.buffer_size * FB_BUFFER_COUNT;
    info->buffer_count = FB_BUFFER_COUNT;

    return HAL_SUCCESS;
}

uint32_t hal_display_get_width(void)
{
    return g_display_initialized ? g_framebuffer.width : 0;
}

uint32_t hal_display_get_height(void)
{
    return g_display_initialized ? g_framebuffer.height : 0;
}

uint32_t hal_display_get_pitch(void)
{
    return g_display_initialized ? g_framebuffer.pitch : 0;
}

/* =============================================================================
 * DISPLAY CONTROL
 * =============================================================================
 */

hal_error_t hal_display_present(framebuffer_t *fb)
{
    if (fb == NULL || !fb->initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    /* Vsync wait (simplified) */
    if (fb->vsync_enabled) {
        /*
         * TODO: Wait for VPU vsync via interrupt or polling.
         * The VPU has VSYNC status in VIU registers.
         * For now, just a small delay.
         */
        hal_delay_us(1000);  /* ~1ms delay */
    }

    /* Swap buffers */
    if (FB_BUFFER_COUNT > 1) {
        uint32_t temp = fb->front_buffer;
        fb->front_buffer = fb->back_buffer;
        fb->back_buffer = temp;
        fb->addr = fb->buffers[fb->back_buffer];

        /*
         * TODO: Update VPU canvas with new framebuffer address.
         * This requires writing to AML_DMC_CAV_LUT_* registers
         * to re-map the canvas entry to the new buffer address.
         */
    }

    fb->frame_count++;
    fb->full_dirty = false;
    fb->dirty_count = 0;

    return HAL_SUCCESS;
}

hal_error_t hal_display_present_immediate(framebuffer_t *fb)
{
    if (fb == NULL || !fb->initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    /* Same as present but no vsync wait */
    if (FB_BUFFER_COUNT > 1) {
        uint32_t temp = fb->front_buffer;
        fb->front_buffer = fb->back_buffer;
        fb->back_buffer = temp;
        fb->addr = fb->buffers[fb->back_buffer];
    }

    fb->frame_count++;
    fb->full_dirty = false;
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
    /* TODO: Implement VPU vsync wait */
    hal_delay_us(16667);  /* ~60Hz frame time */
    return HAL_SUCCESS;
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

    /* Default for La Potato with HDMI */
    config->width = FB_DEFAULT_WIDTH;
    config->height = FB_DEFAULT_HEIGHT;
    config->type = HAL_DISPLAY_TYPE_HDMI;
    config->format = HAL_PIXEL_FORMAT_ARGB8888;
    config->buffer_count = FB_BUFFER_COUNT;
    config->vsync_enabled = true;

    return HAL_SUCCESS;
}
