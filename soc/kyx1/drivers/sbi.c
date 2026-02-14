/*
 * sbi.c — RISC-V SBI Queries and CSR Reads for SpacemiT K1
 * ==========================================================
 *
 * This file implements CPU identification and system control for our
 * bare-metal kernel running in S-mode on the SpacemiT Ky X1 (K1) SoC.
 *
 * THREE CATEGORIES OF INFORMATION:
 *
 * 1. SBI ECALLS (M-mode CSRs we can't read directly)
 *    - mvendorid, marchid, mimpid → CPU identification
 *    - SBI spec version, impl ID → firmware identification
 *    - Extension probing → what services are available
 *
 * 2. S-MODE CSR READS (direct access, no ecall needed)
 *    - cycle → CPU cycle counter (for frequency measurement)
 *    - time → wall-clock timer (24 MHz on K1)
 *    - instret → instructions retired counter
 *    - sstatus → supervisor status (FPU state, etc.)
 *    - satp → address translation mode (off in bare-metal)
 *
 * 3. DERIVED VALUES
 *    - CPU frequency: measured by counting cycles over a known
 *      time interval from rdtime
 *
 * ARM vs RISC-V COMPARISON (for the book):
 * ----------------------------------------
 * On ARM, you'd read MIDR_EL1 directly from EL1 to get the CPU ID.
 * On RISC-V, mvendorid/marchid are M-mode only — you MUST ask
 * OpenSBI via an ecall. This is more secure (S-mode can't see M-mode
 * state) but requires firmware cooperation.
 *
 * Similarly, ARM's CNTFRQ_EL0 gives you the timer frequency directly.
 * On RISC-V, the timebase frequency comes from the device tree, and
 * cycle counting requires mcounteren to be configured by M-mode.
 */

#include "sbi.h"

/* From soc/kyx1/uart.c — debug output */
extern void kyx1_uart_puts(const char *str);
extern void kyx1_uart_puthex(uint64_t val);
extern void kyx1_uart_putc(char c);

/* K1 timebase frequency: 24 MHz (from device tree) */
#define KYX1_TIMEBASE_HZ    24000000UL


/* =============================================================================
 * SBI BASE EXTENSION QUERIES
 * =============================================================================
 *
 * These all use EID=0x10 with different FIDs. Each returns a single
 * value in sbi_ret.value (with sbi_ret.error == 0 on success).
 */

static unsigned long sbi_get_spec_version(void)
{
    sbi_ret_t ret = sbi_ecall_0(SBI_EXT_BASE, SBI_BASE_GET_SPEC_VERSION);
    return (ret.error == SBI_SUCCESS) ? ret.value : 0;
}

static unsigned long sbi_get_impl_id(void)
{
    sbi_ret_t ret = sbi_ecall_0(SBI_EXT_BASE, SBI_BASE_GET_IMPL_ID);
    return (ret.error == SBI_SUCCESS) ? ret.value : 0;
}

static unsigned long sbi_get_impl_version(void)
{
    sbi_ret_t ret = sbi_ecall_0(SBI_EXT_BASE, SBI_BASE_GET_IMPL_VERSION);
    return (ret.error == SBI_SUCCESS) ? ret.value : 0;
}

static unsigned long sbi_get_mvendorid(void)
{
    sbi_ret_t ret = sbi_ecall_0(SBI_EXT_BASE, SBI_BASE_GET_MVENDORID);
    return (ret.error == SBI_SUCCESS) ? ret.value : 0;
}

static unsigned long sbi_get_marchid(void)
{
    sbi_ret_t ret = sbi_ecall_0(SBI_EXT_BASE, SBI_BASE_GET_MARCHID);
    return (ret.error == SBI_SUCCESS) ? ret.value : 0;
}

static unsigned long sbi_get_mimpid(void)
{
    sbi_ret_t ret = sbi_ecall_0(SBI_EXT_BASE, SBI_BASE_GET_MIMPID);
    return (ret.error == SBI_SUCCESS) ? ret.value : 0;
}

/*
 * Probe whether an SBI extension is available.
 * Returns true if the extension is supported by OpenSBI.
 */
static bool sbi_probe_extension(unsigned long eid)
{
    sbi_ret_t ret = sbi_ecall_1(SBI_EXT_BASE, SBI_BASE_PROBE_EXT, eid);
    return (ret.error == SBI_SUCCESS) && (ret.value != 0);
}


