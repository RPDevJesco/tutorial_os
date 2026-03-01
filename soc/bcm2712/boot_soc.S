/*
 * soc/bcm2712/boot_soc.S - BCM2712 SoC-Specific Boot Code
 * ========================================================
 *
 * BCM2712 (Raspberry Pi 5, CM5) — Cortex-A76 cores, ARMv8.2-A
 *
 * KEY DIFFERENCES FROM BCM2711 (and why each matters):
 *
 *   1. Peripheral base at 0x107c000000 (40-bit address, was 0xFE000000)
 *      → TCR_EL1.IPS must be set to 2 (40-bit PA). Without this the MMU
 *        silently treats all OAs as 32-bit, making every peripheral mapping
 *        in this file produce garbage physical addresses.
 *
 *   2. Main peripheral L1 entry OA must be 0x1040000000, NOT 0x107c000000.
 *      → L1 block descriptors require the OA to be 1GB-aligned (bits[29:0]=0).
 *        L1[65] covers the range 0x1040000000–0x107FFFFFFF; the peripherals
 *        at 0x107c000000 sit inside that window.
 *
 *   3. GPIO, UART, and most I/O live on the RP1 south bridge chip at
 *      0x1F000D0000. That is L1[124] (OA = 0x1F00000000). This entry was
 *      completely absent from the first draft.
 *
 *   4. The low 4GB address space (L1[3]: 0xC0000000–0xFFFFFFFF) is normal
 *      RAM on BCM2712 — there are NO peripherals below 4GB. BCM2711 marked
 *      L1[3] as device memory because 0xFE000000 lives there; that is wrong
 *      for BCM2712 and would corrupt any RAM used in the top GB.
 *
 * MEMORY MAP — relevant regions:
 *
 *   0x00000000_00000000 – 0x00000000_FFFFFFFF   Low 4GB  — RAM only
 *   0x00000001_07C00000 – 0x00000001_07CFFFFF   BCM2712 main peripherals
 *   0x00000001_F000D000 – 0x00000001_FFFFFFFF   RP1 south bridge
 *
 * PAGE TABLE LAYOUT (4KB granule, T0SZ=25 → 39-bit VA, L1 base):
 *
 *   Each L1 entry spans exactly 1GB. Each L2 entry spans exactly 2MB.
 *
 *   L1[  0] → L2 table  (VA 0x0000000000–0x003FFFFFFF, 1GB, 2MB granularity)
 *   L1[  1] → 1GB block (VA 0x0040000000–0x007FFFFFFF, Normal RAM)
 *   L1[  2] → 1GB block (VA 0x0080000000–0x00BFFFFFFF, Normal RAM)
 *   L1[  3] → 1GB block (VA 0x00C0000000–0x00FFFFFFFF, Normal RAM)
 *   L1[ 65] → 1GB block (VA 0x1040000000–0x107FFFFFFF, Device — BCM2712 perifs)
 *   L1[124] → 1GB block (VA 0x1F00000000–0x1F3FFFFFFF, Device — RP1 perifs)
 *
 * DESCRIPTOR ATTRIBUTE CONSTANTS:
 *
 *   MAIR_EL1 is programmed as:  Attr0=0x00 (Device-nGnRnE), Attr1=0xFF (Normal WB)
 *
 *   Block descriptor bits used here:
 *     [1:0]   = 0b01              Block (not table, not reserved)
 *     [4:2]   = AttrIdx           0=Device, 1=Normal
 *     [9:8]   = SH                Shareability (0=none for Device, 3=inner for Normal)
 *     [10]    = AF                Access Flag; MUST be 1 — omitting it causes an
 *                                 Access Flag fault on every first access
 *     [47:30] = OA[47:30]         Physical output address, 1GB-aligned
 *
 *   Normal WB  (AttrIdx=1, SH=3, AF=1):  0x4|0x300|0x400|0x1 = 0x705
 *   Device     (AttrIdx=0, SH=0, AF=1):  0x0|0x000|0x400|0x1 = 0x401
 */

.section ".text.boot"

/* -------------------------------------------------------------------------
 * Mailbox constants
 *
 * BCM2712 mailbox base is a 40-bit address; define it so ldr x, =MBOX_BASE
 * emits a 64-bit literal-pool load rather than an instruction-encoded imm.
 * ------------------------------------------------------------------------- */
.equ MBOX_BASE,          0x107c013880   /* BCM2712 mailbox (was 0xFE00B880) */
.equ MBOX_READ,          0x00
.equ MBOX_STATUS,        0x18
.equ MBOX_WRITE,         0x20
.equ MBOX_FULL,          0x80000000
.equ MBOX_EMPTY,         0x40000000
.equ MBOX_CHANNEL_PROP,  8

