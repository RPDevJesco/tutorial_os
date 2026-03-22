//! BCM2710/BCM2837 hardware register addresses.
//!
//! BCM2710 = BCM2837 (same silicon, different marketing names).
//! Used in: Raspberry Pi Zero 2W, Pi 3B, Pi 3B+, CM3.
//!
//! # Memory Map
//!
//! | Range | Region |
//! |-------|--------|
//! | `0x0000_0000 – 0x3EFF_FFFF` | ARM RAM (size varies, GPU takes some) |
//! | `0x3F00_0000 – 0x3FFF_FFFF` | Peripheral registers (64 MB window) |
//! | `0x4000_0000 – 0x400F_FFFF` | ARM local peripherals |

#![allow(dead_code)]

// ============================================================================
// Base Addresses
// ============================================================================

pub const PERIPHERAL_BASE: usize = 0x3F00_0000;
pub const LOCAL_BASE: usize      = 0x4000_0000;

// ============================================================================
// System Timer (1 MHz free-running counter)
// ============================================================================

pub const SYSTIMER_BASE: usize = PERIPHERAL_BASE + 0x0000_3000;
pub const SYSTIMER_CS: usize   = SYSTIMER_BASE + 0x00;
pub const SYSTIMER_CLO: usize  = SYSTIMER_BASE + 0x04;
pub const SYSTIMER_CHI: usize  = SYSTIMER_BASE + 0x08;
pub const SYSTIMER_C0: usize   = SYSTIMER_BASE + 0x0C;
pub const SYSTIMER_C1: usize   = SYSTIMER_BASE + 0x10;
pub const SYSTIMER_C2: usize   = SYSTIMER_BASE + 0x14;
pub const SYSTIMER_C3: usize   = SYSTIMER_BASE + 0x18;

// ============================================================================
// GPIO
// ============================================================================

pub const GPIO_BASE: usize = PERIPHERAL_BASE + 0x0020_0000;

// Function Select (10 pins per register, 3 bits per pin)
pub const GPFSEL0: usize = GPIO_BASE + 0x00;
pub const GPFSEL1: usize = GPIO_BASE + 0x04;
pub const GPFSEL2: usize = GPIO_BASE + 0x08;
pub const GPFSEL3: usize = GPIO_BASE + 0x0C;
pub const GPFSEL4: usize = GPIO_BASE + 0x10;
pub const GPFSEL5: usize = GPIO_BASE + 0x14;

// Pin Output Set
pub const GPSET0: usize = GPIO_BASE + 0x1C;
pub const GPSET1: usize = GPIO_BASE + 0x20;

// Pin Output Clear
pub const GPCLR0: usize = GPIO_BASE + 0x28;
pub const GPCLR1: usize = GPIO_BASE + 0x2C;

// Pin Level (read current state)
pub const GPLEV0: usize = GPIO_BASE + 0x34;
pub const GPLEV1: usize = GPIO_BASE + 0x38;

// Pull-up/down control
pub const GPPUD: usize     = GPIO_BASE + 0x94;
pub const GPPUDCLK0: usize = GPIO_BASE + 0x98;
pub const GPPUDCLK1: usize = GPIO_BASE + 0x9C;

/// BCM GPIO function codes (3-bit values, non-sequential!).
pub mod gpio_func {
    pub const INPUT: u32  = 0; // 000
    pub const OUTPUT: u32 = 1; // 001
    pub const ALT0: u32   = 4; // 100
    pub const ALT1: u32   = 5; // 101
    pub const ALT2: u32   = 6; // 110
    pub const ALT3: u32   = 7; // 111
    pub const ALT4: u32   = 3; // 011
    pub const ALT5: u32   = 2; // 010
}

/// Pull resistor codes.
pub mod gpio_pull {
    pub const OFF: u32  = 0;
    pub const DOWN: u32 = 1;
    pub const UP: u32   = 2;
}

