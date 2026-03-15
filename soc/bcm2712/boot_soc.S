/*
 * soc/bcm2712/boot_soc.S - BCM2712 SoC-Specific Boot Code
 * =========================================================
 *
 * BCM2712 (Raspberry Pi 5, CM5)
 *
 * KEY DIFFERENCES FROM BCM2710/2711:
 *
 *   MAILBOX BASE:
 *     BCM2712 mailbox is a 40-bit address: 0x107c013880
 *     Must be loaded via ldr x, =MBOX_BASE (64-bit literal pool),
 *     not a 32-bit mov/movk sequence.
 *
 *   IPS FIELD IN TCR_EL1:
 *     BCM2712 peripherals sit at 0x107c000000 (37-bit PA).
 *     IPS must be set to 2 (40-bit PA) or the MMU silently truncates
 *     all descriptor OAs to 32 bits, making every peripheral descriptor
 *     point to the wrong address. This field does not exist in BCM2710/11.
 *
 *   EPD1 IN TCR_EL1:
 *     Set to 1 to disable TTBR1 translation walks. We only use TTBR0.
 *     T1SZ must still be set (to 25) to avoid reserved-value faults.
 *
 *   MEMORY MAP — low 4GB is all RAM (no peripherals below 4GB):
 *     0x00000000_00000000 – 0x00000000_FFFFFFFF   Low 4GB  — RAM only
 *     0x00000001_07C00000 – 0x00000001_07CFFFFF   BCM2712 main peripherals
 *     0x00000001_F000D000 – 0x00000001_FFFFFFFF   RP1 south bridge
 *
 *   L1 TABLE — all four low-4GB entries are Normal RAM:
 *     L1[ 0] → L2 table  (first 1GB at 2MB resolution)
 *     L1[ 1] → 1GB Normal RAM  (0x40000000)
 *     L1[ 2] → 1GB Normal RAM  (0x80000000)
 *     L1[ 3] → 1GB Normal RAM  (0xC0000000)  ← Device on BCM2711! Not here.
 *     L1[65] → 1GB Device      (0x1040000000, BCM2712 main peripherals)
 *     L1[124]→ 1GB Device      (0x1F00000000, RP1 south bridge)
 *
 *   BUS ADDRESS:
 *     Same as BCM2710/2711: physical | 0xC0000000 for mbox_buffer,
 *     which lives in .data below the 1GB mark.
 *
 *   FRAMEBUFFER:
 *     BCM2712 VC7 GPU firmware returns physical addresses directly.
 *     Do NOT strip 0xC0000000 alias bits — that was the BCM2711 workaround
 *     and corrupts the address on Pi 5 (display stripe artifact root cause).
 *
 * This function:
 *   1. Sets up the stack (MUST BE FIRST — no BL before this)
 *   2. Installs exception vectors
 *   3. Clears BSS (MMU tables live here — must be zeroed first)
 *   4. Enables FP/SIMD
 *   5. Queries RAM via VideoCore mailbox
 *   6. Stores all boot parameters to canonical globals
 *   7. Sets up MMU (MAIR, TCR with IPS=2, page tables, TTBR0)
 *   8. Enables MMU
 *   9. Calls kernel_main(NULL)
 *
 * SIGNATURE CHANGE (kernel_main):
 *   kernel_main no longer receives dtb_ptr/ram_base/ram_size as parameters.
 *   All three are stored to canonical globals in step 6 before the call.
 *   kernel_main reads from those globals directly.
 *   Signature is now uniform across all architectures:
 *     void kernel_main(framebuffer_t *fb)
 *   We pass NULL — kernel_main calls fb_init() which does the BCM mailbox
 *   framebuffer allocation, exactly as before.
 *
 * ALIAS LABELS (__ram_base / __ram_size):
 *   detected_ram_base and __ram_base are two names for the same .quad.
 *   detected_ram_size and __ram_size are two names for the same .quad.
 *   Existing BCM code using detected_ram_base/size is unaffected.
 *   Portable kernel code uses __ram_base/__ram_size.
 */