.equ TAG_GET_ARM_MEMORY, 0x00010005
.equ TAG_END,            0x00000000

/* -------------------------------------------------------------------------
 * Descriptor attribute constants (see header comment for derivation)
 * ------------------------------------------------------------------------- */
.equ DESC_NORMAL, 0x705    /* Normal WB, inner shareable, AF=1, block */
.equ DESC_DEVICE, 0x401    /* Device-nGnRnE, non-shareable, AF=1, block */
.equ DESC_TABLE,  0x003    /* Next-level table descriptor */


/* =========================================================================
 * soc_early_init
 *
 * Entry point called from boot/arm64/entry.S immediately after the EL1
 * transition, before a C stack exists.
 *
 * Contract on entry:
 *   x19 = DTB pointer passed in by the GPU firmware (preserved throughout)
 *   SP  = undefined (we set it as our very first action)
 *   MMU = off, caches = off
 *
 * On return (to kernel_main via BL at the end):
 *   x0  = DTB pointer
 *   x1  = detected RAM base (physical)
 *   x2  = detected RAM size (bytes)
 * ========================================================================= */
.global soc_early_init
soc_early_init:

    /* ------------------------------------------------------------------
     * 1. Stack pointer — must be set before ANY function call (including
     *    the mailbox query below).  _stack_top is defined in linker.ld.
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
     * 3. Clear BSS — boot/arm64/common_init.S normally handles this, but
     *    we need it zeroed before build_page_tables uses the BSS-resident
     *    MMU tables below.
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
     * 4. Enable FP/SIMD — CPACR_EL1.FPEN = 0b11 grants EL1/EL0 access.
     *    Required before any code that touches floating-point registers.
     * ------------------------------------------------------------------ */
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    /* ------------------------------------------------------------------
     * 5. Detect RAM size via VideoCore mailbox.
     *    Returns: x0 = base, x1 = size (both 0 on timeout/error).
     * ------------------------------------------------------------------ */
    bl      get_arm_memory

    /* Store results in data-section variables */
    ldr     x2, =detected_ram_base
    str     x0, [x2]
    ldr     x2, =detected_ram_size
    str     x1, [x2]

    /* Fall back to a conservative 1GB if the mailbox did not answer.
     * The Pi 5 ships in 1/2/4/8 GB variants; 1GB is always safe.     */
    cbnz    x1, .Lram_detected
    mov     x1, #(1024 << 20)
    ldr     x2, =detected_ram_size
    str     x1, [x2]
