/*
 * boot/x86_64/cache.S - x86_64 Cache Maintenance Functions
 * ==========================================================
 *
 * Cache maintenance operations for x86_64.
 * These are GENERIC - the cache architecture is the same across x86_64 CPUs.
 *
 * CACHE BASICS (x86_64 vs ARM64):
 * --------------------------------
 * x86 has a UNIFIED cache hierarchy - no separate I-cache/D-cache split
 * at the programmer-visible level. The CPU internally maintains coherency
 * between instruction fetch and data access (self-modifying code works
 * after a serializing instruction like CPUID).
 *
 * This is fundamentally different from ARM64 which has separate I-cache
 * and D-cache requiring explicit IC/DC maintenance operations.
 *
 * For DMA/device coherency, x86 provides:
 *   - CLFLUSH:    Write back + invalidate a cache line (ordered, serializing)
 *   - CLFLUSHOPT: Write back + invalidate, weakly ordered (faster, needs SFENCE)
 *   - CLWB:       Write back WITHOUT invalidate (keep line clean in cache)
 *   - WBINVD:     Write back + invalidate ALL caches (very slow, privileged)
 *   - INVD:       Invalidate ALL without writeback (DANGEROUS - data loss!)
 *
 * ARM64 equivalents:
 *   DC CVAC  (Clean by VA)          → CLFLUSH / CLWB
 *   DC IVAC  (Invalidate by VA)     → CLFLUSH (x86 can't invalidate-only by VA)
 *   DC CIVAC (Clean+Invalidate)     → CLFLUSH / CLFLUSHOPT
 *   DC CSW   (Clean by Set/Way)     → WBINVD (x86 has no set/way granularity)
 *   DC ISW   (Invalidate by S/W)    → INVD (no set/way granularity)
 *
 * MEMORY BARRIERS:
 * ----------------
 *   ARM64              x86_64              Purpose
 *   -----              ------              -------
 *   DSB (Data Sync)    MFENCE              Full fence: all loads+stores complete
 *   DMB (Data Memory)  SFENCE / LFENCE     Directional ordering
 *   ISB (Instruction)  Serializing insn    Pipeline flush (CPUID, IRET, etc.)
 *
 * NOTE: x86 has strong memory ordering by default (Total Store Order).
 * MFENCE is only needed for WC/NT operations and cache maintenance.
 * Regular loads/stores are already ordered (no DMB needed for normal code).
 *
 * CACHE LINE SIZE:
 * ----------------
 * Detectable via CPUID leaf 1, EBX[15:8] (CLFLUSH line size in 8-byte units).
 * All modern x86_64 CPUs use 64-byte cache lines, but we query at runtime
 * to be correct, just as ARM64 queries CTR_EL0.
 */

.section ".text"
.code64


/* =============================================================================
 * Cache Line Flush (equivalent of ARM64 DC CIVAC range operations)
 * =============================================================================
 */

/*
 * clflush_range - Flush (write back + invalidate) cache lines for address range
 *
 * Parameters (System V ABI):
 *   RDI = start address
 *   RSI = length in bytes
 *
 * Uses CLFLUSH (available on all x86_64 CPUs).
 * This is ordered/serializing, so no MFENCE needed after.
 *
 * ARM64 equivalent: flush_dcache_range (DC CIVAC loop)
 *
 * Use for bidirectional DMA buffers or when ensuring data reaches RAM.
 */
.global clflush_range
clflush_range:
    /* Get cache line size from CPUID */
    call    .Lget_cache_line_size   /* RAX = line size */
    mov     %rax, %rcx              /* RCX = line size */

    /* Calculate end address */
    mov     %rdi, %rax              /* RAX = start */
    add     %rsi, %rax              /* RAX = end */

    /* Align start down to cache line boundary */
    dec     %rcx                    /* RCX = line_size - 1 (mask) */
    not     %rcx                    /* RCX = ~(line_size - 1) */
    and     %rcx, %rdi              /* RDI = aligned start */

    /* Restore line size for loop increment */
    not     %rcx
    inc     %rcx                    /* RCX = line size again */

.Lclflush_loop:
    cmp     %rax, %rdi
    jae     .Lclflush_done
    clflush (%rdi)                  /* Flush this cache line */
    add     %rcx, %rdi              /* Next line */
    jmp     .Lclflush_loop

.Lclflush_done:
    /* CLFLUSH is serializing, but fence for safety */
    mfence
    ret


/*
 * clflushopt_range - Optimized flush for address range
 *
 * Parameters:
 *   RDI = start address
 *   RSI = length in bytes
 *
 * Uses CLFLUSHOPT which is weakly ordered but faster than CLFLUSH.
 * Requires SFENCE after to ensure completion.
 *
 * CPUID check: leaf 7, EBX bit 23 (CLFLUSHOPT).
 * If not available, falls back to clflush_range.
 *
 * ARM64 equivalent: flush_dcache_range (same purpose, different ordering)
 */
