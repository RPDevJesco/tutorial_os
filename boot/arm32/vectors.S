/*
 * vectors.S — Cortex-M33 Vector Table and Reset Handler
 * =======================================================
 *
 * Target: RP2350 (Raspberry Pi Pico 2, ARM Cortex-M33)
 *
 * THE SIMPLEST BOOT SEQUENCE IN TUTORIAL-OS
 * ==========================================
 *
 * Compare the boot sequences across architectures:
 *
 *   ARM64 (Pi):
 *     1. GPU firmware loads kernel to 0x80000
 *     2. All 4 cores start executing at 0x80000
 *     3. boot_soc.S parks secondary cores (spin on mailbox)
 *     4. Primary core drops from EL2 to EL1 (privilege level)
 *     5. Sets up MMU, exception vectors
 *     6. Zeroes BSS, sets stack, calls kernel_main
 *
 *   RISC-V (Ky X1, JH7110):
 *     1. OpenSBI runs in M-mode, drops to S-mode
 *     2. U-Boot loads kernel to RAM address
 *     3. entry.S parks secondary harts (spin on WFI)
 *     4. common_init.S sets up FP, stvec, BSS, stack
 *     5. Passes DTB pointer, calls kernel_main
 *
 *   x86_64 (LattePanda):
 *     1. UEFI firmware initializes everything
 *     2. efi_main() is called as a C function — no assembly at all
 *
 *   Cortex-M33 (THIS FILE):
 *     1. ROM bootloader runs boot2 (configures flash XIP)
 *     2. Hardware reads vector_table[0] → sets SP
 *     3. Hardware reads vector_table[1] → jumps to Reset_Handler
 *     4. Reset_Handler copies .data, zeroes .bss, calls kernel_main
 *     DONE. No privilege transitions, no secondary core parking,
 *     no MMU setup, no firmware handoff protocol.
 *
 * WHY IS IT SO SIMPLE?
 *   Cortex-M has no privilege levels to configure (everything runs in
 *   Thread mode or Handler mode — both have full access by default).
 *   There's no MMU to set up. The second core starts in DORMANT state
 *   and stays there until we explicitly wake it. The hardware handles
 *   the SP and PC initialization from the vector table automatically.
 *
 * VECTOR TABLE FORMAT (ARM Architecture Reference Manual):
 *   Offset  Name           Description
 *   0x00    Initial SP     Value loaded into SP register at reset
 *   0x04    Reset          Address of Reset_Handler
 *   0x08    NMI            Non-Maskable Interrupt handler
 *   0x0C    HardFault      Hard fault handler
 *   0x10    MemManage      Memory management fault (MPU violations)
 *   0x14    BusFault       Bus error (bad address / alignment)
 *   0x18    UsageFault     Undefined instruction, etc.
 *   0x1C    SecureFault    Security fault (TrustZone — ARMv8-M new)
 *   0x20-28 Reserved
 *   0x2C    SVCall         Supervisor call (SVC instruction)
 *   0x30    DebugMon       Debug monitor
 *   0x34    Reserved
 *   0x38    PendSV         Pending supervisor call
 *   0x3C    SysTick        System tick timer interrupt
 *   0x40+   IRQ0-51        Peripheral interrupts (RP2350 has 52)
 *
 * NOTE: SecureFault at offset 0x1C is new in ARMv8-M (Cortex-M33).
 * The Cortex-M0/M3/M4 have a "Reserved" entry there instead.
 * This is one of the few differences between RP2040 (M0+) and
 * RP2350 (M33) at the vector table level.
 */

    .syntax unified         /* Use unified ARM/Thumb syntax */
    .cpu cortex-m33
    .fpu fpv5-sp-d16
    .thumb                  /* All Cortex-M code is Thumb */

/* =============================================================================
 * VECTOR TABLE
 * =============================================================================
 *
 * Placed in .vectors section, which the linker script puts right after boot2
 * at flash offset 0x100. The 512-byte alignment satisfies the Cortex-M33
 * VTOR (Vector Table Offset Register) requirements.
 *
 * IMPORTANT: These are DATA entries (addresses), not code. The .word
 * directive emits 32-bit values. The Cortex-M33 hardware reads these
 * addresses and jumps to them.
 *
 * The +1 on handler addresses is NOT present here because we use .thumb_func
 * which tells the assembler to set bit 0 automatically. Bit 0 = 1 means
 * "this is a Thumb address" (required by ARM — jumping to an even address
 * on Cortex-M causes a HardFault).
 */

    .section .vectors, "a"
    .align 9                /* 512-byte alignment (2^9 = 512) */
    .globl __vector_table
