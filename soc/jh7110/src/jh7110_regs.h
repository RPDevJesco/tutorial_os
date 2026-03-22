/*
 * jh7110_regs.h — StarFive JH-7110 SoC Register Definitions
 * ===========================================================
 *
 * This header defines ALL peripheral base addresses and key register offsets
 * for the JH-7110 SoC used on the Milk-V Mars (and VisionFive 2).
 *
 * ADDRESS SOURCES:
 *   1. StarFive JH-7110 Datasheet v1.63 (2023/10/25)
 *   2. Linux kernel: arch/riscv/boot/dts/starfive/jh7110.dtsi
 *   3. U-Boot: arch/riscv/dts/jh7110.dtsi, board/starfive/visionfive2/
 *   4. Milk-V Mars schematic V1.21 (2024-05-10)
 *
 * COMPARISON WITH KYX1:
 *   The Ky X1 has all peripherals in the 0xD4000000 range (Marvell PXA heritage).
 *   The JH7110 has peripherals scattered more broadly:
 *     - Main peripherals: 0x10000000 range (UART, I2C, SPI, GPIO)
 *     - System CRG/iomux: 0x13000000 range
 *     - AON domain:        0x17000000 range
 *     - Interrupt ctrl:   0x02000000 (CLINT), 0x0C000000 (PLIC)
 *     - Display subsystem: 0x295xxxxx range
 *
 * This is characteristic of SiFive-derived SoCs vs Marvell-derived SoCs.
 * The address layout is closer to a "standard" RISC-V reference platform.
 */

#ifndef JH7110_REGS_H
#define JH7110_REGS_H

#include "types.h"

/* =============================================================================
 * SYSTEM MEMORY MAP
 * =============================================================================
 *
 * JH7110 Memory Layout (simplified):
 *
 *   0x0000_0000  BootROM (32 KB, read-only after boot)
 *   0x0200_0000  CLINT (Core Local Interruptor — timer + software IRQs)
 *   0x0C00_0000  PLIC (Platform Level Interrupt Controller)
 *   0x1000_0000  Main peripheral region (UART, I2C, SPI, etc.)
 *   0x1300_0000  System CRG (clocks/resets) + GPIO + iomux
 *   0x1700_0000  AON (Always-On) domain — RTC, AON CRG
 *   0x2940_0000  Display subsystem (DC8200, HDMI TX, DSI TX)
 *   0x4000_0000  DRAM start (physical) — 8 GB on Milk-V Mars 8GB
 *   0x4020_0000  OUR KERNEL LOADS HERE (U-Boot default load address)
 *
 * DRAM NOTE:
 *   The Mars 8GB uses LPDDR4 in a 32-bit DDR interface.
 *   The JH7110 DDR controller supports up to 8GB across its address space.
 *   DRAM is mapped from 0x40000000. For our purposes, we use memory from
 *   0x40200000 upward, leaving 0x40000000-0x401FFFFF for OpenSBI+U-Boot.
 */

/* =============================================================================
 * CLINT — CORE LOCAL INTERRUPTOR
 * =============================================================================
 *
 * The CLINT provides per-hart timer and software interrupts.
 * This is the RISC-V standard CLINT layout used by virtually all SiFive SoCs.
 *
 * MSIP registers: one per hart, at base + hart*4
 *   Supervisor timer: accessed via stimecmp CSR (or via CLINT MTIMECMP in M-mode)
 *
 * For bare-metal Tutorial-OS (S-mode), we primarily use:
 *   - rdtime CSR: reads mtime register (24 MHz reference, from OSC)
 *   - Timer interrupts via SBI (OpenSBI manages CLINT from M-mode)
 */
#define JH7110_CLINT_BASE           0x02000000UL
#define JH7110_CLINT_MSIP(hart)     (JH7110_CLINT_BASE + (hart) * 4)
#define JH7110_CLINT_MTIMECMP(hart) (JH7110_CLINT_BASE + 0x4000 + (hart) * 8)
#define JH7110_CLINT_MTIME          (JH7110_CLINT_BASE + 0xBFF8)

