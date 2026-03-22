/*
 * drivers/sbi.c — SBI (Supervisor Binary Interface) Driver for JH7110
 * =====================================================================
 *
 * THIS IS NEARLY IDENTICAL TO soc/kyx1/drivers/sbi.c.
 *
 * Both the Ky X1 and JH7110 run OpenSBI in M-mode. Our kernel runs in
 * S-mode. The SBI specification is architecture-defined — the ecall
 * convention, extension IDs, and function IDs are the same regardless
 * of the underlying SoC.
 *
 * The only reason this is a separate file (rather than a shared sbi.c
 * in common/) is that it uses JH7110-specific constants for CPU vendor
 * and arch ID validation, and we keep each SoC directory self-contained.
 *
 * SBI OVERVIEW:
 * =============
 * SBI (Supervisor Binary Interface) is the RISC-V equivalent of BIOS/UEFI
 * for M-mode to S-mode communication. OpenSBI implements it.
 *
 * We call SBI via ecall with:
 *   a7 = Extension ID (EID)
 *   a6 = Function ID (FID) — 0 for legacy extensions
 *   a0-a5 = Arguments
 *   Return: a0 = error code, a1 = value
 *
 * EXTENSIONS WE USE:
 *   0x01        — Legacy console putchar (always available, single char)
 *   0x10 FID0   — SBI specification version probe
 *   0x10 FID1   — Extension probe (check if an extension is present)
 *   0x54494D45  — SBI Timer (set_timer)
 *   0x48534D   — Hart State Management (hart_start, hart_stop)
 *
 * CPU CSR READS:
 *   RISC-V CSRs mvendorid, marchid, mimpid are M-mode only.
 *   We must use the SBI GetMachineVendorID extension (if available)
 *   or read them via the SBI v0.1 CSR extension.
 *
 *   Alternative: OpenSBI exposes these via the RFENCE extension on
 *   newer platforms. The simplest approach for Tutorial-OS is to
 *   read them directly in M-mode during OpenSBI's platform init —
 *   but since we don't modify OpenSBI, we use the base SBI calls.
 *
 *   On the JH7110:
 *     mvendorid = 0x489 (SiFive, JEDEC bank 10 ID 0x09)
 *     marchid   = 0x8000000000000007 (U74-MC identifier)
 *
 * SIFIVE U74 vs SPACEMIT X60 IDENTIFICATION:
 *   Both are S-mode kernels calling into OpenSBI. The SBI call sequence
 *   is identical. The CPU identification constants differ:
 *     U74:  mvendorid=0x489, marchid=0x8000000000000007
 *     X60:  mvendorid=0x710, marchid=X60-specific
 *   This is how hal_platform_get_info() can report the right CPU details.
 */

#include "sbi.h"
#include "jh7110_regs.h"
#include "types.h"

/* =============================================================================
 * RAW SBI ECALL
 * =============================================================================
 */

typedef struct {
    long error;
    long value;
} sbi_ret_t;

static inline sbi_ret_t sbi_ecall(long eid, long fid,
                                   long a0, long a1, long a2,
                                   long a3, long a4, long a5)
{
    sbi_ret_t ret;
    register long ra0 __asm__("a0") = a0;
    register long ra1 __asm__("a1") = a1;
    register long ra2 __asm__("a2") = a2;
    register long ra3 __asm__("a3") = a3;
    register long ra4 __asm__("a4") = a4;
    register long ra5 __asm__("a5") = a5;
    register long ra6 __asm__("a6") = fid;
    register long ra7 __asm__("a7") = eid;

    __asm__ volatile(
        "ecall"
        : "+r"(ra0), "+r"(ra1)
        : "r"(ra2), "r"(ra3), "r"(ra4), "r"(ra5), "r"(ra6), "r"(ra7)
        : "memory"
    );

    ret.error = ra0;
    ret.value = ra1;
    return ret;
}

/* =============================================================================
 * SBI BASE EXTENSION (EID 0x10)
 * =============================================================================
 */

long sbi_get_spec_version(void)
{
    sbi_ret_t ret = sbi_ecall(0x10, 0, 0, 0, 0, 0, 0, 0);
    return ret.error == 0 ? ret.value : -1;
}

long sbi_probe_extension(long ext_id)
{
    sbi_ret_t ret = sbi_ecall(0x10, 3, ext_id, 0, 0, 0, 0, 0);
    return ret.error == 0 ? ret.value : 0;
}

long sbi_get_mvendorid(void)
{
    sbi_ret_t ret = sbi_ecall(0x10, 4, 0, 0, 0, 0, 0, 0);
    return ret.error == 0 ? ret.value : -1;
}

long sbi_get_marchid(void)
{
    sbi_ret_t ret = sbi_ecall(0x10, 5, 0, 0, 0, 0, 0, 0);
    return ret.error == 0 ? ret.value : -1;
}

long sbi_get_mimpid(void)
{
    sbi_ret_t ret = sbi_ecall(0x10, 6, 0, 0, 0, 0, 0, 0);
    return ret.error == 0 ? ret.value : -1;
}

/* =============================================================================
 * CPU INFO POPULATION
 * =============================================================================
 */

void jh7110_sbi_get_cpu_info(jh7110_cpu_info_t *info)
{
    if (!info) return;

    info->mvendorid = (uint64_t)sbi_get_mvendorid();
    info->marchid   = (uint64_t)sbi_get_marchid();
    info->mimpid    = (uint64_t)sbi_get_mimpid();

    /* Identify the core: SiFive U74 = mvendorid 0x489, marchid 0x8000000000000007 */
    if (info->mvendorid == JH7110_MVENDORID &&
        info->marchid   == JH7110_MARCHID_U74) {
        info->core_name = "SiFive U74";
        info->isa       = "RV64GC";
    } else {
        info->core_name = "RISC-V (unknown)";
        info->isa       = "RV64GC";
    }

    /* SBI spec version for display */
    long spec = sbi_get_spec_version();
    info->sbi_spec_major = (spec >> 24) & 0x7F;
    info->sbi_spec_minor = spec & 0xFFFFFF;
}

/* =============================================================================
 * SYSTEM CONTROL
 * =============================================================================
 */

void sbi_shutdown(void)
{
    /* SBI System Reset extension (EID 0x53525354) */
    sbi_ecall(0x53525354, 0, 0, 0, 0, 0, 0, 0);
    /* If the above fails, try legacy SBI shutdown */
    sbi_ecall(0x08, 0, 0, 0, 0, 0, 0, 0);
    /* Should not reach here */
    while (1) __asm__ volatile("wfi");
}

void sbi_reboot(void)
{
    /* SBI System Reset: warm reset */
    sbi_ecall(0x53525354, 0, 1, 0, 0, 0, 0, 0);
    while (1) __asm__ volatile("wfi");
}