.Lram_detected:

    /* Save DTB pointer for kernel_main */
    ldr     x2, =__dtb_ptr
    str     x19, [x2]

    /* ------------------------------------------------------------------
     * 6. Configure MAIR_EL1 — memory attribute index table.
     *
     *   Attr0 = 0x00 → Device-nGnRnE  (used by DESC_DEVICE entries)
     *   Attr1 = 0xFF → Normal WB-WA   (used by DESC_NORMAL entries)
     *
     * The 0xFF00 pattern sets byte[0]=0x00 and byte[1]=0xFF.
     * ------------------------------------------------------------------ */
    ldr     x0, =0x000000000000FF00
    msr     mair_el1, x0
    isb

    /* ------------------------------------------------------------------
     * 7. Configure TCR_EL1 — Translation Control Register.
     *
     *   T0SZ  [5:0]    = 25   → 39-bit VA space (512 GB). Starts at L1.
     *   IRGN0 [9:8]    = 1    → Page table walk inner WB-WA cacheable.
     *   ORGN0 [11:10]  = 1    → Page table walk outer WB-WA cacheable.
     *   SH0   [13:12]  = 3    → Inner shareable walks.
     *   TG0   [15:14]  = 0    → 4KB granule (default, no ORR needed).
     *   T1SZ  [21:16]  = 25   → Mirror of T0SZ; required even with EPD1=1
     *                           to avoid reserved-value faults.
     *   EPD1  [23]     = 1    → Disable TTBR1 walks; we only use TTBR0.
     *   IPS   [34:32]  = 2    → 40-bit intermediate physical address (1TB).
     *
     * *** IPS IS THE CRITICAL BCM2712-SPECIFIC FIELD ***
     * BCM2712 peripherals sit at 0x107c000000 (37-bit address).
     * Without IPS≥2 the MMU truncates all OAs to 32 bits, making every
     * descriptor that points above 4GB silently wrong.  IPS=2 costs
     * nothing and is safe on all Cortex-A76 devices.
     * ------------------------------------------------------------------ */
    mov     x0, #25                     /* T0SZ = 25 */
    orr     x0, x0, #(1 << 8)           /* IRGN0 = 1 */
    orr     x0, x0, #(1 << 10)          /* ORGN0 = 1 */
    orr     x0, x0, #(3 << 12)          /* SH0 = 3 (inner shareable) */
    orr     x0, x0, #(1 << 23)          /* EPD1 = 1 (disable TTBR1) */
    movk    x0, #25, lsl #16            /* T1SZ = 25 (bits [21:16]) */
    movk    x0, #2,  lsl #32            /* IPS = 2  (bits [34:32], 40-bit PA) */
    msr     tcr_el1, x0
    isb

    /* ------------------------------------------------------------------
     * 8. Build L1 and L2 page tables, then load TTBR0.
     * ------------------------------------------------------------------ */
    bl      build_page_tables

    ldr     x0, =mmu_l1_table
    msr     ttbr0_el1, x0
    isb

    /* Invalidate all TLB entries for EL1 before enabling the MMU */
    tlbi    vmalle1
    dsb     nsh
    isb

    /* ------------------------------------------------------------------
     * 9. Enable MMU.  Caches stay off for now — enabling them before the
     *    rest of the driver stack is initialised can cause coherency issues
     *    with the VC firmware.  The kernel can enable them later via
     *    SCTLR_EL1.C and SCTLR_EL1.I after hal_platform_init().
     * ------------------------------------------------------------------ */
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 0)           /* M  = 1, enable MMU */
    bic     x0, x0, #(1 << 2)           /* C  = 0, D-cache off */
    bic     x0, x0, #(1 << 12)          /* I  = 0, I-cache off */
    msr     sctlr_el1, x0
    isb

    /* ------------------------------------------------------------------
     * 10. Hand off to kernel_main.
     *
     *   x0 = DTB pointer (saved in x19 since entry.S)
     *   x1 = detected RAM base
     *   x2 = detected RAM size
     * ------------------------------------------------------------------ */
    mov     x0, x19
    ldr     x1, =detected_ram_base
    ldr     x1, [x1]
    ldr     x2, =detected_ram_size
    ldr     x2, [x2]
    bl      kernel_main

    /* If kernel_main ever returns (it should not), park this core. */
.Lpark_loop:
    wfe
    b       .Lpark_loop


/* =========================================================================
 * get_arm_memory
 *
 * Query the VideoCore mailbox for the ARM-visible RAM region.
 *
 * Returns:
 *   x0 = base address (physical, usually 0)
 *   x1 = size in bytes  (0 if the query failed/timed out)
 *
 * Clobbers: x0–x4, x19–x20 (saved/restored via the ABI frame below)
 * ========================================================================= */
