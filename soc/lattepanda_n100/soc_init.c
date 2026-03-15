/*
 * soc/lattepanda_n100/soc_init.c — UEFI Entry Point and Platform Init
 *
 * Tutorial-OS: LattePanda N100 (x86_64 / UEFI) SoC Initialization
 *
 * This file IS the EFI application entry point. There is no assembly
 * _start here — the UEFI firmware sets up the stack, segments (flat 64-bit
 * GDT), and calls efi_main() directly, analogous to a hosted C program's
 * main(). The firmware has already:
 *   - Loaded our PE/COFF EFI binary from \EFI\BOOT\BOOTX64.EFI
 *   - Set CS/DS/SS to flat 64-bit descriptors
 *   - Established a valid stack
 *   - Identity-mapped all RAM (UEFI operates in physical mode)
 *
 * BOOT SEQUENCE:
 *   1. efi_main() called by gnu-efi crt0 (System V ABI — args in RDI/RSI)
 *   2. n100_uart_init() — COM1 UART, first call before any Boot Service
 *   3. n100_display_init() — locate GOP, capture FB params
 *      *** GOP is a Boot Service — MUST be called before ExitBootServices ***
 *   4. efi_exit_boot_services() — get memory map, call ExitBootServices()
 *      After this: Boot Services (gBS) are INVALID. Do not call them.
 *   5. kernel_main(fb) — hand off to portable kernel code
 *
 * WHY NOT EFIAPI / ms_abi ON efi_main:
 *   UEFI firmware calls gnu-efi's crt0 _start with ms_abi (ImageHandle in
 *   RCX, SystemTable in RDX). crt0 runs ELF relocation fixups, then calls
 *   efi_main() using System V ABI (args moved to RDI/RSI). So efi_main()
 *   is a normal C function — no __attribute__((ms_abi)) needed.
 *   EFIAPI expands to empty on GCC x86_64 without HAVE_USE_MS_ABI.
 *
 * WHY uefi_call_wrapper FOR BOOT SERVICE CALLS:
 *   UEFI Boot Service function pointers use ms_abi. Our code uses System V.
 *   Without the wrapper, args go into the wrong registers and the firmware
 *   call crashes. uefi_call_wrapper() performs the System V → ms_abi thunk.
 *   With -DHAVE_USE_MS_ABI=1 (set in soc.mk) it becomes a direct call —
 *   the function pointers are tagged __attribute__((ms_abi)) by gnu-efi and
 *   GCC handles the ABI transition automatically at the call site.
 *
 * CONTRAST WITH ARM64/RISC-V:
 *   ARM64  (BCM2710/2711/2712): boot/arm64/entry.S → common_init.S → kernel_main
 *   RISC-V (KyX1/JH7110):      boot/riscv64/entry.S → common_init.S → kernel_main
 *   x86_64 (N100/N150):        UEFI firmware → efi_main() [THIS FILE] → kernel_main
 *
 * The role of entry.S + common_init.S on other platforms is entirely
 * replaced by the UEFI firmware + this efi_main() on x86_64.
 */

#include "hal/hal_types.h"
#include "hal/hal_display.h"
#include "drivers/framebuffer/framebuffer.h"

/* gnu-efi — UEFI types and uefi_call_wrapper (via efi.h → efibind.h) */
#include <efi.h>

/* ============================================================
 * External declarations from other N100 SoC files
 * ============================================================ */

/* uart_8250.c */
extern void n100_uart_init(void);
extern void n100_uart_puts(const char *s);
extern void n100_uart_putdec(uint32_t v);
extern void n100_uart_puthex32(uint32_t v);
extern void n100_uart_puthex64(uint64_t v);
extern void n100_uart_putc(char c);

/* display_gop.c */
extern bool n100_display_init(EFI_HANDLE image_handle,
                               EFI_SYSTEM_TABLE *system_table,
                               framebuffer_t *fb);
extern void n100_display_present(framebuffer_t *fb);

/* hal_platform_lattepanda_n100.c */
extern void n100_hal_platform_init(void);

/* kernel/main.c — the portable kernel entry */
extern void kernel_main(framebuffer_t *fb);

