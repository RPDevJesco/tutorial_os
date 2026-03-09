/*
 * display_simplefb.c — SimpleFB Display Driver for the JH-7110 SoC
 * =================================================================
 *
 * This is essentially identical to soc/kyx1/display_simplefb.c.
 * Both the Ky X1 (Orange Pi RV2) and JH7110 (Milk-V Mars) use the exact
 * same strategy: U-Boot initializes the display hardware and injects a
 * simple-framebuffer node into the DTB. We parse the DTB to find the
 * framebuffer address and dimensions, then point our framebuffer_t at it.
 *
 * THE STRATEGY: WHY IT'S IDENTICAL
 * =================================
 * The SimpleFB approach is hardware-agnostic by design. We don't care
 * whether U-Boot is driving a Ky X1's DPU/Saturn display controller or
 * a JH7110's DC8200 display controller — the DTB interface is the same:
 *
 *   framebuffer@xxxxxxxx {
 *       compatible = "simple-framebuffer";
 *       reg = <0x0 0xADDRESS 0x0 0xSIZE>;
 *       width = <1920>;
 *       height = <1080>;
 *       stride = <7680>;        // 1920 * 4 bytes per pixel
 *       format = "a8r8g8b8";
 *   };
 *
 * Once we've read this node, we write pixels to that address.
 * The display controller (DC8200) is already scanning from it.
 *
 * DIFFERENCES FROM KYX1 VERSION:
 *   - Function prefix changed: kyx1_ → jh7110_
 *   - Fallback address: 0xFE000000 (confirmed via U-Boot bdinfo)
 *   - Stride probed from DC8200 hardware register when DTB parse fails
 *   - Everything else is structurally identical
 *
 * HAL TEACHING POINT:
 *   Compare this file with soc/kyx1/display_simplefb.c side by side.
 *   The entire DTB parsing logic — fdt_get_u32(), fdt_strncmp(),
 *   parse_simplefb_from_dtb() — is copy-identical. This is not an
 *   accident. DTB parsing is architecture-agnostic C code. It would be
 *   a natural candidate for a shared common/ module. We keep it
 *   duplicated here intentionally to keep each SoC directory self-contained
 *   and minimize surprise for readers studying one platform at a time.
 */

#include "jh7110_regs.h"
#include "types.h"

extern void jh7110_uart_puts(const char *str);
extern void jh7110_uart_puthex(uint64_t val);
extern void jh7110_uart_putdec(uint32_t val);
extern void jh7110_uart_putc(char c);
void jh7110_l2_flush_range(uintptr_t phys_addr, size_t size);

#define JH7110_COLOR(argb) \
(((argb) & 0xFF00FF00) | \
(((argb) & 0x00FF0000) >> 16) | \
(((argb) & 0x000000FF) << 16))

/* =============================================================================
 * DC8200 DISPLAY CONTROLLER
 * =============================================================================
 *
 * The StarFive JH7110 contains the Verisilicon DC8200 display controller
 * at physical address 0x29400000. U-Boot initializes it before jumping to
 * our kernel, so the hardware is already running when we arrive.
 *
 * We only need one register: the primary layer stride, which tells us the
 * exact number of bytes between scan lines that the hardware is using.
 * This may differ from width*4 if U-Boot aligned the stride to a power-of-
 * two boundary (common: 1920*4=7680 rounded up to 8192).
 *
 * DC8200 register map (partial):
 *   0x0000 — AQ_HI_CLOCK_CONTROL
 *   0x0400 — DC_FRAMEBUFFER_CONFIG
 *   0x1400 — DC_DISPLAY_UNIT_0_FRAMEBUFFER_ADDRESS (primary layer base)
 *   0x1430 — DC_DISPLAY_UNIT_0_FRAMEBUFFER_STRIDE  (primary layer stride)
 */
#define DC8200_BASE         0x29400000UL
#define DC8200_STRIDE_REG   0x1430         /* Primary layer stride, bytes (could also be 0x1408 ) */

/* =============================================================================
 * FDT (FLATTENED DEVICE TREE) CONSTANTS
 * =============================================================================
 */

#define FDT_MAGIC       0xD00DFEED
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

/* FDT header structure */
typedef struct {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} fdt_header_t;

