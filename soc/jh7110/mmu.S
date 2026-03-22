// =============================================================================
// mmu.S — Minimal Sv39 Page Table Setup for JH7110 (Milk-V Mars)
// =============================================================================
//
// WHY THIS FILE EXISTS
// ====================
// The SiFive U74 has a 2MB L2 cache. Without the MMU, ALL memory accesses
// go through this cache — including writes to the DC8200 framebuffer at
// 0xFE000000. The display controller DMA reads directly from DRAM, bypassing
// the cache entirely. Result: our framebuffer writes sit in the cache and
// the display controller sees stale DRAM content → noise/artifacts on screen.
//
// The fix on Zicbom-capable cores (e.g. Ky X1 / SpacemiT X60) is to call
// cbo.flush after each framebuffer write. The U74 doesn't implement Zicbom.
//
// The correct solution for the U74 is to enable the MMU and mark the
// framebuffer region as Device (non-cacheable) in the page tables. The CPU
// will then bypass the cache for all writes to that virtual address range,
// and the display controller sees them immediately in DRAM.
//
// This is also genuinely great book content: it demonstrates WHY OS kernels
// care about memory types and page table attributes, using a real visible
// consequence (display corruption) as the motivating example.
//
// SV39 OVERVIEW
// =============
// RISC-V Sv39 uses a 3-level page table:
//
//   Virtual address (39-bit):
//   [38:30] VPN[2]  (9 bits) → index into L1 (PGD) table
//   [29:21] VPN[1]  (9 bits) → index into L2 (PMD) table
//   [20:12] VPN[0]  (9 bits) → index into L3 (PTE) table
//   [11: 0] Offset  (12 bits) → byte offset within 4KB page
//
//   Physical address (38-bit on JH7110):
//   [37:12] PPN     (26 bits) → physical page number
//   [11: 0] Offset  (12 bits)
//
// Each PTE is 8 bytes. Each table has 512 entries (512 × 8 = 4096 bytes = 1 page).
//
// HUGE PAGES (MEGAPAGES AND GIGAPAGES)
// =====================================
// We use 2MB megapages (L2 leaf entries) for the kernel and a 4KB region
// for the framebuffer to keep the table count minimal:
//
//   1 × L1 table (PGD): 4KB — covers full 512GB virtual space
//   1 × L2 table (PMD): 4KB — covers the 1GB VA region containing our kernel
//   1 × L2 table (PMD): 4KB — covers the 1GB VA region containing the FB
//   1 × L3 table (PTE): 4KB — fine-grained 4KB pages for FB region
//
// Total: 4 tables × 4KB = 16KB of page table memory.
//
// MEMORY MAP WE ESTABLISH
// ========================
//
//  Virtual Address    Physical Address   Size    Type        Purpose
//  -----------------  -----------------  ------  ----------  ---------------
//  0x40000000         0x40000000         1GB     Cached RWX  Kernel + DRAM
//  0xFE000000         0xFE000000         4KB×n   Device RW   Framebuffer
//  0x00000000         0x00000000         1GB     Device RW   Peripherals
//
// We use identity mapping (VA == PA) throughout. This is standard for
// bare-metal kernels that don't need address space isolation — the MMU
// is used purely for its memory attribute capability, not for protection.
//
// PTE FLAGS
// =========
// Bit  Name  Meaning
//  0   V     Valid
//  1   R     Readable
//  2   W     Writable
//  3   X     Executable
//  4   U     User-accessible (0 = S-mode only)
//  5   G     Global mapping
//  6   A     Accessed (must be 1 for hardware not to fault)
//  7   D     Dirty (must be 1 for writable pages not to fault)
//
// For Device (non-cacheable) memory, Svpbmt extension adds bits [62:61]:
//   00 = PMA (use Physical Memory Attributes — default, cacheable if DRAM)
//   01 = NC  (non-cacheable, idempotent)
//   10 = IO  (non-cacheable, non-idempotent — for MMIO)
//
// The U74 supports Svpbmt. We use NC (01) for the framebuffer: it's
// idempotent (reads return the last written value) unlike true MMIO.
//
// ARM64 EQUIVALENT
// ================
// On ARM64 you'd set the MAIR_EL1 register with memory attribute indices,
// then set the AttrIndx field in the PTE. The concept is identical: you're
// telling the CPU "bypass cache for this address range". RISC-V Svpbmt
// just encodes it differently (inline in the PTE vs. indirect via MAIR).
//
// =============================================================================

