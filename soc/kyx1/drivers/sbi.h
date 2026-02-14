/*
 * sbi.h — RISC-V SBI (Supervisor Binary Interface) and CSR Access
 * ================================================================
 *
 * This header provides two things for our bare-metal RISC-V kernel:
 *
 * 1. SBI ECALL WRAPPERS
 *    From S-mode, we cannot directly read M-mode CSRs like mvendorid,
 *    marchid, mimpid, or mhartid. OpenSBI runs in M-mode and exposes
 *    these values through the SBI Base Extension (EID 0x10). We make
 *    ecall instructions with the appropriate extension/function IDs
 *    and OpenSBI returns the values in a0/a1.
 *
 * 2. CSR READ MACROS
 *    Some CSRs *are* directly readable from S-mode:
 *      - cycle, time, instret (performance counters, if mcounteren allows)
 *      - sstatus, satp, scounteren (supervisor-mode configuration)
 *    These use inline assembly with the `csrr` instruction.
 *
 * WHY THIS MATTERS FOR TUTORIAL-OS:
 * ---------------------------------
 * This is a great teaching moment about the RISC-V privilege model.
 * ARM has a flat register space where EL1 can read most identification
 * registers directly (MIDR_EL1, etc). RISC-V strictly partitions
 * registers by privilege level — M-mode CSRs are completely invisible
 * to S-mode. The SBI is the standard escape hatch.
 *
 * CALLING CONVENTION:
 *   a7 = Extension ID (EID)
 *   a6 = Function ID (FID)
 *   a0-a5 = arguments
 *   Returns: a0 = error code, a1 = value
 *
 * Reference: RISC-V SBI Specification v2.0
 *            https://github.com/riscv-non-isa/riscv-sbi-doc
 */

#ifndef SBI_H
#define SBI_H

#include "types.h"

/* =============================================================================
 * SBI RETURN TYPE
 * =============================================================================
 *
 * Every SBI call returns a pair: error code in a0, value in a1.
 * Error code 0 = SBI_SUCCESS.
 */

typedef struct {
    long error;     /* a0: 0 = success, negative = error */
    long value;     /* a1: return value (only valid if error == 0) */
} sbi_ret_t;

/* Standard SBI error codes */
#define SBI_SUCCESS                 0
#define SBI_ERR_FAILED             -1
#define SBI_ERR_NOT_SUPPORTED      -2
#define SBI_ERR_INVALID_PARAM      -3
#define SBI_ERR_DENIED             -4
#define SBI_ERR_INVALID_ADDRESS    -5
#define SBI_ERR_ALREADY_AVAILABLE  -6
#define SBI_ERR_ALREADY_STARTED    -7
#define SBI_ERR_ALREADY_STOPPED    -8


/* =============================================================================
 * SBI EXTENSION IDs (EIDs)
 * =============================================================================
 *
 * Each SBI extension has a unique 32-bit ID. The Base extension (0x10)
 * is mandatory — every SBI implementation must support it. Other
 * extensions can be probed via sbi_probe_extension().
 */

/* Base Extension — mandatory, always available */
#define SBI_EXT_BASE                0x10

/* Timer Extension — replacement for legacy set_timer */
#define SBI_EXT_TIME                0x54494D45  /* "TIME" */

/* IPI Extension — inter-processor interrupts */
#define SBI_EXT_IPI                 0x735049    /* "sPI" */

/* RFENCE Extension — remote fence operations */
#define SBI_EXT_RFENCE              0x52464E43  /* "RFNC" */

/* HSM Extension — Hart State Management (start/stop/suspend harts) */
#define SBI_EXT_HSM                 0x48534D    /* "HSM" */

/* SRST Extension — System Reset (reboot/shutdown) */
#define SBI_EXT_SRST                0x53525354  /* "SRST" */

/* DBCN Extension — Debug Console (puts/getchar) */
#define SBI_EXT_DBCN                0x4442434E  /* "DBCN" */


/* =============================================================================
 * SBI BASE EXTENSION — Function IDs (FIDs)
 * =============================================================================
 *
 * The Base extension lets us query SBI implementation details and
 * read M-mode identification CSRs that we can't access from S-mode.
 */

#define SBI_BASE_GET_SPEC_VERSION   0   /* Returns SBI spec version */
#define SBI_BASE_GET_IMPL_ID        1   /* Returns implementation ID */
#define SBI_BASE_GET_IMPL_VERSION   2   /* Returns implementation version */
#define SBI_BASE_PROBE_EXT          3   /* Check if extension available */
#define SBI_BASE_GET_MVENDORID      4   /* M-mode mvendorid CSR value */
#define SBI_BASE_GET_MARCHID        5   /* M-mode marchid CSR value */
#define SBI_BASE_GET_MIMPID         6   /* M-mode mimpid CSR value */

