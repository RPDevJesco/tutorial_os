/*
 * display_simplefb.c — SimpleFB Display Driver for the Ky X1 SoC
 * =================================================================
 *
 * This is the display driver that makes Tutorial-OS's entire UI system work
 * on the Orange Pi RV2. It's the Ky X1 equivalent of:
 *   - drivers/framebuffer/framebuffer.c (BCM2710 — mailbox-based FB allocation)
 *   - soc/rk3528a/vop2_framebuffer.c (Rockchip — VOP2 display controller)
 *
 * The Strategy: SimpleFB
 * ======================
 * On the Pi, we ask the VideoCore GPU to allocate a framebuffer via the
 * mailbox interface. On the Ky X1, we get it for FREE from U-Boot.
 *
 * U-Boot's CONFIG_FDT_SIMPLEFB=y means it:
 *   1. Initializes the display hardware (DPU "Saturn" + HDMI TX)
 *   2. Allocates a framebuffer in DRAM
 *   3. Injects a `simple-framebuffer` node into the device tree (DTB)
 *   4. Passes the DTB pointer in register a1 when jumping to our kernel
 *
 * All we need to do is:
 *   1. Parse the DTB to find the simple-framebuffer node
 *   2. Extract: base address, width, height, stride, pixel format
 *   3. Point our framebuffer_t at that memory
 *   4. Start drawing — the hardware is already scanning out!
 *
 * This is the fastest path to pixels on screen. No DPU driver needed.
 * The tradeoff: no double-buffering, no resolution switching, no vsync.
 * Those come later when we write a proper DPU driver.
 *
 * DTB (Device Tree Blob) Parsing:
 * ================================
 * The DTB is a binary format that describes hardware. We parse it manually
 * (no libfdt dependency — remember, Tutorial-OS has no external deps).
 *
 * The simple-framebuffer node looks like:
 *   framebuffer@... {
 *       compatible = "simple-framebuffer";
 *       reg = <0x0 0x???????? 0x0 0x????????>;  // base addr, size
 *       width = <1920>;     // or whatever U-Boot configured
 *       height = <1080>;
 *       stride = <7680>;    // bytes per row (width × 4 for 32bpp)
 *       format = "a8r8g8b8"; // ARGB8888 — same as our framebuffer_t!
 *   };
 *
 * ARGB8888 is exactly the pixel format framebuffer_t uses (uint32_t *addr,
 * colors like 0xFF3366FF). This means ZERO format conversion needed.
 */

#include "kyx1_regs.h"
#include "types.h"

/* Forward declarations for UART debug output */
extern void kyx1_uart_puts(const char *str);
extern void kyx1_uart_puthex(uint64_t val);
extern void kyx1_uart_putdec(uint32_t val);
extern void kyx1_uart_putc(char c);

/* =============================================================================
 * FDT (FLATTENED DEVICE TREE) CONSTANTS
 * =============================================================================
 *
 * The DTB is a binary format with a header, structure block, and strings block.
 * We only need a minimal parser — just enough to find the simple-framebuffer
 * node and extract its properties.
 *
 * Full spec: https://www.devicetree.org/specifications/
 * We implement ~10% of it — the bare minimum.
 */

#define FDT_MAGIC           0xD00DFEED  /* Big-endian magic number */
#define FDT_BEGIN_NODE      0x00000001  /* Start of a node */
#define FDT_END_NODE        0x00000002  /* End of a node */
#define FDT_PROP            0x00000003  /* Property */
#define FDT_NOP             0x00000004  /* No-op (padding) */
#define FDT_END             0x00000009  /* End of structure block */

/* FDT header offsets (all values are big-endian 32-bit) */
#define FDT_OFF_MAGIC       0x00
#define FDT_OFF_TOTALSIZE   0x04
#define FDT_OFF_DT_STRUCT   0x08
#define FDT_OFF_DT_STRINGS  0x0C
#define FDT_OFF_VERSION     0x14

/* =============================================================================
 * ENDIAN HELPERS
 * =============================================================================
 * DTB is always big-endian. RISC-V is little-endian. We need byte-swapping.
 * Same problem exists on ARM64 (also little-endian).
 */

static inline uint32_t fdt_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

static inline uint64_t fdt_be64(const void *p)
{
    return ((uint64_t)fdt_be32(p) << 32) | fdt_be32((const uint8_t *)p + 4);
}

/* =============================================================================
 * STRING HELPERS
 * =============================================================================
 */

