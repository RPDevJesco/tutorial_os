/*
 * boot/x86_64/vectors.S - x86_64 Interrupt Descriptor Table & Handlers
 * ======================================================================
 *
 * This file defines the IDT and interrupt service routines for x86_64.
 * The IDT mechanism is GENERIC for all x86_64 platforms - the hardware
 * behavior is the same regardless of chipset.
 *
 * INTERRUPT TYPES:
 * ----------------
 *   - Exceptions (0-31):   CPU-generated (page fault, GPF, divide error...)
 *   - Hardware IRQs (32+): External interrupts via APIC/PIC
 *   - Software (any):      INT instruction (syscalls, etc.)
 *
 * IDT LAYOUT:
 * -----------
 * The IDT has 256 entries, each 16 bytes (Gate Descriptor):
 *
 *   Bytes   Field
 *   -----   -----
 *   0-1     Offset [15:0]   (handler address low)
 *   2-3     Segment selector (code segment, always 0x08)
 *   4       IST index [2:0] (0 = no IST, 1-7 = use IST stack)
 *   5       Type/Attributes:
 *             Bit 7:    Present (P)
 *             Bits 5-6: DPL (privilege level for INT instruction)
 *             Bit 4:    0 (must be zero)
 *             Bits 0-3: Gate type (0xE = 64-bit interrupt gate)
 *           Interrupt gate = 0x8E (P=1, DPL=0, type=0xE)
 *   6-7     Offset [31:16]  (handler address mid)
 *   8-11    Offset [63:32]  (handler address high)
 *   12-15   Reserved (zero)
 *
 * ERROR CODE NORMALIZATION:
 * -------------------------
 * Some CPU exceptions push an error code onto the stack, others don't:
 *
 *   With error code:  #DF(8), #TS(10), #NP(11), #SS(12),
 *                     #GP(13), #PF(14), #AC(17), #CP(21),
 *                     #VC(29), #SX(30)
 *
 *   Without:          Everything else (0-7, 9, 15-16, 18-20, 22-28, 31)
 *
 * To keep the trap frame layout uniform, stubs for exceptions WITHOUT
 * error codes push a dummy zero. All stubs then push the vector number,
 * giving us a consistent stack layout before entering the common handler.
 *
 * COMPARISON WITH ARM64:
 * ----------------------
 * ARM64's vector table is a static array of branch targets at fixed
 * 0x80-byte intervals, loaded with a single MSR to VBAR_EL1.
 *
 * x86_64's IDT entries encode the handler address split across three
 * fields, so we must build it at runtime. The ISR stubs below are the
 * branch targets; setup_idt() wires them into the IDT structure.
 */


/* =============================================================================
 * ISR Stub Generation Macros
 * =============================================================================
 * Each stub normalizes the stack (dummy error code if needed), pushes the
 * vector number, and jumps to the common handler. The stubs are as small
 * as possible - all real work happens in isr_common.
 */

/*
 * ISR_NOERRCODE - Stub for exceptions/interrupts that do NOT push an error code.
 * We push a dummy zero to keep the trap frame uniform.
 */
.macro ISR_NOERRCODE vec
    .align 16
    .global isr_stub_\vec
    isr_stub_\vec:
        pushq   $0                  /* Dummy error code */
        pushq   $\vec               /* Vector number */
        jmp     isr_common
.endm

/*
 * ISR_ERRCODE - Stub for exceptions that DO push an error code.
 * The CPU already pushed it, so we only push the vector number.
 */
.macro ISR_ERRCODE vec
    .align 16
    .global isr_stub_\vec
    isr_stub_\vec:
        pushq   $\vec               /* Vector number (error code already on stack) */
        jmp     isr_common
.endm


/* =============================================================================
 * CPU Exception Stubs (vectors 0-31)
 * =============================================================================
 * These are the x86_64 equivalent of ARM64's exception vector entries.
 *
 * ARM64 has 4 types × 4 contexts = 16 vectors.
 * x86_64 has 32 dedicated exception vectors, each with specific semantics.
 */

.section ".text.vectors"

