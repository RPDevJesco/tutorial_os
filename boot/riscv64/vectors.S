// =============================================================================
// vectors.S — RISC-V 64-bit Trap Vector Table for Tutorial-OS
// =============================================================================
//
// This file handles ALL traps: exceptions (synchronous) and interrupts
// (asynchronous). It's the RISC-V equivalent of boot/arm64/vectors.S.
//
// How RISC-V traps work vs ARM64:
// ===============================
//
// ARM64 has a 16-entry vector table (4 exception types × 4 execution states),
// each entry 0x80 bytes apart, written to vbar_el1. The CPU jumps to the
// appropriate entry based on the exception type and source EL.
//
// RISC-V is simpler: the stvec CSR points to ONE handler (Direct mode) or
// a table indexed by cause (Vectored mode). We use Direct mode — every trap
// enters at trap_vector, and we read scause to figure out what happened.
//
// Trap types (scause high bit):
//   bit 63 = 0: Exception (synchronous — caused by the current instruction)
//     - 0: Instruction address misaligned
//     - 1: Instruction access fault
//     - 2: Illegal instruction
//     - 3: Breakpoint
//     - 4: Load address misaligned
//     - 5: Load access fault
//     - 6: Store address misaligned
//     - 7: Store access fault
//     - 8: Environment call from U-mode
//     - 9: Environment call from S-mode (our ecalls to OpenSBI)
//     - 12: Instruction page fault
//     - 13: Load page fault
//     - 15: Store page fault
//
//   bit 63 = 1: Interrupt (asynchronous — external event)
//     - 1: Supervisor software interrupt (IPI)
//     - 5: Supervisor timer interrupt
//     - 9: Supervisor external interrupt (PLIC)
//
// The trap frame:
// ===============
// When a trap occurs, we must save ALL registers that C code might use,
// handle the trap, then restore them. This is conceptually identical to
// ARM64's SAVE_CONTEXT/RESTORE_CONTEXT macros, just with different
// register names and quantities.
//
// RISC-V has 31 general-purpose registers (x1-x31, x0 is hardwired zero).
// We also save sepc (return address) and sstatus (interrupt state).
// Total frame: 33 × 8 bytes = 264 bytes.
//
// =============================================================================

.section ".text.vectors"
.global trap_vector

// =============================================================================
// Trap Frame Layout
// =============================================================================
// We define offsets into the trap frame for clarity and maintainability.
// Each slot holds one 64-bit register value (8 bytes).
//
// The layout is organized by register role in the calling convention:
//   - ra (return address)
//   - sp (stack pointer — saved separately)
//   - gp, tp (global/thread pointers — rarely change but must be saved)
//   - t0-t6 (temporaries — caller-saved, but the TRAP interrupted the caller)
//   - s0-s11 (saved registers — callee-saved)
//   - a0-a7 (argument/return registers)
//   - sepc (supervisor exception program counter — WHERE the trap happened)
//   - sstatus (supervisor status — interrupt enable state to restore)
//

.equ TF_RA,       0
.equ TF_SP,       8
.equ TF_GP,       16
.equ TF_TP,       24
.equ TF_T0,       32
.equ TF_T1,       40
.equ TF_T2,       48
.equ TF_S0,       56
.equ TF_S1,       64
.equ TF_A0,       72
.equ TF_A1,       80
.equ TF_A2,       88
.equ TF_A3,       96
.equ TF_A4,       104
.equ TF_A5,       112
.equ TF_A6,       120
.equ TF_A7,       128
.equ TF_S2,       136
.equ TF_S3,       144
.equ TF_S4,       152
.equ TF_S5,       160
.equ TF_S6,       168
.equ TF_S7,       176
.equ TF_S8,       184
.equ TF_S9,       192
.equ TF_S10,      200
.equ TF_S11,      208
.equ TF_T3,       216
.equ TF_T4,       224
.equ TF_T5,       232
.equ TF_T6,       240
.equ TF_SEPC,     248
.equ TF_SSTATUS,  256
.equ TF_SIZE,     264     // Total frame size (must be 16-byte aligned: 264 → 272)

// Round up to 16-byte alignment for stack compliance
.equ TF_SIZE_ALIGNED, 272

