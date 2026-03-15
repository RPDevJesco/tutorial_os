/*
 * soc/lattepanda_n100/display_gop.c — UEFI GOP Framebuffer Driver
 *
 * Tutorial-OS: LattePanda N100 (x86_64 / UEFI) Display Implementation
 *
 * x86_64 display init is fundamentally different from ARM64/RISC-V:
 *   - ARM64: Mailbox property tags request framebuffer from VideoCore GPU
 *   - RISC-V: SimpleFB node read from device tree blob (DTB from U-Boot)
 *   - x86_64: GOP (Graphics Output Protocol) queried from UEFI Boot Services
 *
 * The GOP framebuffer base address is a full 64-bit physical address.
 * The upper 32 bits are non-zero on the LattePanda MU — confirmed in the
 * bring-up UART log. NEVER truncate to uint32_t.
 *
 * Pixel format must be detected at runtime from info->PixelFormat:
 *   PixelRedGreenBlueReserved8BitPerColor = 0  (RGB)
 *   PixelBlueGreenRedReserved8BitPerColor = 1  (BGR) ← LattePanda MU hardware
 *   PixelBitMask                          = 2
 *   PixelBltOnly                          = 3  (no linear framebuffer)
 *
 * IMPORTANT: n100_display_init() must be called BEFORE ExitBootServices().
 * GOP is a Boot Service — LocateProtocol becomes unavailable after EBS.
 * soc_init.c calls us first, saves the framebuffer info, then exits.
 */

#include "hal/hal_types.h"
#include "hal/hal_display.h"
#include "drivers/framebuffer/framebuffer.h"

/* gnu-efi — UEFI types and uefi_call_wrapper (via efi.h → efibind.h) */
#include <efi.h>

/* ============================================================
 * Module state — filled by n100_display_init()
 * ============================================================ */

typedef struct {
    uint64_t    fb_base;        /* 64-bit framebuffer physical address    */
    uint32_t    width;          /* Horizontal resolution in pixels        */
    uint32_t    height;         /* Vertical resolution in pixels          */
    uint32_t    stride;         /* Pixels per scan line (may > width)     */
    uint32_t    pixel_format;   /* 0=RGB, 1=BGR, 2=bitmask, 3=blt-only   */
    bool        initialized;
} n100_display_state_t;

static n100_display_state_t g_display = {0};

/* ============================================================
 * Pixel format helpers
 * ============================================================ */

/*
 * n100_make_pixel — Pack R/G/B into a 32-bit pixel word for this hardware.
 *
 * Called by display_gop.c only during the HAL display_init fill path.
 * The framebuffer driver uses its own ARGB packing — this is for the
 * low-level GOP clear and test pattern drawn before HAL takes over.
 */
