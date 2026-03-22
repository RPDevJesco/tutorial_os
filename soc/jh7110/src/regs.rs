//! StarFive JH7110 register address definitions.
//!
//! This is the Rust port of `jh7110_regs.h`.  Every address and offset
//! defined here comes from the same sources:
//!
//!   1. StarFive JH7110 Datasheet v1.63 (2023/10/25)
//!   2. Linux kernel: `arch/riscv/boot/dts/starfive/jh7110.dtsi`
//!   3. U-Boot: `arch/riscv/dts/jh7110.dtsi`
//!   4. Milk-V Mars schematic V1.21 (2024-05-10)
//!
//! # Memory Map (simplified)
//!
//! ```text
//! 0x0000_0000  BootROM (32 KB, read-only after boot)
//! 0x0200_0000  CLINT (Core Local Interruptor — timer + software IRQs)
//! 0x0C00_0000  PLIC (Platform Level Interrupt Controller)
//! 0x1000_0000  Main peripheral region (UART, I2C, SPI, etc.)
//! 0x1300_0000  System CRG (clocks/resets) + GPIO + iomux
//! 0x1700_0000  AON (Always-On) domain — RTC, AON CRG
//! 0x2940_0000  Display subsystem (DC8200, HDMI TX, DSI TX)
//! 0x4000_0000  DRAM start (physical) — 8 GB on Milk-V Mars 8GB
//! 0x4020_0000  OUR KERNEL LOADS HERE (U-Boot default load address)
//! ```

// ============================================================================
// CLINT — Core Local Interruptor
// ============================================================================

pub const CLINT_BASE: usize = 0x0200_0000;

#[inline]
pub const fn clint_msip(hart: usize) -> usize {
    CLINT_BASE + hart * 4
}

#[inline]
pub const fn clint_mtimecmp(hart: usize) -> usize {
    CLINT_BASE + 0x4000 + hart * 8
}

pub const CLINT_MTIME: usize = CLINT_BASE + 0xBFF8;

// ============================================================================
// PLIC — Platform Level Interrupt Controller
// ============================================================================

pub const PLIC_BASE: usize = 0x0C00_0000;

#[inline]
pub const fn plic_priority(irq: usize) -> usize {
    PLIC_BASE + irq * 4
}

pub const PLIC_PENDING: usize = PLIC_BASE + 0x1000;

#[inline]
pub const fn plic_enable(ctx: usize) -> usize {
    PLIC_BASE + 0x2000 + ctx * 0x80
}

#[inline]
pub const fn plic_threshold(ctx: usize) -> usize {
    PLIC_BASE + 0x20_0000 + ctx * 0x1000
}

#[inline]
pub const fn plic_claim(ctx: usize) -> usize {
    PLIC_BASE + 0x20_0004 + ctx * 0x1000
}

// ============================================================================
// UART — Synopsys DesignWare 8250 / 16550-compatible
// ============================================================================
//
// UART0 (0x10000000): TX = GPIO5, RX = GPIO6 (debug UART on 40-pin header)
// Baud: 115200 8N1.  Clock: 24 MHz OSC.

pub const UART0_BASE: usize = 0x1000_0000;
pub const UART1_BASE: usize = 0x1001_0000;
pub const UART2_BASE: usize = 0x1002_0000;
pub const UART3_BASE: usize = 0x1003_0000;
pub const UART4_BASE: usize = 0x1004_0000;
pub const UART5_BASE: usize = 0x1005_0000;

/// 8250 register offsets (byte offsets, 32-bit wide registers).
pub mod uart8250 {
    pub const RBR: usize = 0x00; // Receive Buffer (DLAB=0, read)
    pub const THR: usize = 0x00; // Transmit Holding (DLAB=0, write)
    pub const DLL: usize = 0x00; // Divisor Latch Low (DLAB=1)
    pub const DLM: usize = 0x04; // Divisor Latch High (DLAB=1)
    pub const IER: usize = 0x04; // Interrupt Enable (DLAB=0)
    pub const IIR: usize = 0x08; // Interrupt Identity (read)
    pub const FCR: usize = 0x08; // FIFO Control (write)
    pub const LCR: usize = 0x0C; // Line Control
    pub const MCR: usize = 0x10; // Modem Control
    pub const LSR: usize = 0x14; // Line Status
    pub const MSR: usize = 0x18; // Modem Status
    pub const SCR: usize = 0x1C; // Scratch