/* =============================================================================
 * HART ID
 * =============================================================================
 *
 * On RISC-V, the hart (hardware thread) ID is stored in the `tp`
 * (thread pointer) register by convention. OpenSBI sets this up
 * before jumping to S-mode. It corresponds to the mhartid CSR
 * value but is accessible without an ecall.
 *
 * Note: This is a RISC-V convention, not a hardware guarantee.
 * The SBI spec says the hart ID passed to S-mode entry in a0,
 * and OpenSBI also stores it in tp.
 */

static unsigned long get_hart_id(void)
{
    unsigned long hartid;
    __asm__ volatile ("mv %0, tp" : "=r"(hartid));
    return hartid;
}


/* =============================================================================
 * CPU FREQUENCY MEASUREMENT
 * =============================================================================
 *
 * We measure CPU frequency by:
 *   1. Read cycle counter (rdcycle)
 *   2. Read time counter (rdtime) — 24 MHz reference on K1
 *   3. Busy-wait for calibration_ms milliseconds
 *   4. Read both counters again
 *   5. freq_hz = (cycles_delta * timebase_hz) / time_delta
 *
 * This gives us the actual running frequency, which may differ from
 * the nominal max depending on DVFS state.
 *
 * IMPORTANT: If mcounteren.CY is not set by OpenSBI, rdcycle will
 * trap. In that case we return 0 (unknown frequency). The K1's
 * OpenSBI should enable this, but we handle failure gracefully.
 */

uint32_t kyx1_measure_cpu_freq(uint32_t calibration_ms)
{
    if (calibration_ms == 0) calibration_ms = 10;

    /* Calculate how many timebase ticks to wait */
    uint64_t ticks_to_wait = ((uint64_t)KYX1_TIMEBASE_HZ * calibration_ms) / 1000;

    /*
     * Read starting counters.
     * We read time first, cycle second — if there's any overhead,
     * it slightly underestimates frequency (conservative).
     */
    uint64_t time_start = csr_read(time);
    uint64_t cycle_start = csr_read(cycle);

    /* Busy-wait using the time counter as reference */
    uint64_t time_target = time_start + ticks_to_wait;
    while (csr_read(time) < time_target)
        ;

    /* Read ending counters */
    uint64_t cycle_end = csr_read(cycle);
    uint64_t time_end = csr_read(time);

    /* Calculate frequency */
    uint64_t cycle_delta = cycle_end - cycle_start;
    uint64_t time_delta = time_end - time_start;

    if (time_delta == 0)
        return 0;

    /*
     * freq_hz = cycle_delta * timebase_hz / time_delta
     * freq_mhz = freq_hz / 1_000_000
     *
     * To avoid overflow with 64-bit math:
     * freq_mhz = (cycle_delta * (timebase_hz / 1_000_000)) / time_delta
     *           = (cycle_delta * 24) / time_delta
     */
    uint32_t freq_mhz = (uint32_t)((cycle_delta * 24) / time_delta);
    return freq_mhz;
}


/* =============================================================================
 * COMPREHENSIVE CPU INFO QUERY
 * =============================================================================
 */

int kyx1_csr_get_info(kyx1_cpu_info_t *info)
{
    if (!info) return -1;

    /* Zero the struct */
    for (uint32_t i = 0; i < sizeof(kyx1_cpu_info_t); i++)
        ((uint8_t *)info)[i] = 0;

    /* ---- SBI Base Extension: M-mode identification ---- */
    info->mvendorid = sbi_get_mvendorid();
    info->marchid = sbi_get_marchid();
    info->mimpid = sbi_get_mimpid();

    /* ---- SBI implementation info ---- */
    info->sbi_spec_version = sbi_get_spec_version();
    info->sbi_impl_id = sbi_get_impl_id();
    info->sbi_impl_version = sbi_get_impl_version();

    /* ---- Probe available SBI extensions ---- */
    info->has_timer  = sbi_probe_extension(SBI_EXT_TIME);
    info->has_ipi    = sbi_probe_extension(SBI_EXT_IPI);
    info->has_rfence = sbi_probe_extension(SBI_EXT_RFENCE);
    info->has_hsm    = sbi_probe_extension(SBI_EXT_HSM);
    info->has_srst   = sbi_probe_extension(SBI_EXT_SRST);
    info->has_dbcn   = sbi_probe_extension(SBI_EXT_DBCN);

    /* ---- Hart ID (from tp register) ---- */
    info->hart_id = get_hart_id();

    /* ---- S-mode CSRs (direct reads) ---- */
    info->sstatus = csr_read(sstatus);
    info->satp = csr_read(satp);

    /* ---- Performance counter snapshot ---- */
    info->time_val = csr_read(time);
    info->cycles = csr_read(cycle);
    info->instret = csr_read(instret);

    /*
     * ---- CPU frequency measurement ----
     * Use a 10ms calibration window. This is a blocking call
     * but acceptable at boot time for a one-shot measurement.
     */
    info->cpu_freq_mhz = kyx1_measure_cpu_freq(10);

    return 0;
}


