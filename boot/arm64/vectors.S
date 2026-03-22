/*
 * boot/arm64/vectors.S - ARM64 Exception Vector Table
 * ====================================================
 *
 * This file defines the exception vector table for ARM64.
 * The vector table is GENERIC for all ARM64 platforms - the hardware
 * mechanism is the same regardless of SoC.
 *
 * EXCEPTION TYPES:
 * ----------------
 *   - Synchronous: Instruction-triggered (syscall, page fault, undefined)
 *   - IRQ: Normal interrupt
 *   - FIQ: Fast interrupt
 *   - SError: System error (async abort)
 *
 * VECTOR TABLE LAYOUT:
 * --------------------
 * The table has 16 entries (4 exception types × 4 contexts):
 *
 *   Offset    Context                         Exception
 *   ------    -------                         ---------
 *   0x000     Current EL, SP_EL0              Synchronous
 *   0x080     Current EL, SP_EL0              IRQ
 *   0x100     Current EL, SP_EL0              FIQ
 *   0x180     Current EL, SP_EL0              SError
 *
 *   0x200     Current EL, SP_ELx              Synchronous
 *   0x280     Current EL, SP_ELx              IRQ
 *   0x300     Current EL, SP_ELx              FIQ
 *   0x380     Current EL, SP_ELx              SError
 *
 *   0x400     Lower EL, AArch64               Synchronous
 *   0x480     Lower EL, AArch64               IRQ
 *   0x500     Lower EL, AArch64               FIQ
 *   0x580     Lower EL, AArch64               SError
 *
 *   0x600     Lower EL, AArch32               Synchronous
 *   0x680     Lower EL, AArch32               IRQ
 *   0x700     Lower EL, AArch32               FIQ
 *   0x780     Lower EL, AArch32               SError
 *
 * Each entry is 0x80 (128) bytes - enough for a small handler or branch.
 */

.section ".text.vectors"
.balign 0x800               /* Vector table must be 2KB aligned */

.global exception_vectors
exception_vectors:

/* =============================================================================
 * Current EL with SP_EL0 (not used in our kernel)
 * =============================================================================
 */
    .balign 0x80
    b       .Lunhandled         /* Synchronous */
    .balign 0x80
    b       .Lunhandled         /* IRQ */
    .balign 0x80
    b       .Lunhandled         /* FIQ */
    .balign 0x80
    b       .Lunhandled         /* SError */

/* =============================================================================
 * Current EL with SP_ELx (kernel context - this is us!)
 * =============================================================================
 */
    .balign 0x80
    b       sync_exception_handler  /* Synchronous (page fault, etc.) */
    .balign 0x80
    b       irq_handler             /* IRQ */
    .balign 0x80
    b       .Lunhandled             /* FIQ (not used) */
    .balign 0x80
    b       .Lunhandled             /* SError */

/* =============================================================================
 * Lower EL using AArch64 (user space, if we had it)
 * =============================================================================
 */
    .balign 0x80
    b       .Lunhandled         /* Synchronous */
    .balign 0x80
    b       .Lunhandled         /* IRQ */
    .balign 0x80
    b       .Lunhandled         /* FIQ */
    .balign 0x80
    b       .Lunhandled         /* SError */

/* =============================================================================
 * Lower EL using AArch32 (not supported)
 * =============================================================================
 */
    .balign 0x80
    b       .Lunhandled         /* Synchronous */
    .balign 0x80
    b       .Lunhandled         /* IRQ */
    .balign 0x80
    b       .Lunhandled         /* FIQ */
    .balign 0x80
    b       .Lunhandled         /* SError */


/* =============================================================================
 * Context Save/Restore Macros
 * =============================================================================
 * When an exception occurs, we must save all registers so the handler
 * can use them, then restore before returning.
 */

.macro SAVE_CONTEXT
    /*
     * Allocate stack frame for all general-purpose registers plus
     * ELR_EL1 (return address) and SPSR_EL1 (saved status).
     *
     * Frame layout (272 bytes):
     *   [sp, #0]   - x0, x1
     *   [sp, #16]  - x2, x3
     *   ...
     *   [sp, #240] - x30
     *   [sp, #248] - ELR_EL1
     *   [sp, #256] - SPSR_EL1
     *   [sp, #264] - padding
     */
    sub     sp, sp, #272

    /* Save general-purpose registers */
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    /* Save exception return state */
    mrs     x0, elr_el1         /* Exception Link Register (return PC) */
    mrs     x1, spsr_el1        /* Saved Program Status Register */
    stp     x0, x1, [sp, #248]
.endm

.macro RESTORE_CONTEXT
    /* Restore exception return state */
    ldp     x0, x1, [sp, #248]
    msr     elr_el1, x0
    msr     spsr_el1, x1

    /* Restore general-purpose registers */
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]

    /* Deallocate stack frame */
    add     sp, sp, #272
.endm


/* =============================================================================
 * Exception Handlers
 * =============================================================================
 */

.section ".text"

/*
 * Synchronous Exception Handler
 *
 * Called for:
 *   - Page faults (data/instruction abort)
 *   - Undefined instructions
 *   - SVC (syscall) - if we implement userspace
 *   - Debug exceptions
 *
 * Calls C handler: handle_sync_exception(context, esr, far)
 */
sync_exception_handler:
    SAVE_CONTEXT

    mov     x0, sp              /* Arg 1: pointer to saved context */
    mrs     x1, esr_el1         /* Arg 2: Exception Syndrome Register */
    mrs     x2, far_el1         /* Arg 3: Fault Address Register */
    bl      handle_sync_exception

    RESTORE_CONTEXT
    eret                        /* Return from exception */

/*
 * IRQ Handler
 *
 * Called for hardware interrupts.
 *
 * Calls C handler: handle_irq(context)
 */
irq_handler:
    SAVE_CONTEXT

    mov     x0, sp              /* Arg 1: pointer to saved context */
    bl      handle_irq

    RESTORE_CONTEXT
    eret

/*
 * Unhandled Exception
 *
 * For exception types we don't handle, just hang.
 * In a real OS, you'd want to print debug info first.
 */
.Lunhandled:
    SAVE_CONTEXT

    mov     x0, sp              /* Arg 1: pointer to saved context */
    mrs     x1, esr_el1         /* Arg 2: Exception Syndrome Register */
    bl      handle_unhandled_exception

    /* Should not return, but just in case... */
1:  wfe
    b       1b


/* =============================================================================
 * Weak default C handlers (can be overridden)
 * =============================================================================
 * These provide default implementations that just hang.
 * The kernel can override them with real implementations.
 */

.weak handle_sync_exception
.weak handle_irq
.weak handle_unhandled_exception

handle_sync_exception:
handle_irq:
handle_unhandled_exception:
1:  wfe
    b       1b
