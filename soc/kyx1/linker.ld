/* =============================================================================
 * linker.ld — RISC-V 64-bit Linker Script for Tutorial-OS
 * =============================================================================
 *
 * Target: Orange Pi RV2 (Ky X1 / SpacemiT K1 SoC)
 * This is the RISC-V equivalent of boot/arm64/linker.ld.
 *
 * What is a linker script?
 * ========================
 * When the compiler turns your C code into machine instructions, each .c file
 * becomes a .o "object file" containing chunks of code and data. The LINKER
 * combines all these chunks into a single binary. But it needs to know:
 *   - WHERE in memory to place the code (the load address)
 *   - In what ORDER to arrange the sections
 *   - Where the stack, heap, and BSS live
 *
 * This script answers all those questions.
 *
 * How is this different from the ARM64 version?
 * =============================================
 * Structurally, it's almost identical! The major differences are:
 *
 *   1. Load address: ARM64 Pi loads at 0x80000 (GPU puts kernel there)
 *      RISC-V Ky X1 loads at 0x11000000 (U-Boot's bootm target)
 *
 *   2. RAM region: ARM64 Pi has peripherals at 0x3F000000 limiting usable RAM
 *      Ky X1 DRAM starts at 0x0, but 0x0-0x11000000 is used by OpenSBI/U-Boot
 *      and 0x2FF40000 is reserved for the display controller
 *
 *   3. Stack alignment: Both ARM64 and RISC-V require 16-byte alignment,
 *      so this is actually the same!
 *
 *   4. No .ARM.* sections to discard (those are ARM-specific metadata)
 *
 * Memory Map Context (from Ky X1 hardware reference):
 * ====================================================
 *   0x0000_0000  OpenSBI (M-mode firmware, resident)
 *   0x0020_0000  U-Boot (S-mode bootloader)
 *   0x0100_0000  U-Boot initial stack
 *   0x1100_0000  *** OUR KERNEL LOADS HERE ***  <-- __kernel_load_addr
 *   0x2FF4_0000  DPU reserved (display MMU tables, DO NOT TOUCH)
 *   0x3000_0000  RCPU reserved regions
 *   0x4000_0000  CMA pool (768 MB, used by Linux — we can reclaim)
 *   0x8000_0000  End of DRAM bank 0 (2 GB)
 *
 * "_start" is defined in entry.S — the first instruction executed!
 */
ENTRY(_start)

/* =============================================================================
 * CONFIGURATION
 * =============================================================================
 */

/*
 * Where U-Boot loads our kernel image.
 *
 * This comes from the vendor U-Boot defconfig:
 *   CONFIG_BOOTCOMMAND = "bootm 0x11000000"
 *   CONFIG_SPL_LOAD_FIT_ADDRESS = 0x11000000
 *
 * U-Boot loads our image to this address, then jumps to _start.
 * If this value doesn't match U-Boot's expectation, our kernel will
 * either crash or execute garbage. Getting this right is critical.
 *
 * Contrast with ARM64 Pi: the GPU firmware always loads kernel8.img
 * to 0x80000. Same concept, different address.
 */
__kernel_load_addr = 0x11000000;

/* Stack size (64KB — same as ARM64 build, plenty for bare-metal) */
__stack_size = 64K;

/* =============================================================================
 * MEMORY REGIONS
 * =============================================================================
 * We define the usable memory region. The linker will refuse to place
 * anything outside these bounds, catching overflow errors at build time.
 *
 * Our usable region starts at the load address (0x11000000) and extends
 * to just before the DPU reserved memory at 0x2FF40000. This gives us
 * approximately 497 MB of contiguous space — far more than we need.
 *
 * We DON'T include the memory below 0x11000000 because OpenSBI and
 * U-Boot live there (and OpenSBI stays resident for ecalls).
 * We DON'T include memory above 0x2FF40000 because the display
 * controller's MMU tables are there (corrupting them = no display).
 *
 * The (rwx) means this region is readable, writable, and executable.
 * In a production OS you'd separate these permissions with the MMU,
 * but for bare-metal education, one region keeps things simple.
 */
MEMORY
{
    /*
     * Usable RAM: From our load address to the DPU reserved region.
     * 0x2FF40000 - 0x11000000 = 0x1EF40000 = ~497 MB
     */
    RAM (rwx) : ORIGIN = 0x11000000, LENGTH = 0x2FF40000 - 0x11000000
}