__vector_table:
    /* Exception 0: Initial SP — hardware loads this into SP at reset */
    .word   _stack_top

    /* Exception 1: Reset — first instruction after boot */
    .word   Reset_Handler

    /* Exception 2: NMI — Non-Maskable Interrupt */
    .word   NMI_Handler

    /* Exception 3: HardFault — unrecoverable error */
    .word   HardFault_Handler

    /* Exception 4: MemManage — MPU violation */
    .word   MemManage_Handler

    /* Exception 5: BusFault — bus error */
    .word   BusFault_Handler

    /* Exception 6: UsageFault — undefined instruction, etc. */
    .word   UsageFault_Handler

    /* Exception 7: SecureFault — TrustZone violation (ARMv8-M only) */
    .word   SecureFault_Handler

    /* Exceptions 8-10: Reserved */
    .word   0
    .word   0
    .word   0

    /* Exception 11: SVCall — supervisor call (SVC instruction) */
    .word   SVCall_Handler

    /* Exception 12: DebugMonitor */
    .word   DebugMon_Handler

    /* Exception 13: Reserved */
    .word   0

    /* Exception 14: PendSV — deferred supervisor call */
    .word   PendSV_Handler

    /* Exception 15: SysTick — system tick timer */
    .word   SysTick_Handler

    /*
     * IRQ 0-51: RP2350 peripheral interrupts
     *
     * For now, all IRQs point to Default_Handler (infinite loop).
     * As we add drivers, we'll replace specific entries with real handlers.
     *
     * Notable IRQs we'll eventually use:
     *   IRQ 5  = SPI0
     *   IRQ 20 = UART0
     *   IRQ 21 = UART1
     *   IRQ 0-3 = Timer alarms
     */
    .word   Default_Handler     /* IRQ  0: TIMER0_IRQ_0 */
    .word   Default_Handler     /* IRQ  1: TIMER0_IRQ_1 */
    .word   Default_Handler     /* IRQ  2: TIMER0_IRQ_2 */
    .word   Default_Handler     /* IRQ  3: TIMER0_IRQ_3 */
    .word   Default_Handler     /* IRQ  4: TIMER1_IRQ_0 */
    .word   Default_Handler     /* IRQ  5: TIMER1_IRQ_1 */
    .word   Default_Handler     /* IRQ  6: TIMER1_IRQ_2 */
    .word   Default_Handler     /* IRQ  7: TIMER1_IRQ_3 */
    .word   Default_Handler     /* IRQ  8: PWM_IRQ_WRAP_0 */
    .word   Default_Handler     /* IRQ  9: PWM_IRQ_WRAP_1 */
    .word   Default_Handler     /* IRQ 10: DMA_IRQ_0 */
    .word   Default_Handler     /* IRQ 11: DMA_IRQ_1 */
    .word   Default_Handler     /* IRQ 12: DMA_IRQ_2 */
    .word   Default_Handler     /* IRQ 13: DMA_IRQ_3 */
    .word   Default_Handler     /* IRQ 14: USBCTRL_IRQ */
    .word   Default_Handler     /* IRQ 15: PIO0_IRQ_0 */
    .word   Default_Handler     /* IRQ 16: PIO0_IRQ_1 */
    .word   Default_Handler     /* IRQ 17: PIO1_IRQ_0 */
    .word   Default_Handler     /* IRQ 18: PIO1_IRQ_1 */
    .word   Default_Handler     /* IRQ 19: PIO2_IRQ_0 */
    .word   Default_Handler     /* IRQ 20: PIO2_IRQ_1 */
    .word   Default_Handler     /* IRQ 21: IO_IRQ_BANK0 */
    .word   Default_Handler     /* IRQ 22: IO_IRQ_BANK0_NS */
    .word   Default_Handler     /* IRQ 23: IO_IRQ_QSPI */
    .word   Default_Handler     /* IRQ 24: IO_IRQ_QSPI_NS */
    .word   Default_Handler     /* IRQ 25: SIO_IRQ_FIFO */
    .word   Default_Handler     /* IRQ 26: SIO_IRQ_BELL */
    .word   Default_Handler     /* IRQ 27: SIO_IRQ_FIFO_NS */
    .word   Default_Handler     /* IRQ 28: SIO_IRQ_BELL_NS */
    .word   Default_Handler     /* IRQ 29: SIO_IRQ_MTIMECMP */
    .word   Default_Handler     /* IRQ 30: CLOCKS_IRQ */
    .word   Default_Handler     /* IRQ 31: SPI0_IRQ */
    .word   Default_Handler     /* IRQ 32: SPI1_IRQ */
    .word   Default_Handler     /* IRQ 33: UART0_IRQ */
    .word   Default_Handler     /* IRQ 34: UART1_IRQ */
    .word   Default_Handler     /* IRQ 35: ADC_IRQ_FIFO */
    .word   Default_Handler     /* IRQ 36: I2C0_IRQ */
    .word   Default_Handler     /* IRQ 37: I2C1_IRQ */
    .word   Default_Handler     /* IRQ 38: OTP_IRQ */
    .word   Default_Handler     /* IRQ 39: TRNG_IRQ */
    .word   Default_Handler     /* IRQ 40: PROC0_IRQ_CTI */
    .word   Default_Handler     /* IRQ 41: PROC1_IRQ_CTI */
    .word   Default_Handler     /* IRQ 42: PLL_SYS_IRQ */
    .word   Default_Handler     /* IRQ 43: PLL_USB_IRQ */
    .word   Default_Handler     /* IRQ 44: POWMAN_IRQ_POW */
    .word   Default_Handler     /* IRQ 45: POWMAN_IRQ_TIMER */
    .word   Default_Handler     /* IRQ 46: SPARE_IRQ_0 */
    .word   Default_Handler     /* IRQ 47: SPARE_IRQ_1 */
    .word   Default_Handler     /* IRQ 48: SPARE_IRQ_2 */
    .word   Default_Handler     /* IRQ 49: SPARE_IRQ_3 */
    .word   Default_Handler     /* IRQ 50: SPARE_IRQ_4 */
    .word   Default_Handler     /* IRQ 51: SPARE_IRQ_5 */