.section ".text.boot"

/* -------------------------------------------------------------------------
 * Mailbox constants
 *
 * BCM2712 mailbox base is a 40-bit address. Define it so that
 * `ldr x, =MBOX_BASE` emits a 64-bit literal-pool load rather than
 * a 32-bit instruction-encoded immediate (which would silently truncate).
 * ------------------------------------------------------------------------- */
.equ MBOX_BASE,          0x107c013880
.equ MBOX_READ,          0x00
.equ MBOX_STATUS,        0x18
.equ MBOX_WRITE,         0x20
.equ MBOX_FULL,          0x80000000
.equ MBOX_EMPTY,         0x40000000
.equ MBOX_CHANNEL_PROP,  8

.equ TAG_GET_ARM_MEMORY, 0x00010005
.equ TAG_END,            0x00000000

/* -------------------------------------------------------------------------
 * Descriptor attribute constants
 *
 * MAIR_EL1 programmed as: Attr0=0x00 (Device-nGnRnE), Attr1=0xFF (Normal WB)
 *
 * Block descriptor bits used here:
 *   [1:0]  = 0b01    Block (not table, not reserved)
 *   [4:2]  = AttrIdx  0=Device, 1=Normal
 *   [9:8]  = SH       0=non-shareable (Device), 3=inner-shareable (Normal)
 *   [10]   = AF       Access Flag — MUST be 1, omitting causes AF fault
 *   [47:30]= OA[47:30] Physical output address, 1GB-aligned
 *
 * Normal WB  (AttrIdx=1, SH=3, AF=1): 0x4|0x300|0x400|0x1 = 0x705
 * Device     (AttrIdx=0, SH=0, AF=1): 0x0|0x000|0x400|0x1 = 0x401
 * Table      (next-level table desc): bits[1:0] = 0b11       = 0x003
 * ------------------------------------------------------------------------- */
.equ DESC_NORMAL, 0x705
.equ DESC_DEVICE, 0x401
.equ DESC_TABLE,  0x003


/* =========================================================================
 * soc_early_init
 *
 * Called from boot/arm64/entry.S immediately after EL1 transition.
 *
 * Contract on entry:
 *   x19 = DTB pointer (preserved from _start throughout)
 *   SP  = undefined
 *   MMU = off, caches = off
 * ========================================================================= */
.global soc_early_init
soc_early_init:

    /* ------------------------------------------------------------------
     * 1. Stack pointer — MUST be first. No BL is legal before this line.
     * ------------------------------------------------------------------ */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* ------------------------------------------------------------------
     * 2. Exception vectors — install early so any fault during bring-up
     *    lands in our handler rather than spinning at a random address.
     * ------------------------------------------------------------------ */
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    /* ------------------------------------------------------------------
     * 3. Clear BSS — MMU tables (mmu_l1_table, mmu_l2_table) live in
     *    .bss and must be zeroed before build_page_tables writes them.
     *    Uses STP to write 16 bytes per iteration.
     * ------------------------------------------------------------------ */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
.Lclear_bss:
    cmp     x0, x1
    b.ge    .Lbss_done
    stp     xzr, xzr, [x0], #16
    b       .Lclear_bss
.Lbss_done:

    /* ------------------------------------------------------------------
     * 4. Enable FP/SIMD — CPACR_EL1.FPEN = 0b11
     * ------------------------------------------------------------------ */
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    /* ------------------------------------------------------------------
     * 5. Query ARM RAM region via VideoCore property mailbox.
     *    Returns: x0 = base, x1 = size (both 0 on timeout/failure).
     * ------------------------------------------------------------------ */
    bl      get_arm_memory

    /* ------------------------------------------------------------------
     * 6. Store all boot parameters to canonical globals.
     *
     *    detected_ram_base/__ram_base and detected_ram_size/__ram_size
     *    are alias pairs — two labels at the same .quad storage.
     *    Writing to either name is a write to both.
     *
     *    __dtb_ptr is already a single label (no alias needed).
     * ------------------------------------------------------------------ */
    ldr     x2, =detected_ram_base
    str     x0, [x2]                    /* __ram_base = mailbox base result */
    ldr     x2, =detected_ram_size
    str     x1, [x2]                    /* __ram_size = mailbox size result  */

    /* If mailbox failed (size==0), fall back to 4GB conservative default */
    cbnz    x1, .Lram_detected
    mov     x1, #(4096 << 20)           /* 4GB — minimum shipped Pi 5 config */
    ldr     x2, =detected_ram_size
    str     x1, [x2]
