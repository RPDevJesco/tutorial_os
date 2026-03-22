//! SBI (Supervisor Binary Interface) ecall interface.
//!
//! Port of `drivers/sbi.c`.  Provides access to M-mode CSRs (mvendorid,
//! marchid, mimpid) via the SBI Base Extension, and system control via
//! the SBI System Reset Extension.
//!
//! # SBI Extensions Used
//!
//! | EID | Name | Purpose |
//! |-----|------|---------|
//! | 0x10 | Base | Spec version, extension probe, mvendorid/marchid/mimpid |
//! | 0x01 | Legacy Console Putchar | Debug output (see `uart.rs`) |
//! | 0x53525354 | System Reset | Reboot / shutdown |
//!
//! # SiFive U74 vs SpacemiT X60
//!
//! Both are S-mode kernels calling into OpenSBI.  The SBI call sequence
//! is identical.  CPU identification constants differ:
//!   - U74: mvendorid=0x489, marchid=0x8000000000000007
//!   - X60: mvendorid=0x710, marchid=X60-specific

use crate::regs;

/// CPU identification info obtained via SBI ecalls.
///
/// Matches the C `jh7110_cpu_info_t` struct in `sbi.h`.
#[derive(Debug, Clone)]
pub struct CpuInfo {
    pub mvendorid: u64,
    pub marchid: u64,
    pub mimpid: u64,
    pub core_name: &'static str,
    pub isa: &'static str,
    pub sbi_spec_major: u32,
    pub sbi_spec_minor: u32,
}

impl Default for CpuInfo {
    fn default() -> Self {
        Self {
            mvendorid: 0,
            marchid: 0,
            mimpid: 0,
            core_name: "Unknown",
            isa: "RV64GC",
            sbi_spec_major: 0,
            sbi_spec_minor: 0,
        }
    }
}

// ============================================================================
// Raw SBI Ecall
// ============================================================================

struct SbiRet {
    error: i64,
    value: i64,
}

/// Issue an SBI ecall with extension ID, function ID, and up to 6 arguments.
///
/// Matches the C `sbi_ecall(eid, fid, a0, a1, a2, a3, a4, a5)` exactly.
#[inline]
fn sbi_ecall(eid: i64, fid: i64, a0: i64, a1: i64, a2: i64,
             a3: i64, a4: i64, a5: i64) -> SbiRet {
    let error: i64;
    let value: i64;
    unsafe {
        core::arch::asm!(
            "ecall",
            inout("a0") a0 => error,
            inout("a1") a1 => value,
            in("a2") a2,
            in("a3") a3,
            in("a4") a4,
            in("a5") a5,
            in("a6") fid,
            in("a7") eid,
            options(nomem, nostack),
        );
    }
    SbiRet { error, value }
}

// ============================================================================
// SBI Base Extension (EID 0x10)
// ============================================================================

const SBI_EXT_BASE: i64 = 0x10;

/// Get SBI specification version.
///
/// Returns packed value: bits [30:24] = major, bits [23:0] = minor.
pub fn get_spec_version() -> i64 {
    let ret = sbi_ecall(SBI_EXT_BASE, 0, 0, 0, 0, 0, 0, 0);
    if ret.error == 0 { ret.value } else { -1 }
}

/// Probe whether an SBI extension is available.
///
/// Returns nonzero if present, 0 if not.
pub fn probe_extension(ext_id: i64) -> i64 {
    let ret = sbi_ecall(SBI_EXT_BASE, 3, ext_id, 0, 0, 0, 0, 0);
    if ret.error == 0 { ret.value } else { 0 }
}

/// Read mvendorid via SBI Base Extension (FID 4).
pub fn get_mvendorid() -> i64 {
    let ret = sbi_ecall(SBI_EXT_BASE, 4, 0, 0, 0, 0, 0, 0);
    if ret.error == 0 { ret.value } else { -1 }
}

/// Read marchid via SBI Base Extension (FID 5).
pub fn get_marchid() -> i64 {
    let ret = sbi_ecall(SBI_EXT_BASE, 5, 0, 0, 0, 0, 0, 0);
    if ret.error == 0 { ret.value } else { -1 }
}

/// Read mimpid via SBI Base Extension (FID 6).
pub fn get_mimpid() -> i64 {
    let ret = sbi_ecall(SBI_EXT_BASE, 6, 0, 0, 0, 0, 0, 0);
    if ret.error == 0 { ret.value } else { -1 }
}

// ============================================================================
// CPU Info Population
// ============================================================================

/// Populate a [`CpuInfo`] by querying all M-mode CSRs via SBI.
///
/// Matches the C `jh7110_sbi_get_cpu_info()` function.
pub fn get_cpu_info() -> CpuInfo {
    let mvendorid = get_mvendorid() as u64;
    let marchid = get_marchid() as u64;
    let mimpid = get_mimpid() as u64;

    // Identify the core: SiFive U74 = mvendorid 0x489, marchid 0x8000000000000007
    let (core_name, isa) = if mvendorid == regs::MVENDORID
        && marchid == regs::MARCHID_U74
    {
        ("SiFive U74", "RV64GC")
    } else {
        ("RISC-V (unknown)", "RV64GC")
    };

    // SBI spec version
    let spec = get_spec_version();
    let (sbi_spec_major, sbi_spec_minor) = if spec >= 0 {
        (((spec >> 24) & 0x7F) as u32, (spec & 0xFF_FFFF) as u32)
    } else {
        (0, 0)
    };

    CpuInfo {
        mvendorid,
        marchid,
        mimpid,
        core_name,
        isa,
        sbi_spec_major,
        sbi_spec_minor,
    }
}

// ============================================================================
// SBI System Reset Extension (EID 0x53525354 "SRST")
// ============================================================================

const SBI_EXT_SRST: i64 = 0x53525354;
const SBI_LEGACY_SHUTDOWN: i64 = 0x08;

/// Shut down the system.  Falls back to legacy SBI shutdown if SRST unavailable.
pub fn shutdown() -> ! {
    sbi_ecall(SBI_EXT_SRST, 0, 0, 0, 0, 0, 0, 0);
    sbi_ecall(SBI_LEGACY_SHUTDOWN, 0, 0, 0, 0, 0, 0, 0);
    loop { unsafe { core::arch::asm!("wfi", options(nomem, nostack)); } }
}

/// Reboot the system (warm reset).
pub fn reboot() -> ! {
    sbi_ecall(SBI_EXT_SRST, 0, 1, 0, 0, 0, 0, 0);
    loop { unsafe { core::arch::asm!("wfi", options(nomem, nostack)); } }
}
