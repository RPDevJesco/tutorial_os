//! BCM2710 (BCM2837) SoC implementation for Tutorial-OS.
//!
//! Provides concrete HAL trait implementations for:
//! - Raspberry Pi Zero 2W
//! - Raspberry Pi 3B / 3B+
//! - Raspberry Pi CM3 / CM3+
//!
//! # Modules
//!
//! | Module | Implements | Hardware |
//! |--------|-----------|----------|
//! | [`regs`] | — | Register address definitions |
//! | [`timer`] | [`hal::timer::Timer`] | 1 MHz system timer |
//! | [`gpio`] | [`hal::gpio::Gpio`] | GPFSEL/GPSET/GPCLR registers |
//! | [`mailbox`] | — (internal) | VideoCore property tag interface |
//! | [`display_dpi`] | [`hal::display::Display`] | Framebuffer via mailbox |
//! | [`soc_init`] | [`hal::platform::Platform`] | Board init & info queries |

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub mod regs;
pub mod timer;
pub mod gpio;
pub mod mailbox;
pub mod display_dpi;
pub mod soc_init;

// Re-export the concrete types that the kernel binary needs.
pub use timer::Bcm2710Timer;
pub use gpio::Bcm2710Gpio;
pub use display_dpi::Bcm2710Display;
pub use soc_init::Bcm2710Platform;
