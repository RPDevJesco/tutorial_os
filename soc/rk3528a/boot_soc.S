/*
 * soc/rk3528a/boot_soc.S - RK3528A SoC-Specific Boot Code
 * ========================================================
 *
 * RK3528A (Radxa Rock 2A, various TV boxes)
 *
 * KEY DIFFERENCES FROM BCM:
 *   - DRAM starts at 0x40000000 (not 0x00000000)
 *   - No VideoCore mailbox - use DTB or fixed RAM size
 *   - U-Boot typically loads kernel to high memory
 *   - Different peripheral layout
 *
 * MEMORY MAP:
 *   0x00200000 - 0x01200000 : SRAM
 *   0x02000000 - 0x3FFFFFFF : Peripherals
 *   0x40000000 - ???        : DRAM
 */

.section ".text.boot"

/*
 * soc_early_init - RK3528A-specific initialization
 *
 * Called from entry.S after EL1 transition.
 * x19 = DTB pointer (preserved from _start)
 *
 * NOTE: On Rockchip, U-Boot typically passes:
 *   x0 = DTB pointer
 *   Kernel loaded somewhere in DRAM (0x40000000+)
 */
.global soc_early_init
soc_early_init:
    /*
     * DEBUG: Blink green LED (GPIO3_C1) to show we're alive
     * GPIO3 base: 0xFF640000
     * C1 = bit 17
     */
    ldr     x10, =0xFF640000        /* GPIO3 base */
    mov     w11, #(1 << 17)         /* Bit 17 = C1 */

    /* Set GPIO3_C1 as output */
    ldr     w12, [x10, #0x04]       /* Read DDR */
    orr     w12, w12, w11
    str     w12, [x10, #0x04]       /* Write DDR */

    /* Blink 5 times */
    mov     x14, #5
.Lblink_loop:
    /* LED ON (active low through transistor, so write 0 to turn on) */
    ldr     w12, [x10, #0x00]
    bic     w12, w12, w11           /* Clear bit = LED ON */
    str     w12, [x10, #0x00]

    /* Delay */
    mov     x13, #0x800000
.Ldelay1:
    sub     x13, x13, #1
    cbnz    x13, .Ldelay1

    /* LED OFF */
    ldr     w12, [x10, #0x00]
    orr     w12, w12, w11           /* Set bit = LED OFF */
    str     w12, [x10, #0x00]

    /* Delay */
    mov     x13, #0x800000
.Ldelay2:
    sub     x13, x13, #1
    cbnz    x13, .Ldelay2

    sub     x14, x14, #1
    cbnz    x14, .Lblink_loop

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
     * RK3528A doesn't have a mailbox like BCM. Options:
     *   1. Parse DTB (complex)
     *   2. Use fixed/known value
     *   3. Probe memory (risky)
     *
     * For now, use a conservative fixed value.
     * Most RK3528A boards have 1GB, 2GB, or 4GB.
     * We default to 1GB which is safe for all.
     *
     * TODO: Parse DTB for actual memory size
     */
    ldr     x2, =detected_ram_base
    mov     x0, #0x40000000         /* DRAM starts at 0x40000000 */
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
     * RK3528A memory map is different from BCM:
     *   0x00000000 - 0x3FFFFFFF : Peripherals and SRAM (device memory)
     *   0x40000000 - ???        : DRAM (normal memory)
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

    /* Enable MMU only (caches off for now) */
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


/*
 * build_page_tables - Create identity-mapped page tables for RK3528A
 *
 * RK3528A memory map:
 *   0x00000000 - 0x3FFFFFFF : Device memory (peripherals, SRAM)
 *   0x40000000 - ???        : Normal memory (DRAM)
 */
build_page_tables:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    ldr     x4, =detected_ram_size
    ldr     x4, [x4]

    ldr     x0, =mmu_l1_table
    ldr     x1, =mmu_l2_table

    /* L1[0] = 1GB device block for 0x00000000-0x3FFFFFFF (peripherals) */
    mov     x2, #0x00000000
    movk    x2, #0x0401, lsl #0     /* Device memory */
    str     x2, [x0, #0]

    /* L1[1] -> L2 table for 0x40000000-0x7FFFFFFF (DRAM, fine-grained) */
    orr     x2, x1, #3              /* Table descriptor */
    str     x2, [x0, #8]

    /* L1[2] = 1GB block for 0x80000000-0xBFFFFFFF (more DRAM if >1GB) */
    mov     x2, #0x80000000
    movk    x2, #0x0705, lsl #0     /* Normal memory */
    str     x2, [x0, #16]

    /* L1[3] = 1GB block for 0xC0000000-0xFFFFFFFF (even more DRAM if >2GB) */
    mov     x2, #0xC0000000
    movk    x2, #0x0705, lsl #0     /* Normal memory */
    str     x2, [x0, #24]

    /* Build L2 table for second 1GB (0x40000000-0x7FFFFFFF) */
    /* This is where our kernel lives */

    /* Calculate how many 2MB blocks of RAM we have */
    lsr     x5, x4, #21             /* RAM blocks (size / 2MB) */
    cmp     x5, #512                /* Cap at 512 (1GB in L2) */
    b.le    .Lram_capped
    mov     x5, #512
.Lram_capped:

    mov     x2, #0                  /* Entry index */
    mov     x3, #0x40000000         /* Physical address starts at DRAM base */

.Lbuild_l2_loop:
    cmp     x2, #512
    b.ge    .Lbuild_l2_done

    cmp     x2, x5
    b.ge    .Lbuild_l2_unmapped

    /* Normal memory: 0x705 */
    mov     x7, x3
    movk    x7, #0x0705, lsl #0
    str     x7, [x1, x2, lsl #3]
    b       .Lbuild_l2_next

.Lbuild_l2_unmapped:
    /* Beyond RAM - mark as invalid/device */
    mov     x7, x3
    movk    x7, #0x0401, lsl #0
    str     x7, [x1, x2, lsl #3]

.Lbuild_l2_next:
    add     x3, x3, #0x200000       /* Next 2MB */
    add     x2, x2, #1
    b       .Lbuild_l2_loop

.Lbuild_l2_done:
    dsb     sy
    ldp     x29, x30, [sp], #16
    ret


/*
 * Accessor Functions
 */
.global get_detected_ram_base
get_detected_ram_base:
    ldr     x0, =detected_ram_base
    ldr     x0, [x0]
    ret

.global get_detected_ram_size
get_detected_ram_size:
    ldr     x0, =detected_ram_size
    ldr     x0, [x0]
    ret


/*
 * Data Section
 */
.section ".data"

.balign 8
.global __dtb_ptr
__dtb_ptr:
    .quad 0

.balign 8
.global detected_ram_base
detected_ram_base:
    .quad 0

.balign 8
.global detected_ram_size
detected_ram_size:
    .quad 0


/*
 * BSS Section
 */
.section ".bss"
.balign 4096

.global mmu_l1_table
mmu_l1_table:
    .space 4096

.balign 4096
.global mmu_l2_table
mmu_l2_table:
    .space 4096


/*
 * External symbols
 */
.extern _stack_top
.extern __bss_start
.extern __bss_end
.extern kernel_main
.extern exception_vectors