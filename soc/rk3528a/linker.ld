/*
 * soc/rk3528a/linker.ld - RK3528A Linker Script
 * ==============================================
 *
 * Memory layout for Radxa Rock 2A and similar RK3528A boards
 *
 * RK3528A MEMORY MAP:
 * -------------------
 *   0x00200000 - 0x01200000 : SRAM
 *   0x02000000 - 0x3FFFFFFF : Peripheral registers
 *   0x40000000 - ???        : DRAM (1GB to 4GB typical)
 *
 * U-Boot typically loads kernel to 0x40200000 or similar.
 * We use 0x40080000 to match common convention (0x80000 offset from DRAM base).
 */

ENTRY(_start)

/* Configuration */
__kernel_load_addr = 0x40080000;    /* DRAM base + 0x80000 */
__stack_size = 64K;

/* Memory regions */
MEMORY
{
    /*
     * DRAM region for RK3528A
     * From 0x40080000 (after reserved area) for ~1GB
     * Most boards have at least 1GB, some have 2GB or 4GB
     */
    RAM (rwx) : ORIGIN = 0x40080000, LENGTH = 0x3FF80000
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

    /* Image size for ARM64 header */
    __image_size = . - __kernel_load_addr;
}

/* Sanity checks */
ASSERT(__text_start == __kernel_load_addr,
       "ERROR: Boot code must start at 0x40080000!")
ASSERT((__bss_start & 7) == 0,
       "ERROR: BSS start must be 8-byte aligned")
ASSERT((__bss_end & 7) == 0,
       "ERROR: BSS end must be 8-byte aligned")
ASSERT((_stack_top & 15) == 0,
       "ERROR: Stack must be 16-byte aligned")
ASSERT((__heap_start & 0xFFF) == 0,
       "ERROR: Heap must be 4KB page aligned")