ISR_NOERRCODE  0                    /* #DE  Divide Error */
ISR_NOERRCODE  1                    /* #DB  Debug */
ISR_NOERRCODE  2                    /*      NMI (Non-Maskable Interrupt) */
ISR_NOERRCODE  3                    /* #BP  Breakpoint (INT3) */
ISR_NOERRCODE  4                    /* #OF  Overflow (INTO) */
ISR_NOERRCODE  5                    /* #BR  Bound Range Exceeded */
ISR_NOERRCODE  6                    /* #UD  Undefined Opcode */
ISR_NOERRCODE  7                    /* #NM  Device Not Available (FPU) */
ISR_ERRCODE    8                    /* #DF  Double Fault */
ISR_NOERRCODE  9                    /*      Coprocessor Segment Overrun (legacy) */
ISR_ERRCODE    10                   /* #TS  Invalid TSS */
ISR_ERRCODE    11                   /* #NP  Segment Not Present */
ISR_ERRCODE    12                   /* #SS  Stack-Segment Fault */
ISR_ERRCODE    13                   /* #GP  General Protection Fault */
ISR_ERRCODE    14                   /* #PF  Page Fault */
ISR_NOERRCODE  15                   /*      Reserved */
ISR_NOERRCODE  16                   /* #MF  x87 Floating-Point Exception */
ISR_ERRCODE    17                   /* #AC  Alignment Check */
ISR_NOERRCODE  18                   /* #MC  Machine Check */
ISR_NOERRCODE  19                   /* #XM  SIMD Floating-Point Exception */
ISR_NOERRCODE  20                   /* #VE  Virtualization Exception */
ISR_ERRCODE    21                   /* #CP  Control Protection Exception */
ISR_NOERRCODE  22                   /*      Reserved */
ISR_NOERRCODE  23                   /*      Reserved */
ISR_NOERRCODE  24                   /*      Reserved */
ISR_NOERRCODE  25                   /*      Reserved */
ISR_NOERRCODE  26                   /*      Reserved */
ISR_NOERRCODE  27                   /*      Reserved */
ISR_NOERRCODE  28                   /*      Reserved */
ISR_ERRCODE    29                   /* #VC  VMM Communication Exception */
ISR_ERRCODE    30                   /* #SX  Security Exception */
ISR_NOERRCODE  31                   /*      Reserved */


/* =============================================================================
 * Hardware/Software Interrupt Stubs (vectors 32-255)
 * =============================================================================
 * These handle APIC interrupts, PIC IRQs, and software interrupts.
 * None push error codes.
 *
 * Vectors 32-47 are typically mapped to legacy PIC IRQs (IRQ 0-15).
 * Vectors 48+ are available for APIC, MSI, and software use.
 */

.altmacro

.macro GEN_IRQ_STUB n
    ISR_NOERRCODE %n
.endm

.set _vec, 32
.rept 224
    GEN_IRQ_STUB _vec
    .set _vec, _vec + 1
.endr

.noaltmacro


/* =============================================================================
 * ISR Stub Address Table
 * =============================================================================
 * Array of 256 function pointers used by setup_idt to build the IDT.
 * Each entry points to the corresponding isr_stub_N.
 */

.section ".data"
.align 8

.altmacro

.macro STUB_ENTRY n
    .quad isr_stub_\n
.endm

.global isr_stub_table
isr_stub_table:
.set _vec, 0
.rept 256
    STUB_ENTRY %_vec
    .set _vec, _vec + 1
.endr

.noaltmacro


