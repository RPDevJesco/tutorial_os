// =============================================================================
// build.rs — Cargo Build Script for Tutorial-OS
// =============================================================================
//
// This is the Rust equivalent of the Makefile's dynamic configuration.
// It runs at compile time and:
//   1. Selects the correct linker script based on board/SoC features
//   2. Sets up cargo:rerun-if-changed for linker scripts
//   3. Passes board-specific constants (display size, load address, etc.)
//
// In the C build:
//   board.mk sets SOC := bcm2710
//   Makefile uses -T soc/$(SOC)/linker.ld
//
// Here, Cargo features drive the same selection.

fn main() {
    // =========================================================================
    // Linker Script Selection
    // =========================================================================
    //
    // Each SoC has its own linker.ld with the correct load address and
    // memory layout. This mirrors: LDFLAGS := -T soc/$(SOC)/linker.ld

    let linker_script = if cfg!(feature = "soc-bcm2710") {
        "soc/bcm2710/linker.ld"
    } else if cfg!(feature = "soc-bcm2711") {
        "soc/bcm2711/linker.ld"
    } else if cfg!(feature = "soc-bcm2712") {
        "soc/bcm2712/linker.ld"
    } else if cfg!(feature = "soc-kyx1") {
        "soc/kyx1/linker.ld"
    } else if cfg!(feature = "soc-rk3528a") {
        "soc/rk3528a/linker.ld"
    } else if cfg!(feature = "soc-s905x") {
        "soc/s905x/linker.ld"
    } else if cfg!(feature = "soc-lattepanda") {
        "soc/lattepanda/linker.ld"
    } else {
        panic!(
            "No board feature selected! Build with e.g.:\n\
             cargo build --features \"board-rpi-zero2w-gpi\" --target aarch64-unknown-none\n\
             cargo build --features \"board-orangepi-rv2\" --target riscv64gc-unknown-none-elf"
        );
    };

    // Tell the linker to use this script
    println!("cargo:rustc-link-arg=-T{linker_script}");

    // Rebuild if the linker script changes
    println!("cargo:rerun-if-changed={linker_script}");

    // =========================================================================
    // Display Resolution Constants
    // =========================================================================
    //
    // In C: -DDISPLAY_WIDTH=640 -DDISPLAY_HEIGHT=480
    // In Rust: cfg flags that code can check at compile time.
    //
    // The actual values are handled via Rust constants in board config modules,
    // but we emit cfg flags so you can do:
    //   #[cfg(feature = "board-rpi-zero2w-gpi")]
    //   const DISPLAY_WIDTH: u32 = 640;

    // Board-specific cfg values (mirrors -D flags from board.mk)
    if cfg!(feature = "board-rpi-zero2w-gpi") {
        println!("cargo:rustc-cfg=display_width=\"640\"");
        println!("cargo:rustc-cfg=display_height=\"480\"");
    } else if cfg!(feature = "board-rpi-cm4-io") {
        println!("cargo:rustc-cfg=display_width=\"1280\"");
        println!("cargo:rustc-cfg=display_height=\"720\"");
    } else if cfg!(feature = "board-orangepi-rv2") {
        println!("cargo:rustc-cfg=display_width=\"1280\"");
        println!("cargo:rustc-cfg=display_height=\"720\"");
    } else if cfg!(feature = "board-radxa-rock2a") {
        println!("cargo:rustc-cfg=display_width=\"1280\"");
        println!("cargo:rustc-cfg=display_height=\"720\"");
    }

    // =========================================================================
    // Rebuild Triggers
    // =========================================================================
    //
    // Rebuild if any assembly or linker-adjacent files change.
    // Cargo watches .rs files automatically, but not .S or .ld files.

    println!("cargo:rerun-if-changed=build.rs");

    // Watch boot assembly files (these are linked in via global_asm! or
    // the linker, and Cargo doesn't track them automatically)
    for dir in &["boot/arm64", "boot/riscv64", "boot/x86_64"] {
        println!("cargo:rerun-if-changed={dir}");
    }
}