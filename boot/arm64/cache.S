/*
 * boot/arm64/cache.S - ARM64 Cache Maintenance Functions
 * =======================================================
 *
 * Cache maintenance operations for ARM64.
 * These are GENERIC - the cache architecture is the same across ARM64 SoCs.
 *
 * CACHE BASICS:
 * -------------
 * ARM has separate instruction (I-cache) and data (D-cache) caches.
 * When sharing memory with DMA or GPU, we need to manage coherency:
 *
 *   - Clean: Write dirty cache lines to RAM (CPU wrote, other device reads)
 *   - Invalidate: Discard cache contents (other device wrote, CPU reads)
 *   - Flush: Clean then invalidate
 *
 * MEMORY BARRIERS:
 * ----------------
 *   - DSB (Data Synchronization Barrier): Wait for all memory ops to complete
 *   - DMB (Data Memory Barrier): Ensure ordering of memory ops
 *   - ISB (Instruction Synchronization Barrier): Flush pipeline
 */

.section ".text"

/* =============================================================================
 * Instruction Cache
 * =============================================================================
 */

/*
 * enable_icache - Enable instruction cache
 *
 * Call this after setting up page tables.
 */
.global enable_icache
enable_icache:
    /* Invalidate entire I-cache first */
    ic      iallu               /* Invalidate All to PoU */
    dsb     nsh                 /* Ensure completion */
    isb                         /* Synchronize context */

    /* Enable I-cache (SCTLR_EL1.I bit 12) */
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 12)  /* Set I bit */
    msr     sctlr_el1, x0
    isb

    ret

/*
 * disable_icache - Disable instruction cache
 */
.global disable_icache
disable_icache:
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 12)  /* Clear I bit */
    msr     sctlr_el1, x0
    isb

    /* Invalidate after disabling */
    ic      iallu
    dsb     nsh
    isb

    ret

/*
 * invalidate_icache - Invalidate entire instruction cache
 */
.global invalidate_icache
invalidate_icache:
    ic      iallu
    dsb     nsh
    isb
    ret


/* =============================================================================
 * Data Cache
 * =============================================================================
 */

/*
 * enable_dcache - Enable data cache
 *
 * Call this after setting up page tables.
 * WARNING: Memory must be properly mapped before enabling!
 */
.global enable_dcache
enable_dcache:
    /* Enable D-cache (SCTLR_EL1.C bit 2) */
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 2)   /* Set C bit */
    msr     sctlr_el1, x0
    isb

    ret

/*
 * disable_dcache - Disable data cache
 *
 * WARNING: Must clean cache before disabling to avoid data loss!
 */
.global disable_dcache
disable_dcache:
    /* Clean entire D-cache first */
    bl      clean_dcache_all

    /* Disable D-cache */
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 2)   /* Clear C bit */
    msr     sctlr_el1, x0
    isb

    /* Invalidate after disabling */
    bl      invalidate_dcache_all

    ret

/*
 * clean_dcache_range - Clean D-cache for address range
 *
 * Parameters:
 *   x0 = start address
 *   x1 = length in bytes
 *
 * Use when CPU has written data that another device (GPU, DMA) needs to read.
 */
.global clean_dcache_range
clean_dcache_range:
    /* Get cache line size from CTR_EL0 */
    mrs     x2, ctr_el0
    ubfx    x3, x2, #16, #4     /* DminLine field (log2 of line size in words) */
    mov     x2, #4
    lsl     x2, x2, x3          /* x2 = cache line size in bytes */

    /* Calculate end address */
    add     x1, x1, x0          /* x1 = end address */

    /* Align start address down to cache line boundary */
    bic     x0, x0, x2
    sub     x0, x0, x2

.Lclean_loop:
    add     x0, x0, x2          /* Move to next cache line */
    dc      cvac, x0            /* Clean by VA to PoC */
    cmp     x0, x1
    b.lo    .Lclean_loop

    dsb     sy                  /* Ensure completion */
    ret

/*
 * invalidate_dcache_range - Invalidate D-cache for address range
 *
 * Parameters:
 *   x0 = start address
 *   x1 = length in bytes
 *
 * Use when another device has written data that CPU needs to read.
 * WARNING: Any dirty cache lines in range will be LOST!
 */
.global invalidate_dcache_range
invalidate_dcache_range:
    /* Get cache line size */
    mrs     x2, ctr_el0
    ubfx    x3, x2, #16, #4
    mov     x2, #4
    lsl     x2, x2, x3

    /* Calculate end address */
    add     x1, x1, x0

    /* Align start down */
    bic     x0, x0, x2
    sub     x0, x0, x2

.Linval_loop:
    add     x0, x0, x2
    dc      ivac, x0            /* Invalidate by VA to PoC */
    cmp     x0, x1
    b.lo    .Linval_loop

    dsb     sy
    ret

