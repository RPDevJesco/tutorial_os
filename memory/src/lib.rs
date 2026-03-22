//! Memory management for Tutorial-OS.
//!
//! Provides a TLSF-inspired allocator suitable for bare-metal environments
//! with no operating system fallback.  Depends only on `common`.

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub mod allocator;