/* =============================================================================
 * SECTIONS
 * =============================================================================
 * Sections control HOW the binary is organized in memory. Each section
 * groups related content:
 *
 *   .text   = executable code (the actual instructions)
 *   .rodata = read-only data (string literals, const arrays)
 *   .data   = initialized global variables (int x = 42;)
 *   .bss    = uninitialized globals (int buf[1024]; — zeroed at boot)
 *   .stack  = reserved space for the call stack
 *   .heap   = dynamically allocated memory (managed by allocator.c)
 *
 * The order matters! Boot code must be at the very start because
 * U-Boot jumps to the load address expecting _start to be there.
 */
SECTIONS
{
    /* =========================================================================
     * CODE SECTION (.text)
     * =========================================================================
     * Contains all executable instructions. Boot code goes first!
     *
     * The (.) is the "location counter" — the current address being filled.
     * We set it to __kernel_load_addr so code starts at 0x11000000.
     */
    . = __kernel_load_addr;

    .text : {
        __text_start = .;

        /*
         * BOOT CODE FIRST!
         *
         * U-Boot jumps to 0x11000000, so entry.S MUST be at this exact
         * address. The ".text.boot" section attribute in entry.S ensures
         * it gets placed here, ahead of all other code.
         *
         * This is identical to how the ARM64 build puts boot.S first
         * at 0x80000 — same trick, different address.
         */
        *(.text.boot)

        /*
         * Trap vectors need alignment.
         *
         * RISC-V requires the trap vector base (written to stvec CSR)
         * to be aligned based on the vector mode:
         *   - Direct mode: 4-byte aligned
         *   - Vectored mode: 4-byte aligned (mode in low 2 bits)
         *
         * We align to 256 bytes for safety and to keep vectors in their
         * own cache lines. ARM64 requires 2KB alignment for its vector
         * table — RISC-V is less strict here.
         */
        . = ALIGN(256);
        *(.text.vectors)

        /*
         * All other code follows — kernel, drivers, HAL, UI widgets.
         * The linker collects every .text section from every .o file.
         */
        *(.text .text.*)

        __text_end = .;
    } > RAM

    /* =========================================================================
     * READ-ONLY DATA SECTION (.rodata)
     * =========================================================================
     * Contains data that should never change:
     *   - String literals: "Hello from Tutorial-OS!"
     *   - const arrays: const uint32_t palette[] = { ... };
     *   - Switch statement jump tables
     *
     * ALIGN(4K) ensures page alignment. When we later enable the MMU,
     * we can make this region read-only (no execute, no write) for
     * proper memory protection. Same rationale as the ARM64 build.
     */
    .rodata : ALIGN(4K) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    } > RAM

    /* =========================================================================
     * INITIALIZED DATA SECTION (.data)
     * =========================================================================
     * Contains global/static variables WITH initial values:
     *   int counter = 42;        // Goes in .data
     *   static char mode = 'A';  // Goes in .data
     *
     * These values are stored in the kernel image and loaded to RAM
     * by U-Boot as part of the `bootm` process.
     */
    .data : ALIGN(4K) {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > RAM

    /* =========================================================================
     * UNINITIALIZED DATA SECTION (.bss)
     * =========================================================================
     * Contains global/static variables WITHOUT initial values:
     *   int buffer[1024];    // Goes in .bss
     *   static int x;        // Goes in .bss
     *
     * BSS = "Block Started by Symbol" (historical name from the 1950s!)
     *
     * These DON'T take space in the kernel image — only the start/end
     * addresses are recorded. entry.S zeroes this region before calling
     * kernel_main(). That's why C guarantees uninitialized globals are
     * zero — it's not magic, it's the boot code doing the work!
     */
    .bss : ALIGN(4K) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)           /* Old-style uninitialized common symbols */
        . = ALIGN(8);       /* Ensure end is doubleword aligned for */
        __bss_end = .;      /* the 8-byte clearing loop in entry.S  */
    } > RAM

    /* =========================================================================
     * STACK REGION
     * =========================================================================
     * The stack is used for:
     *   - Function return addresses (ra register saved here)
     *   - Local variables that don't fit in registers
     *   - Saved callee-saved registers (s0-s11) during function calls
     *   - Trap frame storage when handling interrupts
     *
     * RISC-V calling convention: stack grows DOWNWARD (high → low),
     * same as ARM64 and x86. We point sp to the TOP of this region.
     *
     * NOLOAD means this section doesn't contain data in the image —
     * it's just reserved address space. No bytes are written for it.
     *
     * ALIGN(16) is required by the RISC-V calling convention (same as
     * ARM64's AAPCS64). The spec says: "The stack pointer shall always
     * be aligned to a 128-bit boundary upon procedure entry."
     */
    .stack (NOLOAD) : ALIGN(16) {
        __stack_bottom = .;
        . += __stack_size;      /* Reserve 64KB */
        _stack_top = .;         /* sp points here (entry.S loads this) */
    } > RAM

    /* =========================================================================
     * HEAP REGION START
     * =========================================================================
     * The heap is for dynamic memory allocation (managed by allocator.c,
     * our TLSF-based allocator).
     *
     * We only mark where it STARTS here. The END is determined at runtime
     * based on available memory. For now, a safe upper bound is the DPU
     * reserved region at 0x2FF40000.
     *
     * Runtime calculation:
     *   heap_end = 0x2FF40000 (just below DPU reserved)
     *
     * This gives us potentially hundreds of MB of heap — far more than
     * the ARM64 Pi build, where the heap is limited by GPU memory split.
     */
    .heap (NOLOAD) : ALIGN(4K) {
        __heap_start = .;
        /* __heap_end is computed at runtime! */
    } > RAM

    /* =========================================================================
     * DEBUG SECTIONS (optional)
     * =========================================================================
     * These contain debugging info for GDB. The (0) means they're not
     * loaded into memory — they stay in the ELF file on your host machine
     * for use during debugging sessions.
     *
     * If you connect GDB via JTAG or OpenOCD, these let you set
     * breakpoints by function name, inspect variables, and step through
     * C source code even though the target is running raw machine code.
     */
    .debug_info     0 : { *(.debug_info) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_ranges   0 : { *(.debug_ranges) }
    .debug_loc      0 : { *(.debug_loc) }

    /* =========================================================================
     * DISCARDED SECTIONS
     * =========================================================================
     * These sections consume space but serve no purpose in our bare-metal
     * binary. Throwing them away makes the image smaller.
     *
     * Note: The ARM64 build also discards .ARM.* sections (ARM-specific
     * metadata like unwind tables). We don't have those on RISC-V, but
     * we do discard .riscv.attributes which contains ISA string metadata.
     */
    /DISCARD/ : {
        *(.comment)             /* Compiler version strings */
        *(.note .note.*)        /* ELF notes */
        *(.eh_frame*)           /* C++ exception handling (not used) */
        *(.riscv.attributes)    /* RISC-V ISA extension attributes */
    }
}