// =============================================================================
// Context Save Macro
// =============================================================================
// Save all registers to the stack. Called at the very start of trap handling.
//
// ARM64 equivalent: SAVE_CONTEXT macro using stp (store pair) instructions.
// RISC-V doesn't have store-pair, so we use individual sd (store doubleword).
// More instructions, same concept.
//
.macro SAVE_CONTEXT
    // Allocate trap frame on the stack
    addi    sp, sp, -TF_SIZE_ALIGNED

    // Save all general-purpose registers
    sd      ra,  TF_RA(sp)
    // Note: sp is saved AFTER adjustment — we save the original sp value
    // (before trap frame allocation) so the C handler sees the real stack.
    // We'll compute and store it below.
    sd      gp,  TF_GP(sp)
    sd      tp,  TF_TP(sp)
    sd      t0,  TF_T0(sp)
    sd      t1,  TF_T1(sp)
    sd      t2,  TF_T2(sp)
    sd      s0,  TF_S0(sp)
    sd      s1,  TF_S1(sp)
    sd      a0,  TF_A0(sp)
    sd      a1,  TF_A1(sp)
    sd      a2,  TF_A2(sp)
    sd      a3,  TF_A3(sp)
    sd      a4,  TF_A4(sp)
    sd      a5,  TF_A5(sp)
    sd      a6,  TF_A6(sp)
    sd      a7,  TF_A7(sp)
    sd      s2,  TF_S2(sp)
    sd      s3,  TF_S3(sp)
    sd      s4,  TF_S4(sp)
    sd      s5,  TF_S5(sp)
    sd      s6,  TF_S6(sp)
    sd      s7,  TF_S7(sp)
    sd      s8,  TF_S8(sp)
    sd      s9,  TF_S9(sp)
    sd      s10, TF_S10(sp)
    sd      s11, TF_S11(sp)
    sd      t3,  TF_T3(sp)
    sd      t4,  TF_T4(sp)
    sd      t5,  TF_T5(sp)
    sd      t6,  TF_T6(sp)

    // Save the ORIGINAL stack pointer (before trap frame allocation)
    addi    t0, sp, TF_SIZE_ALIGNED
    sd      t0, TF_SP(sp)

    // Save CSRs
    csrr    t0, sepc                // Where the trap happened
    sd      t0, TF_SEPC(sp)
    csrr    t0, sstatus             // Interrupt enable state
    sd      t0, TF_SSTATUS(sp)
.endm

// =============================================================================
// Context Restore Macro
// =============================================================================
// Restore all registers from the stack. Called just before sret.
//
.macro RESTORE_CONTEXT
    // Restore CSRs first
    ld      t0, TF_SEPC(sp)
    csrw    sepc, t0                // Will return to this address
    ld      t0, TF_SSTATUS(sp)
    csrw    sstatus, t0             // Restore interrupt state

    // Restore all general-purpose registers
    ld      ra,  TF_RA(sp)
    // sp restored last (after deallocating the frame)
    ld      gp,  TF_GP(sp)
    ld      tp,  TF_TP(sp)
    ld      t0,  TF_T0(sp)
    ld      t1,  TF_T1(sp)
    ld      t2,  TF_T2(sp)
    ld      s0,  TF_S0(sp)
    ld      s1,  TF_S1(sp)
    ld      a0,  TF_A0(sp)
    ld      a1,  TF_A1(sp)
    ld      a2,  TF_A2(sp)
    ld      a3,  TF_A3(sp)
    ld      a4,  TF_A4(sp)
    ld      a5,  TF_A5(sp)
    ld      a6,  TF_A6(sp)
    ld      a7,  TF_A7(sp)
    ld      s2,  TF_S2(sp)
    ld      s3,  TF_S3(sp)
    ld      s4,  TF_S4(sp)
    ld      s5,  TF_S5(sp)
    ld      s6,  TF_S6(sp)
    ld      s7,  TF_S7(sp)
    ld      s8,  TF_S8(sp)
    ld      s9,  TF_S9(sp)
    ld      s10, TF_S10(sp)
    ld      s11, TF_S11(sp)
    ld      t3,  TF_T3(sp)
    ld      t4,  TF_T4(sp)
    ld      t5,  TF_T5(sp)
    ld      t6,  TF_T6(sp)

    // Deallocate trap frame (restores original sp)
    addi    sp, sp, TF_SIZE_ALIGNED