static int fdt_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static int fdt_strncmp(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

/* Round up to 4-byte alignment (FDT requires 4-byte aligned tokens) */
static inline uint32_t fdt_align4(uint32_t offset)
{
    return (offset + 3) & ~3U;
}

/* =============================================================================
 * SIMPLEFB RESULT STRUCTURE
 * =============================================================================
 * This holds what we extract from the DTB. The fields map directly onto
 * the framebuffer_t struct fields used by the rest of Tutorial-OS.
 */

typedef struct {
    uint64_t    base_addr;      /* Physical address of pixel data */
    uint64_t    size;           /* Total framebuffer size in bytes */
    uint32_t    width;          /* Horizontal resolution in pixels */
    uint32_t    height;         /* Vertical resolution in pixels */
    uint32_t    stride;         /* Bytes per row (width × bytes_per_pixel) */
    uint32_t    bpp;            /* Bits per pixel (derived from format) */
    bool        found;          /* Did we find the node? */
    char        format[32];     /* Pixel format string ("a8r8g8b8", etc.) */
} simplefb_info_t;

/* =============================================================================
 * DTB PARSER — Find and extract simple-framebuffer properties
 * =============================================================================
 *
 * This is a targeted parser: we walk the FDT structure block looking for
 * a node whose "compatible" property is "simple-framebuffer", then extract
 * its properties.
 *
 * A full DTB parser would handle arbitrary nodes, phandles, overlays, etc.
 * We don't need any of that — just one specific node type.
 */

static bool parse_simplefb_from_dtb(const void *dtb, simplefb_info_t *info)
{
    const uint8_t *base = (const uint8_t *)dtb;

    /* Validate FDT magic */
    if (fdt_be32(base + FDT_OFF_MAGIC) != FDT_MAGIC) {
        kyx1_uart_puts("[simplefb] ERROR: Bad DTB magic\n");
        return false;
    }

    uint32_t struct_off  = fdt_be32(base + FDT_OFF_DT_STRUCT);
    uint32_t strings_off = fdt_be32(base + FDT_OFF_DT_STRINGS);
    const uint8_t *strtab = base + strings_off;

    uint32_t offset = struct_off;
    int depth = 0;
    bool in_simplefb_node = false;

    /* Walk the structure block token by token */
    while (1) {
        uint32_t token = fdt_be32(base + offset);
        offset += 4;

        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)(base + offset);
            uint32_t name_len = 0;
            while (name[name_len] != '\0') name_len++;
            offset += fdt_align4(name_len + 1);
            depth++;

            /*
             * Look for node names starting with "framebuffer@" or
             * "simple-framebuffer". U-Boot typically names it
             * "framebuffer@<address>".
             */
            if (fdt_strncmp(name, "framebuffer", 11) == 0 ||
                fdt_strncmp(name, "simple-framebuffer", 18) == 0) {
                in_simplefb_node = true;
            }
            break;
        }

        case FDT_END_NODE:
            if (in_simplefb_node && depth > 0) {
                /*
                 * We've exited the framebuffer node. If we found the
                 * compatible property confirming it's simple-framebuffer,
                 * we're done.
                 */
                if (info->found) {
                    return true;
                }
                in_simplefb_node = false;
            }
            depth--;
            break;

        case FDT_PROP: {
            uint32_t val_size  = fdt_be32(base + offset);
            uint32_t name_off  = fdt_be32(base + offset + 4);
            offset += 8;

            const uint8_t *val = base + offset;
            const char *pname  = (const char *)(strtab + name_off);
            offset += fdt_align4(val_size);

            if (!in_simplefb_node) break;

            /* Extract properties we care about */
            if (fdt_strcmp(pname, "compatible") == 0) {
                if (fdt_strncmp((const char *)val, "simple-framebuffer", 18) == 0) {
                    info->found = true;
                }
            }
            else if (fdt_strcmp(pname, "reg") == 0) {
                /*
                 * "reg" property format depends on #address-cells and
                 * #size-cells of the parent node. For SimpleFB at the
                 * top level, it's typically 2 cells each (64-bit):
                 *   <addr_hi addr_lo size_hi size_lo>
                 *
                 * We handle both 1-cell (32-bit) and 2-cell (64-bit) cases.
                 */
                if (val_size >= 16) {
                    /* 2-cell address + 2-cell size (#address-cells=2, #size-cells=2) */
                    info->base_addr = fdt_be64(val);
                    info->size      = fdt_be64(val + 8);
                } else if (val_size >= 8) {
                    /* 1-cell address + 1-cell size */
                    info->base_addr = fdt_be32(val);
                    info->size      = fdt_be32(val + 4);
                }
            }
            else if (fdt_strcmp(pname, "width") == 0 && val_size >= 4) {
                info->width = fdt_be32(val);
            }
            else if (fdt_strcmp(pname, "height") == 0 && val_size >= 4) {
                info->height = fdt_be32(val);
            }
            else if (fdt_strcmp(pname, "stride") == 0 && val_size >= 4) {
                info->stride = fdt_be32(val);
            }
            else if (fdt_strcmp(pname, "format") == 0) {
                /* Copy format string (e.g., "a8r8g8b8", "x8r8g8b8") */
                uint32_t copy_len = val_size < 31 ? val_size : 31;
                for (uint32_t i = 0; i < copy_len; i++)
                    info->format[i] = (char)val[i];
                info->format[copy_len] = '\0';

                /* Determine BPP from format string */
                if (fdt_strncmp(info->format, "a8r8g8b8", 8) == 0 ||
                    fdt_strncmp(info->format, "x8r8g8b8", 8) == 0) {
                    info->bpp = 32;
                }
                else if (fdt_strncmp(info->format, "r5g6b5", 6) == 0) {
                    info->bpp = 16;
                }
                else {
                    info->bpp = 32; /* Default assumption */
                }
            }
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            return info->found;

        default:
            /* Unknown token — bail out to avoid infinite loop */
            kyx1_uart_puts("[simplefb] ERROR: Unknown FDT token: ");
            kyx1_uart_puthex(token);
            kyx1_uart_putc('\n');
            return false;
        }
    }
}

