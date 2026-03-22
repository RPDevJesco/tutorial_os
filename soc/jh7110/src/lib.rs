//! StarFive JH7110 SoC implementation for Tutorial-OS.
//!
//! Provides concrete HAL trait implementations for the Milk-V Mars
//! (and VisionFive 2, which shares the same SoC).
//!
//! # Comparison with BCM2710
//!
//! | Aspect | BCM2710 (Pi Zero 2W) | JH7110 (Milk-V Mars) |
//! |--------|---------------------|----------------------|
//! | Architecture | ARM64 | RISC-V 64 (RV64GC) |
//! | Timer | 1 MHz system timer | 24 MHz `rdtime` CSR |
//! | GPIO | GPFSEL/GPSET/GPCLR banks | Per-pin dout/doen registers |
//! | Display | Mailbox framebuffer | SimpleFB from DTB |
//! | UART | PL011 / mini UART | DesignWare 8250 + SBI fallback |
//! | PMIC | None (VideoCore) | AXP15060 via I2C6 |
//! | Cache ops | ARM dc cvac | SiFive L2 Flush64 register |
//!
//! # Comparison with KyX1
//!
//! | Aspect | KyX1 (Orange Pi RV2) | JH7110 (Milk-V Mars) |
//! |--------|---------------------|----------------------|
//! | CPU cores | 8× SpacemiT X60 | 4× SiFive U74 |
//! | ISA | RV64GCV (+ Zicbom) | RV64GC only |
//! | UART | PXA/MMP-style | DesignWare 8250 |
//! | GPIO | MMP banks (4×32) | Per-pin (64 GPIOs) |
//! | PMIC | SPM8821 (undoc'd) | AXP15060 (documented) |
//! | Timer | rdtime @ 24 MHz | rdtime @ 24 MHz (identical) |
//! | Display | SimpleFB from DTB | SimpleFB from DTB (identical) |
//!
//! # Modules
//!
//! | Module | Implements | Hardware |
//! |--------|-----------|----------|
//! | [`regs`] | — | Register address definitions |
//! | [`cpu`] | — | RISC-V barriers, wfi, cache line size |
//! | [`uart`] | — (debug I/O) | SBI putchar + DW 8250 direct |
//! | [`timer`] | [`hal::timer::Timer`] | `rdtime` @ 24 MHz |
//! | [`gpio`] | [`hal::gpio::Gpio`] | Per-pin dout/doen + sys_iomux |
//! | [`cache`] | — | SiFive L2 Flush64 cache writeback |
//! | [`display_simplefb`] | [`hal::display::Display`] | DTB → SimpleFB |
//! | [`soc_init`] | [`hal::platform::Platform`] | Board init & info queries |
//! | [`drivers::sbi`] | — | SBI ecalls for CPU info, reboot |
//! | [`drivers::i2c`] | — | DesignWare I2C6 controller |
//! | [`drivers::pmic_axp15060`] | — | AXP15060 temperature & power |

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub mod regs;
pub mod cpu;
pub mod uart;
pub mod timer;
pub mod gpio;
pub mod cache;
pub mod display_simplefb;
pub mod soc_init;
pub mod drivers;

// Re-export the concrete types that the kernel binary needs.
pub use timer::Jh7110Timer;
pub use gpio::Jh7110Gpio;
pub use display_simplefb::Jh7110Display;
pub use soc_init::Jh7110Platform;
