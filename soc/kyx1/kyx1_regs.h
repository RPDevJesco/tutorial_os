/*
 * kyx1_regs.h — Ky X1 SoC Register Definitions
 * ===============================================
 *
 * This header defines ALL the peripheral base addresses and register offsets
 * for the Ky X1 (SpacemiT K1-derivative) SoC used on the Orange Pi RV2.
 *
 * Every address in this file comes from one of three verified sources:
 *   1. The vendor device tree (x1-orangepi-rv2.dts / x1.dtsi)
 *   2. The vendor U-Boot defconfig (x1_defconfig from v2022.10-ky branch)
 *   3. The Ky X1 datasheet (for SRAM/boot-ROM addresses)
 *
 * The Ky X1's peripheral range lives at 0xD40xxxxx, which is characteristic
 * of Marvell PXA/MMP-derived SoCs. This means legacy Marvell PXA datasheets
 * can partially inform register layouts — a useful research shortcut.
 *
 * COMPARISON WITH BCM2710 (Pi Zero 2W):
 *   BCM2710: All peripherals at 0x3F000000 + offset
 *   Ky X1:   Peripherals scattered across 0xC0000000-0xE4000000 range
 *            No single "PERIPHERAL_BASE" — each subsystem has its own base
 *
 * This file is the RISC-V equivalent of the BCM addresses in gpio.h and mmio.h.
 */

#ifndef KYX1_REGS_H
#define KYX1_REGS_H

#include "types.h"

/* =============================================================================
 * SYSTEM MEMORY MAP
 * =============================================================================
 *
 * Unlike the Pi where DRAM starts at 0x0 and peripherals are at 0x3F000000,
 * the Ky X1 has DRAM at 0x0 and peripherals much higher in the address space.
 *
 *   0x0000_0000 - 0x7FFF_FFFF  DRAM Bank 0 (2 GB)
 *   0x8000_0000 - 0xBFFF_FFFF  (gap or more DRAM)
 *   0xC000_0000 - 0xC0FF_FFFF  SRAM / Boot ROM / RCPU subsystem
 *   0xD400_0000 - 0xD4FF_FFFF  Main peripheral region (APB bus)
 *   0xD800_0000 - 0xD8FF_FFFF  Display subsystem (DPU, HDMI TX)
 *   0xE400_0000 - 0xE40F_FFFF  CLINT (Core Local Interruptor)
 *   0x1_0000_0000              DRAM Bank 1 (2 GB, high address)
 */

/* =============================================================================
 * UART REGISTERS
 * =============================================================================
 *
 * The Ky X1 has 10 UARTs, all PXA-compatible (Marvell PXA series layout).
 * NOT standard 16550, though similar. Key differences from 16550:
 *   - Register shift = 2 (registers are 4 bytes apart, not 1)
 *   - Register I/O width = 4 (32-bit accesses)
 *   - IER bit 6 is vendor-specific (UART unit enable)
 *
 * UART0 is the Linux console (serial0 / ttyS0 at 115200n8).
 * UART2 is also enabled (debug header on the schematic).
 *
 * For bare-metal, we start with SBI console (ecall to OpenSBI), then
 * optionally implement direct UART access for when we want to bypass SBI.
 */

/* UART base addresses (from DTS uart0-uart9 nodes) */
#define KYX1_UART0_BASE         0xD4017000  /* Console (serial0) */
#define KYX1_UART2_BASE         0xD4017100  /* Debug header */
#define KYX1_UART3_BASE         0xD4017200
#define KYX1_UART4_BASE         0xD4017300
#define KYX1_UART5_BASE         0xD4017400
#define KYX1_UART6_BASE         0xD4017500
#define KYX1_UART7_BASE         0xD4017600
#define KYX1_UART8_BASE         0xD4017700
#define KYX1_UART9_BASE         0xD4017800

/* PXA UART register offsets (reg-shift=2, so multiply by 4) */
#define PXA_UART_RBR            (0x00 << 2) /* Receive Buffer (read)         */
#define PXA_UART_THR            (0x00 << 2) /* Transmit Holding (write)      */
#define PXA_UART_IER            (0x01 << 2) /* Interrupt Enable              */
#define PXA_UART_IIR            (0x02 << 2) /* Interrupt Identification (rd) */
#define PXA_UART_FCR            (0x02 << 2) /* FIFO Control (write)          */
#define PXA_UART_LCR            (0x03 << 2) /* Line Control                  */
#define PXA_UART_MCR            (0x04 << 2) /* Modem Control                 */
#define PXA_UART_LSR            (0x05 << 2) /* Line Status (read)            */
#define PXA_UART_MSR            (0x06 << 2) /* Modem Status (read)           */
#define PXA_UART_SCR            (0x07 << 2) /* Scratch                       */
#define PXA_UART_DLL            (0x00 << 2) /* Divisor Latch Low (DLAB=1)    */
#define PXA_UART_DLH            (0x01 << 2) /* Divisor Latch High (DLAB=1)   */