/* =============================================================================
 * FRAMEBUFFER_T INITIALIZATION
 * =============================================================================
 *
 * This is the function that bridges SimpleFB → your existing framebuffer_t.
 * After this returns true, you can call fb_fill_rect(), fb_draw_string(),
 * fb_present(), ui_draw_panel(), etc. — the ENTIRE existing UI stack works.
 *
 * The framebuffer_t struct expects:
 *   .addr     = pointer to pixel data (uint32_t* for ARGB8888)
 *   .width    = horizontal resolution
 *   .height   = vertical resolution
 *   .pitch    = bytes per row (stride)
 *   .initialized = true
 *
 * SimpleFB gives us exactly these values from the DTB.
 */

/* Include the framebuffer type definition */
#include "framebuffer.h"

/*
 * kyx1_display_init — Initialize display from SimpleFB DTB node
 *
 * @param fb   Pointer to framebuffer_t to populate
 * @param dtb  Pointer to the device tree blob (from boot parameter a1)
 *
 * Returns: true if SimpleFB was found and framebuffer is ready for drawing
 *
 * After this returns true, ALL of these just work:
 *   - fb_clear(&fb, color)
 *   - fb_fill_rect(&fb, x, y, w, h, color)
 *   - fb_draw_string_transparent(&fb, x, y, "Hello", color)
 *   - fb_draw_char(&fb, x, y, 'X', fg, bg)
 *   - ui_draw_panel(&fb, rect, &theme, style)
 *   - ui_draw_badge(&fb, x, y, &theme, "BARE METAL")
 *   - ui_draw_toast(&fb, rect, &theme, "Ready", UI_TOAST_SUCCESS)
 *   ... literally every drawing function in the codebase.
 */