.global clflushopt_range
clflushopt_range:
    /* Check CLFLUSHOPT support */
    push    %rbx
    mov     $7, %eax
    xor     %ecx, %ecx
    cpuid
    pop     %rbx
    test    $(1 << 23), %ebx
    jz      clflush_range           /* Fallback if not supported */

    /* Get cache line size */
    push    %rdi
    push    %rsi
    call    .Lget_cache_line_size
    pop     %rsi
    pop     %rdi
    mov     %rax, %rcx

    /* Calculate end address */
    mov     %rdi, %rax
    add     %rsi, %rax

    /* Align start down */
    push    %rcx
    dec     %rcx
    not     %rcx
    and     %rcx, %rdi
    pop     %rcx

.Lclflushopt_loop:
    cmp     %rax, %rdi
    jae     .Lclflushopt_done
    clflushopt (%rdi)               /* Weakly-ordered flush */
    add     %rcx, %rdi
    jmp     .Lclflushopt_loop

.Lclflushopt_done:
    sfence                          /* Ensure all flushes complete */
    ret


/*
 * clwb_range - Write back cache lines WITHOUT invalidating
 *
 * Parameters:
 *   RDI = start address
 *   RSI = length in bytes
 *
 * Writes dirty cache lines to memory but keeps clean copies in cache.
 * Useful for persistent memory (NVDIMM) and when you want the data
 * in RAM but still want cache hits on subsequent reads.
 *
 * CPUID check: leaf 7, EBX bit 24 (CLWB).
 * If not available, falls back to clflushopt_range (which invalidates too).
 *
 * ARM64 equivalent: clean_dcache_range (DC CVAC - clean without invalidate)
 */
.global clwb_range
clwb_range:
    /* Check CLWB support */
    push    %rbx
    mov     $7, %eax
    xor     %ecx, %ecx
    cpuid
    pop     %rbx
    test    $(1 << 24), %ebx
    jz      clflushopt_range        /* Fallback if not supported */

    /* Get cache line size */
    push    %rdi
    push    %rsi
    call    .Lget_cache_line_size
    pop     %rsi
    pop     %rdi
    mov     %rax, %rcx

    /* Calculate end address */
    mov     %rdi, %rax
    add     %rsi, %rax

    /* Align start down */
    push    %rcx
    dec     %rcx
    not     %rcx
    and     %rcx, %rdi
    pop     %rcx

.Lclwb_loop:
    cmp     %rax, %rdi
    jae     .Lclwb_done
    clwb    (%rdi)                  /* Write back, keep in cache */
    add     %rcx, %rdi
    jmp     .Lclwb_loop

.Lclwb_done:
    sfence                          /* Ensure all writebacks complete */
    ret


/* =============================================================================
 * Whole-Cache Operations (slow, use sparingly)
 * =============================================================================
 * x86 does NOT support set/way operations like ARM64. The only whole-cache
 * instructions are WBINVD (write back all + invalidate) and INVD (invalidate
 * without writeback).
 *
 * ARM64's clean_dcache_all and invalidate_dcache_all iterate through cache
 * levels and sets/ways using CLIDR/CCSIDR geometry information.
 * x86 provides no such granularity - it's all-or-nothing.
 */

/*
 * wbinvd_all - Write back and invalidate ALL caches
 *
 * This is EXTREMELY SLOW (flushes every dirty line across all cache levels).
 * It blocks until completion and is not interruptible.
 *
 * Combined equivalent of ARM64's clean_dcache_all + invalidate_dcache_all.
 * Prefer clflush_range / clflushopt_range when you know the address.
 */
.global wbinvd_all
wbinvd_all:
    wbinvd
    ret

/*
 * invd_all - Invalidate ALL caches WITHOUT writing back
 *
 * WARNING: ALL dirty data across ALL cache levels will be LOST!
 * This can corrupt memory and crash the system if any dirty lines exist.
 *
 * Only safe at very early boot before caches contain any data,
 * or when you know with certainty no dirty lines exist.
 *
 * ARM64 equivalent: invalidate_dcache_all (DC ISW loop)
 */
.global invd_all
invd_all:
    invd
    ret


/* =============================================================================
 * Cache Enable / Disable (CR0 control)
 * =============================================================================
 * x86 controls caching globally via CR0 bits:
 *   Bit 29 (NW): Not Write-Through
 *   Bit 30 (CD): Cache Disable
 *
 * ARM64 equivalents: SCTLR_EL1.C (D-cache) and SCTLR_EL1.I (I-cache)
 *
 * NOTE: x86 has a unified cache model, so there's no separate I-cache
 * enable/disable. The enable/disable functions below are provided for
 * API compatibility with the ARM64 interface.
 */

/*
 * enable_cache - Enable CPU caches
 *
 * Clears CR0.CD and CR0.NW to enable write-back caching.
 * Call this after setting up page tables with proper cache attributes.
 *
 * ARM64 equivalent: enable_icache + enable_dcache
 */
.global enable_cache
enable_cache:
    mov     %cr0, %rax
    and     $~(1 << 30), %rax      /* Clear CD (Cache Disable) */
    and     $~(1 << 29), %rax      /* Clear NW (Not Write-through) */
    mov     %rax, %cr0
    wbinvd                          /* Flush stale entries */
    ret