/* =============================================================================
 * SYSTEM RESET VIA SBI SRST
 * =============================================================================
 *
 * The SRST extension is the "proper" way to reboot/shutdown on RISC-V.
 * OpenSBI knows the platform-specific mechanism — on the Orange Pi RV2,
 * it likely writes to the SPM8821 PMIC's power control register or
 * uses an SoC-specific reset controller.
 *
 * This is cleaner than our PMIC stubs because:
 * 1. We don't need to know the PMIC register map
 * 2. OpenSBI handles platform differences
 * 3. It works even if our I2C driver has issues
 *
 * These functions only return on failure — success means the system
 * has already reset/powered off.
 */

int kyx1_sbi_reboot(void)
{
    /* First check if SRST is available */
    if (!sbi_probe_extension(SBI_EXT_SRST)) {
        kyx1_uart_puts("[sbi] SRST extension not available\n");
        return -1;
    }

    kyx1_uart_puts("[sbi] Requesting cold reboot via SRST...\n");

    sbi_ret_t ret = sbi_ecall_2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET,
                                 SBI_SRST_TYPE_COLD_REBOOT,
                                 SBI_SRST_REASON_NONE);

    /* If we get here, the reset failed */
    kyx1_uart_puts("[sbi] Reboot failed! error=");
    kyx1_uart_puthex(ret.error);
    kyx1_uart_putc('\n');
    return (int)ret.error;
}

int kyx1_sbi_shutdown(void)
{
    if (!sbi_probe_extension(SBI_EXT_SRST)) {
        kyx1_uart_puts("[sbi] SRST extension not available\n");
        return -1;
    }

    kyx1_uart_puts("[sbi] Requesting shutdown via SRST...\n");

    sbi_ret_t ret = sbi_ecall_2(SBI_EXT_SRST, SBI_SRST_SYSTEM_RESET,
                                 SBI_SRST_TYPE_SHUTDOWN,
                                 SBI_SRST_REASON_NONE);

    kyx1_uart_puts("[sbi] Shutdown failed! error=");
    kyx1_uart_puthex(ret.error);
    kyx1_uart_putc('\n');
    return (int)ret.error;
}


/* =============================================================================
 * HELPER: SBI IMPLEMENTATION NAME
 * =============================================================================
 */

const char *sbi_impl_id_to_string(unsigned long impl_id)
{
    switch (impl_id) {
        case SBI_IMPL_BBL:      return "BBL (Berkeley Boot Loader)";
        case SBI_IMPL_OPENSBI:  return "OpenSBI";
        case SBI_IMPL_XVISOR:   return "Xvisor";
        case SBI_IMPL_KVM:      return "KVM";
        case SBI_IMPL_RUSTSBI:  return "RustSBI";
        case SBI_IMPL_DIOSIX:   return "Diosix";
        case SBI_IMPL_COFFER:   return "Coffer";
        case SBI_IMPL_XEN:      return "Xen";
        default:                return "Unknown";
    }
}


/* =============================================================================
 * PRETTY-PRINT CPU INFO (UART DEBUG)
 * =============================================================================
 *
 * Dumps everything we know about the CPU to UART. Useful for
 * initial bring-up and verifying that SBI ecalls work correctly.
 *
 * Expected output on Orange Pi RV2:
 *
 *   === RISC-V CPU Information ===
 *   SBI: OpenSBI v1.x (spec v2.0)
 *   Vendor ID:  0x????????
 *   Arch ID:    0x????????
 *   Impl ID:    0x????????
 *   Hart ID:    0
 *   CPU freq:   ~1600 MHz
 *   sstatus:    0x0000000200006000  (FS=Initial)
 *   satp:       0x0000000000000000  (MMU off)
 *   Extensions: TIME IPI RFNC HSM SRST DBCN
 *   ==============================
 */

