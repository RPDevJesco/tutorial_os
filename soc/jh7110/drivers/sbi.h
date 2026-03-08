/*
* drivers/sbi.h — SBI Driver Header for JH7110
 * ==============================================
 */

#ifndef JH7110_SBI_H
#define JH7110_SBI_H

#include "types.h"

/* CPU info populated via SBI ecalls */
typedef struct {
    uint64_t    mvendorid;          /* JEDEC vendor ID (0x489 = SiFive) */
    uint64_t    marchid;            /* Microarch ID (U74 = 0x8000000000000007) */
    uint64_t    mimpid;             /* Implementation ID (firmware version) */
    const char *core_name;          /* Human-readable core name */
    const char *isa;                /* ISA string ("RV64GC") */
    uint32_t    sbi_spec_major;     /* SBI spec major version */
    uint32_t    sbi_spec_minor;     /* SBI spec minor version */
} jh7110_cpu_info_t;

/* SBI base extension queries */
long sbi_get_spec_version(void);
long sbi_probe_extension(long ext_id);
long sbi_get_mvendorid(void);
long sbi_get_marchid(void);
long sbi_get_mimpid(void);

/* JH7110-specific CPU info collection */
void jh7110_sbi_get_cpu_info(jh7110_cpu_info_t *info);

/* System control */
void sbi_shutdown(void);
void sbi_reboot(void);

#endif /* JH7110_SBI_H */