.section ".text"
.global jh7110_mmu_init

// =============================================================================
// Page table storage
// =============================================================================
// 16KB aligned to 4KB (page size). Must be in .data so BSS zeroing doesn't
// wipe them after we populate them. We populate them in assembly below.
//
// Layout:
//   page_table_l1       [0x0000]: PGD — 512 entries × 8 bytes
//   page_table_l2_kernel[0x1000]: PMD for kernel region (0x40000000)
//   page_table_l2_periph[0x2000]: PMD for peripheral region (0x00000000)
//   page_table_l3_fb    [0x3000]: PTE table for framebuffer (0xFE000000)

.section ".data"
.balign 4096
page_table_l1:
    .skip 4096      // L1 PGD: maps 512 × 1GB regions

.balign 4096
page_table_l2_kernel:
    .skip 4096      // L2 PMD: maps 512 × 2MB regions in kernel 1GB window

.balign 4096
page_table_l2_periph:
    .skip 4096      // L2 PMD: maps 512 × 2MB regions in peripheral 1GB window

.balign 4096
page_table_l3_fb:
    .skip 4096      // L3 PTE: maps 512 × 4KB pages for framebuffer region

// =============================================================================
// PTE flag constants
// =============================================================================

// Standard flags
.equ PTE_V,         (1 << 0)   // Valid
.equ PTE_R,         (1 << 1)   // Read
.equ PTE_W,         (1 << 2)   // Write
.equ PTE_X,         (1 << 3)   // Execute
.equ PTE_G,         (1 << 5)   // Global
.equ PTE_A,         (1 << 6)   // Accessed
.equ PTE_D,         (1 << 7)   // Dirty

// Svpbmt memory type bits [62:61] — U74 supports this extension
// These bits are in the HIGH 32 bits of the PTE, so we construct them
// as 64-bit values.
.equ PBMT_PMA,      0           // Use Physical Memory Attributes (default)
.equ PBMT_NC,       (1 << 61)   // Non-cacheable, idempotent (for framebuffer)
.equ PBMT_IO,       (2 << 61)   // Non-cacheable, non-idempotent (for MMIO)

// Common flag combinations
.equ FLAGS_KERNEL_RWX,  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
.equ FLAGS_DEVICE_RW,   (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)

// PPN shift: physical address → PTE PPN field
// PTE[53:10] = PPN = PA[55:12] (PA >> 12, then << 10 in PTE)
// Equivalently: PTE_PPN = (PA >> 12) << 10 = PA >> 2
.equ PPN_SHIFT,     2           // PA >> 2 gives PPN in correct PTE position

// =============================================================================
// jh7110_mmu_init — Set up page tables and enable the MMU
// =============================================================================
// Call this from soc_init.c BEFORE any framebuffer writes.
// After return, the MMU is active and 0xFE000000 is non-cacheable.
//
// C prototype: void jh7110_mmu_init(void);
//
.section ".text"
jh7110_mmu_init:
    // Preserve return address — we call no subroutines so ra is safe,
    // but save it anyway for clarity.
    mv      t6, ra

    // -----------------------------------------------------------------
    // Step 1: Zero all four page tables
    // -----------------------------------------------------------------
    // The .data section is not zeroed by BSS clearing. We must explicitly
    // zero the tables before populating them so stale entries don't cause
    // spurious mappings.

    la      a0, page_table_l1
    li      a1, 4096 * 4            // 4 tables × 4096 bytes