get_arm_memory:
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp

    ldr     x19, =mbox_buffer

    /* Zero the buffer (5 × 8 bytes covers our 32-byte message) */
    stp     xzr, xzr, [x19, #0]
    stp     xzr, xzr, [x19, #16]
    str     xzr, [x19, #32]

    /* Build the property-tag message:
     *   [0]  buffer size = 32 bytes (total including header + tag + end)
     *   [4]  request code = 0
     *   [8]  tag = GET_ARM_MEMORY (0x00010005)
     *   [12] tag value buffer size = 8 bytes (base + size)
     *   [16] request/response indicator = 0
     *   [20] value[0] = (returned: base)
     *   [24] value[1] = (returned: size)
     *   [28] end tag = 0
     */
    mov     w0, #32
    str     w0, [x19, #0]               /* buffer size */
    /* [4] already 0 = request */
    ldr     w0, =TAG_GET_ARM_MEMORY
    str     w0, [x19, #8]               /* tag ID */
    mov     w0, #8
    str     w0, [x19, #12]              /* value buffer length */
    /* [16..28] already 0 */

    /* Ensure all writes are visible before the mailbox reads them */
    dsb     sy

    /* Bus address = physical | 0xC0000000  (L2 coherent alias, low 1GB only)
     * The mbox_buffer lives in .data which is linked just above 0x80000,
     * so the physical address is guaranteed to be < 0x40000000.           */
    mov     x0, x19
    ldr     x1, =0xC0000000
    add     x0, x0, x1

    bl      mbox_call
    cbz     x0, .Lget_mem_fail          /* 0 = timeout */

    /* Check the response code field — must have bit 31 set */
    ldr     w0, [x19, #4]
    tst     w0, #0x80000000
    b.eq    .Lget_mem_fail

    /* Extract the returned values */
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
 * Send a message to VideoCore mailbox channel 8 and wait for the reply.
 *
 * x0 = bus address of the message buffer (must be 16-byte aligned, low 1GB)
 *
 * Returns:
 *   x0 = 1 on success, 0 on timeout
 * ========================================================================= */
mbox_call:
    stp     x29, x30, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    mov     x29, sp

    mov     x19, x0                     /* save bus address */
    ldr     x20, =MBOX_BASE             /* 64-bit literal pool load */

    /* --- Write phase: wait until the WRITE register is not full --- */
    mov     x3, #0x100000              /* timeout counter */
.Lmbox_write_wait:
    sub     x3, x3, #1
    cbz     x3, .Lmbox_timeout
    ldr     w1, [x20, #MBOX_STATUS]
    tst     w1, #MBOX_FULL
    b.ne    .Lmbox_write_wait

    /* Write bus_addr | channel into the WRITE register */
    orr     x0, x19, #MBOX_CHANNEL_PROP
    str     w0, [x20, #MBOX_WRITE]

    /* --- Read phase: drain responses until we see our channel --- */
    mov     x3, #0x100000
.Lmbox_read_wait:
    sub     x3, x3, #1
    cbz     x3, .Lmbox_timeout
    ldr     w1, [x20, #MBOX_STATUS]
    tst     w1, #MBOX_EMPTY
    b.ne    .Lmbox_read_wait

    ldr     w0, [x20, #MBOX_READ]

    and     w1, w0, #0xF                /* extract channel from low 4 bits */
    cmp     w1, #MBOX_CHANNEL_PROP
    b.ne    .Lmbox_read_wait            /* not our channel — discard and retry */

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
 * Populates mmu_l1_table and mmu_l2_table (both in .bss, zeroed above).
 *
 * L1 table (512 entries × 8 bytes = 4KB):
 *   Each valid entry is either a block descriptor (1GB) or a table
 *   descriptor pointing to an L2 table.
 *
 * L2 table (512 entries × 8 bytes = 4KB):
 *   Each valid entry is a 2MB block descriptor used to map the first 1GB
 *   of RAM with 2MB granularity (so we can mark only actual RAM as Normal).
 *
 * Register allocation:
 *   x0  = L1 table base
 *   x1  = L2 table base
 *   x3  = current physical address (walks 2MB per iteration in L2 loop)
 *   x4  = detected_ram_size
 *   x5  = number of RAM-backed 2MB entries in L2 (capped at 512)
 *   x6  = loop counter
 *   x7  = descriptor value being written
 * ========================================================================= */
build_page_tables:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    ldr     x4, =detected_ram_size
    ldr     x4, [x4]

    ldr     x0, =mmu_l1_table
    ldr     x1, =mmu_l2_table

    /* -----------------------------------------------------------------
     * L1[0] — table descriptor pointing to mmu_l2_table.
     *
     * The L2 table covers VA 0x00000000–0x3FFFFFFF (first 1GB) with
     * 2MB resolution, letting us distinguish RAM from any mapped holes.
     *
     * Table descriptor bits [1:0] = 0b11.  The L2 table is in .bss
     * which is in the first 1GB of physical RAM, so its address fits
     * in 48 bits with no truncation.
     * ----------------------------------------------------------------- */
    orr     x7, x1, #DESC_TABLE         /* table descriptor = L2_base | 0x3 */
    str     x7, [x0, #(0 * 8)]

    /* -----------------------------------------------------------------
     * L1[1] — 1GB Normal block: VA/PA 0x40000000–0x7FFFFFFF
     *
     * Covers RAM banks 2–4 on 2/4/8 GB models.  No peripherals here.
     * ----------------------------------------------------------------- */
    mov     x7, #0x40000000
    movk    x7, #DESC_NORMAL, lsl #0    /* set low 16 bits to 0x0705 */
    str     x7, [x0, #(1 * 8)]

    /* -----------------------------------------------------------------
     * L1[2] — 1GB Normal block: VA/PA 0x80000000–0xBFFFFFFF
     * ----------------------------------------------------------------- */
    mov     x7, #0x80000000
    movk    x7, #DESC_NORMAL, lsl #0
    str     x7, [x0, #(2 * 8)]

    /* -----------------------------------------------------------------
     * L1[3] — 1GB Normal block: VA/PA 0xC0000000–0xFFFFFFFF
     *
     * *** BCM2712 DIFFERENCE FROM BCM2711 ***
     * On BCM2711 this GB contains the peripheral bus (0xFE000000) and
     * must be marked Device.  On BCM2712 there are NO peripherals in
     * the low 4GB — marking this as Device would corrupt RAM used by
     * the 4GB Pi 5 model for heap/stack pages above 3GB.
     * ----------------------------------------------------------------- */
    mov     x7, #0xC0000000
    movk    x7, #DESC_NORMAL, lsl #0    /* Normal RAM — NOT device! */
    str     x7, [x0, #(3 * 8)]

    /* -----------------------------------------------------------------
     * L1[65] — 1GB Device block: BCM2712 main peripherals
     *
     * Physical region: 0x107c000000 – 0x107cFFFFFF (mailbox, timer, …)
     *
     * L1 index = 0x107c000000 >> 30 = 65
     * OA (block output address) = 65 << 30 = 0x1040000000
     *
     * The OA MUST be 1GB-aligned (bits[29:0] = 0).  Using 0x107c000000
     * directly would corrupt bits[29:0] of the descriptor, producing an
     * invalid mapping.  The peripheral registers remain reachable at
     * VA 0x107c000000 because they fall within the 1GB window
     * [0x1040000000, 0x107FFFFFFF].
     * ----------------------------------------------------------------- */
    ldr     x7, =0x1040000000           /* OA = 65 << 30  (1GB-aligned!) */
    movk    x7, #DESC_DEVICE, lsl #0    /* set low 16 bits to 0x0401 */
    str     x7, [x0, #(65 * 8)]

    /* -----------------------------------------------------------------
     * L1[124] — 1GB Device block: RP1 south bridge peripherals
     *
     * Physical region: 0x1F000D0000 (GPIO, UART0, SPI, I2C, …)
     *
     * L1 index = 0x1F000D0000 >> 30 = 124
     * OA = 124 << 30 = 0x1F00000000
     *
     * This entry was completely absent from the first draft.  Without it,
     * any access to RP1 registers (GPIO output, UART TX, …) hits an
     * unmapped region and causes a Translation Fault the moment the MMU
     * is enabled.
     * ----------------------------------------------------------------- */
    ldr     x7, =0x1F00000000           /* OA = 124 << 30  (1GB-aligned!) */
    movk    x7, #DESC_DEVICE, lsl #0    /* set low 16 bits to 0x0401 */
    str     x7, [x0, #(124 * 8)]

    /* =================================================================
     * Build L2 table — 2MB blocks covering the first 1GB
     *
     * Entries 0..x5-1  → Normal RAM (AttrIdx=1, SH=inner, AF=1)
     * Entries x5..511  → Device     (safety: no other hardware here, but
     *                                prevents silent aliasing if the kernel
     *                                ever runs off the end of detected RAM)
     *
     * x5 = number of 2MB RAM blocks (RAM size >> 21), capped at 512
     * x6 = loop index (0..511)
     * x3 = physical base address (advances by 2MB per iteration)
     * ================================================================= */
    lsr     x5, x4, #21                 /* x5 = ram_size / 2MB */
    cmp     x5, #512
    b.le    .Ll2_count_ok
    mov     x5, #512
.Ll2_count_ok:

    mov     x6, #0                      /* loop index */
    mov     x3, #0                      /* PA starts at 0 */

.Lbuild_l2_loop:
    cmp     x6, #512
    b.ge    .Lbuild_l2_done

    cmp     x6, x5
    b.ge    .Ll2_device_entry

    /* Normal 2MB RAM block */
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
    /* Ensure all table writes are visible before TTBR0_EL1 is loaded */
    dsb     sy

    ldp     x29, x30, [sp], #16
    ret


/* =========================================================================
 * Accessor functions — called from C (soc_init.c) to retrieve the values
 * that were stored before the C environment was fully set up.
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
 * .data — mutable variables initialised at boot
 * ========================================================================= */
.section ".data"

/* 16-byte aligned mailbox I/O buffer (low 4 bits used for channel number) */
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

/* RAM region detected by the early mailbox query */
.balign 8
.global detected_ram_base
detected_ram_base:
    .quad 0

.balign 8
.global detected_ram_size
detected_ram_size:
    .quad 0


/* =========================================================================
 * .bss — page tables (must be 4KB aligned; zeroed by the BSS clear above)
 * ========================================================================= */
.section ".bss"

.balign 4096
.global mmu_l1_table
mmu_l1_table:
    .space 4096                         /* 512 entries × 8 bytes */

.balign 4096
.global mmu_l2_table
mmu_l2_table:
    .space 4096                         /* 512 entries × 8 bytes */


/* =========================================================================
 * External symbols referenced by this file
 * ========================================================================= */
.extern _stack_top
.extern __bss_start
.extern __bss_end
.extern kernel_main
.extern exception_vectors