pub const GPIO_MAX_PIN: u32 = 53;

// ============================================================================
// Mailbox (ARM ↔ VideoCore communication)
// ============================================================================

pub const MAILBOX_BASE: usize = PERIPHERAL_BASE + 0x0000_B880;
pub const MBOX_READ: usize    = MAILBOX_BASE + 0x00;
pub const MBOX_STATUS: usize  = MAILBOX_BASE + 0x18;
pub const MBOX_WRITE: usize   = MAILBOX_BASE + 0x20;

// Status register bits
pub const MBOX_FULL: u32  = 0x8000_0000;
pub const MBOX_EMPTY: u32 = 0x4000_0000;

// Response codes
pub const MBOX_REQUEST: u32      = 0x0000_0000;
pub const MBOX_RESPONSE_OK: u32  = 0x8000_0000;

// Channels
pub const MBOX_CH_PROP: u8 = 8;

// ============================================================================
// Mailbox Property Tags
// ============================================================================

pub mod tag {
    pub const GET_FIRMWARE_REV: u32    = 0x0000_0001;
    pub const GET_BOARD_MODEL: u32     = 0x0001_0001;
    pub const GET_BOARD_REV: u32       = 0x0001_0002;
    pub const GET_BOARD_MAC: u32       = 0x0001_0003;
    pub const GET_BOARD_SERIAL: u32    = 0x0001_0004;
    pub const GET_ARM_MEMORY: u32      = 0x0001_0005;
    pub const GET_VC_MEMORY: u32       = 0x0001_0006;

    pub const GET_CLOCK_RATE: u32      = 0x0003_0002;
    pub const GET_MAX_CLOCK_RATE: u32  = 0x0003_0004;
    pub const SET_CLOCK_RATE: u32      = 0x0003_8002;
    pub const GET_CLOCK_MEASURED: u32  = 0x0003_0047;

    pub const GET_TEMPERATURE: u32     = 0x0003_0006;
    pub const GET_MAX_TEMP: u32        = 0x0003_000A;
    pub const GET_THROTTLED: u32       = 0x0003_0046;

    pub const GET_POWER_STATE: u32     = 0x0002_0001;
    pub const SET_POWER_STATE: u32     = 0x0002_8001;

    pub const ALLOCATE_BUFFER: u32     = 0x0004_0001;
    pub const SET_PHYSICAL_SIZE: u32   = 0x0004_8003;
    pub const SET_VIRTUAL_SIZE: u32    = 0x0004_8004;
    pub const SET_DEPTH: u32           = 0x0004_8005;
    pub const SET_PIXEL_ORDER: u32     = 0x0004_8006;
    pub const SET_VIRTUAL_OFFSET: u32  = 0x0004_8009;
    pub const GET_PITCH: u32           = 0x0004_0008;
    pub const WAIT_FOR_VSYNC: u32      = 0x0004_800E;

    pub const END: u32                 = 0x0000_0000;
}

// BCM clock IDs
pub mod clock {
    pub const EMMC: u32  = 1;
    pub const UART: u32  = 2;
    pub const ARM: u32   = 3;
    pub const CORE: u32  = 4;
    pub const PWM: u32   = 10;
    pub const PIXEL: u32 = 9;
}

// BCM device IDs (power management)
pub mod device {
    pub const SD: u32    = 0;
    pub const UART0: u32 = 1;
    pub const UART1: u32 = 2;
    pub const USB: u32   = 3;
}

// ============================================================================
// Bus Address Conversion
// ============================================================================

/// Convert ARM physical address to VideoCore bus address.
#[inline(always)]
pub const fn arm_to_bus(addr: u32) -> u32 {
    addr | 0xC000_0000
}

/// Convert VideoCore bus address to ARM physical address.
#[inline(always)]
pub const fn bus_to_arm(addr: u32) -> u32 {
    addr & 0x3FFF_FFFF
}
