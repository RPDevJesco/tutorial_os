/*
 * soc/s905x/linker.ld - Amlogic S905X (Meson GXL) Linker Script
 * ===============================================================
 *
 * Memory layout for Libre Computer La Potato and similar S905X boards
 *
 * S905X MEMORY MAP:
 * -----------------
 *   0x00000000 - ...        : DRAM (starts at 0!)
 *   0x01000000              : U-Boot TEXTBASE
 *   0x01080000              : Default kernel load (PXE/extlinux)
 *   0xC1100000 - 0xC11FFFFF : CBUS (EE-domain peripherals)
 *   0xC8100000 - 0xC81FFFFF : AOBUS (always-on domain)
 *   0xC8834000 - ...        : PERIPHS (pinmux/pad control)
 *   0xC883C000 - ...        : HIU (clocks, PLLs)
 *   0xD0100000 - ...        : VPU (display controller)
 *
 * KEY DIFFERENCE FROM ROCKCHIP:
 *   RK3528A DRAM starts at 0x40000000, but S905X DRAM starts at 0x00000000.
 *   This means our kernel loads at a much lower address.
 *
 * KEY DIFFERENCE FROM BCM:
 *   BCM kernel loads at 0x80000 (very low), but S905X U-Boot expects the
 *   kernel at 0x01080000 for extlinux/PXE boot. This is because the first
 *   ~16MB is used by U-Boot itself and firmware.
 *
 * U-Boot loads our kernel to 0x01080000 (the PXE kernel_addr_r).
 */

ENTRY(_start)

/* Configuration */
__kernel_load_addr = 0x01080000;    /* U-Boot PXE/extlinux default */
__stack_size = 64K;

/* Memory regions */
MEMORY
{
    /*
     * DRAM region for S905X
     * From 0x01080000 (after U-Boot/firmware area)
     * La Potato has 1GB or 2GB variants.
     * We use a conservative region that avoids the first 16MB.
     */
    RAM (rwx) : ORIGIN = 0x01080000, LENGTH = 0x3EF80000
}

/* Sections */
SECTIONS
{
    . = __kernel_load_addr;

    .text : {
        __text_start = .;
        *(.text.boot)
        . = ALIGN(2048);
        *(.text.vectors)
        *(.text .text.*)
        __text_end = .;
    } > RAM

    .rodata : ALIGN(4K) {
        __rodata_start = .;
        *(.rodata .rodata.*)
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
        __bss_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(16) {
        __stack_bottom = .;
        . += __stack_size;
        _stack_top = .;
    } > RAM

    .heap (NOLOAD) : ALIGN(4K) {
        __heap_start = .;
    } > RAM

    /* Debug sections */
    .debug_info     0 : { *(.debug_info) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_ranges   0 : { *(.debug_ranges) }
    .debug_loc      0 : { *(.debug_loc) }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.ARM.*)
    }
}

/* Sanity checks */
ASSERT(__text_start == __kernel_load_addr,
       "ERROR: Boot code must start at 0x01080000!")
ASSERT((__bss_start & 7) == 0,
       "ERROR: BSS start must be 8-byte aligned")
ASSERT((__bss_end & 7) == 0,
       "ERROR: BSS end must be 8-byte aligned")
ASSERT((_stack_top & 15) == 0,
       "ERROR: Stack must be 16-byte aligned")
ASSERT((__heap_start & 0xFFF) == 0,
       "ERROR: Heap must be 4KB page aligned")
