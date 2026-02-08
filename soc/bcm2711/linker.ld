/*
 * soc/bcm2711/linker.ld - BCM2711 Linker Script
 * ==============================================
 *
 * Memory layout for Raspberry Pi 4, CM4, Pi 400
 *
 * BCM2711 MEMORY MAP:
 * -------------------
 *   0x00000000 - 0x00080000 : Reserved for GPU firmware
 *   0x00080000 - ???        : Kernel loads here <-- ENTRY POINT
 *   ???        - 0xFDFFFFFF : Available RAM (up to ~4GB)
 *   0xFE000000 - 0xFEFFFFFF : Peripheral registers
 *   0xFF000000 - 0xFFFFFFFF : ARM local peripherals
 *
 * NOTE: BCM2711 can have up to 8GB RAM, but only ~4GB is directly
 * addressable below the peripheral region in 32-bit bus addresses.
 */

ENTRY(_start)

/* Configuration */
__kernel_load_addr = 0x80000;
__stack_size = 64K;

/* Memory regions */
MEMORY
{
    /* RAM from 0x80000 to just before peripherals at 0xFE000000 */
    RAM (rwx) : ORIGIN = 0x80000, LENGTH = 0xFE000000 - 0x80000
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
       "ERROR: Boot code must start at 0x80000!")
ASSERT((__bss_start & 7) == 0,
       "ERROR: BSS start must be 8-byte aligned")
ASSERT((__bss_end & 7) == 0,
       "ERROR: BSS end must be 8-byte aligned")
ASSERT((_stack_top & 15) == 0,
       "ERROR: Stack must be 16-byte aligned")
ASSERT((__heap_start & 0xFFF) == 0,
       "ERROR: Heap must be 4KB page aligned")
