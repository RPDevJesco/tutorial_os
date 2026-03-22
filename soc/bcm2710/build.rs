//! Build script for the BCM2710 SoC crate.
//!
//! Compiles the shared ARM64 boot assembly and BCM2710-specific boot code
//! into a static archive that gets linked into the final kernel binary.
//!
//! Assembly sources (matching soc.mk):
//!   boot/arm64/entry.S        — _start, EL2→EL1 drop, core parking
//!   boot/arm64/vectors.S      — Exception vector table
//!   boot/arm64/cache.S        — Cache maintenance operations (dc cvac, etc.)
//!   boot/arm64/common_init.S  — Post-SoC common initialization
//!   soc/bcm2710/boot_soc.S    — soc_early_init, mailbox RAM query, MMU setup

use std::path::Path;

fn main() {
    // Shared ARM64 boot assembly (lives at workspace root under boot/)
    let boot_dir = "../../boot/arm64";
    let shared_sources = [
        format!("{boot_dir}/entry.S"),
        format!("{boot_dir}/vectors.S"),
        format!("{boot_dir}/cache.S"),
        format!("{boot_dir}/common_init.S"),
    ];

    // SoC-specific boot assembly (lives alongside this crate)
    let soc_source = "boot_soc.S";

    // Tell Cargo to re-run this script if any assembly file changes.
    for src in &shared_sources {
        println!("cargo:rerun-if-changed={src}");
    }
    println!("cargo:rerun-if-changed={soc_source}");
    println!("cargo:rerun-if-changed=linker.ld");

    // Check if all assembly sources exist.  If the shared boot/ directory
    // hasn't been populated yet, skip compilation so `cargo check` still
    // works during development.  `cargo build` will fail at link time with
    // missing symbols (_start, exception_vectors, etc.) which is a clear
    // enough error.
    let all_present = shared_sources.iter().all(|s| Path::new(s).exists())
        && Path::new(soc_source).exists();

    if !all_present {
        println!(
            "cargo:warning=BCM2710: boot assembly not found — \
             skipping assembly compilation.  \
             Add boot/arm64/*.S files to build a bootable kernel."
        );
        return;
    }

    // Compile all assembly sources into a single static library.
    let mut build = cc::Build::new();

    // Preprocessor defines matching soc.mk: SOC_DEFINES
    build.define("SOC_BCM2710", None);
    build.define("PERIPHERAL_BASE", "0x3F000000");

    // Include paths so assembly can find headers if needed
    build.include(".");
    build.include("../..");

    // Add all source files
    for src in &shared_sources {
        build.file(src);
    }
    build.file(soc_source);

    // Compile into libboot_asm.a
    build.compile("boot_asm");
}