/* =============================================================================
 * PLIC — PLATFORM LEVEL INTERRUPT CONTROLLER
 * =============================================================================
 *
 * The PLIC handles all external interrupts (everything except timer and
 * software interrupts, which are handled by CLINT).
 *
 * Standard SiFive PLIC layout:
 *   0x0C000000: Priority registers (one per interrupt source)
 *   0x0C001000: Pending bits
 *   0x0C002000: Enable bits (per context)
 *   0x0C200000: Context registers (threshold + claim/complete)
 *
 * JH7110 has 4 U74 cores + 1 S7 = 5 harts, 2 contexts per hart (M+S mode)
 * = 10 PLIC contexts.
 */
#define JH7110_PLIC_BASE            0x0C000000UL
#define JH7110_PLIC_PRIORITY(irq)   (JH7110_PLIC_BASE + (irq) * 4)
#define JH7110_PLIC_PENDING         (JH7110_PLIC_BASE + 0x1000)
#define JH7110_PLIC_ENABLE(ctx)     (JH7110_PLIC_BASE + 0x2000 + (ctx) * 0x80)
#define JH7110_PLIC_THRESHOLD(ctx)  (JH7110_PLIC_BASE + 0x200000 + (ctx) * 0x1000)
#define JH7110_PLIC_CLAIM(ctx)      (JH7110_PLIC_BASE + 0x200004 + (ctx) * 0x1000)

/* =============================================================================
 * UART — SYNOPSYS DESIGNWARE 8250 / 16550-COMPATIBLE
 * =============================================================================
 *
 * JH7110 has 6 UART interfaces, all using the DesignWare 8250 IP.
 * This is a standard 16550-compatible UART — NOT the PXA/MMP-style UART
 * used by the Ky X1. Key distinction:
 *
 *   Ky X1 (PXA):  registers spaced 4 bytes apart, IER bit 6 = unit enable
 *   JH7110 (8250): standard 8250 layout, LCR/DLAB for baud divisor
 *
 * UART0 (0x10000000):
 *   TX = GPIO5, RX = GPIO6 (confirmed from Mars schematic page 7 and 19)
 *   This is the debug UART exposed on the 40-pin header.
 *   115200 8N1 is the standard operating mode.
 *
 * Baud rate divisor formula:
 *   divisor = UART_CLK / (16 * baud_rate)
 *   For 24 MHz clock @ 115200 baud: 24000000 / (16 * 115200) = 13.02 → 13
 *
 * NOTE from JH7110 datasheet:
 *   UART3, UART4, UART5 can reach 3 Mbps via fractional divider in SYSTOP_CRG.
 *   UART0-2 are limited to standard rates from the base 24 MHz OSC clock.
 */
#define JH7110_UART0_BASE           0x10000000UL
#define JH7110_UART1_BASE           0x10010000UL
#define JH7110_UART2_BASE           0x10020000UL
#define JH7110_UART3_BASE           0x10030000UL
#define JH7110_UART4_BASE           0x10040000UL
#define JH7110_UART5_BASE           0x10050000UL

/* 8250 Register offsets (byte offsets, 32-bit wide registers) */
#define UART8250_RBR                0x00    /* Receive Buffer (DLAB=0, read) */
#define UART8250_THR                0x00    /* Transmit Holding (DLAB=0, write) */
#define UART8250_DLL                0x00    /* Divisor Latch Low (DLAB=1) */
#define UART8250_DLM                0x04    /* Divisor Latch High (DLAB=1) */
#define UART8250_IER                0x04    /* Interrupt Enable (DLAB=0) */
#define UART8250_IIR                0x08    /* Interrupt Identity (read) */
#define UART8250_FCR                0x08    /* FIFO Control (write) */
#define UART8250_LCR                0x0C    /* Line Control */
#define UART8250_MCR                0x10    /* Modem Control */
#define UART8250_LSR                0x14    /* Line Status */
#define UART8250_MSR                0x18    /* Modem Status */
#define UART8250_SCR                0x1C    /* Scratch */

/* LCR bits */
#define UART8250_LCR_WLS_8          0x03    /* 8-bit word length */
#define UART8250_LCR_STB_1          0x00    /* 1 stop bit */
#define UART8250_LCR_PEN_NONE       0x00    /* No parity */
#define UART8250_LCR_DLAB           0x80    /* Divisor Latch Access Bit */

/* FCR bits */
#define UART8250_FCR_FIFO_EN        0x01    /* Enable FIFO */
#define UART8250_FCR_RX_RESET       0x02    /* Reset RX FIFO */
#define UART8250_FCR_TX_RESET       0x04    /* Reset TX FIFO */
#define UART8250_FCR_TRIG_14        0xC0    /* RX trigger at 14 bytes */

