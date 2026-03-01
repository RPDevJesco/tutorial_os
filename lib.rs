// =============================================================================
// src/lib.rs — Tutorial-OS Crate Root
// =============================================================================
//
// This is the top-level module that mirrors the C build's source tree.
// Feature flags (set via Cargo.toml) control which SoC modules compile,
// exactly like the C build's board.mk → soc.mk inclusion chain.
//
// Module structure mirrors the directory layout:
//   src/
//   ├── lib.rs              ← You are here
//   ├── hal/                ← Hardware Abstraction Layer (portable interfaces)
//   │   ├── mod.rs
//   │   ├── hal_types.rs
//   │   ├── hal_cpu.rs
//   │   ├── hal_platform.rs
//   │   ├── hal_timer.rs
//   │   ├── hal_gpio.rs
//   │   └── hal_display.rs
//   ├── soc/                ← SoC implementations (feature-gated)
//   │   ├── mod.rs
//   │   ├── bcm2710/        ← #[cfg(feature = "soc-bcm2710")]
//   │   ├── bcm2711/        ← #[cfg(feature = "soc-bcm2711")]
//   │   ├── kyx1/           ← #[cfg(feature = "soc-kyx1")]
//   │   └── ...
//   ├── drivers/            ← Portable drivers (framebuffer, fonts, etc.)
//   │   ├── mod.rs
//   │   └── framebuffer/
//   ├── ui/                 ← UI widget system
//   │   ├── mod.rs
//   │   ├── core.rs
//   │   ├── themes.rs
//   │   └── widgets/
//   ├── memory/             ← Allocator, memory management
//   │   ├── mod.rs
//   │   └── allocator.rs
//   └── kernel/             ← Kernel entry point and main loop
//       └── mod.rs

#![no_std]
#![no_main]

// ── Core modules (always compiled) ──
pub mod hal;
pub mod drivers;

// ── SoC implementations (only the selected one compiles) ──
pub mod soc;

// ── Optional subsystems (enabled via features) ──
#[cfg(feature = "alloc")]
pub mod memory;

#[cfg(feature = "ui")]
pub mod ui;

pub mod kernel;