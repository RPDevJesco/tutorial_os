/*
 * boot/arm64/entry.S - ARM64 Entry Point
 * =======================================
 *
 * CRITICAL REQUIREMENTS:
 *   1. ARM64 Linux header for U-Boot compatibility (when UBOOT_BOOT defined)
 *   2. Park secondary cores IMMEDIATELY
 *   3. Handle EL3 → EL2 → EL1 transitions correctly
 *   4. Preserve DTB pointer in x19
 */

.section ".text.boot"
.global _start

#ifdef UBOOT_BOOT
/*
 * ARM64 Linux Kernel Header (64 bytes)
 * Required for U-Boot's 'booti' and extlinux to load the kernel
 *
 * Only included when UBOOT_BOOT is defined (Rockchip, etc.)
 * Pi boots directly via GPU - no header needed.
 */
_start:
    b       _real_start             /* 0x00: Branch to code */
    .word   0                       /* 0x04: Reserved */
    .quad   0                       /* 0x08: Image load offset */
    .quad   __image_size            /* 0x10: Effective image size */
    .quad   0xa                     /* 0x18: Flags (LE, 4K pages) */
    .quad   0                       /* 0x20: Reserved */
    .quad   0                       /* 0x28: Reserved */
    .quad   0                       /* 0x30: Reserved */
    .ascii  "ARM\x64"               /* 0x38: Magic */
    .word   0                       /* 0x3C: Reserved (PE-COFF) */

_real_start:
#else
/*
 * Direct boot entry (Raspberry Pi)
 * GPU loads kernel to 0x80000 and jumps directly here.
 */
_start:
#endif

    /*
     * CRITICAL: Park secondary cores FIRST!
     *
     * On multi-core systems, ALL cores start executing simultaneously.
     * If we don't park cores 1-3 immediately, they'll corrupt memory
     * by all using the same stack/BSS.
     */
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbnz    x1, .Lpark

    /* Save DTB pointer (x0 from bootloader) */
    mov     x19, x0

    /* Detect current exception level and drop to EL1 */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #3
    b.eq    .Lfrom_el3
    cmp     x0, #2
    b.eq    .Lfrom_el2
    b       .Lat_el1

/*
 * EL3 → EL2 transition
 * SCR_EL3 = 0x5b1:
 *   Bit 10 (RW)  = 1: EL2 is AArch64
 *   Bit 8 (HCE)  = 1: HVC enabled
 *   Bit 7 (SMD)  = 1: SMC disabled
 *   Bit 5 (RES1) = 1: Reserved, must be 1
 *   Bit 4 (RES1) = 1: Reserved, must be 1
 *   Bit 0 (NS)   = 1: Non-secure
 */
.Lfrom_el3:
    mov     x0, #0x5b1
    msr     scr_el3, x0
    adr     x0, .Lfrom_el2
    msr     elr_el3, x0
    mov     x0, #0x3c9          /* DAIF masked, target EL2h */
    msr     spsr_el3, x0
    eret

/*
 * EL2 → EL1 transition
 * HCR_EL2:
 *   Bit 31 (RW)   = 1: EL1 is AArch64
 *   Bit 1 (SWIO)  = 1: Set/Way Invalidation Override
 */
.Lfrom_el2:
    msr     cptr_el2, xzr       /* Don't trap FP/SIMD */
    msr     hstr_el2, xzr       /* Don't trap system registers */

    mov     x0, #(1 << 31)
    orr     x0, x0, #(1 << 1)   /* SWIO bit */
    msr     hcr_el2, x0

    adr     x0, .Lat_el1
    msr     elr_el2, x0
    mov     x0, #0x3c5          /* DAIF masked, target EL1h */
    msr     spsr_el2, x0
    eret

/*
 * Now at EL1 - jump to SoC-specific init
 * x19 = DTB pointer (preserved)
 */
.Lat_el1:
    b       soc_early_init

/*
 * Secondary core parking - cores 1, 2, 3 sleep here forever
 */
.Lpark:
    wfe
    b       .Lpark

.extern soc_early_init