/* LSR bits */
#define UART8250_LSR_DR             0x01    /* Data Ready (RX) */
#define UART8250_LSR_THRE           0x20    /* Transmit Holding Register Empty */
#define UART8250_LSR_TEMT           0x40    /* Transmitter Empty (shift + THR) */

/* Clock configuration */
#define JH7110_UART_CLK_HZ          24000000UL  /* 24 MHz from OSC */
#define JH7110_UART_BAUD            115200
#define JH7110_UART_DIVISOR         (JH7110_UART_CLK_HZ / (16 * JH7110_UART_BAUD))

/* =============================================================================
 * I2C — SYNOPSYS DESIGNWARE I2C
 * =============================================================================
 *
 * JH7110 has 7 I2C controllers (I2C0-I2C6), all using the standard
 * Synopsys DesignWare I2C IP. This is the same IP found in hundreds of
 * SoCs (Intel, Rockchip, Allwinner, etc.). The register layout is well
 * documented in the DesignWare databook.
 *
 * The Ky X1 also uses DesignWare I2C (PXA variant), but with different
 * register names. The JH7110 uses the textbook DW I2C register map.
 *
 * I2C6 (0x100E0000):
 *   Connected to AXP15060 PMIC at I2C address 0x36.
 *   This is confirmed by the Mars schematic (page 21: PMIC-AXP15060).
 */
#define JH7110_I2C0_BASE            0x10080000UL
#define JH7110_I2C1_BASE            0x10090000UL
#define JH7110_I2C2_BASE            0x100A0000UL
#define JH7110_I2C3_BASE            0x100B0000UL
#define JH7110_I2C4_BASE            0x100C0000UL
#define JH7110_I2C5_BASE            0x100D0000UL
#define JH7110_I2C6_BASE            0x100E0000UL

/* DW I2C register offsets */
#define DWI2C_IC_CON                0x00    /* Control register */
#define DWI2C_IC_TAR                0x04    /* Target address */
#define DWI2C_IC_SAR                0x08    /* Slave address */
#define DWI2C_IC_DATA_CMD           0x10    /* Data and command */
#define DWI2C_IC_SS_SCL_HCNT        0x14    /* Std speed SCL high count */
#define DWI2C_IC_SS_SCL_LCNT        0x18    /* Std speed SCL low count */
#define DWI2C_IC_FS_SCL_HCNT        0x1C    /* Fast speed SCL high count */
#define DWI2C_IC_FS_SCL_LCNT        0x20    /* Fast speed SCL low count */
#define DWI2C_IC_INTR_STAT          0x2C    /* Interrupt status */
#define DWI2C_IC_INTR_MASK          0x30    /* Interrupt mask */
#define DWI2C_IC_RAW_INTR_STAT      0x34    /* Raw interrupt status */
#define DWI2C_IC_CLR_INTR           0x40    /* Clear all interrupts */
#define DWI2C_IC_CLR_RX_UNDER       0x44
#define DWI2C_IC_CLR_RX_OVER        0x48
#define DWI2C_IC_CLR_TX_OVER        0x4C
#define DWI2C_IC_CLR_TX_ABRT        0x54
#define DWI2C_IC_ENABLE             0x6C    /* Enable/disable I2C */
#define DWI2C_IC_STATUS             0x70    /* I2C status */
#define DWI2C_IC_TXFLR              0x74    /* TX FIFO level */
#define DWI2C_IC_RXFLR              0x78    /* RX FIFO level */
#define DWI2C_IC_TX_ABRT_SOURCE     0x80    /* TX abort source */
#define DWI2C_IC_COMP_PARAM_1       0xF4    /* Component parameter 1 */
#define DWI2C_IC_COMP_VERSION       0xF8    /* Component version */
#define DWI2C_IC_COMP_TYPE          0xFC    /* Component type (0x44570140) */

/* IC_CON bits */
#define DWI2C_CON_MASTER_MODE       (1 << 0)    /* Enable master mode */
#define DWI2C_CON_SPEED_STD         (1 << 1)    /* Standard speed (100 kHz) */
#define DWI2C_CON_SPEED_FAST        (2 << 1)    /* Fast speed (400 kHz) */
#define DWI2C_CON_10BIT_ADDR_MASTER (1 << 4)    /* 10-bit addressing (master) */
#define DWI2C_CON_RESTART_EN        (1 << 5)    /* Enable RESTART */
#define DWI2C_CON_SLAVE_DISABLE     (1 << 6)    /* Disable slave mode */