/* ============================================================
 * RAM base/size globals — read by kernel/main.c allocator
 * ============================================================
 *
 * On ARM64/RISC-V, the boot assembly stores compile-time constants
 * (RAM_BASE, RAM_SIZE) into these globals before calling kernel_main.
 * On x86_64 we don't have that assembly stage, so soc_init.c owns them.
 *
 * We scan the EFI memory map for the largest contiguous conventional RAM
 * region and store its base + total usable size. kernel_main reads these
 * via the extern declarations in kernel/main.c.
 *
 * Using EfiConventionalMemory type (7) to find usable RAM.
 * Pages are 4 KiB each (EFI_PAGE_SIZE = 4096).
 */
uint64_t __ram_base = 0;
uint64_t __ram_size = 0;

#define EFI_CONVENTIONAL_MEMORY 7

static void populate_ram_globals(uint8_t *mmap_buf,
                                 UINTN    mmap_size,
                                 UINTN    desc_size)
{
    uint64_t best_base = 0, best_size = 0;
    UINTN offset = 0;

    while (offset + desc_size <= mmap_size) {
        EFI_MEMORY_DESCRIPTOR *desc =
            (EFI_MEMORY_DESCRIPTOR *)(mmap_buf + offset);

        if (desc->Type == EFI_CONVENTIONAL_MEMORY) {
            uint64_t region_size = desc->NumberOfPages * EFI_PAGE_SIZE;
            if (region_size > best_size) {
                best_base = desc->PhysicalStart;
                best_size = region_size;
            }
        }
        offset += desc_size;
    }

    __ram_base = best_base;
    __ram_size = best_size;
}

/*
 * efi_exit_boot_services — Obtain the UEFI memory map and exit Boot Services.
 *
 * ExitBootServices() is the point of no return:
 *   - After this call, gBS (Boot Services table pointer) is invalid
 *   - All Boot Service protocols (GOP, ConIn, ConOut, etc.) are gone
 *   - Runtime Services (gRT) remain valid
 *   - We own physical memory; UEFI will not reclaim it
 *
 * The memory map key must be fresh at the moment of the call
 * Any Boot Service call between GetMemoryMap and ExitBootServices invalidates
 * the key, requiring a retry. We use a simple retry loop (UEFI spec allows
 * calling GetMemoryMap again after a failed ExitBootServices).
 *
 * We don't need to parse the memory map for Tutorial-OS — we just need
 * to exit cleanly.
 */
#define EFI_MMAP_MAX_SIZE   (64 * 1024)   /* 64 KB — enough for most systems */

static uint8_t g_mmap_buf[EFI_MMAP_MAX_SIZE] __attribute__((aligned(8)));

/* ============================================================
 * EFI Memory Map + ExitBootServices
 * ============================================================ */

static EFI_STATUS efi_exit_boot_services(EFI_HANDLE image_handle,
                                          EFI_SYSTEM_TABLE *system_table)
{
    UINTN   mmap_size   = sizeof(g_mmap_buf);
    UINTN   map_key     = 0;
    UINTN   desc_size   = 0;
    UINT32  desc_ver    = 0;
    EFI_STATUS status;

    /* Retry loop: ExitBootServices may fail if the map key is stale */
    for (int attempts = 0; attempts < 3; attempts++) {
        mmap_size = sizeof(g_mmap_buf);

        status = uefi_call_wrapper(system_table->BootServices->GetMemoryMap, 5,
            &mmap_size, (EFI_MEMORY_DESCRIPTOR *)g_mmap_buf,
            &map_key, &desc_size, &desc_ver);

        if (EFI_ERROR(status)) {
            n100_uart_puts("EBS: GetMemoryMap failed\r\n");
            return status;
        }

        status = uefi_call_wrapper(system_table->BootServices->ExitBootServices, 2,
            image_handle, map_key);

        if (!EFI_ERROR(status)) {
            /*
             * Success — Boot Services are gone. Parse the map NOW
             * while the buffer is still valid and populated.
             */
            populate_ram_globals(g_mmap_buf, mmap_size, desc_size);
            return EFI_SUCCESS;
        }
    }

    return EFI_ABORTED;
}