.endm

// =============================================================================
// Trap Vector Entry Point
// =============================================================================
// This is where the CPU jumps on ANY trap (exception or interrupt).
// The stvec CSR points here (set by common_init.S).
//
// We save context, dispatch to the appropriate C handler based on the
// cause, restore context, and return from trap with `sret`.
//
// ARM64 equivalent: The vector table has separate entries for sync, IRQ,
// FIQ, and SError. RISC-V uses a single entry point and reads scause
// to distinguish — conceptually the same, just organized differently.
//
.balign 4                           // stvec requires 4-byte alignment
trap_vector:
    SAVE_CONTEXT

    // Read the trap cause
    csrr    t0, scause

    // Check high bit: interrupt (bit 63=1) vs exception (bit 63=0)
    // On RV64, the sign bit of scause indicates interrupt vs exception.
    bltz    t0, .Lhandle_interrupt   // if (scause < 0) it's an interrupt

    // -----------------------------------------------------------------
    // Exception (synchronous)
    // -----------------------------------------------------------------
    // Exceptions are caused by the currently executing instruction:
    // illegal instruction, page fault, misaligned access, breakpoint, etc.
    //
    // We call: void handle_exception(trap_frame_t *frame, unsigned long scause,
    //                                 unsigned long stval)
    //   frame  = pointer to saved registers on stack
    //   scause = exception cause code (low bits, high bit already checked)
    //   stval  = trap value (faulting address for page faults, instruction
    //            bits for illegal instruction, etc.)
    //
    mv      a0, sp                  // a0 = trap frame pointer
    mv      a1, t0                  // a1 = scause
    csrr    a2, stval               // a2 = stval (faulting addr / value)
    call    handle_exception
    j       .Ltrap_return

.Lhandle_interrupt:
    // -----------------------------------------------------------------
    // Interrupt (asynchronous)
    // -----------------------------------------------------------------
    // Interrupts come from external sources: timer tick, PLIC (external
    // interrupt controller), or IPI (inter-processor interrupt).
    //
    // We mask off the high bit to get the cause code, then call:
    //   void handle_interrupt(trap_frame_t *frame, unsigned long cause)
    //
    // Common interrupt causes on the Ky X1:
    //   1 = Supervisor software interrupt (IPI via CLINT)
    //   5 = Supervisor timer interrupt (timer via CLINT/SBI)
    //   9 = Supervisor external interrupt (PLIC — GPIO, UART, etc.)
    //
    // ARM64 equivalent: irq_handler calls handle_irq(frame).
    // RISC-V passes the cause code too, since all interrupts share
    // one entry point.
    //
    li      t1, ~(1L << 63)         // Mask to clear high bit
    and     a1, t0, t1              // a1 = interrupt cause code
    mv      a0, sp                  // a0 = trap frame pointer
    call    handle_interrupt

.Ltrap_return:
    RESTORE_CONTEXT
    sret                            // Return from supervisor trap

// =============================================================================
// Default C Handlers (weak symbols)
// =============================================================================
// These provide safe defaults if the kernel hasn't installed its own handlers.
// They just loop forever — a trap with no handler should halt, not corrupt
// the system by returning to a broken state.
//
// The `weak` attribute means any C function with the same name will override
// these. This is the same pattern as ARM64's default exception handlers.
//
// In C, you override them like:
//   void handle_exception(trap_frame_t *frame, unsigned long cause,
//                         unsigned long tval) { ... }
//   void handle_interrupt(trap_frame_t *frame, unsigned long cause) { ... }
//
.weak handle_exception
handle_exception:
    // Default: print 'E' for unhandled exception and halt
    li      a7, 1                   // SBI_PUTCHAR
    li      a0, 'E'
    ecall
1:  wfi
    j       1b

.weak handle_interrupt
handle_interrupt:
    // Default: print 'I' for unhandled interrupt and halt
    li      a7, 1                   // SBI_PUTCHAR
    li      a0, 'I'
    ecall
1:  wfi
    j       1b