/* Known SBI implementation IDs (from spec Table 4) */
#define SBI_IMPL_BBL                0   /* Berkeley Boot Loader */
#define SBI_IMPL_OPENSBI            1   /* OpenSBI */
#define SBI_IMPL_XVISOR             2   /* Xvisor */
#define SBI_IMPL_KVM                3   /* KVM */
#define SBI_IMPL_RUSTSBI            4   /* RustSBI */
#define SBI_IMPL_DIOSIX             5   /* Diosix */
#define SBI_IMPL_COFFER             6   /* Coffer */
#define SBI_IMPL_XEN                7   /* Xen */


/* =============================================================================
 * SBI SRST EXTENSION — Function IDs
 * =============================================================================
 *
 * System Reset extension allows us to reboot or shutdown the system
 * without needing to know the PMIC register addresses. OpenSBI
 * handles the platform-specific power control.
 */

#define SBI_SRST_SYSTEM_RESET       0

/* Reset types */
#define SBI_SRST_TYPE_SHUTDOWN      0   /* Power off */
#define SBI_SRST_TYPE_COLD_REBOOT   1   /* Full power cycle */
#define SBI_SRST_TYPE_WARM_REBOOT   2   /* Reset without power cycle */

/* Reset reasons */
#define SBI_SRST_REASON_NONE        0   /* No reason */
#define SBI_SRST_REASON_FAILURE     1   /* System failure */


/* =============================================================================
 * SBI HSM EXTENSION — Function IDs
 * =============================================================================
 */

#define SBI_HSM_HART_START          0
#define SBI_HSM_HART_STOP           1
#define SBI_HSM_HART_GET_STATUS     2
#define SBI_HSM_HART_SUSPEND        3


/* =============================================================================
 * RAW SBI ECALL
 * =============================================================================
 *
 * This is the fundamental primitive. Every SBI function goes through
 * this inline assembly sequence. We use a0-a5 for arguments (matching
 * the SBI spec), a6 for FID, a7 for EID.
 *
 * The inline asm clobbers are carefully chosen:
 *   - a0, a1 are output (error, value)
 *   - a2-a5 are input arguments (may be clobbered)
 *   - a6, a7 are EID/FID
 *   - "memory" clobber because ecall may have side effects
 */

static inline sbi_ret_t sbi_ecall(unsigned long eid, unsigned long fid,
                                   unsigned long a0, unsigned long a1,
                                   unsigned long a2, unsigned long a3,
                                   unsigned long a4, unsigned long a5)
{
    sbi_ret_t ret;

    register unsigned long r_a0 __asm__("a0") = a0;
    register unsigned long r_a1 __asm__("a1") = a1;
    register unsigned long r_a2 __asm__("a2") = a2;
    register unsigned long r_a3 __asm__("a3") = a3;
    register unsigned long r_a4 __asm__("a4") = a4;
    register unsigned long r_a5 __asm__("a5") = a5;
    register unsigned long r_a6 __asm__("a6") = fid;
    register unsigned long r_a7 __asm__("a7") = eid;

    __asm__ volatile (
        "ecall"
        : "+r"(r_a0), "+r"(r_a1)
        : "r"(r_a2), "r"(r_a3), "r"(r_a4), "r"(r_a5),
          "r"(r_a6), "r"(r_a7)
        : "memory"
    );

    ret.error = r_a0;
    ret.value = r_a1;
    return ret;
}

/* Convenience: ecall with no arguments (most Base extension calls) */
static inline sbi_ret_t sbi_ecall_0(unsigned long eid, unsigned long fid)
{
    return sbi_ecall(eid, fid, 0, 0, 0, 0, 0, 0);
}

/* Convenience: ecall with 1 argument */
static inline sbi_ret_t sbi_ecall_1(unsigned long eid, unsigned long fid,
                                      unsigned long a0)
{
    return sbi_ecall(eid, fid, a0, 0, 0, 0, 0, 0);
}

/* Convenience: ecall with 2 arguments */
static inline sbi_ret_t sbi_ecall_2(unsigned long eid, unsigned long fid,
                                      unsigned long a0, unsigned long a1)
{
    return sbi_ecall(eid, fid, a0, a1, 0, 0, 0, 0);
}