/* IC_DATA_CMD bits */
#define DWI2C_DATA_CMD_READ         (1 << 8)    /* 1 = read, 0 = write */
#define DWI2C_DATA_CMD_STOP         (1 << 9)    /* Issue STOP after byte */
#define DWI2C_DATA_CMD_RESTART      (1 << 10)   /* Issue RESTART before byte */

/* IC_STATUS bits */
#define DWI2C_STATUS_ACTIVITY       (1 << 0)    /* I2C bus busy */
#define DWI2C_STATUS_TFNF           (1 << 1)    /* TX FIFO not full */
#define DWI2C_STATUS_TFE            (1 << 2)    /* TX FIFO empty */
#define DWI2C_STATUS_RFNE           (1 << 3)    /* RX FIFO not empty */
#define DWI2C_STATUS_RFF            (1 << 4)    /* RX FIFO full */
#define DWI2C_STATUS_MST_ACTIVITY   (1 << 5)    /* Master state machine active */

/* IC_RAW_INTR_STAT bits */
#define DWI2C_INTR_TX_ABRT          (1 << 6)    /* TX abort */
#define DWI2C_INTR_TX_EMPTY         (1 << 4)    /* TX FIFO empty */
#define DWI2C_INTR_RX_FULL          (1 << 2)    /* RX FIFO >= threshold */

/* I2C clock: APB bus @ ~100 MHz (from sys_crg). Standard mode = 100 kHz.
 * SS_SCL_HCNT = APB_CLK/(2*SCL_freq) - 7 = 100MHz/(200kHz) - 7 = 493
 * SS_SCL_LCNT = APB_CLK/(2*SCL_freq) - 1 = 100MHz/(200kHz) - 1 = 499 */
#define DWI2C_SS_SCL_HCNT_100MHZ    493
#define DWI2C_SS_SCL_LCNT_100MHZ    499

/* PMIC I2C address */
#define JH7110_AXP15060_I2C_ADDR    0x36
#define JH7110_AXP15060_I2C_BASE    JH7110_I2C6_BASE

/* =============================================================================
 * GPIO / SYS_IOMUX
 * =============================================================================
 *
 * JH7110 has 64 GPIOs (GPIO0-GPIO63), controlled by two subsystems:
 *
 *   sys_iomux (0x13000000):
 *     Pin multiplexing — each GPIO can route one of several peripheral
 *     signals (UART TX/RX, I2C SDA/SCL, SPI, etc.) to the physical pad.
 *     Register layout: func_sel registers, one per GPIO function.
 *
 *   sys_gpio (0x13040000):
 *     GPIO output data and output enable for GPIO mode.
 *     Each GPIO has a dedicated dout (data out) and doen (output enable) reg.
 *
 * The iomux model is different from Ky X1 (which uses a different pinctrl
 * controller with offset-based function select).
 *
 * FROM SCHEMATIC:
 *   UART0 TX = GPIO5 (UART0_TX_GPIO5 in schematic)
 *   UART0 RX = GPIO6 (UART0_RX_GPIO6 in schematic)
 *   HDMI I2C SCL = GPIO0, SDA = GPIO1
 *   DSI I2C SCL = GPIO3, SDA = GPIO2
 *   HDMI HPD = GPIO15, CEC = GPIO14
 */
#define JH7110_SYS_IOMUX_BASE       0x13000000UL
#define JH7110_SYS_CRG_BASE         0x13020000UL
#define JH7110_SYS_GPIO_BASE        0x13040000UL

/*
 * JH7110 sys_gpio register layout (from Linux driver: pinctrl-starfive-jh7110.c)
 *
 * Each GPIO has:
 *   DOUT register:  sets output data (1 bit per GPIO, but word-aligned)
 *   DOEN register:  sets output enable (1=output, 0=input)
 *   DIN  register:  reads input data
 *
 * The registers are packed 4 GPIOs per word in some implementations,
 * but the JH7110 sys_gpio uses individual 32-bit registers per GPIO.
 * Base + GPIO_NUM * 4 for each group.
 */