    // LCR bits
    pub const LCR_WLS_8: u32    = 0x03; // 8-bit word length
    pub const LCR_STB_1: u32    = 0x00; // 1 stop bit
    pub const LCR_PEN_NONE: u32 = 0x00; // No parity
    pub const LCR_DLAB: u32     = 0x80; // Divisor Latch Access Bit

    // FCR bits
    pub const FCR_FIFO_EN: u32  = 0x01;
    pub const FCR_RX_RESET: u32 = 0x02;
    pub const FCR_TX_RESET: u32 = 0x04;
    pub const FCR_TRIG_14: u32  = 0xC0;

    // LSR bits
    pub const LSR_DR: u32       = 0x01; // Data Ready (RX)
    pub const LSR_THRE: u32     = 0x20; // Transmit Holding Register Empty
    pub const LSR_TEMT: u32     = 0x40; // Transmitter Empty
}

/// UART clock and baud rate configuration.
pub const UART_CLK_HZ: u32 = 24_000_000;
pub const UART_BAUD: u32   = 115_200;
pub const UART_DIVISOR: u32 = UART_CLK_HZ / (16 * UART_BAUD); // = 13

// ============================================================================
// I2C — Synopsys DesignWare I2C
// ============================================================================
//
// I2C6 (0x100E0000): Connected to AXP15060 PMIC at address 0x36.

pub const I2C0_BASE: usize = 0x1008_0000;
pub const I2C1_BASE: usize = 0x1009_0000;
pub const I2C2_BASE: usize = 0x100A_0000;
pub const I2C3_BASE: usize = 0x100B_0000;
pub const I2C4_BASE: usize = 0x100C_0000;
pub const I2C5_BASE: usize = 0x100D_0000;
pub const I2C6_BASE: usize = 0x100E_0000;

/// DesignWare I2C register offsets.
pub mod dwi2c {
    pub const IC_CON: usize            = 0x00;
    pub const IC_TAR: usize            = 0x04;
    pub const IC_SAR: usize            = 0x08;
    pub const IC_DATA_CMD: usize       = 0x10;
    pub const IC_SS_SCL_HCNT: usize    = 0x14;
    pub const IC_SS_SCL_LCNT: usize    = 0x18;
    pub const IC_FS_SCL_HCNT: usize    = 0x1C;
    pub const IC_FS_SCL_LCNT: usize    = 0x20;
    pub const IC_INTR_STAT: usize      = 0x2C;
    pub const IC_INTR_MASK: usize      = 0x30;
    pub const IC_RAW_INTR_STAT: usize  = 0x34;
    pub const IC_CLR_INTR: usize       = 0x40;
    pub const IC_CLR_RX_UNDER: usize   = 0x44;
    pub const IC_CLR_RX_OVER: usize    = 0x48;
    pub const IC_CLR_TX_OVER: usize    = 0x4C;
    pub const IC_CLR_TX_ABRT: usize    = 0x54;
    pub const IC_ENABLE: usize         = 0x6C;
    pub const IC_STATUS: usize         = 0x70;
    pub const IC_TXFLR: usize          = 0x74;
    pub const IC_RXFLR: usize          = 0x78;
    pub const IC_TX_ABRT_SOURCE: usize = 0x80;
    pub const IC_COMP_PARAM_1: usize   = 0xF4;
    pub const IC_COMP_VERSION: usize   = 0xF8;
    pub const IC_COMP_TYPE: usize      = 0xFC;

    // IC_CON bits
    pub const CON_MASTER_MODE: u32       = 1 << 0;
    pub const CON_SPEED_STD: u32         = 1 << 1;
    pub const CON_SPEED_FAST: u32        = 2 << 1;
    pub const CON_10BIT_ADDR_MASTER: u32 = 1 << 4;
    pub const CON_RESTART_EN: u32        = 1 << 5;
    pub const CON_SLAVE_DISABLE: u32     = 1 << 6;

    // IC_DATA_CMD bits
    pub const DATA_CMD_READ: u32    = 1 << 8;
    pub const DATA_CMD_STOP: u32    = 1 << 9;
    pub const DATA_CMD_RESTART: u32 = 1 << 10;

    // IC_STATUS bits
    pub const STATUS_ACTIVITY: u32     = 1 << 0;
    pub const STATUS_TFNF: u32        = 1 << 1;
    pub const STATUS_TFE: u32         = 1 << 2;
    pub const STATUS_RFNE: u32        = 1 << 3;
    pub const STATUS_RFF: u32         = 1 << 4;
    pub const STATUS_MST_ACTIVITY: u32 = 1 << 5;