/* =============================================================================
 * S-MODE CSR READ MACROS
 * =============================================================================
 *
 * These CSRs are directly readable from S-mode (no ecall needed).
 * We use GCC's inline asm with the `csrr` instruction.
 *
 * NOTE: cycle, time, and instret require mcounteren bits to be set
 * by M-mode firmware. OpenSBI typically enables these for S-mode.
 */

#define csr_read(csr)                                           \
({                                                              \
    unsigned long __val;                                        \
    __asm__ volatile ("csrr %0, " #csr : "=r"(__val));          \
    __val;                                                      \
})

#define csr_write(csr, val)                                     \
({                                                              \
    unsigned long __val = (unsigned long)(val);                  \
    __asm__ volatile ("csrw " #csr ", %0" :: "rK"(__val));      \
})

#define csr_set(csr, val)                                       \
({                                                              \
    unsigned long __val = (unsigned long)(val);                  \
    __asm__ volatile ("csrs " #csr ", %0" :: "rK"(__val));      \
})

#define csr_clear(csr, val)                                     \
({                                                              \
    unsigned long __val = (unsigned long)(val);                  \
    __asm__ volatile ("csrc " #csr ", %0" :: "rK"(__val));      \
})

/*
 * Readable S-mode CSRs we care about:
 *
 * cycle   (0xC00) — CPU cycle counter
 * time    (0xC01) — Wall-clock time (timebase frequency)
 * instret (0xC02) — Instructions retired
 * sstatus (0x100) — Supervisor status (FS field shows FPU state)
 * satp    (0x180) — Address translation config (MODE=0 means MMU off)
 */


/* =============================================================================
 * CPU INFORMATION STRUCTURE
 * =============================================================================
 *
 * Filled by kyx1_csr_get_info(). Contains everything we can learn
 * about the CPU from CSRs and SBI calls.
 */

typedef struct {
    /* From SBI Base Extension (M-mode CSRs via ecall) */
    unsigned long mvendorid;        /* JEDEC manufacturer ID */
    unsigned long marchid;          /* Microarchitecture ID */
    unsigned long mimpid;           /* Implementation version */

    /* SBI implementation info */
    unsigned long sbi_spec_version; /* SBI spec version (major.minor packed) */
    unsigned long sbi_impl_id;      /* 0=BBL, 1=OpenSBI, etc */
    unsigned long sbi_impl_version; /* Implementation-specific version */

    /* Available SBI extensions (probed) */
    bool has_timer;                 /* TIME extension */
    bool has_ipi;                   /* IPI extension */
    bool has_rfence;                /* RFENCE extension */
    bool has_hsm;                   /* HSM (Hart State Management) */
    bool has_srst;                  /* SRST (System Reset) */
    bool has_dbcn;                  /* DBCN (Debug Console) */

    /* Hart (hardware thread) info */
    unsigned long hart_id;          /* Current hart ID (from tp register) */

    /* From S-mode CSRs (direct reads) */
    unsigned long sstatus;          /* Supervisor status register */
    unsigned long satp;             /* Address translation config */

    /* Measured CPU frequency */
    uint32_t cpu_freq_mhz;         /* Estimated from cycle/time ratio */

    /* Performance counters snapshot */
    uint64_t cycles;                /* Cycle counter at query time */
    uint64_t instret;               /* Instructions retired at query time */
    uint64_t time_val;              /* Time counter at query time */
} kyx1_cpu_info_t;


/* =============================================================================
 * PUBLIC API
 * =============================================================================
 */

/*
 * Query all CPU identification and status information.
 * Fills the provided struct with SBI ecall results, CSR reads,
 * and a CPU frequency estimate.
 */
int kyx1_csr_get_info(kyx1_cpu_info_t *info);

/*
 * Measure CPU frequency by counting cycles over a known time interval.
 * Returns frequency in MHz. Uses rdtime (24 MHz timebase on K1) as
 * the reference clock and rdcycle as the measurement.
 *
 * calibration_ms: how long to measure (longer = more accurate, 10-100ms typical)
 */
uint32_t kyx1_measure_cpu_freq(uint32_t calibration_ms);

/*
 * System reset via SBI SRST extension.
 * These are the "clean" way to reboot/shutdown — OpenSBI knows how
 * to talk to the PMIC or reset controller for the specific platform.
 *
 * Returns only on failure (successful reset never returns).
 */
int kyx1_sbi_reboot(void);
int kyx1_sbi_shutdown(void);

/*
 * Print CPU info to UART for debugging.
 * Shows SBI version, vendor/arch IDs, extensions, frequency, etc.
 */
void kyx1_csr_print_info(void);

/*
 * Get string name for SBI implementation ID.
 */
const char *sbi_impl_id_to_string(unsigned long impl_id);


#endif /* SBI_H */
