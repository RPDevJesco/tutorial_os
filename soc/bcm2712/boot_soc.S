/*
 * soc/bcm2712/boot_soc.S - BCM2712 SoC-Specific Boot Code
 * ========================================================
 *
 * BCM2712 (Raspberry Pi 5, CM5)
 *
 * KEY DIFFERENCES FROM BCM2711:
 *   - Peripheral base at 0x107c000000 (was 0xFE000000)
 *   - Mailbox at 0x107c013880 (was 0xFE00B880)
 *   - GPIO/UART on RP1 south bridge (separate chip)
 *   - Cortex-A76 cores (was A72)
 */

.section ".text.boot"

/*
 * Mailbox constants - BCM2712 addresses
 */
.equ MBOX_BASE,          0x107c013880
.equ MBOX_READ,          0x00
.equ MBOX_STATUS,        0x18
.equ MBOX_WRITE,         0x20
.equ MBOX_FULL,          0x80000000
.equ MBOX_EMPTY,         0x40000000
.equ MBOX_CHANNEL_PROP,  8

.equ TAG_GET_ARM_MEMORY, 0x00010005
.equ TAG_END,            0x00000000

/*
 * soc_early_init - BCM2712-specific initialization
 *
 * Called from entry.S after EL1 transition.
 * x19 = DTB pointer (preserved from _start)
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

    /* Query RAM info via mailbox */
    bl      get_arm_memory
    /* Returns: x0 = RAM base, x1 = RAM size (0 on failure) */

    /* Store results */
    ldr     x2, =detected_ram_base
    str     x0, [x2]
    ldr     x2, =detected_ram_size
    str     x1, [x2]

    /* If mailbox failed, use conservative default (1GB) */
    cbnz    x1, .Lram_detected
    mov     x1, #(1024 << 20)       /* 1GB default */
    ldr     x2, =detected_ram_size
    str     x1, [x2]
.Lram_detected:

    /* Store DTB pointer for kernel */
    ldr     x2, =__dtb_ptr
    str     x19, [x2]

    /*
     * Set up MMU
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
 * get_arm_memory - Query ARM memory via mailbox
 */
