/*
 * soc/jh7110/linker.ld — JH7110 Linker Script
 * =============================================
 *
 * MILK-V MARS MEMORY MAP:
 * -----------------------
 *   0x00000000 - 0x3FFFFFFF : Peripheral / BootROM / CLINT / PLIC
 *   0x40000000 - 0x401FFFFF : Reserved for OpenSBI + U-Boot
 *   0x40200000 - ???        : Kernel loads here <-- ENTRY POINT
 *   ???        - 0x43FFFFFF : Available RAM (8 GB total from 0x40000000)
 *
 * COMPARISON WITH BCM2710:
 *   BCM2710 loads at 0x80000 (GPU firmware below)
 *   JH7110 loads at 0x40200000 (OpenSBI + U-Boot below)
 *
 * KERNEL MEMORY LAYOUT:
 *   +----------------------+ 0x40200000
 *   |      .text           |    Code (boot code first!)
 *   |  (.text.boot first)  |
 *   |  (.text.vectors)     |
 *   |  (.text)             |
 *   +----------------------+
 *   |     .rodata          |    Read-only data
 *   +----------------------+
 *   |      .data           |    Initialized globals
 *   +----------------------+
 *   |      .bss            |    Uninitialized globals (zeroed)
 *   +----------------------+
 *   |      Stack           |    Function call stack (grows DOWN)
 *   +----------------------+ _stack_top
 */

ENTRY(_start)

/* Configuration */
__kernel_load_addr = 0x40200000;
__stack_size = 64K;

/* Memory regions */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x40200000, LENGTH = 0x40000000
}

SECTIONS
{
    /* =======================================================================
     * Code Section
     * ======================================================================= */
    . = __kernel_load_addr;

    .text : {
        __text_start = .;

        /* Boot code MUST be first — CPU starts executing at load address */
        *(.text.boot)

        /* Exception vectors need 4-byte alignment on RISC-V */
        . = ALIGN(4);
        *(.text.vectors)

        /* All other code */
        *(.text .text.*)

        __text_end = .;
    } > RAM

    /* =======================================================================
     * Read-Only Data
     * ======================================================================= */
    .rodata : ALIGN(4K) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    } > RAM

    /* =======================================================================
     * Initialized Data
     * ======================================================================= */
    .data : ALIGN(4K) {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > RAM

    /* =======================================================================
     * Uninitialized Data (BSS)
     * ======================================================================= */
    .bss (NOLOAD) : ALIGN(4K) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    . = ALIGN(16);
    __bss_size = __bss_end - __bss_start;

    /* =======================================================================
     * Stack
     * ======================================================================= */
    . = ALIGN(16);
    __stack_bottom = .;
    . += __stack_size;
    _stack_top = .;            /* single underscore — matches common_init.S */
    __stack_top = .;           /* double underscore alias for consistency */

    /* =======================================================================
     * Kernel End
     * ======================================================================= */
     /* Stack */
     . = ALIGN(16);
     __stack_bottom = .;
     . += __stack_size;
     _stack_top = .;
     __stack_top = .;

     /* =======================================================================
      * Heap
      * =======================================================================
      * __heap_end is computed at runtime based on actual RAM size.
      */
     .heap (NOLOAD) : ALIGN(4K) {
         __heap_start = .;
     } > RAM

     /* Kernel End */
     . = ALIGN(4K);
     __kernel_end = .;
    __image_size = __kernel_end - __kernel_load_addr;

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
