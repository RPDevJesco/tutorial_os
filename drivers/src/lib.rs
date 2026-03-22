//! Portable drivers for Tutorial-OS.
//!
//! Everything in this crate is architecture-independent — it depends only on
//! `hal` traits and `common` primitives, never on a specific SoC.  This is
//! enforced at the Cargo dependency level: `drivers` cannot depend on any
//! `soc/*` crate.
//!
//! # Modules
//!
//! | Module | Purpose |
//! |--------|---------|
//! | [`framebuffer`] | 32-bit ARGB8888 framebuffer drawing primitives |

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub mod framebuffer;
