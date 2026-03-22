//! Build script for the JH7110 SoC crate.
//!
//! Compiles the shared RISC-V boot assembly into a static archive that
//! gets linked into the final kernel binary.
//!
//! Assembly sources (matching C project's soc.mk):
//!   boot/riscv64/entry.S        — _start, hart parking, DTB preservation
//!   boot/riscv64/vectors.S      — Trap handler (stvec)
//!   boot/riscv64/common_init.S  — Stack, BSS, FP enable, save DTB
//!
//! NOTE: cache.S is deliberately EXCLUDED.
//!   The SiFive U74 implements RV64GC only — no Zicbom extension.
//!   The Ky X1 (SpacemiT X60) compiles cache.S for its cbo.clean loops.
//!   Same HAL, different ISA extensions, different source set.

use std::path::Path;

fn main() {
    // Shared RISC-V boot assembly (lives at workspace root under boot/)
    let boot_dir = "../../boot/riscv64";
    let shared_sources = [
        format!("{boot_dir}/entry.S"),
        format!("{boot_dir}/vectors.S"),
        format!("{boot_dir}/common_init.S"),
        // NOTE: NO cache.S — U74 has no Zicbom (see jh7110_cpu.h commentary)
    ];

    // Tell Cargo to re-run this script if any assembly file changes.
    for src in &shared_sources {
        println!("cargo:rerun-if-changed={src}");
    }
    println!("cargo:rerun-if-changed=linker.ld");

    // Check if all assembly sources exist.
    let all_present = shared_sources.iter().all(|s| Path::new(s).exists());

    if !all_present {
        println!(
            "cargo:warning=JH7110: boot assembly not found — \
             skipping assembly compilation.  \
             Add boot/riscv64/*.S files to build a bootable kernel."
        );
        return;
    }

    // Compile all assembly sources into a single static library.
    let mut build = cc::Build::new();

    // Preprocessor defines matching C project's soc.mk / board.mk
    build.define("SOC_JH7110", None);
    build.define("LOAD_ADDRESS", "0x40200000");

    // JH7110 hart layout: hart 0 = S7 monitor core, harts 1-4 = U74 app cores.
    // U-Boot boots on hart 1 (first U74).  entry.S uses BOOT_HART_ID to park
    // all other harts.
    build.define("BOOT_HART_ID", "1");
    build.define("NUM_CORES", "4");

    // RAM layout — common_init.S stores these to __ram_base / __ram_size globals.
    // JH7110: DRAM starts at 0x40000000, Milk-V Mars 8GB model.
    build.define("RAM_BASE", "0x40000000");
    build.define("RAM_SIZE", "0x200000000"); // 8 GB

    // Include paths
    build.include(".");
    build.include("../..");

    // CRITICAL: Rust's riscv64gc-unknown-none-elf target uses the lp64d ABI
    // (hard-float double).  The cc crate defaults to lp64 (soft-float) which
    // produces .o files the linker rejects with:
    //   "cannot link object files with different floating-point ABI"
    // Override to match Rust's ABI.
    build.flag("-mabi=lp64d");

    for src in &shared_sources {
        build.file(src);
    }

    // Compile into libboot_asm.a
    build.compile("boot_asm");
}
