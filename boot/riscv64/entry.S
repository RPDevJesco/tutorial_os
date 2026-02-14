// =============================================================================
// entry.S — RISC-V 64-bit Entry Point for Tutorial-OS
// =============================================================================
//
// This is the FIRST code that executes when U-Boot's `booti` jumps to our
// kernel image. It includes a 64-byte RISC-V Linux image header that
// booti validates, followed by the actual entry point code.
//
// What happens before we get here:
//   1. BootROM loads FSBL (SPL) from SPI NOR or eMMC
//   2. SPL initializes DDR and loads OpenSBI + U-Boot as a FIT image
//   3. OpenSBI takes ownership of M-mode (machine mode)
//   4. U-Boot runs in S-mode, initializes display (SimpleFB), etc.
//   5. U-Boot does `booti` — validates our image header and jumps here
//
// When we arrive:
//   - a0 = Hart ID (which CPU core, 0-7 on the 8-core Ky X1)
//   - a1 = DTB pointer (physical address of the flattened device tree)
//   - We're in S-mode (supervisor mode) — OpenSBI owns M-mode above us
//   - All 8 harts may be running — we must park 7 of them
//   - Interrupts are disabled (sstatus.SIE = 0)
//   - The MMU is off (satp = 0) — we're using physical addresses
//
// Responsibilities (this file only):
//   1. Park non-primary harts
//   2. Save boot parameters (hart ID, DTB pointer)
//   3. Print first sign of life via SBI ecall
//   4. Jump to common_init for the rest of setup
//
// RISC-V vs ARM64 — What's Different Here:
//   ARM64 entry.S: Core parking → EL3→EL2→EL1 privilege drop → common_init
//   RISC-V entry.S: Hart parking → (no privilege drop needed!) → common_init
//
//   The ARM64 version spends ~40 lines dropping exception levels because
//   the Pi's GPU firmware enters at EL3. On RISC-V, OpenSBI already put
//   us in S-mode, so we skip straight to initialization. This makes our
//   entry.S significantly simpler.
//
// =============================================================================

.section ".text.boot"
.global _start

// =============================================================================
// SBI Constants
// =============================================================================
// The Supervisor Binary Interface (SBI) is how S-mode talks to M-mode
// (OpenSBI). Think of it as RISC-V's equivalent of ARM's SMC calls or
// the Pi's VideoCore mailbox — a standardized firmware API.
//
// We use the legacy putchar extension for early debug output. This works
// because OpenSBI already initialized the UART (the kernel command line
// says earlycon=sbi, meaning OpenSBI is the console provider).
//
.equ SBI_PUTCHAR,       0x01    // Legacy putchar: a7=1, a0=character

// =============================================================================
// RISC-V Linux Image Header (64 bytes)
// =============================================================================
// U-Boot's `booti` command expects this header to validate the image.
// Without it, booti rejects us with "Bad Linux RISCV Image magic!".
//
// The header format matches Linux's arch/riscv/include/asm/image.h:
//   Offset  Size  Field
//   0x00    4     code0        — First instruction (jump past header)
//   0x04    4     code1        — Reserved (zero)
//   0x08    8     text_offset  — Load offset from RAM base
//   0x10    8     image_size   — Effective image size (0 = unknown)
//   0x18    8     flags        — Kernel flags
//   0x20    4     version      — Header version (minor:major)
//   0x24    4     res1         — Reserved
//   0x28    8     res2         — Reserved
//   0x30    8     magic        — "RISCV\0\0\0" (validates the image)
//   0x38    4     magic2       — "RSC\x05" (PE header compat)
//   0x3C    4     res3         — Reserved
//
// Why `booti` matters: Unlike `go` (which just jumps raw), `booti`
// properly passes a0=hart_id and a1=DTB pointer, matching the Linux
// boot protocol that our entry code expects.
//
// =============================================================================

// =============================================================================
// Entry Point
// =============================================================================

// Disable compressed instructions for the header — the jump MUST be
// exactly 4 bytes, or all offsets shift and booti can't find the magic.
.option norvc

_start:
    // code0: Jump past the 64-byte header to real code
    j       _real_start             // 4 bytes: offset +64

    // code1: Reserved
    .word   0                       // offset 0x04

    // text_offset: Where to load relative to RAM base (2MB aligned)
    .dword  0x200000                // offset 0x08

    // image_size: Size of image. Must be non-zero for some U-Boot builds.
    .dword  0x100000                // offset 0x10  (1MB, safe upper bound)

    // flags: 0 = little-endian, MMU off
    .dword  0                       // offset 0x18

    // version: header version 0.2
    .word   0x0002                  // offset 0x20

    // res1
    .word   0                       // offset 0x24

    // res2
    .dword  0                       // offset 0x28

    // magic: "RISCV\0\0\0" — THE critical field booti checks
    .ascii  "RISCV\000\000\000"     // offset 0x30

    // magic2: PE-compatible magic "RSC\x05"
    .byte   'R', 'S', 'C', 0x05    // offset 0x38

    // res3
    .word   0                       // offset 0x3C

