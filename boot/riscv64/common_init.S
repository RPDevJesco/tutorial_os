// =============================================================================
// common_init.S — RISC-V 64-bit Common Initialization
// =============================================================================
//
// Called from entry.S after hart parking and SBI hello. This file handles
// the "boring but essential" setup that every OS needs before C code can run.
// It's the RISC-V equivalent of boot/arm64/common_init.S.
//
// On entry:
//   - a0 = hart ID (0, from entry.S)
//   - a1 = DTB pointer (from U-Boot, saved/restored by entry.S)
//   - We're on the boot hart, in S-mode, interrupts disabled
//   - No stack yet! We can't call functions until sp is set up
//
// Responsibilities:
//   1. Set up the stack pointer
//   2. Zero the BSS section
//   3. Install the trap vector (stvec)
//   4. Store boot parameters to global variables
//   5. Call kernel_main()
//
// ARM64 common_init.S comparison:
//   ARM64 also: enables FP/SIMD (cpacr_el1), which RISC-V doesn't need
//   here because the F/D extensions are always available in S-mode when
//   the sstatus.FS field is set (we do that in step 3).
//
// =============================================================================

.section ".text"
.global common_init

common_init:
    // -----------------------------------------------------------------
    // Step 1: Set up the stack pointer
    // -----------------------------------------------------------------
    // The stack grows DOWNWARD on RISC-V (high → low addresses), same
    // as ARM64, x86, and almost every other architecture. We load sp
    // with the TOP of the stack region from the linker script.
    //
    // RISC-V calling convention requires 16-byte stack alignment.
    // The linker script guarantees _stack_top is 16-byte aligned via
    // the ALIGN(16) directive on the .stack section.
    //
    // CRITICAL: Until this instruction completes, we cannot use `call`,
    // `jal`, or any instruction that pushes to the stack. That's why
    // entry.S uses `j common_init` (a jump, not a call) to get here.
    //
    la      sp, _stack_top

    // Save boot parameters to callee-saved registers again.
    // We need them to survive the BSS clearing loop and function calls.
    mv      s0, a0                  // s0 = hart ID
    mv      s1, a1                  // s1 = DTB pointer

    // -----------------------------------------------------------------
    // Step 2: Zero the BSS section
    // -----------------------------------------------------------------
    // BSS (Block Started by Symbol) contains uninitialized global and
    // static variables. The C standard guarantees these are zero, but
    // the hardware doesn't do it — our kernel image only has .text,
    // .rodata, and .data. BSS is just a size marker in the ELF.
    //
    // We fill it with zeros using doubleword (8-byte) stores for
    // efficiency on 64-bit RISC-V. The linker script guarantees both
    // __bss_start and __bss_end are 8-byte aligned.
    //
    // ARM64 equivalent uses `stp xzr, xzr, [x0], #16` to zero 16
    // bytes per iteration. We do 8 bytes per iteration — simpler and
    // RISC-V doesn't have a store-pair instruction anyway.
    //
    la      t0, __bss_start         // t0 = current pointer
    la      t1, __bss_end           // t1 = end boundary
    bgeu    t0, t1, .Lbss_done      // Skip if BSS is empty

.Lbss_clear:
    sd      zero, 0(t0)             // *t0 = 0  (store doubleword)
    addi    t0, t0, 8               // t0 += 8
    bltu    t0, t1, .Lbss_clear     // while (t0 < t1)