.Lram_detected:

    /* Store DTB pointer — x19 has held this since entry.S */
    ldr     x2, =__dtb_ptr
    str     x19, [x2]

    /* ------------------------------------------------------------------
     * 7. Configure MMU attributes and address space.
     *
     *    MAIR_EL1:
     *      Attr0 = 0x00 = Device-nGnRnE
     *      Attr1 = 0xFF = Normal Write-Back, Read-Write Allocate
     *
     *    TCR_EL1 (BCM2712-specific fields in addition to BCM2710/11):
     *      T0SZ  [5:0]   = 25  → 39-bit VA space (512GB)
     *      IRGN0 [9:8]   = 1   → Inner WB, RA
     *      ORGN0 [11:10] = 1   → Outer WB, RA
     *      SH0   [13:12] = 3   → Inner Shareable
     *      T1SZ  [21:16] = 25  → Mirror of T0SZ; required even with EPD1=1
     *      EPD1  [23]    = 1   → Disable TTBR1 walks (we only use TTBR0)
     *      IPS   [34:32] = 2   → 40-bit intermediate physical address (1TB)
     *
     *    *** IPS IS THE CRITICAL BCM2712-SPECIFIC FIELD ***
     *    BCM2712 peripherals live at 0x107c000000 (37-bit address).
     *    Without IPS>=2, the MMU truncates all OAs to 32 bits, making
     *    every descriptor pointing above 4GB silently wrong. IPS=2
     *    costs nothing and is safe on all Cortex-A76 devices.
     * ------------------------------------------------------------------ */
    ldr     x0, =0x000000000000FF00
    msr     mair_el1, x0
    isb

    mov     x0, #25                     /* T0SZ = 25 */
    orr     x0, x0, #(1 << 8)           /* IRGN0 = 1 */
    orr     x0, x0, #(1 << 10)          /* ORGN0 = 1 */
    orr     x0, x0, #(3 << 12)          /* SH0 = 3 (inner shareable) */
    orr     x0, x0, #(1 << 23)          /* EPD1 = 1 (disable TTBR1) */
    movk    x0, #25, lsl #16            /* T1SZ = 25 (bits [21:16]) */
    movk    x0, #2,  lsl #32            /* IPS  = 2  (bits [34:32], 40-bit PA) */
    msr     tcr_el1, x0
    isb

    /* Build L1 and L2 page tables (written into .bss, now zeroed) */
    bl      build_page_tables

    /* Load TTBR0 with physical address of L1 table */
    ldr     x0, =mmu_l1_table
    msr     ttbr0_el1, x0
    isb

    /* Invalidate all stale TLB entries before enabling the MMU */
    tlbi    vmalle1
    dsb     nsh
    isb

    /* ------------------------------------------------------------------
     * 8. Enable MMU. D-cache and I-cache stay off for now — the kernel
     *    enables them after hal_platform_init().
     * ------------------------------------------------------------------ */
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 0)           /* M = 1, enable MMU   */
    bic     x0, x0, #(1 << 2)           /* C = 0, D-cache off  */
    bic     x0, x0, #(1 << 12)          /* I = 0, I-cache off  */
    msr     sctlr_el1, x0
    isb

    /* ------------------------------------------------------------------
     * 9. Call kernel_main(NULL).
     *
     *    All boot parameters are now in globals:
     *      __dtb_ptr    — DTB address from GPU firmware
     *      __ram_base   — physical RAM base (alias of detected_ram_base)
     *      __ram_size   — total RAM bytes  (alias of detected_ram_size)
     *
     *    We pass NULL for fb. kernel_main calls fb_init() which drives
     *    the BCM framebuffer mailbox allocation, same as before.
     *
     *    NOTE — BCM2712 framebuffer bus address:
     *    The VC7 firmware returns physical addresses directly. Do NOT
     *    strip 0xC0000000 from the returned fb address. The display
     *    driver (display_bcm2712.c) handles this correctly. This note
     *    is here to prevent future "fix" regressions.
     * ------------------------------------------------------------------ */
    mov     x0, xzr                     /* fb = NULL */
    bl      kernel_main

    /* kernel_main must not return. If it does, park this core. */