/* =============================================================================
 * Context Save/Restore Macros
 * =============================================================================
 * When an interrupt or exception fires, we must save all registers so the
 * handler can use them freely, then restore everything before IRETQ.
 *
 * TRAP FRAME LAYOUT:
 * ------------------
 * On x86_64, the CPU automatically pushes SS, RSP, RFLAGS, CS, RIP
 * (and error code for some exceptions) before entering the ISR.
 * Our stub adds the vector number and dummy error code (if needed).
 * SAVE_CONTEXT then pushes all general-purpose registers.
 *
 * Stack after SAVE_CONTEXT (growing downward, RSP at top):
 *
 *   Offset  Register        Pushed by
 *   ------  --------        ---------
 *   [rsp+0x88]  SS          CPU (automatic)
 *   [rsp+0x80]  RSP         CPU (automatic)
 *   [rsp+0x78]  RFLAGS      CPU (automatic)
 *   [rsp+0x70]  CS          CPU (automatic)
 *   [rsp+0x68]  RIP         CPU (automatic)
 *   [rsp+0x60]  Error code  CPU or stub (normalized)
 *   [rsp+0x58]  Vector #    ISR stub
 *   [rsp+0x50]  RAX         SAVE_CONTEXT
 *   [rsp+0x48]  RBX         SAVE_CONTEXT
 *   [rsp+0x40]  RCX         SAVE_CONTEXT
 *   [rsp+0x38]  RDX         SAVE_CONTEXT
 *   [rsp+0x30]  RSI         SAVE_CONTEXT
 *   [rsp+0x28]  RDI         SAVE_CONTEXT
 *   [rsp+0x20]  RBP         SAVE_CONTEXT
 *   [rsp+0x18]  R8          SAVE_CONTEXT
 *   [rsp+0x10]  R9          SAVE_CONTEXT
 *   [rsp+0x08]  R10         SAVE_CONTEXT
 *   [rsp+0x00]  R11         SAVE_CONTEXT
 *              --- below only saved for full context ---
 *              R12-R15 are callee-saved and preserved by C handlers.
 *              We save them anyway for completeness (crash dumps, etc.)
 *
 * Total frame: 160 bytes (10 regs × 8) + 56 bytes (CPU + stub) = 176 bytes
 * With R12-R15: +32 = 208 bytes
 *
 * COMPARISON WITH ARM64:
 * ----------------------
 * ARM64 saves x0-x30 + ELR + SPSR = 272 bytes via STP pairs.
 * x86_64 saves 15 GPRs + CPU frame via individual PUSH = 176-208 bytes.
 * Both achieve the same goal: preserve complete execution state.
 */

.macro SAVE_CONTEXT
    /*
     * Save all general-purpose registers.
     * We save R12-R15 too (callee-saved) for complete state capture -
     * useful for crash dumps and context inspection even though C
     * handlers would preserve them.
     */
    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %rdx
    pushq   %rsi
    pushq   %rdi
    pushq   %rbp
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    /*
     * Ensure kernel data segments are loaded.
     * If we came from userspace (future), segment registers might hold
     * user selectors. Force them to kernel data (0x10).
     */
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
.endm

.macro RESTORE_CONTEXT
    /* Restore general-purpose registers (reverse order) */
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rbp
    popq    %rdi
    popq    %rsi
    popq    %rdx
    popq    %rcx
    popq    %rbx
    popq    %rax

    /* Skip vector number and error code (added by stub) */
    add     $16, %rsp
.endm


/* =============================================================================
 * Common ISR Entry Point
 * =============================================================================
 * All 256 ISR stubs jump here after normalizing the stack.
 * We save context, determine the vector type, and dispatch.
 */

.section ".text"

isr_common:
    SAVE_CONTEXT

    /*
     * Pass trap frame pointer as first argument to C handlers.
     * RSP now points to the saved register block.
     */
    mov     %rsp, %rdi              /* Arg 1: trap frame pointer */

    /*
     * Load the vector number to determine which handler to call.
     * Vector number is at [rsp + 0x78] (15 pushed regs × 8 = 120 = 0x78).
     *
     * Wait: 15 regs (R15..RAX) = 120 bytes. Vector is above that.
     * So vector# is at RSP + 15*8 = RSP + 120 = RSP + 0x78
     */
    movq    0x78(%rsp), %rsi        /* Arg 2: vector number */

    /* Dispatch based on vector number */
    cmp     $32, %rsi
    jb      .Lexception             /* Vectors 0-31: CPU exceptions */
    jmp     .Lirq                   /* Vectors 32+: hardware/software interrupts */


/*
 * CPU Exception Dispatcher (vectors 0-31)
 *
 * Called for:
 *   - Page faults (#PF, vector 14)
 *   - General protection faults (#GP, vector 13)
 *   - Divide errors (#DE, vector 0)
 *   - Undefined opcodes (#UD, vector 6)
 *   - All other CPU exceptions
 *
 * Calls C handler: handle_exception(trap_frame*, vector, error_code, cr2)
 *
 * This is the x86_64 equivalent of ARM64's sync_exception_handler.
 * ARM64 passes (context, ESR, FAR); we pass (frame, vector, error_code, CR2).
 */