bool kyx1_display_init(framebuffer_t *fb, const void *dtb)
{
    if (!fb || !dtb) return false;

    simplefb_info_t info;
    /* Zero-init the struct */
    for (uint32_t i = 0; i < sizeof(info); i++)
        ((uint8_t *)&info)[i] = 0;

    kyx1_uart_puts("[simplefb] Parsing DTB for simple-framebuffer...\n");

    if (!parse_simplefb_from_dtb(dtb, &info) || !info.found) {
        kyx1_uart_puts("[simplefb] No simple-framebuffer node in DTB\n");
        kyx1_uart_puts("[simplefb] Using hardcoded U-Boot framebuffer (fb=7f700000 1920x1080)\n");

        /*
         * Fallback: Use the framebuffer U-Boot already set up.
         *
         * U-Boot prints "fb=7f700000, size=1920 1080" during HDMI init,
         * telling us exactly where the display hardware is scanning from.
         * Even without CONFIG_FDT_SIMPLEFB=y, the DPU + HDMI TX are fully
         * initialized — we just write pixels to that address.
         *
         * This is the same approach used when a GPU firmware sets up a
         * display but doesn't advertise it via device tree. The address
         * and resolution come from the U-Boot source or boot log.
         */
        info.base_addr = 0x7f700000;
        info.width     = 1920;
        info.height    = 1080;
        info.stride    = 1920 * 4;      /* ARGB8888 = 4 bytes/pixel */
        info.size      = 1920 * 1080 * 4;
        info.bpp       = 32;
        info.found     = true;
        info.format[0] = 'a'; info.format[1] = '8';
        info.format[2] = 'r'; info.format[3] = '8';
        info.format[4] = 'g'; info.format[5] = '8';
        info.format[6] = 'b'; info.format[7] = '8';
        info.format[8] = '\0';
    }

    /* Sanity checks */
    if (info.base_addr == 0 || info.width == 0 || info.height == 0) {
        kyx1_uart_puts("[simplefb] ERROR: Invalid framebuffer parameters\n");
        return false;
    }

    if (info.stride == 0) {
        /* Calculate stride if not provided */
        info.stride = info.width * (info.bpp / 8);
    }

    /* Print what we found */
    kyx1_uart_puts("[simplefb] Found framebuffer:\n");
    kyx1_uart_puts("  Address: "); kyx1_uart_puthex(info.base_addr); kyx1_uart_putc('\n');
    kyx1_uart_puts("  Size:    "); kyx1_uart_puthex(info.size);      kyx1_uart_putc('\n');
    kyx1_uart_puts("  ");          kyx1_uart_putdec(info.width);
    kyx1_uart_puts("x");           kyx1_uart_putdec(info.height);
    kyx1_uart_puts(" @ ");         kyx1_uart_putdec(info.bpp);
    kyx1_uart_puts("bpp\n");
    kyx1_uart_puts("  Stride:  "); kyx1_uart_putdec(info.stride);    kyx1_uart_putc('\n');
    kyx1_uart_puts("  Format:  "); kyx1_uart_puts(info.format);      kyx1_uart_putc('\n');

    /*
     * Populate the framebuffer_t struct.
     *
     * The SimpleFB base address is a PHYSICAL address. Since we're running
     * with the MMU off (satp = 0), physical == virtual. We can cast it
     * directly to a pointer.
     *
     * NOTE: If we ever enable the MMU, we'll need to map this address first.
     */
    fb->addr    = (uint32_t *)(uintptr_t)info.base_addr;
    fb->width   = info.width;
    fb->height  = info.height;
    fb->pitch   = info.stride;

    /*
     * Single-buffer setup for SimpleFB.
     * No double-buffering support since we don't control the DPU scanout.
     * The DPU is already scanning this memory region — writes are immediately
     * visible (which also means tearing is possible, but that's fine for now).
     */
    fb->buffers[0]    = fb->addr;
    fb->buffers[1]    = fb->addr;    /* Both point to same buffer */
    fb->front_buffer  = 0;
    fb->back_buffer   = 0;
    fb->buffer_size   = info.stride * info.height;
    fb->virtual_height = info.height;

    /* Initialize tracking state */
    fb->dirty_count   = 0;
    fb->full_dirty    = false;
    fb->clip_depth    = 0;
    fb->clip_stack[0].x = 0;
    fb->clip_stack[0].y = 0;
    fb->clip_stack[0].w = info.width;
    fb->clip_stack[0].h = info.height;
    fb->frame_count    = 0;
    fb->vsync_enabled  = false;  /* No vsync control with SimpleFB */
    fb->initialized    = true;

    kyx1_uart_puts("[simplefb] Framebuffer initialized — UI system is GO!\n");

    return true;
}

/*
 * kyx1_display_present — "Present" the framebuffer
 *
 * With SimpleFB, this is essentially a no-op because the DPU is already
 * scanning the memory we're writing to. However, we need to ensure our
 * cached writes are visible in DRAM (the DPU reads from DRAM, not cache).
 *
 * This function calls clean_dcache_range to flush dirty cache lines.
 * It matches the fb_present() pattern used on BCM (which does a mailbox
 * buffer swap) and Rockchip (which programs VOP2 scanout address).
 */

/* Cache operations — declared in kyx1_cpu.h, implemented in boot/riscv64/cache.S */
extern void clean_dcache_range(uintptr_t start, size_t size);

void kyx1_display_present(framebuffer_t *fb)
{
    if (!fb || !fb->initialized) return;

    /*
     * Flush the entire framebuffer from cache to DRAM so the DPU sees
     * our pixel writes. This is the same concept as dsb() after
     * framebuffer writes on ARM64 — ensuring the display controller
     * (which is a separate bus master) sees coherent data.
     */
    clean_dcache_range((uintptr_t)fb->addr, fb->buffer_size);

    fb->frame_count++;
}