//! JH7110-specific peripheral drivers.
//!
//! These are internal to the SoC crate — they implement the hardware
//! details that the HAL trait implementations in the parent modules
//! delegate to.
//!
//! | Driver | Hardware | Used By |
//! |--------|----------|---------|
//! | [`sbi`] | SBI ecalls | `soc_init` (CPU info, reboot) |
//! | [`i2c`] | DesignWare I2C6 | `pmic_axp15060` |
//! | [`pmic_axp15060`] | AXP15060 PMIC | `soc_init` (temperature, power) |

pub mod sbi;
pub mod i2c;
pub mod pmic_axp15060;