.Lbss_done:

    // -----------------------------------------------------------------
    // Step 3: Enable floating-point and install trap vector
    // -----------------------------------------------------------------
    // RISC-V floating-point state is controlled by the FS field in the
    // sstatus CSR (bits 14:13). By default it may be "Off" (0), which
    // causes an illegal-instruction trap on any FP operation. We set it
    // to "Initial" (1) to enable FP without triggering context-save
    // overhead.
    //
    // ARM64 equivalent: writing to cpacr_el1 to enable FP/SIMD.
    //
    // sstatus.FS values:
    //   0 = Off (FP instructions trap)
    //   1 = Initial (FP enabled, clean state)
    //   2 = Clean (FP enabled, no modifications since last save)
    //   3 = Dirty (FP enabled, modified)
    //
    li      t0, (1 << 13)           // FS = Initial (bits 14:13 = 01)
    csrs    sstatus, t0             // Set the FS bits (OR into sstatus)

    // Install the trap vector. The stvec CSR tells the CPU where to
    // jump when a trap (exception or interrupt) occurs.
    //
    // We use Direct mode (stvec[1:0] = 00): ALL traps go to a single
    // handler entry point, which then dispatches based on scause.
    //
    // ARM64 equivalent: writing to vbar_el1 to set the exception
    // vector base address. ARM64 uses a table of 16 entries × 0x80
    // bytes each; RISC-V uses either direct (one handler) or vectored
    // (one entry per interrupt cause) mode.
    //
    la      t0, trap_vector
    csrw    stvec, t0               // All traps → trap_vector

    // -----------------------------------------------------------------
    // Step 4: Store boot parameters for C code
    // -----------------------------------------------------------------
    // Save hart ID and DTB pointer to global variables. C code will
    // need these — especially the DTB pointer, which contains the
    // SimpleFB framebuffer info that U-Boot injected.
    //
    // The DTB tells us:
    //   - Framebuffer physical address, width, height, stride, format
    //   - Memory regions (how much RAM we have)
    //   - Peripheral descriptions (for future driver use)
    //
    la      t0, __dtb_ptr
    sd      s1, 0(t0)               // __dtb_ptr = DTB address

    la      t0, __boot_hart_id
    sd      s0, 0(t0)               // __boot_hart_id = 0

    // -----------------------------------------------------------------
    // Step 5: Jump to C code
    // -----------------------------------------------------------------
    // Pass arguments matching kernel_main(dtb_ptr, ram_base, ram_size).
    // This matches the ARM64 convention so the SAME main.c works on
    // both architectures without #ifdefs.
    //
    //   void kernel_main(uintptr_t dtb_ptr, uintptr_t ram_base, uintptr_t ram_size);
    //
    // On ARM64, entry.S queries RAM via the VideoCore mailbox.
    // On RISC-V, we pass hardcoded values from the platform spec.
    // The DTB also has memory info, but parsing FDT in assembly would
    // be unnecessarily complex — the HAL handles details in C.
    //
    mv      a0, s1                  // a0 = DTB pointer
    li      a1, 0x0                 // a1 = RAM base (0x0 on Ky X1)
    li      a2, 0x100000000         // a2 = RAM size (4 GB)
    call    kernel_main

    // -----------------------------------------------------------------
    // Halt: kernel_main should never return
    // -----------------------------------------------------------------
    // If we get here, something went wrong. Print '!' as a distress
    // signal and halt. On ARM64, the equivalent is `wfe; b halt`.
    //
    li      a7, 1                   // SBI_PUTCHAR
    li      a0, '!'                 // '!' = kernel returned (bad!)
    ecall

.Lhalt:
    wfi
    j       .Lhalt

// =============================================================================
// Data Section — Boot Parameters
// =============================================================================
// These globals are written during init and read by C code throughout the
// kernel's lifetime. They live in .data (not .bss) because .bss gets zeroed
// by this very code — we'd overwrite them if they were in .bss!
//
// Actually, we write them AFTER zeroing BSS, so .bss would be fine too.
// But keeping them in .data makes the dependency explicit and safe against
// future reordering. Belt and suspenders.

.section ".data"

// DTB pointer — address of the flattened device tree passed by U-Boot.
// Contains SimpleFB framebuffer info, memory regions, and peripheral
// descriptions. This is the single most important value from boot.
.balign 8
.global __dtb_ptr
__dtb_ptr:
    .quad 0

// Boot hart ID — which core we're running on (usually 0).
// Useful for SMP awareness and diagnostic output.
.balign 8
.global __boot_hart_id
__boot_hart_id:
    .quad 0