/* ============================================================
 * EFI Application Entry Point
 * ============================================================ */

/*
 * efi_main — called by gnu-efi crt0 after ELF relocation fixups.
 *
 * crt0 (_start) is the real PE/COFF entry point. UEFI calls _start with
 * ms_abi. crt0 moves ImageHandle and SystemTable to RDI/RSI and calls us
 * with System V ABI — so no EFIAPI / ms_abi attribute here.
 *
 * InitializeLib() is intentionally NOT called. Tutorial-OS does not use
 * any efilib helper functions (Print, AllocatePool wrappers, etc.). All
 * Boot Service calls go through direct uefi_call_wrapper() calls, which
 * with HAVE_USE_MS_ABI=1 are just direct function calls — GCC handles the
 * ABI at the call site automatically via the ms_abi-tagged function pointers.
 *
 * UART is initialized first, before any Boot Service call, so that any
 * subsequent failure produces visible output rather than a silent freeze.
 */
EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    /* ------------------------------------------------------------------
     * Step 1: UART — absolute first, before any Boot Service call.
     *
     * Port I/O (outb/inb) has no dependency on UEFI state — it works
     * from the moment crt0 hands us control. Getting UART up first means
     * every subsequent failure produces visible output instead of a
     * silent freeze at the firmware logo.
     * ------------------------------------------------------------------ */
    n100_uart_init();

    n100_uart_puts("\r\n========================================\r\n");
    n100_uart_puts(" Tutorial-OS  LattePanda MU  x86_64\r\n");
    n100_uart_puts("========================================\r\n");
    n100_uart_puts("UART: OK\r\n");

    /* ------------------------------------------------------------------
     * Step 2: GOP framebuffer — MUST happen before ExitBootServices
     *
     * n100_display_init() calls LocateProtocol (a Boot Service).
     * After ExitBootServices, this call would crash or return garbage.
     * ------------------------------------------------------------------ */
    static framebuffer_t fb;    /* static: not on stack, survives EBS */

    bool display_ok = n100_display_init(ImageHandle, SystemTable, &fb);

    if (!display_ok) {
        n100_uart_puts("DISPLAY: init failed — continuing headless\r\n");
        /*
         * Not fatal for Tutorial-OS boot, UART still works.
         * kernel_main must check fb.initialized before drawing.
         */
    }

    /* ------------------------------------------------------------------
     * Step 3: ExitBootServices
     *
     * From this point forward:
     *   - Do NOT call any uefi_call_wrapper()
     *   - Do NOT use SystemTable->BootServices
     *   - gop, ConOut, etc. are all invalid
     *   - Memory is ours; no UEFI reclaim
     * ------------------------------------------------------------------ */
    n100_uart_puts("EBS: calling ExitBootServices...\r\n");

    EFI_STATUS ebs_status = efi_exit_boot_services(ImageHandle, SystemTable);

    if (EFI_ERROR(ebs_status)) {
        /*
         * EBS failed — we are in an undefined state. Boot Services may
         * or may not be valid. The safest thing is to halt.
         * We can still use UART (port I/O, not a Boot Service).
         */
        n100_uart_puts("EBS: FAILED — system halted\r\n");
        while (1) __asm__ volatile ("hlt");
    }

    /* UART still works — port I/O is independent of Boot Services */
    n100_uart_puts("EBS: done — Boot Services exited\r\n");

    /* ------------------------------------------------------------------
     * Step 4: HAL platform init (no Boot Services required)
     * ------------------------------------------------------------------ */
    n100_hal_platform_init();

    /* ------------------------------------------------------------------
     * Step 5: Hand off to the portable kernel
     *
     * From here, kernel_main() is identical to what runs on ARM64 and
     * RISC-V. It uses only the HAL API and fb_*() drawing functions —
     * no architecture-specific code.
     * ------------------------------------------------------------------ */
    n100_uart_puts("SOC_INIT: entering kernel_main\r\n");

    kernel_main(&fb);

    /* Should never reach here — kernel_main loops forever */
    n100_uart_puts("SOC_INIT: kernel_main returned (unexpected)\r\n");
    while (1) __asm__ volatile ("hlt");

    return EFI_SUCCESS;
}