#define JH7110_GPIO_DOUT_BASE       (JH7110_SYS_GPIO_BASE + 0x000)
#define JH7110_GPIO_DOEN_BASE       (JH7110_SYS_GPIO_BASE + 0x040)
#define JH7110_GPIO_DIN_BASE        (JH7110_SYS_GPIO_BASE + 0x080)

/* GPIO register for a specific pin */
#define JH7110_GPIO_DOUT(n)         (JH7110_GPIO_DOUT_BASE + (n) * 4)
#define JH7110_GPIO_DOEN(n)         (JH7110_GPIO_DOEN_BASE + (n) * 4)
#define JH7110_GPIO_DIN(n)          (JH7110_GPIO_DIN_BASE  + (n) * 4)

/* DOEN values */
#define JH7110_GPIO_OUTPUT_EN       0x00    /* Enable output (active low: 0=output) */
#define JH7110_GPIO_INPUT_EN        0x01    /* Enable input  (1=input/high-Z) */

/* =============================================================================
 * AON (ALWAYS-ON) DOMAIN
 * =============================================================================
 *
 * The AON domain contains the always-on GPIO (RGPIO0-3) and RTC.
 * RGPIO0 and RGPIO1 are used for boot mode selection (SW2 dip switch
 * on the Mars schematic, connected to BOOT_Mode pins).
 *
 * We don't typically need to touch these in bare metal, but the register
 * addresses are here for completeness and reference.
 */
#define JH7110_AON_CRG_BASE         0x17000000UL
#define JH7110_AON_IOMUX_BASE       0x17010000UL
#define JH7110_AON_GPIO_BASE        0x17020000UL

/* =============================================================================
 * DISPLAY SUBSYSTEM
 * =============================================================================
 *
 * JH7110's display subsystem (DOM_VOUT_TOP in the datasheet) contains:
 *   - DC8200 display controller (same IP as VisionFive 2)
 *   - HDMI 2.0 TX (compliant with HDMI 2.0, 1.4, DVI 1.0)
 *   - MIPI DSI TX (4-lane, up to 1080p@60fps)
 *   - VOUT CRG (clock/reset for display subsystem)
 *
 * For Tutorial-OS, we use the SimpleFB strategy:
 *   U-Boot initializes the DC8200 + HDMI TX and allocates a framebuffer.
 *   It injects a simple-framebuffer node in the DTB.
 *   We parse the DTB at boot to find the framebuffer address.
 *   No DC8200 register programming needed — U-Boot already did it.
 *
 * These addresses are here for future use (direct DC8200 driver).
 */
#define JH7110_VOUT_CRG_BASE        0x295C0000UL
#define JH7110_DC8200_BASE          0x29400000UL    /* Display controller */
#define JH7110_HDMI_BASE            0x29590000UL    /* HDMI TX */
#define JH7110_DSITX_BASE           0x295B0000UL    /* MIPI DSI TX */
#define JH7110_MIPITX_DPHY_BASE     0x295D0000UL    /* MIPI TX DPHY */

/* =============================================================================
 * SoC IDENTIFICATION CONSTANTS
 * =============================================================================
 *
 * SiFive U74 RISC-V CPU identification (readable via SBI ecall):
 *
 *   mvendorid = 0x489  (SiFive, Inc. — JEDEC bank 10, ID 0x09)
 *   marchid   = 0x8000000000000007  (U74-MC)
 *   mimpid    = implementation-specific (firmware version)
 *
 * These are compared during platform init to verify we're on the right
 * hardware. Same approach as checking BCM2712's board revision code.
 */
#define JH7110_MVENDORID            0x489UL
#define JH7110_MARCHID_U74          0x8000000000000007ULL

/* CPU constants from JH7110 datasheet */
#define JH7110_CPU_FREQ_HZ          1500000000UL    /* 1.5 GHz max */
#define JH7110_TIMER_FREQ_HZ        24000000UL      /* 24 MHz OSC timebase */
#define JH7110_NUM_CORES            4               /* U74 quad-core */
#define JH7110_TOTAL_RAM            (8ULL * 1024 * 1024 * 1024)  /* 8 GB (Mars 8GB) */
#define JH7110_THERMAL_MAX_MC       105000          /* 105°C max junction */

/* DDR NOTE: JH7110 datasheet recommends DDR/LPDDR4 at 2133 Mbps (not 2800)
 * due to a known tuning limitation (see datasheet section 8.2). */
#define JH7110_DDR_RECOMMENDED_MBPS 2133

#endif /* JH7110_REGS_H */