.Lexception:
    /* RDI = trap frame (already set) */
    /* RSI = vector number (already set) */
    movq    0x80(%rsp), %rdx        /* Arg 3: error code (at RSP + 15*8 + 8) */
    mov     %cr2, %rcx              /* Arg 4: CR2 (fault address, like ARM64's FAR) */
    call    handle_exception
    jmp     .Lreturn


/*
 * IRQ/Interrupt Dispatcher (vectors 32-255)
 *
 * Called for hardware interrupts (APIC timer, keyboard, etc.)
 *
 * Calls C handler: handle_irq(trap_frame*, vector)
 *
 * This is the x86_64 equivalent of ARM64's irq_handler.
 */
.Lirq:
    /* RDI = trap frame (already set) */
    /* RSI = vector number (already set) */
    call    handle_irq
    jmp     .Lreturn


/*
 * Return from interrupt/exception
 */
.Lreturn:
    RESTORE_CONTEXT
    iretq                           /* Pops RIP, CS, RFLAGS, RSP, SS */


/* =============================================================================
 * IDT Setup Function
 * =============================================================================
 * Build and load the IDT at runtime.
 *
 * Called from boot_continue (common_init.S).
 * Uses the isr_stub_table[] array to get handler addresses and encodes
 * them into proper IDT gate descriptors.
 *
 * Each IDT entry (16 bytes):
 *   [0:1]   offset_low   = handler[15:0]
 *   [2:3]   selector     = 0x08 (kernel code segment)
 *   [4]     ist          = 0 (no IST)
 *   [5]     type_attr    = 0x8E (P=1, DPL=0, 64-bit interrupt gate)
 *   [6:7]   offset_mid   = handler[31:16]
 *   [8:11]  offset_high  = handler[63:32]
 *   [12:15] reserved     = 0
 */

.global setup_idt
setup_idt:
    push    %rbx
    push    %r12

    lea     idt_table(%rip), %rbx       /* RBX = IDT base */
    lea     isr_stub_table(%rip), %r12  /* R12 = stub pointer table */
    xor     %ecx, %ecx                  /* ECX = vector counter (0-255) */

.Lidt_fill:
    /* Load handler address for this vector */
    mov     (%r12, %rcx, 8), %rax       /* RAX = isr_stub_table[ecx] */

    /* Calculate IDT entry address: base + vector * 16 */
    lea     (%rbx, %rcx, 8), %rdx
    lea     (%rdx, %rcx, 8), %rdx       /* RDX = idt_table + ecx * 16 */

    /* Encode IDT gate descriptor */
    movw    %ax, (%rdx)                 /* offset_low  = handler[15:0] */
    movw    $0x08, 2(%rdx)              /* selector    = kernel CS */
    movb    $0x00, 4(%rdx)              /* IST         = 0 */
    movb    $0x8E, 5(%rdx)              /* type_attr   = interrupt gate, DPL=0 */
    shr     $16, %rax
    movw    %ax, 6(%rdx)               /* offset_mid  = handler[31:16] */
    shr     $16, %rax
    movl    %eax, 8(%rdx)              /* offset_high = handler[63:32] */
    movl    $0, 12(%rdx)               /* reserved    = 0 */

    inc     %ecx
    cmp     $256, %ecx
    jb      .Lidt_fill

    /* Load the IDT */
    lidt    idt_pointer(%rip)

    pop     %r12
    pop     %rbx
    ret


/* =============================================================================
 * IDT Data
 * =============================================================================
 */

.section ".bss"
.align 16

/* 256 entries × 16 bytes = 4096 bytes (exactly one page) */
.global idt_table
idt_table:
    .skip   256 * 16

.section ".data"
.align 8

idt_pointer:
    .word   (256 * 16) - 1              /* Limit (size - 1 = 4095) */
    .quad   idt_table                    /* Base address */


/* =============================================================================
 * Weak Default C Handlers (can be overridden)
 * =============================================================================
 * These provide default implementations that just halt.
 * The kernel can override them with real implementations.
 *
 * Same pattern as ARM64's weak handle_sync_exception / handle_irq.
 */

.section ".text"

.weak handle_exception
.weak handle_irq
.weak handle_unhandled

handle_exception:
handle_irq:
handle_unhandled:
1:  hlt
    jmp     1b