.Lpark_loop:
    wfe
    b       .Lpark_loop


/* =========================================================================
 * get_arm_memory
 *
 * Query the VideoCore property mailbox for the ARM-visible RAM region.
 *
 * Returns:
 *   x0 = base address (physical, usually 0x00000000)
 *   x1 = size in bytes (0 if query failed or timed out)
 *
 * Clobbers: x0-x4, x19-x20 (saved/restored via ABI frame)
 * ========================================================================= */
get_arm_memory:
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp

    ldr     x19, =mbox_buffer

    /* Zero the buffer — 5×8 bytes covers our 32-byte message */
    stp     xzr, xzr, [x19, #0]
    stp     xzr, xzr, [x19, #16]
    str     xzr, [x19, #32]

    /*
     * Property message layout (32 bytes total):
     *   [0]   u32  buffer_size     = 32
     *   [4]   u32  request_code   = 0
     *   [8]   u32  tag_id         = TAG_GET_ARM_MEMORY (0x00010005)
     *   [12]  u32  value_buf_len  = 8
     *   [16]  u32  req/resp flags = 0
     *   [20]  u32  value[0]       → returned: base
     *   [24]  u32  value[1]       → returned: size
     *   [28]  u32  end_tag        = 0
     */
    mov     w0, #32
    str     w0, [x19, #0]               /* buffer_size */
    ldr     w0, =TAG_GET_ARM_MEMORY
    str     w0, [x19, #8]               /* tag_id */
    mov     w0, #8
    str     w0, [x19, #12]              /* value_buf_len */

    dsb     sy                          /* all writes visible before mailbox read */

    /*
     * BCM2712 bus address = physical | 0xC0000000 (L2 coherent alias).
     * mbox_buffer is in .data, linked just above 0x80000 — well within
     * the low 1GB, so the physical address fits the alias scheme.
     * (This alias is for the mailbox protocol only — NOT for the
     * framebuffer address returned by a separate allocate-fb call.)
     */
    mov     x0, x19
    ldr     x1, =0xC0000000
    add     x0, x0, x1

    bl      mbox_call
    cbz     x0, .Lget_mem_fail

    /* Validate response: bit 31 of [4] must be set */
    ldr     w0, [x19, #4]
    tst     w0, #0x80000000
    b.eq    .Lget_mem_fail

    /* Extract results — returned as 32-bit values, zero-extend to 64-bit */
    ldr     w0, [x19, #20]              /* base */
    ldr     w1, [x19, #24]              /* size */
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


/* =========================================================================
 * mbox_call
 *
 * Write a message to VideoCore mailbox channel 8 and wait for the reply.
 *
 * x0 = bus address of the message buffer (16-byte aligned, low 1GB)
 *
 * Returns:
 *   x0 = 1 on success, 0 on timeout
 *
 * NOTE: MBOX_BASE is a 40-bit address on BCM2712. The `ldr x20, =MBOX_BASE`
 * below emits a literal-pool load, which is correct. Do not replace it with
 * a mov/movk sequence — those are limited to 16-bit immediate fields.
 * ========================================================================= */
mbox_call:
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp

    mov     x19, x0                     /* save bus address */
    ldr     x20, =MBOX_BASE             /* 64-bit literal pool load */

    /* Wait until WRITE register is not full */
    mov     x3, #0x100000
.Lmbox_write_wait:
    sub     x3, x3, #1
    cbz     x3, .Lmbox_timeout
    ldr     w1, [x20, #MBOX_STATUS]
    tst     w1, #MBOX_FULL
    b.ne    .Lmbox_write_wait

    /* Write: bus_address | channel */
    orr     x0, x19, #MBOX_CHANNEL_PROP
    str     w0, [x20, #MBOX_WRITE]

    /* Wait for a response on our channel */
    mov     x3, #0x100000
.Lmbox_read_wait:
    sub     x3, x3, #1
    cbz     x3, .Lmbox_timeout
    ldr     w1, [x20, #MBOX_STATUS]
    tst     w1, #MBOX_EMPTY
    b.ne    .Lmbox_read_wait

    ldr     w0, [x20, #MBOX_READ]
    and     w1, w0, #0xF                /* extract channel from low nibble */
    cmp     w1, #MBOX_CHANNEL_PROP
    b.ne    .Lmbox_read_wait            /* not ours — drain and retry */

    mov     x0, #1                      /* success */
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret

.Lmbox_timeout:
    mov     x0, #0
    ldp     x19, x20, [sp], #16
    ldp     x29, x30, [sp], #16
    ret


/* =========================================================================
 * build_page_tables
 *
 * Populates mmu_l1_table and mmu_l2_table (in .bss, zeroed above).
 *
 * L1 table (512 entries × 8 bytes, each covers 1GB):
 *   L1[  0] → L2 table  (first 1GB at 2MB granularity)
 *   L1[  1] → 1GB Normal RAM  (0x40000000–0x7FFFFFFF)
 *   L1[  2] → 1GB Normal RAM  (0x80000000–0xBFFFFFFF)
 *   L1[  3] → 1GB Normal RAM  (0xC0000000–0xFFFFFFFF)
 *              *** NOT Device — unlike BCM2711! No peripherals in low 4GB. ***
 *   L1[ 65] → 1GB Device      (0x1040000000, BCM2712 main peripherals)
 *   L1[124] → 1GB Device      (0x1F00000000, RP1 south bridge / GPIO)
 *
 * L2 table (512 entries × 8 bytes, each covers 2MB):
 *   Entries 0..(ram_blocks-1) → Normal WB  (0x705)
 *   Entries ram_blocks..511   → Device      (0x401, safety for unmapped space)
 *
 * Register allocation:
 *   x0 = L1 table base
 *   x1 = L2 table base
 *   x3 = physical address (walks 2MB per L2 iteration)
 *   x4 = detected_ram_size
 *   x5 = RAM-backed 2MB entries in L2 (ram_size >> 21, capped at 512)
 *   x6 = L2 loop index (0..511)
 *   x7 = descriptor being written
 * ========================================================================= */
build_page_tables:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    ldr     x4, =detected_ram_size
    ldr     x4, [x4]

    ldr     x0, =mmu_l1_table
    ldr     x1, =mmu_l2_table

    /* L1[0] → L2 table descriptor (first 1GB) */
    orr     x7, x1, #DESC_TABLE
    str     x7, [x0, #(0 * 8)]

    /* L1[1] → 1GB Normal RAM: 0x40000000–0x7FFFFFFF */
    mov     x7, #0x40000000
    movk    x7, #DESC_NORMAL, lsl #0
    str     x7, [x0, #(1 * 8)]

    /* L1[2] → 1GB Normal RAM: 0x80000000–0xBFFFFFFF */
    mov     x7, #0x80000000
    movk    x7, #DESC_NORMAL, lsl #0
    str     x7, [x0, #(2 * 8)]

    /* L1[3] → 1GB Normal RAM: 0xC0000000–0xFFFFFFFF
     *
     * BCM2712 DIFFERENCE: No peripherals in the low 4GB.
     * On BCM2711, 0xFE000000 (peripheral bus) lives in this GB and
     * forces this entry to be Device. On BCM2712 that bus moved above
     * 4GB — marking this Device would corrupt RAM on 4GB/8GB Pi 5. */
    mov     x7, #0xC0000000
    movk    x7, #DESC_NORMAL, lsl #0
    str     x7, [x0, #(3 * 8)]

    /* L1[65] → 1GB Device: 0x1040000000–0x107FFFFFFF (BCM2712 peripherals)
     *
     * L1 index = VA >> 30 = 0x1040000000 >> 30 = 65 */
    ldr     x7, =0x1040000000
    movk    x7, #DESC_DEVICE, lsl #0
    str     x7, [x0, #(65 * 8)]

    /* L1[124] → 1GB Device: 0x1F00000000–0x1F3FFFFFFF (RP1 south bridge)
     *
     * RP1 hosts all GPIO, I2C, SPI, UART on Pi 5. Without this entry,
     * every GPIO access after MMU enable causes a Translation Fault.
     * L1 index = 0x1F00000000 >> 30 = 124 */
    ldr     x7, =0x1F00000000
    movk    x7, #DESC_DEVICE, lsl #0
    str     x7, [x0, #(124 * 8)]

    /* Build L2: 512 entries covering 0x00000000–0x3FFFFFFF at 2MB resolution */
    lsr     x5, x4, #21               /* RAM entries = ram_size / 2MB */
    cmp     x5, #512
    b.le    .Ll2_count_ok
    mov     x5, #512
.Ll2_count_ok:

    mov     x6, #0                     /* loop index */
    mov     x3, #0                     /* physical address */

.Lbuild_l2_loop:
    cmp     x6, #512
    b.ge    .Lbuild_l2_done

    cmp     x6, x5
    b.ge    .Ll2_device_entry

    /* Normal Write-Back 2MB block */
    mov     x7, x3
    movk    x7, #DESC_NORMAL, lsl #0
    b       .Ll2_write

.Ll2_device_entry:
    /* Device mapping for address space beyond detected RAM */
    mov     x7, x3
    movk    x7, #DESC_DEVICE, lsl #0

.Ll2_write:
    str     x7, [x1, x6, lsl #3]       /* L2[x6] = x7 */
    add     x3, x3, #0x200000           /* advance PA by 2MB */
    add     x6, x6, #1
    b       .Lbuild_l2_loop

.Lbuild_l2_done:
    dsb     sy                          /* table writes visible before TTBR0 load */
    ldp     x29, x30, [sp], #16
    ret


/* =========================================================================
 * Accessor functions (BCM-specific, used by soc_init.c)
 * ========================================================================= */
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


/* =========================================================================
 * .data — boot parameters written during soc_early_init
 *
 * ALIAS PAIRS:
 *   detected_ram_base / __ram_base  → same .quad storage
 *   detected_ram_size / __ram_size  → same .quad storage
 *
 *   BCM-specific code (soc_init.c, build_page_tables) uses the
 *   detected_ram_* names. Portable kernel code (kernel/main.c) uses
 *   the __ram_* names. Both resolve to the same physical address.
 * ========================================================================= */
.section ".data"

/* 16-byte aligned mailbox I/O buffer (low 4 bits carry channel number) */
.balign 16
.global mbox_buffer
mbox_buffer:
    .space 64

/* Scratch word for post-mortem mailbox debugging (readable via JTAG) */
.balign 8
.global mbox_debug
mbox_debug:
    .quad 0

/* DTB pointer forwarded from GPU firmware to kernel_main */
.balign 8
.global __dtb_ptr
__dtb_ptr:
    .quad 0

.balign 8
.global detected_ram_base
.global __ram_base
detected_ram_base:
__ram_base:
    .quad 0

.balign 8
.global detected_ram_size
.global __ram_size
detected_ram_size:
__ram_size:
    .quad 0


/* =========================================================================
 * .bss — MMU page tables (4KB aligned, zeroed by BSS clear above)
 * ========================================================================= */
.section ".bss"

.balign 4096
.global mmu_l1_table
mmu_l1_table:
    .space 4096

.balign 4096
.global mmu_l2_table
mmu_l2_table:
    .space 4096


/* =========================================================================
 * External symbols
 * ========================================================================= */
.extern _stack_top
.extern __bss_start
.extern __bss_end
.extern kernel_main
.extern exception_vectors