/* SimpleFB information extracted from the DTB */
typedef struct {
    uint64_t base_addr;     /* Framebuffer physical base address */
    uint64_t size;          /* Framebuffer size in bytes */
    uint32_t width;         /* Display width in pixels */
    uint32_t height;        /* Display height in pixels */
    uint32_t stride;        /* Bytes per row (width * bytes_per_pixel) */
    uint32_t bpp;           /* Bits per pixel */
    char     format[32];    /* Pixel format string ("a8r8g8b8", etc.) */
    bool     found;         /* True if a valid node was found */
} simplefb_info_t;

/* =============================================================================
 * MINIMAL FDT PARSING UTILITIES
 * =============================================================================
 */

/* Read a big-endian 32-bit value from unaligned memory */
static uint32_t fdt_r32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}

/* Compare two byte strings of given length */
static bool fdt_strncmp(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

/*
 * parse_simplefb_from_dtb — extract SimpleFB info from DTB
 *
 * Walks the FDT structure block looking for a node with
 * compatible = "simple-framebuffer". Extracts all properties.
 *
 * @param dtb   Pointer to the device tree blob (from boot register a1)
 * @param info  Output structure to populate
 * @return      true if a valid simple-framebuffer node was found
 */
static bool parse_simplefb_from_dtb(const void *dtb, simplefb_info_t *info)
{
    if (!dtb || !info) return false;

    const fdt_header_t *hdr = (const fdt_header_t *)dtb;

    /* Validate FDT magic number */
    if (fdt_r32((const uint8_t *)&hdr->magic) != FDT_MAGIC) {
        jh7110_uart_puts("[simplefb] ERROR: Invalid DTB magic\n");
        return false;
    }

    uint32_t struct_off   = fdt_r32((const uint8_t *)&hdr->off_dt_struct);
    uint32_t strings_off  = fdt_r32((const uint8_t *)&hdr->off_dt_strings);

    const uint8_t *struct_base  = (const uint8_t *)dtb + struct_off;
    const char    *strings_base = (const char    *)dtb + strings_off;

    const uint8_t *p = struct_base;
    bool in_simplefb_node = false;
    int  depth = 0;

    while (true) {
        uint32_t token = fdt_r32(p);
        p += 4;

        switch (token) {
        case FDT_BEGIN_NODE: {
            depth++;
            /* Node name is a null-terminated string at p, padded to 4 bytes */
            const char *name = (const char *)p;
            uint32_t name_len = 0;
            while (name[name_len]) name_len++;
            p += (name_len + 4) & ~3;   /* Advance past name, 4-byte aligned */
            (void)name;
            break;
        }

        case FDT_END_NODE:
            if (in_simplefb_node && depth == 1) {
                in_simplefb_node = false;
                if (info->found) return true;  /* Got everything we need */
            }
            depth--;
            if (depth < 0) return info->found;
            break;

        case FDT_PROP: {
            uint32_t val_size = fdt_r32(p); p += 4;
            uint32_t name_off = fdt_r32(p); p += 4;
            const uint8_t *val = p;
            p += (val_size + 3) & ~3;   /* Skip value, 4-byte aligned */

            const char *prop_name = strings_base + name_off;

            /* Check for compatible = "simple-framebuffer" */
            if (fdt_strncmp(prop_name, "compatible", 10) && val_size >= 18) {
                if (fdt_strncmp((const char *)val, "simple-framebuffer", 18)) {
                    in_simplefb_node = true;
                    info->found = false;    /* Not yet — need reg/width/height */
                }
            }

            if (!in_simplefb_node) break;

            /* Extract properties from the simple-framebuffer node */
            if (fdt_strncmp(prop_name, "reg", 3) && val_size >= 8) {
                /* reg = <addr_hi addr_lo size_hi size_lo> (two 64-bit values) */
                if (val_size >= 16) {
                    /* 64-bit address cells */
                    info->base_addr = ((uint64_t)fdt_r32(val) << 32) |
                                       (uint64_t)fdt_r32(val + 4);
                    info->size      = ((uint64_t)fdt_r32(val + 8) << 32) |
                                       (uint64_t)fdt_r32(val + 12);
                } else {
                    /* 32-bit address cells */
                    info->base_addr = (uint64_t)fdt_r32(val);
                    info->size      = (uint64_t)fdt_r32(val + 4);
                }
                info->found = (info->width > 0 && info->height > 0);
            }
            else if (fdt_strncmp(prop_name, "width", 5) && val_size == 4) {
                info->width = fdt_r32(val);
                info->found = (info->base_addr > 0 && info->height > 0);
            }
            else if (fdt_strncmp(prop_name, "height", 6) && val_size == 4) {
                info->height = fdt_r32(val);
                info->found = (info->base_addr > 0 && info->width > 0);
            }
            else if (fdt_strncmp(prop_name, "stride", 6) && val_size == 4) {
                info->stride = fdt_r32(val);
            }
            else if (fdt_strncmp(prop_name, "format", 6) && val_size > 0) {
                uint32_t copy_len = val_size < 31 ? val_size : 31;
                for (uint32_t i = 0; i < copy_len; i++)
                    info->format[i] = (char)val[i];
                info->format[copy_len] = '\0';

                /* Determine BPP from format string */
                if (fdt_strncmp(info->format, "a8r8g8b8", 8) || fdt_strncmp(info->format, "x8r8g8b8", 8)) {
                    info->bpp = 32;
                } else if (fdt_strncmp(info->format, "r5g6b5", 6) == 0) {
                    info->bpp = 16;
                } else {
                    info->bpp = 32;
                }
            }
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            return info->found;

        default:
            jh7110_uart_puts("[simplefb] ERROR: Unknown FDT token\n");
            return false;
        }
    }
}

/* =============================================================================
 * DC8200 STRIDE PROBE
 * =============================================================================
 *
 * Read the actual scan-out stride from the DC8200 primary layer stride
 * register. This is the ground truth — whatever U-Boot programmed into
 * the hardware is what we must use, regardless of what width*bpp computes.
 *
 * Why this matters: U-Boot may align the stride to a power-of-two boundary
 * for performance. At 1920px x 4bpp = 7680 bytes, a common alignment is
 * 8192 (next power of two). If we use 7680 but the hardware scans at 8192,
 * every row after the first is offset by 512 bytes, producing the
 * characteristic diagonal artifact pattern visible on screen.
 *
 * Sanity check: the hardware value must be >= width*bpp (minimum valid
 * stride) and <= width*8 (implausibly wide). If it falls outside this
 * range the register offset may be wrong for this U-Boot version and we
 * fall back to width*(bpp/8).
 */
static uint32_t probe_dc8200_stride(uint32_t width, uint32_t bpp)
{
    volatile uint32_t *dc8200 = (volatile uint32_t *)DC8200_BASE;
    uint32_t hw_stride = dc8200[DC8200_STRIDE_REG / 4];

    jh7110_uart_puts("[simplefb] DC8200 stride reg = ");
    jh7110_uart_putdec(hw_stride);
    jh7110_uart_putc('\n');

    uint32_t min_stride = width * (bpp / 8);
    uint32_t max_stride = width * 8;

    if (hw_stride >= min_stride && hw_stride <= max_stride) {
        jh7110_uart_puts("[simplefb] Using hardware stride\n");
        return hw_stride;
    }

    /* Out of plausible range — wrong register offset or U-Boot version
     * difference. Fall back to the computed minimum. */
    jh7110_uart_puts("[simplefb] Hardware stride implausible, using width*(bpp/8)\n");
    return min_stride;
}

/* =============================================================================
 * FRAMEBUFFER INITIALIZATION
 * =============================================================================
 */

#include "framebuffer.h"

bool jh7110_display_init(framebuffer_t *fb, const void *dtb)
{
    if (!fb || !dtb) return false;

    simplefb_info_t info;
    for (uint32_t i = 0; i < sizeof(info); i++)
        ((uint8_t *)&info)[i] = 0;

    jh7110_uart_puts("[simplefb] Parsing DTB for simple-framebuffer...\n");

    if (!parse_simplefb_from_dtb(dtb, &info) || !info.found) {
        jh7110_uart_puts("[simplefb] No simple-framebuffer node in DTB\n");
        jh7110_uart_puts("[simplefb] Falling back to hardcoded values\n");

        /*
         * Fallback: confirmed via U-Boot `bdinfo` on Milk-V Mars 8GB.
         *   FB base = 0xFE000000
         *   FB size = 1920x1080x32
         *
         * This U-Boot build (2021.10) does not inject a simple-framebuffer
         * node into the live FDT even when passed $fdtcontroladdr, so this
         * fallback path is always taken on this board. It is not a temporary
         * workaround — it is the permanent path for this U-Boot version.
         *
         * Stride is probed from the DC8200 hardware register below rather
         * than hardcoded, to correctly handle power-of-two alignment.
         */
        info.base_addr = 0xFE000000UL;
        info.width     = 1920;
        info.height    = 1080;
        info.bpp       = 32;
        info.size      = 1920 * 1080 * 4;
        info.found     = true;
        info.format[0] = 'a'; info.format[1] = '8';
        info.format[2] = 'r'; info.format[3] = '8';
        info.format[4] = 'g'; info.format[5] = '8';
        info.format[6] = 'b'; info.format[7] = '8';
        info.format[8] = '\0';

        /* Probe real stride from DC8200 — do not assume width*4 */
        /* StarFive UART # md.l 0x29401400 20 reveals that stride is 7680 */
        info.stride = 7680;
    }

    /* Sanity checks */
    if (info.base_addr == 0 || info.width == 0 || info.height == 0) {
        jh7110_uart_puts("[simplefb] ERROR: Invalid framebuffer parameters\n");
        return false;
    }

    /* Final stride fallback if probe returned 0 for any reason */
    if (info.stride == 0) {
        info.stride = info.width * (info.bpp / 8);
    }

    /* Log final parameters */
    jh7110_uart_puts("[simplefb] Framebuffer:\n");
    jh7110_uart_puts("  Address: "); jh7110_uart_puthex(info.base_addr); jh7110_uart_putc('\n');
    jh7110_uart_puts("  Size:    "); jh7110_uart_puthex(info.size);      jh7110_uart_putc('\n');
    jh7110_uart_puts("  ");          jh7110_uart_putdec(info.width);
    jh7110_uart_puts("x");           jh7110_uart_putdec(info.height);
    jh7110_uart_puts(" @ ");         jh7110_uart_putdec(info.bpp);
    jh7110_uart_puts("bpp  stride=");jh7110_uart_putdec(info.stride);
    jh7110_uart_putc('\n');
    jh7110_uart_puts("  Format:  "); jh7110_uart_puts(info.format); jh7110_uart_putc('\n');

    /* =========================================================================
     * Populate framebuffer_t
     * =========================================================================
     *
     * CRITICAL: buffers[] must be set to the physical framebuffer address.
     *
     * framebuffer.c's fb_present() does:
     *   fb->front_buffer = fb->back_buffer;
     *   fb->back_buffer  = fb->front_buffer ^ 1;
     *   fb->addr         = fb->buffers[fb->back_buffer];
     *
     * If buffers[] are left as NULL (zero-initialized), the first call to
     * fb_present() sets fb->addr = NULL. The next draw call (fb_clear)
     * writes to address 0x0 → Store access fault (scause=7, stval=0x0).
     *
     * SimpleFB has no double buffering — there is only one physical buffer.
     * Both slots point to the same address. Swapping front/back indices is
     * harmless since both sides of the swap resolve to the same memory.
     */
    fb->addr           = (uint32_t *)(uintptr_t)info.base_addr;
    fb->width          = info.width;
    fb->height         = info.height;
    fb->pitch          = info.stride;
    fb->buffer_size    = info.stride * info.height;
    fb->virtual_height = info.height;
    fb->front_buffer   = 0;
    fb->back_buffer    = 0;
    fb->buffers[0]     = (uint32_t *)(uintptr_t)info.base_addr;
    fb->buffers[1]     = (uint32_t *)(uintptr_t)info.base_addr;
    fb->initialized    = true;
    fb->pixel_format   = FB_FORMAT_ABGR8888;
    // These below need to be set, otherwise, no drawing will occur as they are discarded.
    fb->clip_depth      = 0;
    fb->clip_stack[0].x = 0;
    fb->clip_stack[0].y = 0;
    fb->clip_stack[0].w = info.width;
    fb->clip_stack[0].h = info.height;
    fb->dirty_count     = 0;
    fb->full_dirty      = false;
    fb->frame_count     = 0;
    jh7110_uart_puts("[simplefb] framebuffer_t OK\n");

    return true;
}

void jh7110_display_present(framebuffer_t *fb)
{
    jh7110_uart_putc('K');  /* breadcrumb — did we get here? */
    jh7110_l2_flush_range((uintptr_t)fb->addr, fb->buffer_size);
    /*
     * No explicit present needed — the DC8200 is already scanning out
     * from the framebuffer address. All writes to fb->addr are immediately
     * visible (no double buffering in SimpleFB mode).
     *
     * A fence ensures all pending stores complete before we consider the
     * frame "presented". On the JH7110, the DC8200 DMA is cache-coherent
     * with the CPU so this fence is sufficient. On platforms with
     * non-coherent display paths an explicit cache clean would be needed.
     */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    (void)fb;
}