/* PXA UART Line Status Register bits */
#define PXA_LSR_DR              (1 << 0)    /* Data Ready */
#define PXA_LSR_OE              (1 << 1)    /* Overrun Error */
#define PXA_LSR_PE              (1 << 2)    /* Parity Error */
#define PXA_LSR_FE              (1 << 3)    /* Framing Error */
#define PXA_LSR_BI              (1 << 4)    /* Break Interrupt */
#define PXA_LSR_THRE            (1 << 5)    /* TX Holding Register Empty */
#define PXA_LSR_TEMT            (1 << 6)    /* Transmitter Empty */

/* PXA UART IER vendor quirk (from U-Boot CONFIG_SYS_NS16550_IER=0x40) */
#define PXA_IER_UUE             (1 << 6)    /* UART Unit Enable (PXA-specific!) */

/* PXA UART FIFO Control Register bits */
#define PXA_FCR_FIFOE           (1 << 0)    /* FIFO Enable */
#define PXA_FCR_RFIFOR          (1 << 1)    /* RX FIFO Reset */
#define PXA_FCR_XFIFOR          (1 << 2)    /* TX FIFO Reset */

/* PXA UART Line Control Register bits */
#define PXA_LCR_WLS_8           0x03        /* 8-bit word length */
#define PXA_LCR_STB_1           0x00        /* 1 stop bit */
#define PXA_LCR_DLAB            (1 << 7)    /* Divisor Latch Access Bit */


/* =============================================================================
 * GPIO REGISTERS
 * =============================================================================
 *
 * 4 GPIO banks, base at 0xD4019000.
 * Each bank controls 32 pins. Total: ~128 GPIO pins.
 *
 * The GPIO controller is `ky,x1-gpio` — similar to Marvell MMP GPIO.
 * Pin muxing is separate, handled by the pinctrl block at 0xD401E000.
 *
 * BCM2710 comparison:
 *   BCM: GPFSEL0-5 for function select, GPSET/GPCLR for output
 *   Ky X1: Separate direction, output, and level registers per bank
 */

#define KYX1_GPIO_BASE          0xD4019000

/* GPIO bank offsets (from DTS: bank 0,1,2 at +0x0, +0x4, +0x8; bank 3 at +0x100) */
#define KYX1_GPIO_BANK0_OFF     0x000
#define KYX1_GPIO_BANK1_OFF     0x004
#define KYX1_GPIO_BANK2_OFF     0x008
#define KYX1_GPIO_BANK3_OFF     0x100

/* Per-bank register offsets (MMP-style GPIO controller) */
#define GPIO_PLR                0x000   /* Pin Level Register (read) */
#define GPIO_PDR                0x00C   /* Pin Direction Register (1=output) */
#define GPIO_PSR                0x018   /* Pin Set Register (write 1 to set) */
#define GPIO_PCR                0x024   /* Pin Clear Register (write 1 to clear) */
#define GPIO_RER                0x030   /* Rising Edge detect Enable */
#define GPIO_FER                0x03C   /* Falling Edge detect Enable */
#define GPIO_EDR                0x048   /* Edge Detect status Register */
#define GPIO_SDR                0x054   /* Set Direction Register (1=output) */
#define GPIO_CDR                0x060   /* Clear Direction Register (1=input) */

/* Number of GPIO pins per bank */
#define KYX1_GPIO_PINS_PER_BANK 32
#define KYX1_GPIO_NUM_BANKS     4
#define KYX1_GPIO_MAX_PINS      (KYX1_GPIO_PINS_PER_BANK * KYX1_GPIO_NUM_BANKS)

/* Heartbeat LED (from DTS: GPIO 96, active low) */
#define KYX1_LED_GPIO           96


/* =============================================================================
 * PIN CONTROLLER (PINMUX)
 * =============================================================================
 *
 * Pin muxing is handled separately from GPIO, at 0xD401E000.
 * Type is `pinconf-single-aib` — a single-register-per-pin mux controller.
 * Each pin has a mux register that selects which function (GPIO, UART, I2C, etc.)
 * the pin is assigned to.
 */

