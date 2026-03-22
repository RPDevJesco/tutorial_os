//! Fundamental type definitions shared across all crates.
//!
//! In the C implementation these live in `common/types.h` and provide
//! `uint32_t`, `size_t`, `bool`, `NULL`, etc.  Rust's built-in primitive
//! types already cover all of this, so this module is much smaller —
//! it exists primarily for cross-cutting definitions that don't belong
//! in the HAL.

/// Cache line size on all supported platforms (ARM64, RISC-V, x86_64).
///
/// Both the `hal::cpu` module and the `common::mmio` module reference
/// this value.  It is duplicated in `hal::cpu::CACHE_LINE_SIZE` for
/// crates that depend on `hal` but not `common`.
pub const CACHE_LINE_SIZE: usize = 64;
