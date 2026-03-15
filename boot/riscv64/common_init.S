// =============================================================================
// common_init.S — RISC-V 64-bit Common Initialization
// =============================================================================
//
// Called from entry.S after hart parking and SBI hello. This file handles
// the "boring but essential" setup that every OS needs before C code can run.
// It's the RISC-V equivalent of boot/arm64/common_init.S.
//
// On entry:
//   - a0 = hart ID (from entry.S / booti)
//   - a1 = DTB pointer (from U-Boot, saved/restored by entry.S)
//   - We're on the boot hart, in S-mode, interrupts disabled
//   - No stack yet! We can't call functions until sp is set up
//
// Responsibilities:
//   1. Set up the stack pointer
//   2. Zero the BSS section
//   3. Install the trap vector (stvec) and enable FP
//   4. Store boot parameters to global variables
//   5. Call kernel_main()
//
// CHANGE (kernel_main signature):
//   kernel_main no longer receives dtb_ptr/ram_base/ram_size as parameters.
//   Instead, ALL boot parameters are stored to canonical globals here in
//   Step 4, before the call. kernel_main reads from those globals when
//   needed. This makes kernel_main's signature uniform across all
//   architectures — void kernel_main(framebuffer_t *fb).
//
//   On ARM64 (BCM), detected_ram_base/detected_ram_size are aliased as
//   __ram_base/__ram_size in boot_soc.S. On RISC-V, we store the
//   compile-time constants directly to __ram_base/__ram_size here.
//
// BUG FIX (2025):
//   The original code placed the breadcrumb ecall BEFORE saving a0/a1
//   to callee-saved registers. RISC-V SBI calling convention uses a0
//   and a1 as argument registers — the ecall overwrites both. This meant
//   __dtb_ptr was always written as 0, causing fb_init() to fail silently
//   on every non-BCM platform (JH7110, KyX1).
//
//   Fix: mv s0, a0 / mv s1, a1 MUST come before the first ecall.
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

    // -----------------------------------------------------------------
    // CRITICAL: Save boot parameters to callee-saved registers NOW,
    // before ANY ecall, function call, or instruction that may clobber
    // a0/a1. The RISC-V SBI calling convention uses a0-a7 as argument
    // and return registers — an ecall will overwrite them.
    //
    // s0 = hart ID  (from a0, callee-saved, survives ecalls)
    // s1 = DTB ptr  (from a1, callee-saved, survives ecalls)
    //
    mv      s0, a0                  // Save hart ID before any ecall
    mv      s1, a1                  // Save DTB pointer before any ecall

    // -----------------------------------------------------------------
    // Step 2: Zero the BSS section
    // -----------------------------------------------------------------
    // BSS holds uninitialized global variables. The C standard requires
    // them to be zero at program start. On hosted systems the OS does
    // this; in bare-metal we do it ourselves.
    //
    // We use a simple byte-at-a-time loop for clarity. A production
    // kernel might use 8-byte stores (SD) for speed, but at boot time
    // BSS is small enough that simplicity wins.
    //
    la      t0, __bss_start
    la      t1, __bss_end
.Lclear_bss:
    bge     t0, t1, .Lbss_done
    sb      zero, 0(t0)
    addi    t0, t0, 1
    j       .Lclear_bss