    // IC_RAW_INTR_STAT bits
    pub const INTR_TX_ABRT: u32  = 1 << 6;
    pub const INTR_TX_EMPTY: u32 = 1 << 4;
    pub const INTR_RX_FULL: u32  = 1 << 2;

    // SCL timing for standard mode @ 100 MHz APB clock
    pub const SS_SCL_HCNT_100MHZ: u32 = 493;
    pub const SS_SCL_LCNT_100MHZ: u32 = 499;
}

/// PMIC I2C address and bus.
pub const AXP15060_I2C_ADDR: u8 = 0x36;
pub const AXP15060_I2C_BASE: usize = I2C6_BASE;

// ============================================================================
// GPIO / SYS_IOMUX
// ============================================================================
//
// 64 GPIOs (0–63), controlled by sys_iomux (pinmux) and sys_gpio (data).
// Per-pin dout/doen registers — NOT the MMP bank model used by KyX1.

pub const SYS_IOMUX_BASE: usize = 0x1300_0000;
pub const SYS_CRG_BASE: usize   = 0x1302_0000;
pub const SYS_GPIO_BASE: usize  = 0x1304_0000;

pub const GPIO_DOUT_BASE: usize = SYS_GPIO_BASE + 0x000;
pub const GPIO_DOEN_BASE: usize = SYS_GPIO_BASE + 0x040;
pub const GPIO_DIN_BASE: usize  = SYS_GPIO_BASE + 0x080;

#[inline]
pub const fn gpio_dout(n: u32) -> usize {
    GPIO_DOUT_BASE + (n as usize) * 4
}

#[inline]
pub const fn gpio_doen(n: u32) -> usize {
    GPIO_DOEN_BASE + (n as usize) * 4
}

#[inline]
pub const fn gpio_din(n: u32) -> usize {
    GPIO_DIN_BASE + (n as usize) * 4
}

/// DOEN values.
pub const GPIO_OUTPUT_EN: u32 = 0x00; // 0 = output enabled (active low)
pub const GPIO_INPUT_EN: u32  = 0x01; // 1 = input / high-Z

/// Maximum valid GPIO pin number.
pub const GPIO_MAX_PIN: u32 = 63;

// ============================================================================
// AON (Always-On) Domain
// ============================================================================

pub const AON_CRG_BASE: usize   = 0x1700_0000;
pub const AON_IOMUX_BASE: usize = 0x1701_0000;
pub const AON_GPIO_BASE: usize  = 0x1702_0000;

// ============================================================================
// Display Subsystem
// ============================================================================
//
// DC8200 display controller + HDMI 2.0 TX + MIPI DSI TX.
// We use the SimpleFB strategy: U-Boot initializes everything, we just
// parse the DTB for the framebuffer address.

pub const VOUT_CRG_BASE: usize   = 0x295C_0000;
pub const DC8200_BASE: usize     = 0x2940_0000;
pub const HDMI_BASE: usize       = 0x2959_0000;
pub const DSITX_BASE: usize      = 0x295B_0000;
pub const MIPITX_DPHY_BASE: usize = 0x295D_0000;

/// DC8200 register offsets (only what we need for stride probe).
pub const DC8200_STRIDE_REG: usize = 0x1430;

// ============================================================================
// L2 Cache Controller
// ============================================================================
//
// SiFive L2 cache controller for cache line flush (substitute for Zicbom).

pub const L2_CACHE_BASE: usize    = 0x0201_0000;
pub const L2_FLUSH64_OFFSET: usize = 0x200;

// ============================================================================
// SoC Identification Constants
// ============================================================================

/// SiFive JEDEC vendor ID (bank 10, ID 0x09).
pub const MVENDORID: u64 = 0x489;
/// U74-MC architecture ID.
pub const MARCHID_U74: u64 = 0x8000_0000_0000_0007;

// ============================================================================
// SoC Parameters
// ============================================================================

/// Maximum CPU frequency (1.5 GHz).
pub const CPU_FREQ_HZ: u64 = 1_500_000_000;
/// Timer reference frequency (24 MHz OSC).
pub const TIMER_FREQ_HZ: u64 = 24_000_000;
/// Number of U74 CPU cores.
pub const NUM_CORES: u32 = 4;
/// Total RAM on Milk-V Mars 8GB.
pub const TOTAL_RAM: u64 = 8 * 1024 * 1024 * 1024;
/// Maximum junction temperature (millicelsius).
pub const THERMAL_MAX_MC: i32 = 105_000;

/// Peripheral base address (for MemoryInfo reporting).
pub const PERI_BASE: usize = 0x1000_0000;
/// DRAM start address.
pub const DRAM_BASE: usize = 0x4000_0000;