/*
 * disable_cache - Disable CPU caches
 *
 * WARNING: Must write back all dirty lines first to avoid data loss!
 * Sets CR0.CD to disable caching. All memory accesses go directly to RAM.
 *
 * ARM64 equivalent: disable_icache + disable_dcache
 */
.global disable_cache
disable_cache:
    wbinvd                          /* Write back all dirty lines first */
    mov     %cr0, %rax
    or      $(1 << 30), %rax       /* Set CD (Cache Disable) */
    mov     %rax, %cr0
    wbinvd                          /* Invalidate after disabling */
    ret


/* =============================================================================
 * Instruction Serialization
 * =============================================================================
 * x86 doesn't have a dedicated "invalidate I-cache" instruction because
 * the cache is unified and the CPU tracks self-modifying code automatically.
 *
 * However, after modifying code in memory, you MUST execute a serializing
 * instruction to flush the pipeline and ensure the CPU fetches the new code.
 *
 * ARM64 equivalent: IC IALLU + DSB NSH + ISB
 *
 * These are provided for API compatibility with the ARM64 interface.
 */

/*
 * serialize_instruction_stream - Ensure modified code is visible
 *
 * Use after writing new code to memory (JIT, runtime patching, etc.)
 * CPUID is used as a serializing instruction - it forces the CPU to
 * complete all prior instructions and flush the instruction pipeline.
 *
 * ARM64 equivalent: invalidate_icache (IC IALLU + DSB + ISB)
 */
.global serialize_instruction_stream
serialize_instruction_stream:
    push    %rbx                    /* CPUID clobbers EBX */
    xor     %eax, %eax
    cpuid                           /* Serializing instruction */
    pop     %rbx
    ret


/* =============================================================================
 * Memory Barrier Helpers (for C code)
 * =============================================================================
 * NOTE: x86_64 has Total Store Order (TSO) by default. Regular loads
 * and stores are already ordered (stores are never reordered with other
 * stores, loads are never reordered with other loads). These barriers
 * are only needed for:
 *   - Non-temporal (NT) stores (MOVNTI, MOVNTPS, etc.)
 *   - Write-combining (WC) memory regions
 *   - Ordering around CLFLUSHOPT / CLWB
 *   - Ensuring visibility across devices (MMIO, DMA)
 *
 * For normal memory operations, compiler barriers (__asm__ __volatile__)
 * are sufficient. These hardware fences are the big hammer.
 *
 * ARM64 equivalents:
 *   DMB SY → MFENCE (full fence)
 *   DMB ST → SFENCE (store fence - mostly for NT/WC stores)
 *   DMB LD → LFENCE (load fence - mostly for RDTSC serialization)
 *   DSB SY → MFENCE (x86 MFENCE covers both DMB and DSB semantics)
 *   ISB    → serialize_instruction_stream (see above)
 */

/*
 * mfence_barrier - Full memory fence
 *
 * Ensures all prior loads AND stores complete before any subsequent
 * loads or stores execute. The strongest barrier.
 *
 * ARM64 equivalent: DSB SY / DMB SY
 */
.global mfence_barrier
mfence_barrier:
    mfence
    ret

/*
 * sfence_barrier - Store fence
 *
 * Ensures all prior stores are globally visible before subsequent stores.
 * Required after non-temporal stores (MOVNTI) and CLFLUSHOPT/CLWB.
 *
 * In normal code with TSO, regular stores are already ordered.
 * You only need SFENCE for NT/WC stores.
 *
 * ARM64 equivalent: DMB ST (but ARM needs this for ALL stores)
 */
.global sfence_barrier
sfence_barrier:
    sfence
    ret

/*
 * lfence_barrier - Load fence
 *
 * Ensures all prior loads complete before subsequent loads execute.
 * Also serializes instruction execution (useful for RDTSC timing).
 *
 * ARM64 equivalent: DMB LD
 */
.global lfence_barrier
lfence_barrier:
    lfence
    ret


/* =============================================================================
 * Internal Helper: Get Cache Line Size
 * =============================================================================
 * Returns cache line size in bytes in RAX.
 * Uses CPUID leaf 1, EBX bits [15:8] (CLFLUSH line size in 8-byte units).
 *
 * ARM64 equivalent: reading CTR_EL0.DminLine
 */
.Lget_cache_line_size:
    push    %rbx                    /* CPUID clobbers EBX */
    push    %rcx
    push    %rdx

    mov     $1, %eax
    cpuid

    /* EBX[15:8] = CLFLUSH line size in 8-byte (quadword) units */
    mov     %ebx, %eax
    shr     $8, %eax
    and     $0xFF, %eax
    shl     $3, %eax                /* Multiply by 8 → bytes */

    /* Sanity check: if zero (shouldn't happen), default to 64 */
    test    %eax, %eax
    jnz     .Lline_size_ok
    mov     $64, %eax

.Lline_size_ok:
    pop     %rdx
    pop     %rcx
    pop     %rbx
    ret