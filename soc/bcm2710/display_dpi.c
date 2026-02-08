/*
 * soc/bcm2710/display_dpi.c - BCM2710 Display Implementation (DPI/HDMI)
 *
 * Tutorial-OS: BCM2710 HAL Implementation
 *
 * Implements hal_display.h for BCM2710.
 * Existing framebuffer initialization code (fb_init_with_size) reorganized to fit the HAL structure.
 *
 * IMPORTANT: drawing functions (fb_clear, fb_draw_rect, etc.) are
 * completely portable and stay in drivers/framebuffer/framebuffer.c.
 * This file ONLY handles platform-specific initialization and buffer swap.
 */

#include "hal/hal_display.h"
#include "hal/hal_gpio.h"
#include "bcm2710_mailbox.h"

/* Include the portable framebuffer header for the struct definition */
/* This header stays in drivers/framebuffer/ */
#include "framebuffer.h"

/* =============================================================================
 * CONFIGURATION
 * =============================================================================
 */

#define FB_BITS_PER_PIXEL   32
#define FB_BUFFER_COUNT     2       /* Double buffering */
#define FB_DEFAULT_WIDTH    640
#define FB_DEFAULT_HEIGHT   480

/* =============================================================================
 * INTERNAL STATE
 * =============================================================================
 */

/* The actual framebuffer structure - matches existing framebuffer_t */
static framebuffer_t g_framebuffer;
static bool g_display_initialized = false;

/* =============================================================================
 * FRAMEBUFFER ALLOCATION VIA MAILBOX
 * =============================================================================
 * This is existing fb_init_with_size() mailbox code.
 */

static hal_error_t allocate_framebuffer(uint32_t width, uint32_t height)
{
    bcm_mailbox_buffer_t mbox = { .data = {0} };
    uint32_t virtual_height = height * FB_BUFFER_COUNT;

    /*
     * Build the framebuffer allocation request.
     */
    uint32_t i = 0;
    mbox.data[i++] = 0;                         /* Total size (filled at end) */
    mbox.data[i++] = BCM_MBOX_REQUEST;

    /* Set physical size */
    mbox.data[i++] = BCM_TAG_SET_PHYSICAL_SIZE;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = width;
    mbox.data[i++] = height;

    /* Set virtual size (height * buffer_count for double buffering) */
    mbox.data[i++] = BCM_TAG_SET_VIRTUAL_SIZE;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = width;
    mbox.data[i++] = virtual_height;

    /* Set virtual offset */
    mbox.data[i++] = BCM_TAG_SET_VIRTUAL_OFFSET;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;

    /* Set depth */
    mbox.data[i++] = BCM_TAG_SET_DEPTH;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    mbox.data[i++] = FB_BITS_PER_PIXEL;

    /* Set pixel order (BGR) */
    mbox.data[i++] = BCM_TAG_SET_PIXEL_ORDER;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;                         /* 0 = BGR */

    /* Allocate buffer */
    mbox.data[i++] = BCM_TAG_ALLOCATE_BUFFER;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    uint32_t alloc_idx = i;
    mbox.data[i++] = 16;                        /* Alignment */
    mbox.data[i++] = 0;                         /* Size (output) */

    /* Get pitch */
    mbox.data[i++] = BCM_TAG_GET_PITCH;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    uint32_t pitch_idx = i;
    mbox.data[i++] = 0;                         /* Pitch (output) */

    /* End tag */
    mbox.data[i++] = BCM_TAG_END;

    /* Fill in total size */
    mbox.data[0] = i * 4;

    /* Send request */
    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return HAL_ERROR_DISPLAY_MAILBOX;
    }

    /* Extract results */
    uint32_t fb_addr = mbox.data[alloc_idx];
    uint32_t fb_size_bytes = mbox.data[alloc_idx + 1];
    uint32_t pitch = mbox.data[pitch_idx];

    if (fb_addr == 0 || fb_size_bytes == 0) {
        return HAL_ERROR_DISPLAY_NO_FB;
    }

    /* Convert bus address to ARM address */
    fb_addr = BCM_BUS_TO_ARM(fb_addr);

    /* Fill in framebuffer structure */
    g_framebuffer.width = width;
    g_framebuffer.height = height;
    g_framebuffer.pitch = pitch;
    g_framebuffer.virtual_height = virtual_height;
    g_framebuffer.buffer_size = pitch * height;

    g_framebuffer.buffers[0] = (uint32_t *)(uintptr_t)fb_addr;
    if (FB_BUFFER_COUNT > 1) {
        g_framebuffer.buffers[1] = (uint32_t *)(uintptr_t)(fb_addr + g_framebuffer.buffer_size);
    }

    g_framebuffer.front_buffer = 0;
    g_framebuffer.back_buffer = (FB_BUFFER_COUNT > 1) ? 1 : 0;
    g_framebuffer.addr = g_framebuffer.buffers[g_framebuffer.back_buffer];

    /* Initialize clipping to full screen */
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

    /* Configure GPIO for DPI (if using DPI output like GPi Case) */
    hal_error_t err = hal_gpio_configure_dpi();
    if (HAL_FAILED(err)) {
        /* DPI configuration failed - might be HDMI only board */
        /* Continue anyway, let mailbox handle it */
    }

    /* Allocate framebuffer via mailbox */
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

    /* For now, just use width/height from config */
    return hal_display_init_with_size(config->width, config->height, fb_out);
}

hal_error_t hal_display_shutdown(void)
{
    if (!g_display_initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    /* Can't really deallocate the framebuffer on BCM */
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
    info->type = HAL_DISPLAY_TYPE_DPI;
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
 * These match existing fb_present() and fb_set_vsync() functions.
 */

hal_error_t hal_display_present(framebuffer_t *fb)
{
    if (fb == NULL || !fb->initialized) {
        return HAL_ERROR_NOT_INIT;
    }

    /* Wait for vsync if enabled */
    if (fb->vsync_enabled) {
        bcm_mailbox_wait_vsync();
    }

    /* Swap buffers */
    if (FB_BUFFER_COUNT > 1) {
        uint32_t temp = fb->front_buffer;
        fb->front_buffer = fb->back_buffer;
        fb->back_buffer = temp;
        fb->addr = fb->buffers[fb->back_buffer];
    }

    /* Set virtual offset to display front buffer */
    uint32_t y_offset = fb->front_buffer * fb->height;
    bcm_mailbox_set_virtual_offset(0, y_offset);

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

    /* Same as present but skip vsync */
    if (FB_BUFFER_COUNT > 1) {
        uint32_t temp = fb->front_buffer;
        fb->front_buffer = fb->back_buffer;
        fb->back_buffer = temp;
        fb->addr = fb->buffers[fb->back_buffer];
    }

    uint32_t y_offset = fb->front_buffer * fb->height;
    bcm_mailbox_set_virtual_offset(0, y_offset);

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
    if (bcm_mailbox_wait_vsync()) {
        return HAL_SUCCESS;
    }
    return HAL_ERROR_HARDWARE;
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

    /* Default for GPi Case 2W */
    config->width = FB_DEFAULT_WIDTH;
    config->height = FB_DEFAULT_HEIGHT;
    config->type = HAL_DISPLAY_TYPE_DPI;
    config->format = HAL_PIXEL_FORMAT_ARGB8888;
    config->buffer_count = FB_BUFFER_COUNT;
    config->vsync_enabled = true;

    return HAL_SUCCESS;
}
