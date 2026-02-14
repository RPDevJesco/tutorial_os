/*
 * boot/x86_64/common_init.S - Common Post-Platform Initialization
 * =================================================================
 *
 * This code runs AFTER platform_early_init has completed.
 * At this point:
 *   - RAM has been detected (stored in detected_ram_base/size)
 *   - Page tables have been built (or UEFI identity map is still active)
 *   - ACPI tables have been located
 *
 * This file handles the GENERIC tasks:
 *   - Set up Interrupt Descriptor Table (IDT)
 *   - Set up stack
 *   - Clear BSS
 *   - Enable SSE/SSE2 (and AVX if available)
 *   - Jump to kernel_main
 *
 * CALLING CONVENTION:
 * -------------------
 * platform_early_init should jump here with:
 *   RBX = boot_info pointer (preserved from entry)
 */

.section ".text"
.code64

.global boot_continue
boot_continue:
    /*
     * At this point, platform_early_init has:
     *   - Stored RAM base in detected_ram_base
     *   - Stored RAM size in detected_ram_size
     *   - Built and possibly loaded new page tables
     *
     * RBX still contains the boot_info pointer from entry.
     */


    /* =========================================================================
     * Set up Interrupt Descriptor Table (IDT)
     * =========================================================================
     * Build and load the IDT, which is the x86_64 equivalent of ARM64's
     * VBAR_EL1 exception vector table.
     *
     * ARM64: Single instruction (MSR VBAR_EL1, <addr>) loads a fixed-layout
     *        table of 16 branch targets, each at 0x80-byte intervals.
     *
     * x86_64: The IDT is 256 entries × 16 bytes = 4KB. Each entry encodes
     *         the handler address split across three fields plus attributes.
     *         We must build it at runtime, then load with LIDT.
     *
     * setup_idt (in vectors.S) populates the IDT from the ISR stub table
     * and loads it via LIDT.
     */
    call    setup_idt


    /* =========================================================================
     * Set up Stack
     * =========================================================================
     * Stack grows downward, so RSP points to the TOP of the stack region.
     * _stack_top is defined by the linker script.
     *
     * Identical concept to ARM64 (ldr x0, =_stack_top; mov sp, x0).
     */
    lea     _stack_top(%rip), %rsp


    /* =========================================================================
     * Clear BSS
     * =========================================================================
     * Zero-initialize the BSS section (uninitialized global variables).
     * __bss_start and __bss_end are defined by the linker script.
     *
     * We use REP STOSQ to write 8 bytes at a time for efficiency.
     * The linker script should ensure BSS is 8-byte aligned.
     *
     * ARM64 equivalent used STP (16 bytes/iter); REP STOSQ is the x86
     * idiom for bulk zeroing and is heavily optimized in microcode.
     */
    lea     __bss_start(%rip), %rdi /* Destination */
    lea     __bss_end(%rip), %rcx
    sub     %rdi, %rcx              /* RCX = byte count */
    shr     $3, %rcx                /* Convert to QWORD count */
    xor     %eax, %eax              /* Value to store: zero */
    rep stosq


    /* =========================================================================
     * Enable SSE/SSE2
     * =========================================================================
     * By default, SSE instructions will #UD or #NM if not enabled.
     * SSE2 is guaranteed on all x86_64 CPUs (it's part of the spec).
     *
     * This is the x86_64 equivalent of ARM64's CPACR_EL1 FP/SIMD enable:
     *   ARM64:  mov x0, #(3 << 20); msr cpacr_el1, x0
     *   x86_64: CR0.EM=0, CR0.MP=1, CR4.OSFXSR=1, CR4.OSXMMEXCPT=1
     *
     * CR0 flags:
     *   Bit 1 (MP)  = 1: Monitor Coprocessor (proper #NM behavior)
     *   Bit 2 (EM)  = 0: No FPU emulation (MUST be clear for SSE)
     *   Bit 3 (TS)  = 0: No task switch pending (clear lazy FPU trap)
     *
     * CR4 flags:
     *   Bit 9  (OSFXSR)      = 1: OS supports FXSAVE/FXRSTOR
     *   Bit 10 (OSXMMEXCPT)  = 1: OS handles #XM (SIMD exceptions)
     */
    mov     %cr0, %rax
    and     $~(1 << 2), %rax       /* Clear EM (no FPU emulation) */
    and     $~(1 << 3), %rax       /* Clear TS (no task-switch trap) */
    or      $(1 << 1), %rax        /* Set MP (monitor coprocessor) */
    mov     %rax, %cr0

    mov     %cr4, %rax
    or      $(1 << 9), %rax        /* Set OSFXSR */
    or      $(1 << 10), %rax       /* Set OSXMMEXCPT */
    mov     %rax, %cr4


    /* =========================================================================
     * Enable AVX (if supported)
     * =========================================================================
     * AVX requires XSAVE support and explicit opt-in via XCR0.
     * We check CPUID first - not all x86_64 CPUs have AVX.
     *
     * If AVX is not available, SSE2 is still enabled (which is fine -
     * SSE2 is mandatory on x86_64).
     */

    /* Check CPUID.1:ECX bit 26 (XSAVE) and bit 28 (AVX) */
    mov     $1, %eax
    cpuid
    test    $(1 << 26), %ecx       /* XSAVE support? */
    jz      .Lno_avx
    test    $(1 << 28), %ecx       /* AVX support? */
    jz      .Lno_avx

    /* Enable XSAVE in CR4 (bit 18: OSXSAVE) */
    mov     %cr4, %rax
    or      $(1 << 18), %rax       /* Set OSXSAVE */
    mov     %rax, %cr4

    /*
     * Set XCR0 to enable SSE and AVX state saving.
     * XCR0 bits:
     *   Bit 0: x87 FPU state (must be 1)
     *   Bit 1: SSE state (XMM registers)
     *   Bit 2: AVX state (YMM upper halves)
     */
    xor     %ecx, %ecx             /* XCR0 index = 0 */
    xgetbv                          /* Read current XCR0 → EDX:EAX */
    or      $0x07, %eax             /* Enable x87 + SSE + AVX */
    xsetbv                          /* Write XCR0 */

.Lno_avx:


    /* =========================================================================
     * Jump to Kernel
     * =========================================================================
     * Call kernel_main with parameters (System V AMD64 ABI):
     *   RDI = boot_info pointer
     *   RSI = RAM base address
     *   RDX = RAM size
     *
     * ARM64 equivalent: x0=DTB, x1=ram_base, x2=ram_size; bl kernel_main
     */
    mov     %rbx, %rdi              /* Boot info pointer */

    lea     detected_ram_base(%rip), %rax
    mov     (%rax), %rsi            /* RAM base */

    lea     detected_ram_size(%rip), %rax
    mov     (%rax), %rdx            /* RAM size */

    call    kernel_main


    /* =========================================================================
     * Park Core (kernel returned)
     * =========================================================================
     * If kernel_main ever returns, park the CPU in a low-power loop.
     * HLT halts until the next interrupt (x86_64 equivalent of ARM64's WFE).
     * Since interrupts may be disabled, this may halt permanently - which
     * is fine, since kernel_main should never return.
     */
.Lpark:
    hlt
    jmp     .Lpark


/* =============================================================================
 * External Symbols
 * =============================================================================
 */

/* From linker script */
.extern __bss_start
.extern __bss_end
.extern _stack_top

/* From vectors.S */
.extern setup_idt

/* From platform boot code */
.extern detected_ram_base
.extern detected_ram_size

/* From kernel */
.extern kernel_main