1:  sd      zero, 0(a0)
    addi    a0, a0, 8
    addi    a1, a1, -8
    bnez    a1, 1b

    // -----------------------------------------------------------------
    // Step 2: L1 PGD — install pointers to L2 tables
    // -----------------------------------------------------------------
    // The L1 table has 512 entries, each covering 1GB of virtual space.
    // VA[38:30] selects the entry. For identity mapping:
    //   0x00000000 → entry 0   (peripheral region)
    //   0x40000000 → entry 1   (kernel/DRAM region)
    //   0xFE000000 → entry 3   (framebuffer — in the 0xC0000000-0xFFFFFFFF GB)
    //
    // L1 non-leaf entries point to the next-level table.
    // Format: PTE = (PPN_of_L2_table << 10) | V

    // Entry 0: points to page_table_l2_periph (0x00000000 region)
    la      t0, page_table_l1
    la      t1, page_table_l2_periph
    srli    t1, t1, 2               // PPN = PA >> 2
    ori     t1, t1, PTE_V           // Non-leaf: only V bit set
    sd      t1, 0(t0)               // L1[0] = &l2_periph | V

    // Entry 1: points to page_table_l2_kernel (0x40000000 region)
    // VA 0x40000000: VPN[2] = 0x40000000 >> 30 = 1 → L1 index 1
    la      t1, page_table_l2_kernel
    srli    t1, t1, 2
    ori     t1, t1, PTE_V
    sd      t1, 8(t0)               // L1[1] = &l2_kernel | V (offset 8 = entry 1)

    // Entry 3: points to page_table_l2_fb (reuse periph for 0xFE region)
    // VA 0xFE000000: VPN[2] = 0xFE000000 >> 30 = 3 → L1 index 3
    // We create a new L2 for this — reuse l2_periph slot 3.
    // Actually: 0xFE000000 >> 30 = 3, so L1[3] must point to an L2 table.
    // We'll use page_table_l3_fb as an L2 for the 0xC0000000 GB region,
    // then within that L2, entry for 0xFE000000 points to page_table_l3_fb.
    //
    // Re-plan: use l2_periph for the 0xFE region's L2 as well,
    // since 0x00000000 and 0xFE000000 are in different L1 entries anyway.
    // For simplicity, map the entire 0xC0000000-0xFFFFFFFF GB with a
    // dedicated L2. We only have 4 table slots, so:
    //   l2_periph  → used for L1[0] (0x00000000 GB) AND L1[3] (0xC0000000 GB)
    //
    // This works because l2_periph serves double duty:
    //   - As the L2 for peripheral region (0x00000000)
    //   - As the L2 for the upper region (0xC0000000) where FB lives
    // Both regions need device mapping, so sharing the table is fine.
    //
    // For the FB region in the upper GB:
    //   0xFE000000: within the 0xC0000000 GB
    //   VPN[1] within that GB = (0xFE000000 - 0xC0000000) >> 21 = 0x1F000000 >> 21 = 0xF8 = 248
    //
    // We'll point L1[3] to a fresh L2 (we need a 5th table... or use a simpler approach).
    //
    // SIMPLER APPROACH: Map the entire upper 1GB (0xC0000000-0xFFFFFFFF) as
    // a single gigapage with Device/NC attributes. This covers 0xFE000000
    // without needing more tables, at the cost of making 0xC0000000-0xFDFFFFFF
    // also non-cacheable (which is fine — it's all peripheral space on JH7110).
    //
    // Gigapage leaf PTE in L1: PPN = 0xC0000000 >> 12, flags = Device RW + PBMT_NC
    li      t1, 0xC0000000
    srli    t1, t1, 2               // PPN field
    li      t2, FLAGS_DEVICE_RW
    or      t1, t1, t2
    // Add PBMT_NC (bit 61) for non-cacheable
    li      t2, (1 << 29)           // bit 61 in the 64-bit PTE = bit 29 of upper 32
                                    // We'll construct this properly:
    li      t2, 0
    li      t3, 1
    slli    t3, t3, 61              // t3 = PBMT_NC = bit 61
    or      t1, t1, t3
    sd      t1, 24(t0)              // L1[3] = gigapage for 0xC0000000, Device NC

    // -----------------------------------------------------------------
    // Step 3: L2 PMD for kernel region (0x40000000-0x7FFFFFFF)
    // -----------------------------------------------------------------
    // Map each 2MB megapage in the kernel 1GB window as Cached RWX.
    // VA 0x40000000 is in L1[1], and within that L2:
    //   VPN[1] = (0x40000000 >> 21) & 0x1FF = 0x20 = 32 → but for identity
    //   mapping within this 1GB window, index = (PA - 0x40000000) >> 21
    //   Wait: VA == PA identity mapping, so:
    //   VPN[1] for VA=0x40000000 = (0x40000000 >> 21) & 0x1FF = 32
    //   But L2 is indexed by VPN[1] directly (0-511).
    //
    // For the kernel 1GB window (L1[1], covering 0x40000000-0x7FFFFFFF):
    //   VPN[1] range = 32 to 63 (since 0x40000000/2MB = 512 entries from base,
    //   but L2 is indexed 0-511 within the 1GB window).
    //
    // Actually: VPN[1] = VA[29:21]. For VA in 0x40000000-0x7FFFFFFF:
    //   0x40000000 >> 21 = 0x200, & 0x1FF = 0 → index 0 in this L2
    //   0x40200000 >> 21 = 0x201, & 0x1FF = 1 → index 1
    //   etc.
    //
    // So the L2 for the kernel window is indexed 0-511, and index 0
    // maps VA 0x40000000 ↔ PA 0x40000000 (2MB megapage).
    //
    // We map 256 entries = 512MB starting at 0x40000000.
    // This covers our kernel (loads at 0x44000000) with plenty of headroom.

    la      t0, page_table_l2_kernel
    li      t1, 0x40000000          // Starting PA
    li      t2, 256                 // 256 megapages = 512MB
    li      t4, FLAGS_KERNEL_RWX

