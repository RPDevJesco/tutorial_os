/*
 * soc/bcm2710/linker.ld - BCM2710 Linker Script
 * ==============================================
 *
 * This matches the original working memory_layout.ld
 *
 * RASPBERRY PI ZERO 2W MEMORY MAP:
 * --------------------------------
 *   0x00000000 - 0x00080000 : Reserved for GPU firmware
 *   0x00080000 - ???        : Kernel loads here <-- ENTRY POINT
 *   ???        - 0x3EFFFFFF : Available RAM (size from mailbox)
 *   0x3F000000 - 0x3FFFFFFF : Peripheral registers (64MB)
 *   0x40000000 - 0xFFFFFFFF : ARM local peripherals / unused
 *
 * KERNEL MEMORY LAYOUT:
 * ---------------------
 *   +----------------------+ 0x00080000  <-- Kernel starts here
 *   |      .text           |             Code (boot code first!)
 *   |  (.text.boot first)  |
 *   |  (.text.vectors)     |
 *   |  (.text)             |
 *   +----------------------+
 *   |     .rodata          |             Read-only data
 *   +----------------------+
 *   |      .data           |             Initialized globals
 *   +----------------------+
 *   |      .bss            |             Uninitialized globals (zeroed)
 *   +----------------------+
 *   |      Stack           |             Function call stack (grows DOWN)
 *   |        |             |
 *   |        v             |
 *   +----------------------+ _stack_top
 *   |       Heap           |             Dynamic memory
 *   +----------------------+
 */

ENTRY(_start)

/* Configuration */
__kernel_load_addr = 0x80000;
__stack_size = 64K;

/* Memory regions */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000, LENGTH = 0x3F000000 - 0x80000
}

/* Sections */
SECTIONS
{
    /* =======================================================================
     * Code Section
     * =======================================================================
     */
    . = __kernel_load_addr;

    .text : {
        __text_start = .;

        /* Boot code MUST be first - CPU starts executing at 0x80000 */
        *(.text.boot)

        /* Exception vectors need 2KB alignment */
        . = ALIGN(2048);
        *(.text.vectors)

        /* All other code */
        *(.text .text.*)

        __text_end = .;
    } > RAM

    /* =======================================================================
     * Read-Only Data
     * =======================================================================
     */
    .rodata : ALIGN(4K) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    } > RAM

    /* =======================================================================
     * Initialized Data
     * =======================================================================
     */
    .data : ALIGN(4K) {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > RAM

    /* =======================================================================
     * Uninitialized Data (BSS)
     * =======================================================================
     * ALIGN(4K) to ensure MMU tables in BSS are properly aligned.
     * BSS is zeroed by boot code.
     */
    .bss : ALIGN(4K) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    /* =======================================================================
     * Stack
     * =======================================================================
     * Stack grows downward, SP points to _stack_top.
     * ARM64 requires 16-byte alignment.
     */
    .stack (NOLOAD) : ALIGN(16) {
        __stack_bottom = .;
        . += __stack_size;
        _stack_top = .;
    } > RAM

    /* =======================================================================
     * Heap
     * =======================================================================
     * __heap_end is computed at runtime based on actual RAM size.
     */
    .heap (NOLOAD) : ALIGN(4K) {
        __heap_start = .;
    } > RAM

    /* =======================================================================
     * Debug Sections
     * =======================================================================
     */
    .debug_info     0 : { *(.debug_info) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_ranges   0 : { *(.debug_ranges) }
    .debug_loc      0 : { *(.debug_loc) }

    /* =======================================================================
     * Discarded Sections
     * =======================================================================
     */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.ARM.*)
    }
}

/* =============================================================================
 * Sanity Checks
 * =============================================================================
 */

/* Boot code must be at exactly 0x80000 */
ASSERT(__text_start == __kernel_load_addr,
       "ERROR: Boot code must start at 0x80000!")

/* BSS must be 8-byte aligned for the zeroing loop */
ASSERT((__bss_start & 7) == 0,
       "ERROR: BSS start must be 8-byte aligned")
ASSERT((__bss_end & 7) == 0,
       "ERROR: BSS end must be 8-byte aligned")

/* ARM64 requires 16-byte stack alignment */
ASSERT((_stack_top & 15) == 0,
       "ERROR: Stack must be 16-byte aligned")

/* Heap should be page-aligned */
ASSERT((__heap_start & 0xFFF) == 0,
       "ERROR: Heap must be 4KB page aligned")
