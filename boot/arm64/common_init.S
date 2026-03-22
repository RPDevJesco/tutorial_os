/*
 * boot/arm64/common_init.S - Common Post-SoC Initialization
 * ==========================================================
 *
 * This code runs AFTER soc_early_init has completed.
 * At this point:
 *   - RAM has been detected (stored in detected_ram_base/size)
 *   - Page tables have been built
 *   - MMU may or may not be enabled (depending on SoC init)
 *
 * This file handles the GENERIC tasks:
 *   - Set up exception vectors
 *   - Set up stack
 *   - Clear BSS
 *   - Enable FP/SIMD
 *   - Jump to kernel_main
 *
 * CALLING CONVENTION:
 * -------------------
 * soc_early_init should jump here with:
 *   x19 = DTB pointer (preserved from entry)
 */

.section ".text"

.global boot_continue
boot_continue:
    /*
     * At this point, soc_early_init has:
     *   - Stored RAM base in detected_ram_base
     *   - Stored RAM size in detected_ram_size
     *   - Built and possibly enabled page tables
     *
     * x19 still contains DTB pointer from entry
     */

    /* =========================================================================
     * Set up Exception Vectors
     * =========================================================================
     * Point VBAR_EL1 to our exception vector table.
     */
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    /* =========================================================================
     * Set up Stack
     * =========================================================================
     * Stack grows downward, so SP points to the TOP of the stack region.
     * _stack_top is defined by the linker script.
     */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* =========================================================================
     * Clear BSS
     * =========================================================================
     * Zero-initialize the BSS section (uninitialized global variables).
     * __bss_start and __bss_end are defined by the linker script.
     *
     * We use STP (Store Pair) to write 16 bytes at a time for efficiency.
     * The linker script ensures BSS is 16-byte aligned.
     */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end

.Lclear_bss:
    cmp     x0, x1
    b.ge    .Lbss_done
    stp     xzr, xzr, [x0], #16     /* Store 16 bytes of zeros, increment x0 */
    b       .Lclear_bss

.Lbss_done:

    /* =========================================================================
     * Enable FP/SIMD
     * =========================================================================
     * By default, floating-point and NEON instructions trap.
     * We enable them by setting CPACR_EL1.FPEN to 0b11.
     */
    mov     x0, #(3 << 20)          /* FPEN bits [21:20] = 0b11 */
    msr     cpacr_el1, x0
    isb

    /* =========================================================================
     * Jump to Kernel
     * =========================================================================
     * Call kernel_main with parameters:
     *   x0 = DTB pointer
     *   x1 = RAM base address
     *   x2 = RAM size
     *
     * The kernel can use these to initialize the heap and other subsystems.
     */
    mov     x0, x19                 /* DTB pointer */

    ldr     x1, =detected_ram_base
    ldr     x1, [x1]                /* RAM base */

    ldr     x2, =detected_ram_size
    ldr     x2, [x2]                /* RAM size */

    bl      kernel_main

    /* =========================================================================
     * Park Core (kernel returned)
     * =========================================================================
     * If kernel_main ever returns, we just park the CPU in a low-power loop.
     * WFE = Wait For Event - puts CPU in low-power state until interrupt.
     */
.Lpark:
    wfe
    b       .Lpark


/* =============================================================================
 * External Symbols
 * =============================================================================
 */

/* From linker script */
.extern __bss_start
.extern __bss_end
.extern _stack_top

/* From vectors.S */
.extern exception_vectors

/* From SoC boot code */
.extern detected_ram_base
.extern detected_ram_size

/* From kernel */
.extern kernel_main
