/* =============================================================================
 * linker.ld — RISC-V 64-bit Linker Script for Tutorial-OS on JH7110
 * =============================================================================
 *
 * Target: Milk-V Mars (StarFive JH-7110 SoC)
 *
 * This is nearly identical to soc/kyx1/linker.ld. The only meaningful
 * difference is the LOAD ADDRESS — everything else (section layout,
 * stack setup, BSS zeroing strategy) is identical between the two RISC-V
 * bare-metal environments.
 *
 * KYX1 (Orange Pi RV2):  loads at 0x11000000 (SpacemiT U-Boot target)
 * JH7110 (Milk-V Mars):  loads at 0x44000000 (StarFive U-Boot target)
 *
 * WHY 0x44000000?
 *   JH7110 DRAM starts at physical address 0x40000000.
 *   OpenSBI occupies the first part of DRAM (typically < 2MB).
 *   U-Boot sits above that. The StarFive U-Boot (and Milk-V's fork)
 *   uses 0x44000000 as the default kernel load address:
 *
 *     CONFIG_SYS_LOAD_ADDR = 0x44000000
 *
 *   This is documented in the extlinux.conf default configuration and
 *   is consistent with the VisionFive 2 which uses the same JH7110 SoC.
 *
 *   Contrast with Linux on this board, which is loaded to the same
 *   address. Our bare-metal image is treated exactly like a Linux kernel
 *   image by U-Boot (same booti command, same address, same DTB handoff).
 *
 * MEMORY LAYOUT (Milk-V Mars 8GB):
 *   0x4000_0000  DRAM start
 *   0x4000_0000  OpenSBI (M-mode, ~512KB)
 *   0x4008_0000  U-Boot (S-mode, up to ~1.5 MB)
 *   0x401F_FFFF  End of OpenSBI+U-Boot zone
 *   0x4020_0000  *** OUR KERNEL LOADS HERE *** <-- __kernel_load_addr
 *   0x4020_0000+  .text, .rodata, .data, .bss, .stack, .heap
 *   0x2_4000_0000 End of 8GB DRAM (on 8GB variant)
 *
 * No DPU reserved region like the Ky X1 — the DC8200 display controller
 * gets its framebuffer allocated by U-Boot dynamically, and U-Boot tells
 * us where it is via the DTB simple-framebuffer node. We don't need to
 * avoid a fixed address for the display buffer.
 */
ENTRY(_start)

/* =============================================================================
 * CONFIGURATION
 * =============================================================================
 */

/*
 * Where U-Boot loads our kernel image on the JH7110.
 *
 * This is the StarFive/Milk-V standard load address, set in:
 *   - StarFive U-Boot: CONFIG_SYS_LOAD_ADDR = 0x44000000
 *   - VisionFive2 extlinux.conf: kernel loaded to fdt_addr_r = 0x44000000
 *   - Milk-V Mars extlinux.conf: same address (same U-Boot fork)
 *
 * If this mismatches U-Boot's expectation, our _start will execute at the
 * wrong address and crash immediately (or worse, silently corrupt something).
 */
__kernel_load_addr = 0x44000000;

/*
 * Stack size: 64 KB
 *
 * Identical to the Ky X1 — sufficient for our single-threaded bare-metal
 * kernel with nested interrupt handling (trap frame = 264 bytes per level).
 */
__stack_size = 64K;

/*
 * RAM region definition.
 *
 * ORIGIN: Our kernel's private view of RAM starts at the load address.
 * LENGTH: We define a conservative working range. The actual physical
 *         RAM is 8GB, but we limit the linker's view to 256MB for safety.
 *         The heap's runtime limit is determined at boot from the DTB.
 *
 * NOTE: Unlike the Ky X1, there's no hard reserved region we must avoid
 * within our working range. The display framebuffer is allocated by U-Boot
 * at a high address (typically > 0x80000000) and told to us via DTB.
 */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x44000000, LENGTH = 256M
}

/* =============================================================================
 * SECTION LAYOUT
 * =============================================================================
 *
 * Identical structure to soc/kyx1/linker.ld. See that file for the full
 * pedagogical commentary on what each section is and why it's laid out
 * this way. This file keeps comments brief to avoid duplication.
 *
 * For the book, this comparison is valuable: "The JH7110 linker script
 * differs from the Ky X1 script in only ONE line — the load address.
 * Everything else — code layout, stack alignment, BSS initialization
 * strategy, debug section handling — is identical. This illustrates
 * that the differences between RISC-V bare-metal platforms are often
 * smaller than you might expect."
 */
SECTIONS
{
    . = __kernel_load_addr;

    .text : {
        __text_start = .;
        *(.text.boot)           /* entry.S _start — must be at exactly 0x44000000 */
        . = ALIGN(256);
        *(.text.vectors)        /* Trap vector table — 256-byte aligned */
        *(.text .text.*)        /* All other code */
        __text_end = .;
    } > RAM

    .rodata : ALIGN(4K) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        *(.got .got.plt)
        *(.srodata .srodata.*)  /* Small read-only data (RISC-V gp-relative) */
        __rodata_end = .;
    } > RAM

    .data : ALIGN(4K) {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > RAM

    .bss : ALIGN(4K) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(16) {
        __stack_bottom = .;
        . += __stack_size;
        _stack_top = .;
    } > RAM

    .heap (NOLOAD) : ALIGN(4K) {
        __heap_start = .;
        /*
         * __heap_end is computed at runtime from the DTB memory node.
         * We can safely use up to wherever U-Boot placed the framebuffer
         * (reported in the DTB). That's typically at 0x80000000 or higher,
         * giving us hundreds of MB of heap space.
         */
    } > RAM

    /* Debug sections — stay in ELF, not loaded to target memory */
    .debug_info     0 : { *(.debug_info) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_ranges   0 : { *(.debug_ranges) }
    .debug_loc      0 : { *(.debug_loc) }

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame*)
        *(.riscv.attributes)
    }
}

/* =============================================================================
 * SANITY CHECKS
 * =============================================================================
 */

ASSERT(__text_start == __kernel_load_addr,
       "ERROR: Boot code must start at 0x44000000! Check .text.boot placement.")

ASSERT((__bss_start & 7) == 0,
       "ERROR: BSS start must be 8-byte aligned")

ASSERT((__bss_end & 7) == 0,
       "ERROR: BSS end must be 8-byte aligned")

ASSERT((_stack_top & 15) == 0,
       "ERROR: Stack must be 16-byte aligned (RISC-V calling convention)")

ASSERT((__heap_start & 0xFFF) == 0,
       "ERROR: Heap start must be 4KB page-aligned")

/*
 * Verify we haven't grown past the end of our conservative 256MB window.
 * (The actual DRAM extends to 8GB, but staying in 256MB keeps addressing simple.)
 */
ASSERT(__heap_start < 0x50200000,
       "ERROR: Kernel + stack exceeds 256MB working region")
