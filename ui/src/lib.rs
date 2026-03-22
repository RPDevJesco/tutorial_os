//! UI system for Tutorial-OS.
//!
//! Provides the visual layer that renders the Hardware Inspector — the
//! diagnostic display shown on boot.  Built on the portable
//! [`drivers::framebuffer`] crate and themed via the [`themes`] module.
//!
//! # Architecture
//!
//! ```text
//! widgets  →  canvas  →  framebuffer driver  →  HAL display
//! ```
//!
//! Widgets never touch hardware directly.  They draw into a [`Canvas`],
//! which translates/clips and forwards to the framebuffer driver.

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub mod core;
pub mod themes;
pub mod widgets;
