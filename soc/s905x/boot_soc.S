/*
 * soc/s905x/boot_soc.S - Amlogic S905X SoC-Specific Boot Code
 * =============================================================
 *
 * Libre Computer La Potato (AML-S905X-CC), Khadas VIM1, various TV boxes
 *
 * KEY DIFFERENCES FROM BCM:
 *   - DRAM starts at 0x00000000 (not after peripherals)
 *   - No VideoCore mailbox - use DTB or fixed RAM size
 *   - U-Boot loads kernel to 0x01080000 (PXE default)
 *   - Peripheral registers scattered across CBUS/AOBUS/PERIPHS
 *
 * KEY DIFFERENCES FROM ROCKCHIP (RK3528A):
 *   - DRAM at 0x00000000 vs 0x40000000
 *   - Different peripheral base addresses (CBUS vs RK_PERIPHERAL_BASE)
 *   - UART_AO at 115200 baud (same as Pi, unlike Rock 2A's 1.5Mbaud)
 *   - Boot chain: BL1 -> BL2 -> BL30 -> BL31 -> BL33(U-Boot)
 *     vs Rockchip: Boot ROM -> SPL -> U-Boot
 *
 * SIMILARITIES TO ROCKCHIP:
 *   - Both use ARM Generic Timer (system registers, not MMIO)
 *   - Both use U-Boot with extlinux.conf
 *   - Both produce flat "Image" binary
 *   - Both are Cortex-A53 based
 *
 * MEMORY MAP:
 *   0x00000000 - ...        : DRAM
 *   0x01000000              : U-Boot runs here
 *   0x01080000              : Kernel loaded here (our entry point)
 *   0xC1100000 - 0xC11FFFFF : CBUS (EE peripherals)
 *   0xC8100000 - 0xC81FFFFF : AOBUS (always-on peripherals)
 */

.section ".text.boot"

/*
 * soc_early_init - S905X-specific initialization
 *
 * Called from entry.S after EL1 transition.
 * x19 = DTB pointer (preserved from _start)
 *
 * NOTE: On Amlogic, U-Boot passes:
 *   x0 = DTB pointer
 *   Kernel loaded at 0x01080000
 */
.global soc_early_init
soc_early_init:
    /*
     * CRITICAL: Set up stack BEFORE any function calls!
     */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* Set up exception vectors early */
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    /*
     * Clear BSS section
     */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
.Lclear_bss:
    cmp     x0, x1
    b.ge    .Lbss_done
    stp     xzr, xzr, [x0], #16
    b       .Lclear_bss
.Lbss_done:

    /* Enable FP/SIMD */
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    /*
     * Detect RAM size
     *
     * S905X doesn't have a mailbox like BCM. Options:
     *   1. Parse DTB (complex but accurate)
     *   2. Use fixed/known value
     *   3. Read Amlogic-specific registers
     *
     * For now, use a fixed value.
     * La Potato comes in 1GB and 2GB variants.
     * We default to 1GB which is safe for all.
     *
     * KEY DIFFERENCE: RAM base is 0x00000000, not 0x40000000 like RK3528A.
     * However, the first ~16MB is used by firmware/U-Boot, so usable RAM
     * effectively starts higher.
     *
     * TODO: Parse DTB /memory node for actual size
     */
    ldr     x2, =detected_ram_base
    mov     x0, #0x00000000         /* DRAM starts at 0x00000000 */
    str     x0, [x2]

    ldr     x2, =detected_ram_size
    mov     x1, #(1024 << 20)       /* Default 1GB */
    str     x1, [x2]

    /* Try to get RAM size from DTB if available */
    cbz     x19, .Lram_detected     /* No DTB? Use default */

    /* TODO: Parse DTB /memory node for actual size */
    /* For now, just use the default */

