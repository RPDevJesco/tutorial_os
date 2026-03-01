// =============================================================================
// soc/mod.rs — SoC Module Router
// =============================================================================
//
// This is the Rust equivalent of the Makefile's:
//   include soc/$(SOC)/soc.mk
//
// Only ONE SoC module compiles per build, selected by Cargo features.
// Each SoC module implements the HAL traits (CpuOps, PlatformOps, etc.)
// for its specific hardware.

#[cfg(feature = "soc-bcm2710")]
pub mod bcm2710;

#[cfg(feature = "soc-bcm2711")]
pub mod bcm2711;

#[cfg(feature = "soc-bcm2712")]
pub mod bcm2712;

#[cfg(feature = "soc-kyx1")]
pub mod kyx1;

#[cfg(feature = "soc-rk3528a")]
pub mod rk3528a;

#[cfg(feature = "soc-s905x")]
pub mod s905x;

#[cfg(feature = "soc-lattepanda")]
pub mod lattepanda;

// ── Re-export the active platform as a uniform name ──
// This lets portable code use `soc::Platform` without knowing which SoC.

#[cfg(feature = "soc-bcm2710")]
pub use bcm2710::Bcm2710Platform as Platform;

#[cfg(feature = "soc-bcm2711")]
pub use bcm2711::Bcm2711Platform as Platform;

#[cfg(feature = "soc-bcm2712")]
pub use bcm2712::Bcm2712Platform as Platform;

#[cfg(feature = "soc-kyx1")]
pub use kyx1::KyX1Platform as Platform;

#[cfg(feature = "soc-rk3528a")]
pub use rk3528a::Rk3528aPlatform as Platform;

#[cfg(feature = "soc-s905x")]
pub use s905x::S905xPlatform as Platform;

#[cfg(feature = "soc-lattepanda")]
pub use lattepanda::LattePandaPlatform as Platform;