/* =============================================================================
 * RESET HANDLER — First code that runs after boot2
 * =============================================================================
 *
 * When the Cortex-M33 comes out of reset:
 *   1. Hardware loads SP from vector_table[0]     ← already done
 *   2. Hardware jumps to vector_table[1]          ← we're here now
 *
 * Our job:
 *   1. Copy .data from flash (LMA) to SRAM (VMA)
 *   2. Zero .bss in SRAM
 *   3. Initialize clocks (XOSC → PLL → clk_sys)
 *   4. Call kernel_main()
 *   5. If kernel_main returns, infinite loop
 *
 * COMPARISON WITH ARM64/RISC-V BOOT:
 *   - No privilege level transitions (no EL2→EL1, no M-mode→S-mode)
 *   - No secondary core parking (core 1 is in DORMANT, not executing)
 *   - No FPU enable needed (Cortex-M33 FPU is enabled via CPACR but
 *     the default out of reset allows lazy stacking — good enough)
 *   - No exception vector setup (hardware reads VTOR which defaults
 *     to 0x10000100 after boot2, exactly where our table lives)
 */

    .section .text, "ax"
    .thumb_func
    .globl Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:

    /*
     * Step 1: Copy .data section from flash (LMA) to SRAM (VMA)
     *
     * The linker places initial values for global variables in flash
     * (right after .rodata). We must copy them to SRAM where the code
     * expects to find them.
     *
     * Symbols from linker script:
     *   __data_lma_start  = source address in flash
     *   __data_start      = destination address in SRAM
     *   __data_end        = end of destination in SRAM
     */
    ldr     r0, =__data_start       /* dst (SRAM) */
    ldr     r1, =__data_lma_start   /* src (flash) */
    ldr     r2, =__data_end         /* dst end */

    cmp     r0, r2                  /* if dst == dst_end, nothing to copy */
    beq     .L_data_done

.L_data_copy:
    ldr     r3, [r1], #4            /* load word from flash, post-increment */
    str     r3, [r0], #4            /* store word to SRAM, post-increment */
    cmp     r0, r2
    blt     .L_data_copy