.Lram_detected:
    /* Store DTB pointer for kernel */
    ldr     x2, =__dtb_ptr
    str     x19, [x2]

    /*
     * Set up MMU
     *
     * S905X memory map is different from both BCM and RK3528A:
     *   0x00000000 - 0xBFFFFFFF : DRAM (normal memory, cacheable)
     *   0xC0000000 - 0xFFFFFFFF : Peripherals (device memory, uncacheable)
     *
     * This is simpler than Rockchip where peripherals are in the middle
     * of the address space (0x02000000-0x3FFFFFFF with DRAM at 0x40000000+).
     * Here, all peripherals are above 0xC0000000.
     */

    /* MAIR: Attr0=Device-nGnRnE(0x00), Attr1=Normal-WB(0xFF) */
    ldr     x0, =0x000000000000FF00
    msr     mair_el1, x0
    isb

    /* TCR_EL1: T0SZ=25, IRGN0=1, ORGN0=1, SH0=3 */
    mov     x0, #25
    orr     x0, x0, #(1 << 8)   /* IRGN0 */
    orr     x0, x0, #(1 << 10)  /* ORGN0 */
    orr     x0, x0, #(3 << 12)  /* SH0 */
    msr     tcr_el1, x0
    isb

    /* Build page tables */
    bl      build_page_tables

    /* Set TTBR0 */
    ldr     x0, =mmu_l1_table
    msr     ttbr0_el1, x0
    isb

    /* Invalidate TLB */
    tlbi    vmalle1
    dsb     nsh
    isb

    /* Enable MMU only (caches off for now - kernel can enable) */
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 0)   /* M = MMU enable */
    bic     x0, x0, #(1 << 2)   /* C = D-cache off */
    bic     x0, x0, #(1 << 12)  /* I = I-cache off */
    msr     sctlr_el1, x0
    isb

    /*
     * Jump to kernel with parameters:
     *   x0 = DTB pointer
     *   x1 = RAM base
     *   x2 = RAM size
     */
    mov     x0, x19
    ldr     x1, =detected_ram_base
    ldr     x1, [x1]
    ldr     x2, =detected_ram_size
    ldr     x2, [x2]
    bl      kernel_main

    /* If kernel returns, park */
.Lpark_loop:
    wfe
    b       .Lpark_loop


/* =============================================================================
 * PAGE TABLE CONSTRUCTION
 * =============================================================================
 *
 * S905X page table layout (1GB blocks, L1 only):
 *
 *   Entry 0: 0x00000000 - 0x3FFFFFFF -> Normal memory (DRAM)
 *   Entry 1: 0x40000000 - 0x7FFFFFFF -> Normal memory (DRAM, if >1GB)
 *   Entry 2: 0x80000000 - 0xBFFFFFFF -> Normal memory (DRAM, if >2GB)
 *   Entry 3: 0xC0000000 - 0xFFFFFFFF -> Device memory (ALL peripherals)
 *
 * This is cleaner than Rockchip where we needed to split peripherals
 * from DRAM within the lower address range.
 */
build_page_tables:
    stp     x29, x30, [sp, #-16]!

    /* Clear table */
    ldr     x0, =mmu_l1_table
    mov     x1, #512
    mov     x2, xzr
.Lclear_table:
    str     x2, [x0], #8
    subs    x1, x1, #1
    b.ne    .Lclear_table

    ldr     x0, =mmu_l1_table

    /*
     * Block descriptor format:
     * [1:0]   = 0b01 (block descriptor)
     * [4:2]   = AttrIndx (0=Device, 1=Normal)
     * [7:6]   = AP (0b00 = RW at EL1)
     * [9:8]   = SH (0b11 = Inner Shareable)
     * [10]    = AF (Access Flag, must be 1)
     */

    /* Entry 0: 0x00000000 - 0x3FFFFFFF = Normal memory (DRAM) */
    ldr     x1, =0x00000000
    orr     x1, x1, #(1 << 0)       /* Block descriptor */
    orr     x1, x1, #(1 << 2)       /* AttrIndx=1 (Normal) */
    orr     x1, x1, #(3 << 8)       /* SH=Inner Shareable */
    orr     x1, x1, #(1 << 10)      /* AF=1 */
    str     x1, [x0, #0]

    /* Entry 1: 0x40000000 - 0x7FFFFFFF = Normal memory */
    ldr     x1, =0x40000000
    orr     x1, x1, #(1 << 0)
    orr     x1, x1, #(1 << 2)
    orr     x1, x1, #(3 << 8)
    orr     x1, x1, #(1 << 10)
    str     x1, [x0, #8]

    /* Entry 2: 0x80000000 - 0xBFFFFFFF = Normal memory */
    ldr     x1, =0x80000000
    orr     x1, x1, #(1 << 0)
    orr     x1, x1, #(1 << 2)
    orr     x1, x1, #(3 << 8)
    orr     x1, x1, #(1 << 10)
    str     x1, [x0, #16]

    /* Entry 3: 0xC0000000 - 0xFFFFFFFF = Device memory (peripherals) */
    ldr     x1, =0xC0000000
    orr     x1, x1, #(1 << 0)       /* Block descriptor */
    /* AttrIndx=0 (Device) - bits [4:2] = 0 */
    orr     x1, x1, #(1 << 10)      /* AF=1 */
    str     x1, [x0, #24]

    ldp     x29, x30, [sp], #16
    ret


/* =============================================================================
 * DATA
 * =============================================================================
 */

.section ".data"

.global detected_ram_base
detected_ram_base:
    .quad   0x00000000

.global detected_ram_size
detected_ram_size:
    .quad   0x40000000              /* 1GB default */

.global __dtb_ptr
__dtb_ptr:
    .quad   0

/* =============================================================================
 * PAGE TABLES (must be 4KB aligned)
 * =============================================================================
 */

.section ".bss"
.balign 4096

.global mmu_l1_table
mmu_l1_table:
    .space  4096