#define KYX1_PINCTRL_BASE       0xD401E000


/* =============================================================================
 * TIMER REGISTERS
 * =============================================================================
 *
 * The Ky X1 has two hardware timers at 0xD4014000 and 0xD4016000, but for
 * bare-metal use, the RISC-V `rdtime` instruction is far simpler. It reads
 * the `time` CSR at the timebase frequency of 24 MHz (from the DTS).
 *
 * BCM2710 comparison:
 *   BCM: System timer at PERIPHERAL_BASE + 0x3000, runs at 1 MHz
 *   Ky X1: rdtime instruction, runs at 24 MHz (24x more resolution!)
 */

#define KYX1_TIMER0_BASE        0xD4014000
#define KYX1_TIMER1_BASE        0xD4016000
#define KYX1_TIMEBASE_FREQ      24000000    /* 24 MHz (from DTS timebase-frequency) */


/* =============================================================================
 * INTERRUPT CONTROLLER (PLIC / CLINT)
 * =============================================================================
 *
 * RISC-V uses two interrupt controllers:
 *   CLINT: Core-Local Interruptor — timer interrupts, software interrupts (IPI)
 *   PLIC:  Platform-Level Interrupt Controller — external device interrupts
 *
 * BCM2710 comparison:
 *   BCM: Custom interrupt controller at PERIPHERAL_BASE + 0xB200
 *   Ky X1: Standard RISC-V CLINT + PLIC (much more portable!)
 */

#define KYX1_CLINT_BASE         0xE4000000  /* Timer + IPI (from DTS) */
#define KYX1_PLIC_BASE          0xE0000000  /* External interrupts (from DTS) */


/* =============================================================================
 * DISPLAY SUBSYSTEM
 * =============================================================================
 *
 * The Ky X1 has a DPU (Display Processing Unit) codenamed "Saturn" and an
 * HDMI transmitter. For Tutorial-OS, we DON'T need to drive these directly
 * because U-Boot already initialized the display and passed a SimpleFB
 * framebuffer in the DTB.
 *
 * These addresses are documented here for reference and future use when
 * we want to go beyond SimpleFB (resolution switching, double-buffering, etc.)
 */

#define KYX1_DPU_BASE           0xD8000000  /* Display Processing Unit */
#define KYX1_HDMI_TX_BASE       0xD8018000  /* HDMI Transmitter */
#define KYX1_DPU_RESERVED_ADDR  0x2FF40000  /* DPU MMU tables in DRAM (DO NOT TOUCH) */
#define KYX1_DPU_RESERVED_SIZE  0x00060000  /* 384 KB reserved */


/* =============================================================================
 * OTHER PERIPHERALS
 * =============================================================================
 */

#define KYX1_DMA_BASE           0xD4000000  /* PDMA (16 channels) */
#define KYX1_I2C0_BASE          0xD4010800
#define KYX1_I2C1_BASE          0xD4011000
#define KYX1_I2C2_BASE          0xD4012000
#define KYX1_I2C8_BASE          0xD401D800  /* PMIC: SPM8821 @ 0x41 */
#define KYX1_MAILBOX_BASE       0xD4013400  /* Inter-processor mailbox */
#define KYX1_WATCHDOG_BASE      0xD4080000
#define KYX1_RTC_BASE           0xD4010000  /* MMP RTC */
#define KYX1_THERMAL_BASE       0xD4018000  /* 5 BJT sensors */
#define KYX1_CLOCK_BASE         0xD4050000  /* Clock + Reset controller */
#define KYX1_IR_BASE            0xD4017F00  /* IR receiver */
#define KYX1_SRAM_BASE          0xC0800000  /* 256 KB shared SRAM */


/* =============================================================================
 * CPU CONFIGURATION
 * =============================================================================
 */

#define KYX1_NUM_HARTS          8           /* 2 clusters × 4 cores */
#define KYX1_DRAM_BASE          0x00000000  /* DRAM starts at physical 0 */
#define KYX1_DRAM_BANK0_SIZE    0x80000000  /* 2 GB */
#define KYX1_DRAM_BANK1_BASE    0x100000000ULL
#define KYX1_DRAM_BANK1_SIZE    0x80000000  /* 2 GB */
#define KYX1_TOTAL_DRAM         (KYX1_DRAM_BANK0_SIZE + KYX1_DRAM_BANK1_SIZE)

#endif /* KYX1_REGS_H */