/* =============================================================================
 * SANITY CHECKS
 * =============================================================================
 * These ASSERT statements make the linker FAIL with a helpful error message
 * if something goes wrong. Much better than mysterious crashes on hardware!
 *
 * Without these, a misconfigured build might silently produce a binary that
 * boots to a black screen with no indication of what went wrong. These turn
 * build-time mistakes into clear error messages.
 */

/* Boot code must be at exactly 0x11000000 — U-Boot jumps here at startup */
ASSERT(__text_start == __kernel_load_addr,
       "ERROR: Boot code must start at 0x11000000! Check .text.boot placement.")

/* BSS must be 8-byte aligned for the doubleword zeroing loop in entry.S */
ASSERT((__bss_start & 7) == 0,
       "ERROR: BSS start must be 8-byte aligned")
ASSERT((__bss_end & 7) == 0,
       "ERROR: BSS end must be 8-byte aligned")

/* RISC-V requires 16-byte stack alignment */
ASSERT((_stack_top & 15) == 0,
       "ERROR: Stack must be 16-byte aligned (RISC-V calling convention)")

/* Heap should be page-aligned for clean memory management */
ASSERT((__heap_start & 0xFFF) == 0,
       "ERROR: Heap start must be 4KB page aligned")

/* Make sure we haven't accidentally overlapped with DPU reserved memory */
ASSERT(__heap_start < 0x2FF40000,
       "ERROR: Kernel image overflows into DPU reserved region at 0x2FF40000!")

/* =============================================================================
 * SYMBOLS AVAILABLE TO C CODE
 * =============================================================================
 * All the __name and _name symbols defined above can be used in C:
 *
 *   extern uint8_t __text_start[];    // Address of code start (0x11000000)
 *   extern uint8_t __bss_start[];     // BSS region start
 *   extern uint8_t __bss_end[];       // BSS region end
 *   extern uint8_t _stack_top[];      // Initial stack pointer value
 *   extern uint8_t __heap_start[];    // Where heap begins
 *   extern uint8_t __rodata_start[];  // Read-only data start
 *
 * Note: Declare these as arrays (not pointers) to get the ADDRESS of the
 * symbol without dereferencing. `__bss_start` IS the address — there's no
 * pointer stored at that location, the symbol IS the location.
 *
 * This is one of C's weirder corners: `extern uint8_t __bss_start[]` means
 * "there's something at this address" and `__bss_start` gives you that
 * address. Using `extern uint8_t *__bss_start` would try to READ a pointer
 * value FROM that address, which is not what we want.
 */