// =============================================================================
// Real Entry (after header)
// =============================================================================
// When booti jumps here:
//   a0 = Hart ID (0 on the boot core)
//   a1 = DTB pointer (device tree with SimpleFB node)
//
// Re-enable compressed instructions for the rest of the code
.option rvc

_real_start:
    // -----------------------------------------------------------------
    // Step 1: Park non-primary harts
    // -----------------------------------------------------------------
    // The Ky X1 has 8 harts (cores 0-7). We only want ONE hart to do
    // initialization — the others must wait. This is the same concept
    // as ARM64's `mrs x1, mpidr_el1; and x1, x1, #3; cbnz x1, park`
    // but simpler: the hart ID arrives in a0 as a gift from booti.
    //
    // We designate hart 0 as the boot hart. All others branch to a
    // wfi (wait-for-interrupt) loop and sleep until we wake them.
    //
    // Note: We can't assume hart 0 is always the boot hart on every
    // RISC-V platform, but on the Ky X1, U-Boot boots on hart 0.
    //
    bnez    a0, .Lpark_hart         // if (hart_id != 0) goto sleep

    // -----------------------------------------------------------------
    // Step 2: Save boot parameters to callee-saved registers
    // -----------------------------------------------------------------
    // a0 and a1 are caller-saved (argument) registers — any function
    // call or ecall will clobber them. We save to s0/s1 which are
    // callee-saved and survive across calls.
    //
    // ARM64 equivalent: mov x19, x0 (saving DTB pointer to a
    // callee-saved register before doing mailbox queries).
    //
    // s1 (DTB pointer) is especially critical — U-Boot injected a
    // `simple-framebuffer` node with the display address/resolution.
    // Losing this pointer means no display without writing a DPU driver.
    //
    mv      s0, a0                  // s0 = hart ID (0)
    mv      s1, a1                  // s1 = DTB pointer

    // -----------------------------------------------------------------
    // Step 3: First sign of life — SBI putchar
    // -----------------------------------------------------------------
    // Before touching memory, stack, or anything complex, we emit a
    // single character via SBI. If you see 'T' on the serial console,
    // you know the entire chain works:
    //   ✓ U-Boot loaded the image to the right address
    //   ✓ The entry point is correct
    //   ✓ We're executing RISC-V instructions
    //   ✓ The SBI ecall interface to OpenSBI is functional
    //
    // This is the bare-metal equivalent of printf("I'm alive!") and
    // it's the single most important debug technique in OS development.
    //
    // SBI legacy putchar: a7 = extension ID (1), a0 = character
    // OpenSBI handles the actual UART register poking in M-mode.
    //
    li      a7, SBI_PUTCHAR
    li      a0, 'T'                 // 'T' for Tutorial-OS
    ecall

    // -----------------------------------------------------------------
    // Step 4: Jump to common initialization
    // -----------------------------------------------------------------
    // Restore boot parameters to a0/a1 (common_init expects them) and
    // jump to the shared init code that sets up stack, clears BSS, and
    // eventually calls kernel_main.
    //
    mv      a0, s0                  // a0 = hart ID
    mv      a1, s1                  // a1 = DTB pointer
    j       common_init             // Tail call — never returns

// =============================================================================
// Secondary Hart Parking
// =============================================================================
// Non-primary harts spin here in a low-power wait state. The `wfi`
// instruction puts the core to sleep until an interrupt arrives.
//
// On ARM64, parked cores use `wfe` (wait for event) which can be woken
// by `sev` (send event) from another core. RISC-V doesn't have wfe/sev —
// `wfi` is the closest equivalent, woken by any interrupt including IPIs
// (inter-processor interrupts) sent via the CLINT at 0xE4000000.
//
// Future SMP wake-up sequence:
//   1. Primary hart writes a function pointer to a shared variable
//   2. Primary hart sends IPI to target hart via CLINT
//   3. Parked hart wakes from wfi, reads the function pointer, jumps to it
//
// For now, they just sleep forever. Single-core is fine for education.
//
.Lpark_hart:
    wfi
    j       .Lpark_hart