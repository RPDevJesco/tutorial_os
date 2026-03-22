//! Hardware Abstraction Layer for Tutorial-OS.
//!
//! This crate defines the **trait contracts** that every SoC must implement.
//! It contains zero platform-specific code — just types, traits, and portable
//! helper functions.  The `common` crate provides low-level MMIO primitives;
//! each `soc/<name>` crate provides concrete implementations of these traits.
//!
//! # Crate Policy
//!
//! - `#![no_std]` — no standard library.
//! - Zero external dependencies at runtime.
//! - All types are `Send` where possible (bare-metal has no threads, but
//!   the constraint documents intent).
//!
//! # Module Map
//!
//! | Module | C Equivalent | Purpose |
//! |--------|-------------|---------|
//! | [`types`] | `hal_types.h` | Error codes, platform IDs, utility fns |
//! | [`platform`] | `hal_platform.h` | Board init, info, temp, clocks, power |
//! | [`timer`] | `hal_timer.h` | Timing and delay |
//! | [`gpio`] | `hal_gpio.h` | Pin control |
//! | [`display`] | `hal_display.h` | Framebuffer init and presentation |
//! | [`cpu`] | `hal_cpu.h` / `hal_types.h` barriers | Barriers, hints, cache |
//! | [`dma`] | `hal_dma.h` | DMA coherency and transfers |
//! | [`dsi`] | `hal_dsi.h` | MIPI DSI protocol and host ops |

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub mod types;
pub mod platform;
pub mod timer;
pub mod gpio;
pub mod display;
pub mod cpu;
pub mod dma;
pub mod dsi;

/// HAL version — matches the C implementation's `HAL_VERSION_*` defines.
pub const VERSION: &str = "1.0.0";
pub const VERSION_MAJOR: u32 = 1;
pub const VERSION_MINOR: u32 = 0;
pub const VERSION_PATCH: u32 = 0;

// Re-export the most commonly used items at crate root for convenience.
pub use types::{HalError, HalResult};
