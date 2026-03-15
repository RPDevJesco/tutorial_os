#include <stdint.h>
#include <stddef.h>

#define JH7110_L2_CACHE_BASE    0x02010000UL
#define L2_FLUSH64_OFFSET       0x200

/*
 * jh7110_l2_flush_range — flush physical address range to DRAM
 *
 * The SiFive L2 cache controller's Flush64 register accepts a physical
 * address and writes the corresponding dirty cache line (64 bytes) back
 * to DRAM, making it visible to bus masters like the DC8200 display DMA.
 *
 * This is our substitute for Zicbom cbo.flush (not implemented on the U74
 * in JH7110) and Svpbmt non-cacheable page table entries (also absent).
 *
 * Called once after all drawing is complete, before fb_present().
 * For a 1920x1080x4 framebuffer: ~130,000 cache line flushes.
 * At bare-metal MMIO speeds this completes in well under 100ms — fine
 * for a static display. For animation you'd want double-buffering with
 * async flush; for Tutorial-OS a single static frame is sufficient.
 */
void jh7110_l2_flush_range(uintptr_t phys_addr, size_t size)
{
    jh7110_uart_putc('L');   /* confirm entry */
    volatile uint64_t *flush64 =
        (volatile uint64_t *)(0x02010000UL + 0x200);
    uintptr_t line = phys_addr & ~63UL;
    uintptr_t end  = phys_addr + size;
    while (line < end) {
        *flush64 = (uint64_t)line;
        line += 64;
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    jh7110_uart_putc('l');   /* confirm exit */
}