/*
 * flush_dcache_range - Clean and invalidate D-cache for address range
 *
 * Parameters:
 *   x0 = start address
 *   x1 = length in bytes
 *
 * Use for bidirectional DMA buffers.
 */
.global flush_dcache_range
flush_dcache_range:
    /* Get cache line size */
    mrs     x2, ctr_el0
    ubfx    x3, x2, #16, #4
    mov     x2, #4
    lsl     x2, x2, x3

    /* Calculate end address */
    add     x1, x1, x0

    /* Align start down */
    bic     x0, x0, x2
    sub     x0, x0, x2

.Lflush_loop:
    add     x0, x0, x2
    dc      civac, x0           /* Clean and Invalidate by VA to PoC */
    cmp     x0, x1
    b.lo    .Lflush_loop

    dsb     sy
    ret


/* =============================================================================
 * Whole-Cache Operations (slower, use sparingly)
 * =============================================================================
 */

/*
 * clean_dcache_all - Clean entire D-cache by set/way
 *
 * This is SLOW. Prefer clean_dcache_range when you know the address.
 */
.global clean_dcache_all
clean_dcache_all:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    /* Get cache level ID */
    mrs     x0, clidr_el1
    and     w3, w0, #0x07000000 /* Extract LoC (Level of Coherence) */
    lsr     w3, w3, #23         /* Cache level value (1-7) */
    cbz     w3, .Lclean_all_done

    mov     w10, #0             /* Start at level 0 */

.Lclean_all_level:
    add     w2, w10, w10, lsr #1    /* Work out 3x cache level */
    lsr     w1, w0, w2              /* Extract cache type for this level */
    and     w1, w1, #7
    cmp     w1, #2                  /* No cache or only I-cache? */
    b.lt    .Lclean_all_skip

    /* Select this cache level in CSSELR_EL1 */
    msr     csselr_el1, x10
    isb

    /* Get cache geometry from CCSIDR_EL1 */
    mrs     x1, ccsidr_el1
    and     w2, w1, #7              /* Line size (log2 - 4) */
    add     w2, w2, #4              /* Line size (log2) */
    ubfx    w4, w1, #3, #10         /* Max way number */
    clz     w5, w4                  /* Way shift */
    ubfx    w7, w1, #13, #15        /* Max set number */

.Lclean_all_set:
    mov     w6, w4                  /* Way counter */

.Lclean_all_way:
    lsl     w11, w6, w5             /* Way number << way shift */
    lsl     w12, w7, w2             /* Set number << set shift */
    orr     w11, w10, w11           /* Combine level, way, set */
    orr     w11, w11, w12
    dc      csw, x11                /* Clean by set/way */
    subs    w6, w6, #1
    b.ge    .Lclean_all_way

    subs    w7, w7, #1
    b.ge    .Lclean_all_set

.Lclean_all_skip:
    add     w10, w10, #2            /* Next cache level */
    cmp     w3, w10
    b.gt    .Lclean_all_level

.Lclean_all_done:
    dsb     sy
    isb
    ldp     x29, x30, [sp], #16
    ret

/*
 * invalidate_dcache_all - Invalidate entire D-cache by set/way
 *
 * WARNING: All dirty data will be LOST!
 * Use with caution - typically only at boot before enabling caches.
 */
.global invalidate_dcache_all
invalidate_dcache_all:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    mrs     x0, clidr_el1
    and     w3, w0, #0x07000000
    lsr     w3, w3, #23
    cbz     w3, .Linval_all_done

    mov     w10, #0

.Linval_all_level:
    add     w2, w10, w10, lsr #1
    lsr     w1, w0, w2
    and     w1, w1, #7
    cmp     w1, #2
    b.lt    .Linval_all_skip

    msr     csselr_el1, x10
    isb

    mrs     x1, ccsidr_el1
    and     w2, w1, #7
    add     w2, w2, #4
    ubfx    w4, w1, #3, #10
    clz     w5, w4
    ubfx    w7, w1, #13, #15

.Linval_all_set:
    mov     w6, w4

.Linval_all_way:
    lsl     w11, w6, w5
    lsl     w12, w7, w2
    orr     w11, w10, w11
    orr     w11, w11, w12
    dc      isw, x11                /* Invalidate by set/way */
    subs    w6, w6, #1
    b.ge    .Linval_all_way

    subs    w7, w7, #1
    b.ge    .Linval_all_set

.Linval_all_skip:
    add     w10, w10, #2
    cmp     w3, w10
    b.gt    .Linval_all_level

.Linval_all_done:
    dsb     sy
    isb
    ldp     x29, x30, [sp], #16
    ret


/* =============================================================================
 * Memory Barrier Helpers (for C code)
 * =============================================================================
 */

.global dmb_sy
dmb_sy:
    dmb     sy
    ret

.global dsb_sy
dsb_sy:
    dsb     sy
    ret

.global isb_sy
isb_sy:
    isb
    ret
