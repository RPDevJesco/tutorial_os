/*
 * soc/lattepanda_n100/linker.ld — x86_64 UEFI PE/COFF Linker Script
 *
 * Tutorial-OS: LattePanda MU (Intel N100)
 *
 * HOW THIS WORKS
 * ──────────────
 * We use ld's PE/COFF emulation directly:
 *
 *   ld -m i386pep --oformat pei-x86-64 --image-base 0x0 \
 *      --entry efi_main -T linker.ld [objects] -o BOOTX64.EFI
 *
 * This produces a native PE/COFF binary in one step. No ELF intermediate,
 * no objcopy conversion. OUTPUT_FORMAT(pei-x86-64) here must match
 * --oformat pei-x86-64 on the command line.
 *
 * Contrast with the ELF→objcopy approach used by gnu-efi projects:
 *   - gnu-efi: gcc → ELF → objcopy --target=efi-app-x86_64 → PE/COFF
 *   - Tutorial-OS: ld -m i386pep → PE/COFF directly
 *
 * The direct approach avoids all ELF dynamic linking infrastructure
 * (.dynamic, .got, .gnu.hash, .plt) which has no meaning in a PE binary
 * and caused linker errors when combined with -pie.
 *
 * LOAD ADDRESS
 * ────────────
 * --image-base 0x0 sets the preferred load address to zero. UEFI firmware
 * will always relocate our image to wherever it has free memory. The PE
 * .reloc section (generated automatically by ld for pei-x86-64 targets)
 * provides the fixup information the firmware needs.
 *
 * ENTRY POINT
 * ───────────
 * efi_main() in soc/lattepanda_n100/soc_init.c IS the entry point.
 * There is no assembly _start or crt0. UEFI firmware calls efi_main()
 * directly with Microsoft x64 ABI (args in RCX, RDX).
 */

OUTPUT_FORMAT(pei-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)

SECTIONS
{
    . = 0x0;

    /* ------------------------------------------------------------------ */
    /* .text — executable code                                             */
    /* ------------------------------------------------------------------ */
    .text : {
        *(.text.boot)       /* Entry point first — efi_main in soc_init.c */
        *(.text*)
        *(.gnu.linkonce.t*)
    }

    . = ALIGN(4096);

    /* ------------------------------------------------------------------ */
    /* .rodata — read-only data, string literals                           */
    /* ------------------------------------------------------------------ */
    .rodata : {
        *(.rodata*)
        *(.gnu.linkonce.r*)
    }

    . = ALIGN(4096);

    /* ------------------------------------------------------------------ */
    /* .data — initialized writable data                                   */
    /* ------------------------------------------------------------------ */
    .data : {
        *(.sdata*)
        *(.data*)
        *(.gnu.linkonce.d*)
    }

    . = ALIGN(4096);

    /* ------------------------------------------------------------------ */
    /* .bss — zero-initialized data                                        */
    /*                                                                     */
    /* No crt0 to zero this for us — soc_init.c does it if needed.        */
    /* __bss_start / __bss_end are available as linker symbols.           */
    /* ------------------------------------------------------------------ */
    __bss_start = .;
    .bss : {
        *(.bss*)
        *(.sbss*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;

    . = ALIGN(4096);

    /* ------------------------------------------------------------------ */
    /* .reloc — PE/COFF base relocation table                              */
    /*                                                                     */
    /* ld populates this automatically when targeting pei-x86-64.         */
    /* UEFI firmware reads it to fix up absolute addresses at load time.  */
    /* Without .reloc the image is position-dependent and UEFI may        */
    /* refuse to load it or crash on the first absolute address access.   */
    /* ------------------------------------------------------------------ */
    .reloc : {
        *(.reloc)
    }

    . = ALIGN(4096);

    /* ------------------------------------------------------------------ */
    /* Stack — 256 KB, 16-byte aligned                                     */
    /*                                                                     */
    /* UEFI provides a firmware stack before ExitBootServices. After EBS  */
    /* that memory may be reclaimed. We carve out our own stack here so   */
    /* post-EBS code has a guaranteed valid stack.                        */
    /* ------------------------------------------------------------------ */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_bottom = .;
        . += 256K;
        . = ALIGN(16);
        __stack_top = .;
    }

    /* ------------------------------------------------------------------ */
    /* Heap — 512 KB                                                       */
    /*                                                                     */
    /* memory/allocator.c uses __heap_start / __heap_end to locate the    */
    /* initial heap region. Same pattern as ARM64 and RISC-V platforms.   */
    /* ------------------------------------------------------------------ */
    __heap_start = .;
    . += 512K;
    __heap_end = .;

    /* ------------------------------------------------------------------ */
    /* Discard — sections with no meaning in a PE/COFF EFI binary         */
    /* ------------------------------------------------------------------ */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.dynamic*)
        *(.dynsym*)
        *(.dynstr*)
        *(.gnu.hash)
        *(.gnu.version*)
        *(.interp)
        *(.plt*)
        *(.iplt*)
        *(.got*)
        *(.rela.dyn*)
        *(.rel.dyn*)
    }
}