/* Helper: print decimal number */
static void put_dec(uint32_t val)
{
    char buf[12];
    int i = 0;

    if (val == 0) {
        kyx1_uart_putc('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        kyx1_uart_putc(buf[--i]);
    }
}

void kyx1_csr_print_info(void)
{
    kyx1_cpu_info_t info;

    if (kyx1_csr_get_info(&info) < 0) {
        kyx1_uart_puts("[csr] Failed to query CPU info\n");
        return;
    }

    kyx1_uart_puts("\n=== RISC-V CPU Information ===\n");

    /* SBI firmware */
    kyx1_uart_puts("  SBI: ");
    kyx1_uart_puts(sbi_impl_id_to_string(info.sbi_impl_id));
    kyx1_uart_puts(" v");
    put_dec((uint32_t)(info.sbi_impl_version >> 16));  /* Major (varies by impl) */
    kyx1_uart_putc('.');
    put_dec((uint32_t)(info.sbi_impl_version & 0xFFFF));
    kyx1_uart_puts(" (spec v");
    put_dec((uint32_t)(info.sbi_spec_version >> 24));   /* Major */
    kyx1_uart_putc('.');
    put_dec((uint32_t)(info.sbi_spec_version & 0xFFFFFF)); /* Minor */
    kyx1_uart_puts(")\n");

    /* CPU identification */
    kyx1_uart_puts("  Vendor ID:  0x");
    kyx1_uart_puthex(info.mvendorid);
    kyx1_uart_putc('\n');

    kyx1_uart_puts("  Arch ID:    0x");
    kyx1_uart_puthex(info.marchid);
    kyx1_uart_putc('\n');

    kyx1_uart_puts("  Impl ID:    0x");
    kyx1_uart_puthex(info.mimpid);
    kyx1_uart_putc('\n');

    /* Hart */
    kyx1_uart_puts("  Hart ID:    ");
    put_dec((uint32_t)info.hart_id);
    kyx1_uart_putc('\n');

    /* Frequency */
    kyx1_uart_puts("  CPU freq:   ~");
    if (info.cpu_freq_mhz > 0) {
        put_dec(info.cpu_freq_mhz);
        kyx1_uart_puts(" MHz\n");
    } else {
        kyx1_uart_puts("unknown (cycle counter unavailable?)\n");
    }

    /* S-mode CSRs */
    kyx1_uart_puts("  sstatus:    0x");
    kyx1_uart_puthex(info.sstatus);

    /*
     * Decode sstatus.FS field (bits 14:13) — FPU state
     *   00 = Off, 01 = Initial, 10 = Clean, 11 = Dirty
     */
    uint32_t fs = (info.sstatus >> 13) & 0x3;
    switch (fs) {
        case 0: kyx1_uart_puts("  (FS=Off)");     break;
        case 1: kyx1_uart_puts("  (FS=Initial)"); break;
        case 2: kyx1_uart_puts("  (FS=Clean)");   break;
        case 3: kyx1_uart_puts("  (FS=Dirty)");   break;
    }
    kyx1_uart_putc('\n');

    kyx1_uart_puts("  satp:       0x");
    kyx1_uart_puthex(info.satp);
    if (info.satp == 0)
        kyx1_uart_puts("  (MMU off — bare metal)");
    kyx1_uart_putc('\n');

    /* SBI extensions */
    kyx1_uart_puts("  Extensions:");
    if (info.has_timer)  kyx1_uart_puts(" TIME");
    if (info.has_ipi)    kyx1_uart_puts(" IPI");
    if (info.has_rfence) kyx1_uart_puts(" RFNC");
    if (info.has_hsm)    kyx1_uart_puts(" HSM");
    if (info.has_srst)   kyx1_uart_puts(" SRST");
    if (info.has_dbcn)   kyx1_uart_puts(" DBCN");
    kyx1_uart_putc('\n');

    /* Performance counters */
    kyx1_uart_puts("  Cycles:     ");
    kyx1_uart_puthex(info.cycles);
    kyx1_uart_putc('\n');

    kyx1_uart_puts("  Instret:    ");
    kyx1_uart_puthex(info.instret);
    kyx1_uart_putc('\n');

    kyx1_uart_puts("==============================\n\n");
}