static inline uint32_t n100_make_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    if (g_display.pixel_format == 1) {
        /* BGR: LattePanda MU confirmed hardware (fmt=1) */
        return ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
    } else {
        /* RGB: default assumption for other x86_64 hardware */
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

/*
 * n100_fill_rect — Fill a rectangle in the GOP framebuffer.
 *
 * Uses 64-bit fb_base + row offset to form each line pointer.
 * This is the pattern from the bring-up test that confirmed correct
 * color bar rendering on the LattePanda MU display.
 */
static void n100_fill_rect(uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h,
                            uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t px = n100_make_pixel(r, g, b);
    for (uint32_t row = y; row < y + h; row++) {
        uint32_t *line = (uint32_t *)(uintptr_t)(
            g_display.fb_base + (uint64_t)row * g_display.stride * 4);
        for (uint32_t col = x; col < x + w; col++)
            line[col] = px;
    }
}

/* ============================================================
 * External UART helpers (from uart_8250.c)
 * ============================================================ */
extern void n100_uart_puts(const char *s);
extern void n100_uart_putdec(uint32_t v);
extern void n100_uart_puthex64(uint64_t v);
extern void n100_uart_putc(char c);

/* ============================================================
 * Public API
 * ============================================================ */

/*
 * n100_display_init — Locate GOP and capture framebuffer parameters.
 *
 * Must be called while UEFI Boot Services are still active.
 * Does NOT call ExitBootServices — that is soc_init.c's responsibility.
 *
 * Returns true on success, false if GOP is not found or has no linear FB.
 *
 * On success, populates g_display and sets the HAL framebuffer fields
 * in *fb so that fb_*() drawing functions can be used immediately.
 */
bool n100_display_init(EFI_HANDLE image_handle,
                       EFI_SYSTEM_TABLE *system_table,
                       framebuffer_t *fb)
{
    (void)image_handle; /* Not needed for LocateProtocol */

    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

    EFI_STATUS status = uefi_call_wrapper(system_table->BootServices->LocateProtocol, 3,
        &gop_guid, NULL, (VOID **)&gop);

    if (EFI_ERROR(status) || !gop) {
        n100_uart_puts("GOP: NOT FOUND\r\n");
        return false;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;

    /* PixelBltOnly means no linear framebuffer, cannot use directly */
    if (info->PixelFormat == 3) {
        n100_uart_puts("GOP: PixelBltOnly — no linear framebuffer\r\n");
        return false;
    }

    /* Capture all parameters before ExitBootServices invalidates GOP */
    g_display.fb_base      = (uint64_t)gop->Mode->FrameBufferBase;
    g_display.width        = info->HorizontalResolution;
    g_display.height       = info->VerticalResolution;
    g_display.stride       = info->PixelsPerScanLine;
    g_display.pixel_format = (uint32_t)info->PixelFormat;
    g_display.initialized  = true;

    /* Log everything to UART*/
    n100_uart_puts("GOP: found  res=");
    n100_uart_putdec(g_display.width);
    n100_uart_putc('x');
    n100_uart_putdec(g_display.height);
    n100_uart_puts("  stride=");
    n100_uart_putdec(g_display.stride);
    n100_uart_puts("  fmt=");
    n100_uart_putdec(g_display.pixel_format);
    n100_uart_puts("  fb=");
    n100_uart_puthex64(g_display.fb_base);
    n100_uart_puts("\r\n");

    /*
     * Populate the HAL framebuffer_t.
     *
     * buffer is the 64-bit GOP address cast through uintptr_t.
     * On x86_64, uintptr_t is 64 bits — no truncation.
     *
     * buffer_size uses size_t (also 64 bits on x86_64).
     * stride_bytes = stride * 4 (32bpp always for GOP ARGB/BGRX modes).
     *
     * pixel_format: framebuffer.h uses ARGB internally.
     * The HAL translates at present-time if needed; for now we store the
     * raw GOP fmt so present can do the right thing.
     */
    fb->addr            = (uint32_t *)(uintptr_t)g_display.fb_base;
    fb->width           = g_display.width;
    fb->height          = g_display.height;
    fb->pitch           = g_display.stride * 4;  /* bytes per row */
    fb->buffer_size     = (size_t)g_display.stride * g_display.height * 4;

    /* Clip stack must be initialized to avoid garbage clip_depth */
    fb->clip_depth      = 0;
    fb->clip_stack[0].x = 0;
    fb->clip_stack[0].y = 0;
    fb->clip_stack[0].w = g_display.width;
    fb->clip_stack[0].h = g_display.height;
    fb->dirty_count     = 0;
    fb->full_dirty      = false;
    fb->frame_count     = 0;
    fb->initialized     = true;

    /*
     * Clear the framebuffer to black before handing control to kernel.
     * This avoids displaying whatever the BIOS/EFI left on screen.
     */
    n100_fill_rect(0, 0, g_display.width, g_display.height, 0, 0, 0);

    n100_uart_puts("GOP: framebuffer ready\r\n");
    return true;
}

/*
 * n100_display_present — Flush framebuffer to screen.
 *
 * On x86_64 with UEFI GOP, the framebuffer is write-combining memory
 * (MTRR/PAT configured by UEFI firmware). No explicit cache flush is
 * needed — stores become visible to the display controller without
 * flushing. This is unlike ARM64/RISC-V where explicit cache ops are
 * required for DMA visibility.
 *
 * A store fence (sfence) ensures all prior writes are globally visible
 * before the display controller's next scanout.
 */
void n100_display_present(framebuffer_t *fb)
{
    (void)fb;
    /* sfence: all stores complete before display scanout reads */
    __asm__ volatile ("sfence" ::: "memory");
}

/*
 * n100_display_get_pixel_format — Return the raw GOP pixel format index.
 *
 * 0 = RGB (PixelRedGreenBlueReserved8BitPerColor)
 * 1 = BGR (PixelBlueGreenRedReserved8BitPerColor) — LattePanda MU
 * 2 = Bitmask
 */
uint32_t n100_display_get_pixel_format(void)
{
    return g_display.pixel_format;
}

/*
 * n100_display_get_fb_base — Return the raw 64-bit framebuffer address.
 */
uint64_t n100_display_get_fb_base(void)
{
    return g_display.fb_base;
}