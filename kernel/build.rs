//! Build script for the kernel binary crate.
//!
//! Selects the correct linker script based on the active board feature.
//! This is the Rust equivalent of the C build's `LINKER_SCRIPT` variable
//! set in `soc.mk` and passed to the linker via `-T`.

fn main() {
    // Determine which linker script to use based on the board feature.
    // Each SoC crate keeps its linker script alongside its source.
    let linker_script = if cfg!(feature = "board-rpi-zero2w") {
        "soc/bcm2710/linker.ld"
    // Future boards:
    // } else if cfg!(feature = "board-rpi-cm4") {
    //     "soc/bcm2711/linker.ld"
    // } else if cfg!(feature = "board-rpi5") {
    //     "soc/bcm2712/linker.ld"
    // } else if cfg!(feature = "board-orangepi-rv2") {
    //     "soc/kyx1/linker.ld"
    } else if cfg!(feature = "board-milkv-mars") {
         "soc/jh7110/linker.ld"
    // } else if cfg!(feature = "board-lattepanda-mu") {
    //     "soc/n100/linker.ld"
    // } else if cfg!(feature = "board-lattepanda-iota") {
    //     "soc/n150/linker.ld"
    } else {
        // No board selected — no linker script.
        // The binary won't be bootable but `cargo check` still works.
        return;
    };

    // Resolve relative to workspace root (kernel/ is one level down).
    let workspace_root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("..");
    let script_path = workspace_root.join(linker_script);

    // Tell the linker to use this script.
    println!("cargo:rustc-link-arg=-T{}", script_path.display());

    // Tell Cargo to re-run if the linker script changes.
    println!("cargo:rerun-if-changed={}", script_path.display());
}
