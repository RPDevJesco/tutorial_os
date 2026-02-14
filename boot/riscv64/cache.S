// =============================================================================
// cache.S — RISC-V 64-bit Cache Control for Tutorial-OS
// =============================================================================
//
// Cache management functions for the Ky X1 SoC (and any RISC-V with Zicbom).
// This is the RISC-V equivalent of boot/arm64/cache.S.
//
// Why cache management matters in bare-metal:
// ============================================
// When you write pixels to the framebuffer, the CPU may cache those writes.
// The display controller (DPU) reads directly from DRAM, not the CPU cache.
// If cached data hasn't been flushed to DRAM, the display shows stale pixels.
//
// This is the EXACT same problem on ARM64 — the Pi code uses `dc cvac` to
// clean cache lines after framebuffer writes. On RISC-V with Zicbom, we use
// `cbo.clean` for the same purpose.
//
// RISC-V Cache Management vs ARM64:
// ==================================
//   ARM64                    RISC-V (Zicbom)         Purpose
//   ─────                    ───────────────         ───────
//   dc cvac  (addr)          cbo.clean (rs1)         Write cache → RAM
//   dc ivac  (addr)          cbo.inval (rs1)         Discard cache line
//   dc civac (addr)          cbo.flush (rs1)         Write + discard
//   ic iallu                 fence.i                 Sync instruction cache
//   dsb sy                   fence iorw, iorw        Full memory barrier
//   dmb sy                   fence iorw, iorw        Data barrier (same on RV)
//   isb                      fence.i                 Instruction barrier
//
// The beauty of the Ky X1: it supports STANDARD Zicbom/Zicboz extensions,
// not vendor-specific CSR hacks. This means our cache code is portable to
// any RISC-V chip with these extensions — a win for the educational value
// of Tutorial-OS.
//
// Cache geometry (from device tree):
//   - L1I: 32KB per core, 64-byte line, 128 sets
//   - L1D: 32KB per core, 64-byte line, 128 sets
//   - L2:  512KB per cluster, 64-byte line, unified
//   - Cache block size for Zicbom/Zicboz: 64 bytes
//
// =============================================================================

.section ".text"

// Cache line size in bytes — from the device tree's cbom-block-size
// and cboz-block-size properties. Both are 64 bytes on the Ky X1.
.equ CACHE_LINE_SIZE,    64

// =============================================================================
// Zicbom Instructions — Encoding Reminder
// =============================================================================
// GCC/binutils may not recognize cbo.* mnemonics on older toolchains.
// If you get "unrecognized instruction" errors, use the raw encodings:
//
//   cbo.clean  (rs1)  →  .insn i 0x0F, 0x2, x0, rs1, 0x001
//   cbo.inval  (rs1)  →  .insn i 0x0F, 0x2, x0, rs1, 0x000
//   cbo.flush  (rs1)  →  .insn i 0x0F, 0x2, x0, rs1, 0x002
//   cbo.zero   (rs1)  →  .insn i 0x0F, 0x2, x0, rs1, 0x004
//
// For now we use the proper mnemonics and assume a modern toolchain.

// =============================================================================
// clean_dcache_range — Write dirty cache lines to DRAM
// =============================================================================
// Ensures cached data is visible to non-CPU bus masters (DMA, DPU, etc.).
// Use this after writing to a framebuffer, DMA buffer, or any memory that
// a hardware device will read.
//
// ARM64 equivalent: clean_dcache_range using `dc cvac` in a loop.
//
// Arguments:
//   a0 = start address (will be aligned down to cache line boundary)
//   a1 = size in bytes
//
// Clobbers: t0, t1, t2
//
.global clean_dcache_range
clean_dcache_range:
    // Calculate end address
    add     t1, a0, a1              // t1 = start + size = end

    // Align start address DOWN to cache line boundary.
    // Any address within a cache line refers to the whole line.
    // -CACHE_LINE_SIZE creates a mask: -64 = 0xFFFFFFFFFFFFFFC0
    li      t2, ~(CACHE_LINE_SIZE - 1)
    and     t0, a0, t2              // t0 = start & ~63  (aligned down)

    // Walk through cache lines and clean each one
    bgeu    t0, t1, 1f              // Skip if empty range

.Lclean_loop:
    cbo.clean (t0)                  // Write this cache line to DRAM
    addi    t0, t0, CACHE_LINE_SIZE // Advance to next line
    bltu    t0, t1, .Lclean_loop    // Loop while t0 < end

1:  fence   iorw, iorw              // Ensure all cleans complete
    ret

// =============================================================================
// invalidate_dcache_range — Discard cached data
// =============================================================================
// Discards cache lines WITHOUT writing them back to DRAM. Use this BEFORE
// reading from a buffer that a DMA device has written to — the cache may
// contain stale data from before the DMA transfer.
//
// WARNING: Any dirty data in the affected cache lines will be LOST!
// Only use when you're certain the cache contents are no longer needed
// (e.g., a DMA receive buffer that the device just filled).
//
// ARM64 equivalent: invalidate_dcache_range using `dc ivac`.
//
// Arguments:
//   a0 = start address
//   a1 = size in bytes
//
.global invalidate_dcache_range
invalidate_dcache_range:
    add     t1, a0, a1
    li      t2, ~(CACHE_LINE_SIZE - 1)
    and     t0, a0, t2

    bgeu    t0, t1, 1f

