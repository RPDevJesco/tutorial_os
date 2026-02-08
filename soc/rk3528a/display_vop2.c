/*
 * soc/rk3528a/display_vop2.c - RK3528A Display Implementation
 *
 * Tutorial-OS: RK3528A HAL Implementation
 *
 * COMPLETELY DIFFERENT FROM BCM!
 *
 * Rockchip uses VOP2 (Video Output Processor 2) for display.
 * This is much more complex than BCM's mailbox-based approach:
 *
 *   1. Configure CRU clocks for VOP2
 *   2. Set up VOP2 "win" (window/layer) for framebuffer
 *   3. Configure output interface (HDMI, MIPI DSI, etc.)
 *   4. Connect VOP2 to HDMI TX or other output
 *
 * This is a SIMPLIFIED implementation that:
 *   - Uses a pre-allocated framebuffer region
 *   - Assumes U-Boot has already initialized display
 *   - Just provides HAL interface compatibility
 *
 * For full VOP2 initialization, see Rockchip kernel driver.
 */

#include "hal/hal_display.h"
#include "hal/hal_gpio.h"
#include "hal/hal_timer.h"
#include "rk3528a_regs.h"

/* Include the portable framebuffer header */
#include "framebuffer.h"

/* =============================================================================
 * CONFIGURATION
 * =============================================================================
 */

#define FB_BITS_PER_PIXEL   32
#define FB_BUFFER_COUNT     2

/* Override default dimensions from framebuffer.h for Rock 2A (HDMI) */
#undef FB_DEFAULT_WIDTH
#undef FB_DEFAULT_HEIGHT
#define FB_DEFAULT_WIDTH    1280
#define FB_DEFAULT_HEIGHT   720

/*
 * Reserved framebuffer region
 * In a real implementation, this would be allocated from reserved memory
 * or obtained from device tree.
 */
#define FB_RESERVED_BASE    0x7F000000  /* Near top of 2GB RAM */
#define FB_RESERVED_SIZE    (16 * 1024 * 1024)  /* 16MB for framebuffer */

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

static framebuffer_t g_framebuffer;
static bool g_display_initialized = false;

/* =============================================================================
 * VOP2 CONFIGURATION (Simplified)
 * =============================================================================
 */

static hal_error_t vop2_configure(uint32_t width, uint32_t height, uintptr_t fb_addr)
{
    /*
     * Full VOP2 configuration is complex and requires:
     * 1. Enable VOP2 clocks in CRU
     * 2. Configure VOP2 output mode (timing, resolution)
     * 3. Set up win (window) layer with framebuffer address
     * 4. Configure scaling if needed
     * 5. Connect to HDMI/DSI/etc.
     *
     * For now, assume U-Boot has done basic setup and we just
     * update the framebuffer address.
     */

    (void)height;  /* Currently unused in simplified implementation */

    /* Write framebuffer address to VOP2 WIN0 */
    hal_mmio_write32(RK_VOP2_BASE + RK_VOP2_WIN0_YRGB_MST, (uint32_t)fb_addr);

    /* Set virtual stride (pitch) */
    uint32_t stride = width * (FB_BITS_PER_PIXEL / 8);
    hal_mmio_write32(RK_VOP2_BASE + RK_VOP2_WIN0_VIR, stride / 4);

    /* Trigger configuration update */
    hal_mmio_write32(RK_VOP2_BASE + RK_VOP2_REG_CFG_DONE, 1);

    HAL_DMB();

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
    if (fb_out == NULL) {
        return HAL_ERROR_NULL_PTR;
    }

    if (g_display_initialized) {
        return HAL_ERROR_ALREADY_INIT;
    }

    /* Calculate framebuffer parameters */
    uint32_t pitch = width * (FB_BITS_PER_PIXEL / 8);
    uint32_t buffer_size = pitch * height;
    uint32_t total_size = buffer_size * FB_BUFFER_COUNT;

    if (total_size > FB_RESERVED_SIZE) {
        return HAL_ERROR_NO_MEMORY;
    }

    /* Use reserved memory region for framebuffer */
    uintptr_t fb_addr = FB_RESERVED_BASE;

    /* Configure VOP2 (simplified) */
    hal_error_t err = vop2_configure(width, height, fb_addr);
    if (HAL_FAILED(err)) {
        return err;
    }

    /* Fill in framebuffer structure */
    g_framebuffer.width = width;
    g_framebuffer.height = height;
    g_framebuffer.pitch = pitch;
    g_framebuffer.virtual_height = height * FB_BUFFER_COUNT;
    g_framebuffer.buffer_size = buffer_size;

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
    info->type = HAL_DISPLAY_TYPE_HDMI;  /* Rock 2A has HDMI */
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

    /* Vsync wait (simplified - read VOP2 status) */
    if (fb->vsync_enabled) {
        /* TODO: Wait for VOP2 vsync interrupt or frame done */
        /* For now, just a small delay */
        hal_delay_us(1000);  /* ~1ms delay */
    }

    /* Swap buffers */
    if (FB_BUFFER_COUNT > 1) {
        uint32_t temp = fb->front_buffer;
        fb->front_buffer = fb->back_buffer;
        fb->back_buffer = temp;
        fb->addr = fb->buffers[fb->back_buffer];

        /* Update VOP2 with new framebuffer address */
        hal_mmio_write32(RK_VOP2_BASE + RK_VOP2_WIN0_YRGB_MST,
                         (uint32_t)(uintptr_t)fb->buffers[fb->front_buffer]);

        /* Trigger config update */
        hal_mmio_write32(RK_VOP2_BASE + RK_VOP2_REG_CFG_DONE, 1);
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

        hal_mmio_write32(RK_VOP2_BASE + RK_VOP2_WIN0_YRGB_MST,
                         (uint32_t)(uintptr_t)fb->buffers[fb->front_buffer]);
        hal_mmio_write32(RK_VOP2_BASE + RK_VOP2_REG_CFG_DONE, 1);
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
    /* TODO: Implement VOP2 vsync wait */
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

    /* Default for Rock 2A with HDMI */
    config->width = FB_DEFAULT_WIDTH;
    config->height = FB_DEFAULT_HEIGHT;
    config->type = HAL_DISPLAY_TYPE_HDMI;
    config->format = HAL_PIXEL_FORMAT_ARGB8888;
    config->buffer_count = FB_BUFFER_COUNT;
    config->vsync_enabled = true;

    return HAL_SUCCESS;
}