get_arm_memory:
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp

    ldr     x19, =mbox_buffer

    /* Clear buffer */
    mov     x0, #0
    str     x0, [x19, #0]
    str     x0, [x19, #8]
    str     x0, [x19, #16]
    str     x0, [x19, #24]
    str     x0, [x19, #32]

    /* Build mailbox request */
    mov     w0, #32
    str     w0, [x19, #0]

    mov     w0, #0
    str     w0, [x19, #4]

    ldr     w0, =TAG_GET_ARM_MEMORY
    str     w0, [x19, #8]

    mov     w0, #8
    str     w0, [x19, #12]

    mov     w0, #0
    str     w0, [x19, #16]
    str     w0, [x19, #20]
    str     w0, [x19, #24]
    str     w0, [x19, #28]

    dsb     sy

    /* Convert to bus address (physical | 0xC0000000) */
    mov     x0, x19
    ldr     x1, =0xC0000000
    add     x0, x0, x1

    bl      mbox_call

    cbz     x0, .Lget_mem_fail

    ldr     w0, [x19, #4]
    tst     w0, #0x80000000
    b.eq    .Lget_mem_fail

    ldr     w0, [x19, #20]
    ldr     w1, [x19, #24]

    uxtw    x0, w0
    uxtw    x1, w1

    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret

.Lget_mem_fail:
    mov     x0, #0
    mov     x1, #0
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret


/*
 * mbox_call - Send/receive message via mailbox channel 8
 */
mbox_call:
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp

    mov     x19, x0
    ldr     x20, =MBOX_BASE

    mov     x3, #0x100000

.Lmbox_write_wait:
    sub     x3, x3, #1
    cbz     x3, .Lmbox_timeout
    ldr     w1, [x20, #MBOX_STATUS]
    tst     w1, #MBOX_FULL
    b.ne    .Lmbox_write_wait

    orr     x0, x19, #MBOX_CHANNEL_PROP
    str     w0, [x20, #MBOX_WRITE]

    mov     x3, #0x100000

.Lmbox_read_wait:
    sub     x3, x3, #1
    cbz     x3, .Lmbox_timeout
    ldr     w1, [x20, #MBOX_STATUS]
    tst     w1, #MBOX_EMPTY
    b.ne    .Lmbox_read_wait

    ldr     w0, [x20, #MBOX_READ]

    and     w1, w0, #0xF
    cmp     w1, #MBOX_CHANNEL_PROP
    b.ne    .Lmbox_read_wait

    mov     x0, #1
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret

.Lmbox_timeout:
    mov     x0, #0
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret


/*
 * build_page_tables - Create identity-mapped page tables for BCM2712
 *
 * BCM2712 memory map:
 *   0x00000000 - 0x3FFFFFFF   : RAM (first 1GB)
 *   0x40000000 - 0xFFFFFFFF   : More RAM (up to 4GB total in low range)
 *   0x107c000000 - 0x107cFFFFFF : Main peripherals
 *   0x1F000D0000 - ???        : RP1 peripherals
 *
 * For simplicity, we map:
 *   - First 1GB as normal memory (L2 table)
 *   - 1GB-4GB as normal memory (L1 blocks)
 *   - High addresses for peripherals (requires larger address space)
 *
 * NOTE: Full BCM2712 support requires 48-bit addressing for RP1 access.
 * For initial bring-up, we use 39-bit (512GB) which covers main peripherals.
 */
build_page_tables:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    ldr     x4, =detected_ram_size
    ldr     x4, [x4]

    ldr     x0, =mmu_l1_table
    ldr     x1, =mmu_l2_table

    /* L1[0] -> L2 table (covers 0x00000000 - 0x3FFFFFFF) */
    orr     x2, x1, #3
    str     x2, [x0, #0]

    /* L1[1] = 1GB block for 0x40000000-0x7FFFFFFF (RAM) */
    mov     x2, #0x40000000
    movk    x2, #0x0705, lsl #0     /* Normal memory */
    str     x2, [x0, #8]

    /* L1[2] = 1GB block for 0x80000000-0xBFFFFFFF (RAM) */
    mov     x2, #0x80000000
    movk    x2, #0x0705, lsl #0
    str     x2, [x0, #16]

    /* L1[3] = 1GB block for 0xC0000000-0xFFFFFFFF (RAM/bus) */
    mov     x2, #0xC0000000
    movk    x2, #0x0705, lsl #0
    str     x2, [x0, #24]

    /*
     * For BCM2712 peripherals at 0x107c000000, we need higher L1 entries
     * L1 index = addr >> 30
     * 0x107c000000 >> 30 = 65 (0x41)
     * Entry at offset 65 * 8 = 520
     */
    ldr     x2, =0x107c000000
    movk    x2, #0x0401, lsl #0     /* Device memory */
    str     x2, [x0, #520]

    /* Build L2 table for first 1GB (2MB blocks) */
    lsr     x5, x4, #21             /* RAM blocks */
    cmp     x5, #512                /* Cap at 512 (1GB) */
    b.le    .Lram_capped
    mov     x5, #512
.Lram_capped:

    mov     x2, #0
    mov     x3, #0

.Lbuild_l2_loop:
    cmp     x2, #512
    b.ge    .Lbuild_l2_done

    cmp     x2, x5
    b.ge    .Lbuild_l2_device

    /* Normal memory: 0x705 */
    mov     x7, x3
    movk    x7, #0x0705, lsl #0
    str     x7, [x1, x2, lsl #3]
    b       .Lbuild_l2_next

.Lbuild_l2_device:
    /* Beyond RAM - device memory */
    mov     x7, x3
    movk    x7, #0x0401, lsl #0
    str     x7, [x1, x2, lsl #3]

.Lbuild_l2_next:
    add     x3, x3, #0x200000
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

.balign 16
.global mbox_buffer
mbox_buffer:
    .space 64

.balign 8
.global mbox_debug
mbox_debug:
    .quad 0

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