.L_data_done:

    /*
     * Step 2: Zero the .bss section in SRAM
     *
     * BSS holds uninitialized global variables. C guarantees they start
     * at zero. The linker doesn't put them in flash (that would waste
     * space), so we must zero them here.
     *
     * This is the SAME concept as every other Tutorial-OS boot sequence:
     *   ARM64:   common_init zeroes BSS with stp xzr,xzr loop
     *   RISC-V:  common_init.S zeroes BSS with sd zero loop
     *   x86_64:  UEFI firmware handles it (efi_main gets clean memory)
     *   CM33:    We do it right here with a str zero loop
     */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    movs    r2, #0

    cmp     r0, r1
    beq     .L_bss_done

.L_bss_zero:
    str     r2, [r0], #4
    cmp     r0, r1
    blt     .L_bss_zero

.L_bss_done:

    /*
     * Step 3: Enable FPU (Cortex-M33 with FPv5)
     *
     * The FPU is disabled by default on Cortex-M33. We need to enable
     * it before any C code that uses float (the compiler might generate
     * float instructions even for integer code if it decides to use
     * FPU registers for optimization).
     *
     * CPACR (Coprocessor Access Control Register) at 0xE000ED88:
     *   Bits 23:20 = CP10 and CP11 access
     *   0b11 = Full access (both privileged and unprivileged)
     *
     * After setting CPACR, we need a DSB + ISB to ensure the FPU is
     * actually enabled before we execute any float instructions.
     */
    ldr     r0, =0xE000ED88         /* CPACR address */
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)    /* CP10 + CP11 = full access */
    str     r1, [r0]
    dsb                              /* Data synchronization barrier */
    isb                              /* Instruction synchronization barrier */

    /*
     * Step 4: Call kernel_main()
     *
     * On application processors, kernel_main receives platform-specific
     * arguments (DTB pointer on RISC-V, nothing on x86_64, etc.).
     *
     * On the RP2350, we pass nothing — there's no DTB, no mailbox,
     * no firmware discovery protocol. The hardware is fixed and known
     * at compile time. This is the simplest kernel_main signature.
     *
     * TODO: Once soc_init.c is implemented, it will be called here
     * (or from kernel_main via hal_platform_init) to configure clocks,
     * bring peripherals out of reset, and set up UART.
     */
    bl      kernel_main

    /*
     * Step 5: If kernel_main returns, halt.
     *
     * On a real system, we might reset the chip. For now, spin forever.
     * WFI (Wait For Interrupt) puts the CPU in low-power state between
     * interrupts, which is good practice even in an infinite loop.
     */
.L_halt:
    wfi
    b       .L_halt


/* =============================================================================
 * DEFAULT EXCEPTION HANDLERS
 * =============================================================================
 *
 * Weak symbols — any C function with the same name will override these.
 * This is the standard Cortex-M pattern: provide safe defaults here,
 * let the application replace specific handlers as needed.
 *
 * For debugging, the HardFault_Handler is particularly useful — it tells
 * you that something went catastrophically wrong. A real implementation
 * would read the fault status registers (CFSR, HFSR, MMFAR, BFAR) and
 * print diagnostic information over UART.
 */

    .thumb_func
    .weak   NMI_Handler
    .type   NMI_Handler, %function
NMI_Handler:
    b       .

    .thumb_func
    .weak   HardFault_Handler
    .type   HardFault_Handler, %function
HardFault_Handler:
    b       .

    .thumb_func
    .weak   MemManage_Handler
    .type   MemManage_Handler, %function
MemManage_Handler:
    b       .

    .thumb_func
    .weak   BusFault_Handler
    .type   BusFault_Handler, %function
BusFault_Handler:
    b       .

    .thumb_func
    .weak   UsageFault_Handler
    .type   UsageFault_Handler, %function
UsageFault_Handler:
    b       .

    .thumb_func
    .weak   SecureFault_Handler
    .type   SecureFault_Handler, %function
SecureFault_Handler:
    b       .

    .thumb_func
    .weak   SVCall_Handler
    .type   SVCall_Handler, %function
SVCall_Handler:
    b       .

    .thumb_func
    .weak   DebugMon_Handler
    .type   DebugMon_Handler, %function
DebugMon_Handler:
    b       .

    .thumb_func
    .weak   PendSV_Handler
    .type   PendSV_Handler, %function
PendSV_Handler:
    b       .

    .thumb_func
    .weak   SysTick_Handler
    .type   SysTick_Handler, %function
SysTick_Handler:
    b       .

    .thumb_func
    .weak   Default_Handler
    .type   Default_Handler, %function
Default_Handler:
    b       .