.Linval_loop:
    cbo.inval (t0)
    addi    t0, t0, CACHE_LINE_SIZE
    bltu    t0, t1, .Linval_loop

1:  fence   iorw, iorw
    ret

// =============================================================================
// flush_dcache_range — Clean + Invalidate (write back, then discard)
// =============================================================================
// Writes dirty data to DRAM AND discards the cache line. This is the
// "nuclear option" — use when you need the data in DRAM and also need
// the cache line freed (e.g., before handing a buffer to a device that
// will both read and write it).
//
// ARM64 equivalent: using `dc civac` (clean + invalidate by virtual address).
//
// Arguments:
//   a0 = start address
//   a1 = size in bytes
//
.global flush_dcache_range
flush_dcache_range:
    add     t1, a0, a1
    li      t2, ~(CACHE_LINE_SIZE - 1)
    and     t0, a0, t2

    bgeu    t0, t1, 1f

.Lflush_loop:
    cbo.flush (t0)
    addi    t0, t0, CACHE_LINE_SIZE
    bltu    t0, t1, .Lflush_loop

1:  fence   iorw, iorw
    ret

// =============================================================================
// zero_dcache_range — Zero memory using cache block zero (Zicboz)
// =============================================================================
// Zeros an entire cache line using the hardware-accelerated `cbo.zero`
// instruction. This is faster than a store loop because it doesn't need
// to read the line from DRAM first (the cache allocates a clean zero line).
//
// Great for zeroing framebuffers, BSS, or any large memory region.
// There is no direct ARM64 equivalent — ARM64 uses `dc zva` for the same
// purpose, but it's optional and not commonly used in our Pi code.
//
// IMPORTANT: Only works on aligned, full cache lines. For partial lines
// at the start/end, use regular stores. This function assumes the caller
// has ensured alignment and size is a multiple of CACHE_LINE_SIZE.
//
// Arguments:
//   a0 = start address (MUST be cache-line aligned)
//   a1 = size in bytes (MUST be multiple of CACHE_LINE_SIZE)
//
.global zero_dcache_range
zero_dcache_range:
    add     t1, a0, a1              // t1 = end address
    mv      t0, a0                  // t0 = current address

    bgeu    t0, t1, 1f

.Lzero_loop:
    cbo.zero (t0)                   // Zero this 64-byte cache line
    addi    t0, t0, CACHE_LINE_SIZE
    bltu    t0, t1, .Lzero_loop

1:  ret

// =============================================================================
// sync_icache — Synchronize instruction cache
// =============================================================================
// After modifying code in memory (self-modifying code, JIT compilation,
// or loading new code), the instruction cache may still hold the OLD
// instructions. `fence.i` ensures the I-cache sees the updated memory.
//
// ARM64 equivalent: `ic iallu` (invalidate all instruction cache) + `isb`.
// RISC-V's `fence.i` is simpler — one instruction does both.
//
// Call this after:
//   - Writing new instructions to memory
//   - Loading code from SD card or network
//   - Patching function pointers or trampolines
//
.global sync_icache
sync_icache:
    fence.i                         // Synchronize I-cache with D-cache/DRAM
    ret

// =============================================================================
// Memory Barrier Functions
// =============================================================================
// These are called from C via the HAL to ensure memory ordering.
// RISC-V's memory model is RVWMO (Weak Memory Ordering), similar to
// ARM64's weakly-ordered model. Both architectures need explicit fences
// for device I/O and inter-processor communication.
//
// ARM64 equivalents:
//   data_memory_barrier()  → dmb sy    → fence iorw, iorw
//   data_sync_barrier()    → dsb sy    → fence iorw, iorw
//   instruction_barrier()  → isb       → fence.i
//
// Note: RISC-V doesn't distinguish between dmb and dsb the way ARM64
// does. `fence iorw, iorw` covers both — it orders all I/O reads,
// I/O writes, memory reads, and memory writes before the fence with
// respect to all such operations after the fence.
//

.global data_memory_barrier
data_memory_barrier:
    fence   iorw, iorw              // Full ordering fence
    ret

.global data_sync_barrier
data_sync_barrier:
    fence   iorw, iorw              // Same as above on RISC-V
    ret

.global instruction_barrier
instruction_barrier:
    fence.i                         // I-cache sync
    ret

// =============================================================================
// Device I/O Barriers
// =============================================================================
// Lighter-weight fences specifically for MMIO device register access.
// These ensure that device register reads/writes happen in the expected
// order — critical when talking to hardware peripherals.
//
// Example: Writing a command register then reading a status register.
// Without a fence between them, the CPU might reorder the reads/writes
// and you'd read stale status.
//

// Fence before MMIO read — ensures prior writes complete first
.global io_read_barrier
io_read_barrier:
    fence   ow, ir                  // Order prior writes before input reads
    ret

// Fence after MMIO write — ensures the write lands before proceeding
.global io_write_barrier
io_write_barrier:
    fence   ow, ow                  // Order output writes with each other
    ret