2:  // PTE = (PA >> 2) | flags  (leaf megapage: PPN points to 2MB-aligned PA)
    mv      t3, t1
    srli    t3, t3, 2               // PPN = PA >> 2
    or      t3, t3, t4
    sd      t3, 0(t0)
    addi    t0, t0, 8               // Next PTE slot
    li      t5, 0x200000
    add     t1, t1, t5              // PA += 2MB
    addi    t2, t2, -1
    bnez    t2, 2b

    // -----------------------------------------------------------------
    // Step 4: L2 PMD for peripheral region (0x00000000-0x3FFFFFFF)
    // -----------------------------------------------------------------
    // Map the lower 1GB as Device RW (IO peripherals: UART, GPIO, DC8200, etc.)
    // These are already non-cacheable by PMA on most SoCs, but explicitly
    // marking them as IO (PBMT_IO) in the PTE is belt-and-suspenders correct.

    la      t0, page_table_l2_periph
    li      t1, 0x00000000          // Starting PA
    li      t2, 512                 // All 512 entries = 1GB
    li      t4, FLAGS_DEVICE_RW
    li      t5, 1
    slli    t5, t5, 62              // PBMT_IO = bits [62:61] = 10 = bit 62
    or      t4, t4, t5

3:  mv      t3, t1
    srli    t3, t3, 2
    or      t3, t3, t4
    sd      t3, 0(t0)
    addi    t0, t0, 8
    li      t5, 0x200000
    add     t1, t1, t5
    addi    t2, t2, -1
    bnez    t2, 3b

    // -----------------------------------------------------------------
    // Step 5: Enable MMU
    // -----------------------------------------------------------------
    // Write satp CSR:
    //   [63:60] MODE = 8 (Sv39)
    //   [59:44] ASID = 0 (single address space)
    //   [43: 0] PPN  = physical page number of L1 table
    //
    // MODE=8 selects Sv39. This is the same value used by Linux on RISC-V.

    la      t0, page_table_l1
    srli    t0, t0, 12              // PPN = PA >> 12
    li      t1, 8
    slli    t1, t1, 60              // MODE = 8, shifted to bits [63:60]
    or      t0, t0, t1

    // Flush TLB before enabling (sfence.vma with no args = full flush)
    sfence.vma

    csrw    satp, t0                // Enable MMU!

    // Flush TLB again after enabling
    sfence.vma

    // -----------------------------------------------------------------
    // Return — MMU is now active
    // -----------------------------------------------------------------
    mv      ra, t6
    ret