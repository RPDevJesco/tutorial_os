//! Shared minimal runtime — `no_std` foundations for Tutorial-OS.
//!
//! This crate provides the lowest-level building blocks that all other
//! crates depend on: volatile memory access primitives and fundamental
//! type definitions.  It has **no** dependencies (not even on `hal`).
//!
//! # Modules
//!
//! | Module | C Equivalent | Purpose |
//! |--------|-------------|---------|
//! | [`mmio`] | `common/mmio.h` | Volatile read/write, barriers, x86 port I/O |
//! | [`types`] | `common/types.h` | Shared constants and type aliases |

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub mod mmio;
pub mod types;