.Lbss_done:

    // Breadcrumb: BSS cleared (SBI putchar, visible on UART before
    // the UART driver is initialized). '1'=entry, '2'=bss done.
    li      a7, 1
    li      a0, '2'
    ecall

    // -----------------------------------------------------------------
    // Step 3: Enable FP and install trap vector
    // -----------------------------------------------------------------
    // RISC-V sstatus.FS controls floating-point availability.
    // By default it may be "Off" (0), which causes an illegal-instruction
    // trap on any FP operation. We set it to "Initial" (1).
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
    // vector base address.
    //
    la      t0, trap_vector
    csrw    stvec, t0               // All traps → trap_vector

    li      a7, 1
    li      a0, '3'
    ecall

    // -----------------------------------------------------------------
    // Step 4: Store boot parameters to globals
    // -----------------------------------------------------------------
    // Write ALL boot parameters to their canonical global variables.
    // kernel_main reads from these globals — it no longer receives
    // any of this as function parameters.
    //
    // WHY GLOBALS INSTEAD OF PARAMETERS:
    //   On ARM64, RAM info comes from the VideoCore mailbox (runtime).
    //   On RISC-V, RAM info comes from compile-time SOC defines.
    //   On x86_64, RAM info comes from the UEFI memory map (runtime).
    //   Passing these as kernel_main parameters would force a different
    //   signature per architecture. Globals are the clean solution.
    //
    // NOTE: We store s0/s1 here, NOT a0/a1. The ecalls above
    // overwrote a0 and a1 — s0/s1 have held the correct values
    // since the very start of Step 1.
    //

    // __dtb_ptr: address of U-Boot's flattened device tree.
    // display_simplefb.c reads this to find the GOP/SimpleFB node.
    la      t0, __dtb_ptr
    sd      s1, 0(t0)               // __dtb_ptr = DTB address (from s1)

    // __boot_hart_id: which hart (core) we're running on.
    la      t0, __boot_hart_id
    sd      s0, 0(t0)               // __boot_hart_id = hart ID (from s0)

    // __ram_base: physical base address of DRAM, from SOC defines.
    // On RISC-V these are compile-time constants — not runtime queries
    // like the BCM mailbox. We write them to globals so kernel_main
    // can read them the same way regardless of architecture.
    la      t0, __ram_base
    li      t1, RAM_BASE
    sd      t1, 0(t0)               // __ram_base = RAM_BASE (e.g. 0x40000000)

    // __ram_size: total DRAM size available to the kernel.
    la      t0, __ram_size
    li      t1, RAM_SIZE
    sd      t1, 0(t0)               // __ram_size = RAM_SIZE (e.g. 0x40000000)

    li      a7, 1
    li      a0, '4'
    ecall

    // -----------------------------------------------------------------
    // Step 5: Jump to kernel_main
    // -----------------------------------------------------------------
    // kernel_main signature: void kernel_main(framebuffer_t *fb)
    //
    // We pass NULL (zero) for fb — kernel_main will call fb_init()
    // which routes to the platform-specific display driver. On ARM64
    // and RISC-V, fb_init() does the real work (mailbox/DTB).
    //
    // On x86_64, soc_init.c::efi_main() passes a real fb* because the
    // GOP framebuffer must be captured before ExitBootServices(). That
    // is the one architectural difference that is genuinely unavoidable
    // and is handled in soc_init.c, not here.
    //
    // ALL boot data (DTB, RAM base, RAM size) is now in globals.
    // The parameter list is clean and platform-uniform.
    //
    mv      a0, zero                // fb = NULL — kernel_main calls fb_init()
    call    kernel_main

    // -----------------------------------------------------------------
    // Halt: kernel_main should never return
    // -----------------------------------------------------------------
    // If we get here, something went wrong. Print '!' as a distress
    // signal and halt. On ARM64, the equivalent is `wfe; b halt`.
    //
    li      a7, 1
    li      a0, '!'                 // '!' = kernel returned (bad!)
    ecall

.Lhalt:
    wfi
    j       .Lhalt


// =============================================================================
// Data Section — Boot Parameters
// =============================================================================
// These globals are written during init (Step 4) and read by C code
// throughout the kernel's lifetime. They live in .data (not .bss) because
// .bss gets zeroed by this very code — writing them after zeroing is safe,
// but .data makes the dependency explicit.
//
// NAMING CONVENTION:
//   __dtb_ptr        — used by display_simplefb.c (RISC-V/ARM64 only)
//   __boot_hart_id   — used by SMP code if ever added
//   __ram_base       — used by kernel_main heap init
//   __ram_size       — used by kernel_main heap init
//
// On ARM64 (BCM), __ram_base and __ram_size are defined as aliases in
// boot_soc.S alongside detected_ram_base/detected_ram_size. The same
// names resolve to the same storage. C code uses only the __ prefixed
// names for portability.

.section ".data"

// DTB pointer — address of the flattened device tree passed by U-Boot.
// Contains SimpleFB framebuffer info, memory regions, and peripheral
// descriptions. Most important single value from boot.
.balign 8
.global __dtb_ptr
__dtb_ptr:
    .quad 0

// Boot hart ID — which core we're running on (usually 1 on JH7110,
// 0 on KyX1 — controlled by BOOT_HART_ID in soc.mk).
.balign 8
.global __boot_hart_id
__boot_hart_id:
    .quad 0

// RAM base address — physical start of DRAM accessible to the kernel.
// On RISC-V: populated from RAM_BASE compile-time constant (soc.mk).
// On ARM64: aliased from detected_ram_base in boot_soc.S (mailbox result).
.balign 8
.global __ram_base
__ram_base:
    .quad 0

// RAM size — total bytes of DRAM available to the kernel.
// On RISC-V: populated from RAM_SIZE compile-time constant (soc.mk).
// On ARM64: aliased from detected_ram_size in boot_soc.S (mailbox result).
.balign 8
.global __ram_size
__ram_size:
    .quad 0