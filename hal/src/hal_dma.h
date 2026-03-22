/*
 * hal/hal_dma.h — DMA Coherency and Transfer Contract for Tutorial-OS
 * =====================================================================
 *
 * WHY THIS FILE EXISTS:
 * ---------------------
 * DMA is not optional in Tutorial-OS — it is already happening everywhere:
 *
 *   - Mailbox:      ARM writes a buffer, VideoCore GPU reads it via DMA.
 *                   BCM_ARM_TO_BUS() translates the address. DSB ST ensures
 *                   CPU writes are visible before the GPU reads them.
 *
 *   - Framebuffer:  The display controller (DC8200 on JH7110, VC7 on BCM2712,
 *                   DPI on BCM2710/2711) is a DMA master. It reads from the
 *                   framebuffer continuously. The CPU must flush the cache
 *                   before presenting a new frame or the display sees stale
 *                   pixels.
 *
 *   - Audio:        The PWM FIFO is DMA-fed. The BCM_PWM_DMAC register exists
 *                   in the register map. Audio buffers handed to the FIFO
 *                   must be cache-clean before the hardware reads them.
 *
 *   - USB (DWC2):   BCM_USB_HCDMA(n) per-channel DMA registers are defined.
 *                   The current implementation uses FIFO mode but the DMA
 *                   path is the natural next step for performance.
 *
 *   - SD Card:      Sector reads from SDHOST land in a CPU buffer. The CPU
 *                   must invalidate cache for that buffer before reading
 *                   the data or it may see whatever was there before the
 *                   transfer.
 *
 * Without this header, each subsystem independently managed its own
 * coherency in different ways with no shared language:
 *   - Mailbox:      manual DSB + address translation
 *   - JH7110 FB:    jh7110_l2_flush_range() via SiFive L2 controller
 *   - ARM64 cache:  clean_dcache_range / invalidate_dcache_range
 *   - RISC-V cache: cbo.flush (Zicbom) in boot/riscv64/cache.S
 *   - Framebuffer:  write_volatile for every pixel write
 *
 * This header gives all of that a unified contract. Every driver past and
 * future (including networking) follows the same protocol.
 *
 * ==========================================================================
 * THE FUNDAMENTAL DMA PROBLEM:
 * ==========================================================================
 *
 * Modern CPUs have caches. DMA devices do not go through those caches —
 * they read and write directly to physical memory (DRAM).
 *
 * This creates two failure modes:
 *
 *   1. CPU writes data to a buffer, but cache hasn't flushed to DRAM yet.
 *      Device reads stale data from DRAM. → Clean cache before TO_DEVICE.
 *
 *   2. Device writes data to DRAM. CPU reads from cache which still has
 *      the old value. → Invalidate cache before FROM_DEVICE read.
 *
 * Cache line size matters: clean/invalidate must cover entire cache lines,
 * not just the bytes the driver cares about. Partial cache line operations
 * on shared lines can corrupt adjacent data.
 *
 * Memory barriers matter: the compiler and CPU can reorder memory accesses.
 * A barrier ensures all previous writes are visible before the DMA starts.
 *
 * ==========================================================================
 * PLATFORM-SPECIFIC REALITIES:
 * ==========================================================================
 *
 * BCM2710/2711 (ARM64):
 *   - VideoCore GPU sees memory through a bus address alias (0xC0000000 offset)
 *   - ARM physical address must be converted via BCM_ARM_TO_BUS() for mailbox
 *   - Cache operations: clean_dcache_range / invalidate_dcache_range (cache.S)
 *   - Mailbox buffers require 16-byte alignment
 *   - Framebuffer allocated via mailbox with alignment=16 for DMA access
 *
 * BCM2712 (ARM64, Pi 5 / CM5):
 *   - VC7 GPU returns ARM-accessible physical addresses directly
 *   - No alias stripping needed (unlike BCM2711) — stripping corrupts the address
 *   - DSB ST before mailbox write; DMB after mailbox read
 *   - RP1 south bridge peripherals at 0x1F000D0000 have their own DMA domain
 *
 * JH7110 (RISC-V, Milk-V Mars):
 *   - SiFive U74 is RV64GC ONLY — NO Zicbom extension.
 *     cbo.clean / cbo.flush / cbo.inval instructions do NOT exist here.
 *     cache.S is intentionally NOT compiled for the JH7110 target.
 *   - Cache flush mechanism: SiFive L2 cache controller MMIO.
 *     jh7110_l2_flush_range() writes physical addresses to the Flush64
 *     register at 0x02010200, one 64-byte cache line at a time.
 *     This is the ONLY way to flush cache lines on U74 without Zicbom.
 *   - fence iorw, iorw after the L2 flush loop to ensure store ordering.
 *   - DC8200 display controller at 0x29400000 is a DMA master that
 *     reads the framebuffer directly from DRAM via TileLink fabric.
 *   - MMU NOTE: mmu.S marks the framebuffer region (0xFE000000) as
 *     Device (non-cacheable) in the Sv39 page tables. When the MMU is
 *     active, CPU writes to the framebuffer bypass the cache entirely
 *     and go straight to DRAM. hal_dma_flush_framebuffer() becomes a
 *     fence-only operation in that case — no L2 flush needed.
 *     hal_dma_prepare() must check MMU state before deciding which
 *     path to take, or the SoC implementation handles it transparently.
 *   - No BCM-style bus address alias — physical address used directly.
 *
 * KYX1 (RISC-V, Orange Pi RV2):
 *   - SpacemiT X60 is RV64GCV + Zicbom + Zicboz — full cache block ops.
 *   - cache.S IS compiled for KYX1, implementing:
 *       clean_dcache_range()      → cbo.clean loop + fence iorw,iorw
 *       invalidate_dcache_range() → cbo.inval loop + fence iorw,iorw
 *       flush_dcache_range()      → cbo.flush loop + fence iorw,iorw
 *       zero_dcache_range()       → cbo.zero loop  (Zicboz, no fence needed)
 *   - All Zicbom instructions use raw .insn encoding if toolchain is old.
 *   - Cache line size: 64 bytes (cbom-block-size and cboz-block-size in DT).
 *   - PXA-style peripherals; PMIC I2C transfers are polled, not DMA.
 *     I2C is NOT a DMA consumer on this platform.
 *   - Display via SimpleFB: U-Boot initializes DPU, hands us framebuffer.
 *     The DPU is a DMA master — clean_dcache_range() required before present.
 *   - Physical addresses used directly (no bus alias).
 *
 * x86_64 (LattePanda IOTA / MU):
 *   - x86 is cache-coherent for DMA by default (MTRR / PAT managed by UEFI)
 *   - Cache operations are no-ops on x86_64 for most DMA scenarios
 *   - UEFI GOP framebuffer is write-combining memory type — no flush needed
 *   - Memory barriers: SFENCE for stores, LFENCE for loads, MFENCE for both
 *
 * ==========================================================================
 * HOW TO USE THIS HEADER:
 * ==========================================================================
 *
 * SENDING data TO a device (CPU → Device):
 *
 *   uint8_t tx_buf[512] __attribute__((aligned(HAL_DMA_ALIGN)));
 *   // ... fill tx_buf with data ...
 *   hal_dma_prepare(tx_buf, sizeof(tx_buf), HAL_DMA_TO_DEVICE);
 *   // now hand tx_buf to the hardware
 *   device_start_tx(hal_dma_to_device_addr(tx_buf));
 *
 * RECEIVING data FROM a device (Device → CPU):
 *
 *   uint8_t rx_buf[512] __attribute__((aligned(HAL_DMA_ALIGN)));
 *   hal_dma_prepare(rx_buf, sizeof(rx_buf), HAL_DMA_FROM_DEVICE);
 *   // hand rx_buf to hardware, wait for completion
 *   device_start_rx(hal_dma_to_device_addr(rx_buf));
 *   device_wait_complete();
 *   hal_dma_complete(rx_buf, sizeof(rx_buf), HAL_DMA_FROM_DEVICE);
 *   // now safe to read rx_buf from CPU
 *
 * FRAMEBUFFER present (CPU writes → Display DMA reads):
 *
 *   // After drawing is complete:
 *   hal_dma_prepare(fb->buffer, fb->size, HAL_DMA_TO_DEVICE);
 *   display_flip();   // or mailbox virtual offset update
 *
 * ==========================================================================
 */

#ifndef HAL_DMA_H
#define HAL_DMA_H

#include "hal_types.h"   /* uintptr_t, uint32_t, size_t, bool */

/* ==========================================================================
 * DMA TRANSFER DIRECTION
 * ==========================================================================
 *
 * Direction determines which cache operation is performed:
 *
 *   HAL_DMA_TO_DEVICE:
 *     CPU has written data; device will read it.
 *     Action: Clean (write-back) cache lines to DRAM before transfer.
 *     After: CPU must NOT write to buffer until transfer completes.
 *     Example: mailbox request, audio sample buffer, framebuffer present,
 *              network TX packet, SD card write sector.
 *
 *   HAL_DMA_FROM_DEVICE:
 *     Device will write data; CPU will read it.
 *     Action: Invalidate cache lines before transfer starts.
 *             This discards any stale CPU-cached data for the buffer region.
 *     After: Call hal_dma_complete() before CPU reads the buffer.
 *     Example: SD card read sector, USB HID input report, network RX packet.
 *
 *   HAL_DMA_BIDIRECTIONAL:
 *     Buffer is both written by CPU and written by device (or direction
 *     is not known at prepare time).
 *     Action: Flush (clean + invalidate) cache lines.
 *     More expensive — use only when necessary.
 *     Example: USB control transfers, ping-pong DMA buffers.
 */
typedef enum {
    HAL_DMA_TO_DEVICE       = 0,    /* CPU → Device: clean cache */
    HAL_DMA_FROM_DEVICE     = 1,    /* Device → CPU: invalidate cache */
    HAL_DMA_BIDIRECTIONAL   = 2,    /* Both directions: flush cache */
} hal_dma_dir_t;

/* ==========================================================================
 * DMA CHANNEL IDENTIFIERS
 * ==========================================================================
 *
 * Channels represent logical DMA consumers/producers. On platforms with
 * a hardware DMA controller (BCM DMA engine, JH7110 PDMA), these map to
 * physical channels with configurable priority. On platforms without
 * (FIFO-mode USB, polled I2C), they are used only for ordering and
 * cache coherency tracking.
 *
 * Priority order (highest to lowest):
 *   DISPLAY > AUDIO > USB > SDCARD > MAILBOX > NETWORK > GENERIC
 *
 * DISPLAY is highest because frame timing is hard real-time — a missed
 * frame is immediately visible. AUDIO is next for the same reason.
 * NETWORK is lowest because packet latency tolerance is much higher than
 * audio or display latency.
 *
 * TEACHING NOTE:
 *   This priority ordering is why you can't just "add networking" without
 *   thinking about DMA. A network RX burst that starves the display DMA
 *   will cause visible tearing. The channel system makes this explicit.
 */
typedef enum {
    HAL_DMA_CH_DISPLAY      = 0,    /* Display controller / framebuffer DMA */
    HAL_DMA_CH_AUDIO        = 1,    /* PWM FIFO / I2S DMA */
    HAL_DMA_CH_USB          = 2,    /* DWC2 host channels */
    HAL_DMA_CH_SDCARD       = 3,    /* SDHOST / eMMC transfers */
    HAL_DMA_CH_MAILBOX      = 4,    /* VideoCore GPU mailbox (BCM only) */
    HAL_DMA_CH_NETWORK      = 5,    /* Network RX/TX (future) */
    HAL_DMA_CH_GENERIC      = 6,    /* General purpose / one-shot transfers */
    HAL_DMA_CH_COUNT        = 7,
} hal_dma_channel_t;

/* ==========================================================================
 * DMA BUFFER DESCRIPTOR
 * ==========================================================================
 *
 * Tracks a DMA buffer's state. Drivers hold one of these for each active
 * DMA buffer. The descriptor captures everything the coherency layer needs
 * to correctly prepare and complete a transfer.
 *
 * OWNERSHIP MODEL:
 *   CPU_OWNED:    CPU may freely read/write the buffer.
 *   DEVICE_OWNED: CPU must NOT touch the buffer. Device is reading/writing.
 *
 * The transition is:
 *   CPU_OWNED → hal_dma_prepare() → DEVICE_OWNED → hal_dma_complete() → CPU_OWNED
 *
 * Reading a DEVICE_OWNED buffer from the CPU is undefined behavior —
 * the device may be mid-write and cache state is invalid.
 */
typedef enum {
    HAL_DMA_OWNER_CPU       = 0,
    HAL_DMA_OWNER_DEVICE    = 1,
} hal_dma_owner_t;

typedef struct {
    void            *cpu_addr;      /* CPU virtual address of buffer */
    uintptr_t        dev_addr;      /* Device (bus) address — may differ on BCM */
    size_t           size;          /* Buffer size in bytes */
    hal_dma_dir_t    direction;     /* Transfer direction */
    hal_dma_channel_t channel;      /* Which DMA channel owns this buffer */
    hal_dma_owner_t  owner;         /* Current ownership state */
} hal_dma_buf_t;

/* ==========================================================================
 * ALIGNMENT REQUIREMENTS
 * ==========================================================================
 *
 * DMA buffers must satisfy two alignment constraints:
 *
 *   HAL_DMA_ALIGN:
 *     Minimum buffer alignment. Must be at least the cache line size so
 *     that cache operations don't affect adjacent data in the same line.
 *     64 bytes covers all platforms (ARM64 and RISC-V both use 64-byte lines).
 *     x86_64 cache lines are also 64 bytes.
 *
 *   HAL_DMA_MAILBOX_ALIGN:
 *     BCM mailbox buffers require 16-byte alignment (low 4 bits are used
 *     for the channel number in the mailbox write register).
 *     16 < 64, so HAL_DMA_ALIGN satisfies this automatically.
 *
 * Usage:
 *   uint8_t buf[512] __attribute__((aligned(HAL_DMA_ALIGN)));
 *   hal_dma_buf_t HAL_DMA_BUFFER(buf, sizeof(buf), HAL_DMA_TO_DEVICE,
 *                                 HAL_DMA_CH_SDCARD);
 */
#define HAL_DMA_ALIGN           64      /* Cache line size on all platforms */
#define HAL_DMA_MAILBOX_ALIGN   16      /* BCM mailbox minimum alignment */

/*
 * HAL_DMA_BUFFER() — Declare and initialize a dma_buf_t on the stack
 *
 * @param _ptr   Pointer to the DMA buffer (must be aligned to HAL_DMA_ALIGN)
 * @param _size  Size of the buffer in bytes
 * @param _dir   hal_dma_dir_t direction
 * @param _ch    hal_dma_channel_t channel
 */
#define HAL_DMA_BUFFER(_ptr, _size, _dir, _ch) {   \
    .cpu_addr  = (_ptr),                            \
    .dev_addr  = 0,                                 \
    .size      = (_size),                           \
    .direction = (_dir),                            \
    .channel   = (_ch),                             \
    .owner     = HAL_DMA_OWNER_CPU,                 \
}

/* ==========================================================================
 * CORE API — BUFFER LIFECYCLE
 * ==========================================================================
 */

/*
 * hal_dma_prepare() — Transfer buffer ownership from CPU to device
 *
 * Call this AFTER filling the buffer (for TO_DEVICE) or BEFORE starting
 * the device transfer (for FROM_DEVICE). After this call, the CPU must
 * NOT read or write the buffer until hal_dma_complete() is called.
 *
 * What this does per platform:
 *   ARM64 (BCM):    clean_dcache_range() for TO_DEVICE
 *                   invalidate_dcache_range() for FROM_DEVICE
 *                   flush_dcache_range() for BIDIRECTIONAL
 *                   DSB barrier to ensure ordering
 *
 *   RISC-V (JH7110): jh7110_l2_flush_range() (SiFive L2 Flush64 register)
 *                    fence iorw, iorw
 *
 *                    *** JH7110 LIMITATION — READ CAREFULLY ***
 *                    The SiFive U74 does NOT implement Zicbom. The L2 Flush64
 *                    register performs a CLEAN (write-back) only — it has no
 *                    invalidate equivalent.
 *
 *                    HAL_DMA_TO_DEVICE:   jh7110_l2_flush_range() + fence ✓
 *                    HAL_DMA_FROM_DEVICE: fence iorw,iorw ONLY — relies on
 *                                         TileLink fabric hardware coherency.
 *                                         No explicit cache invalidate exists.
 *                    HAL_DMA_BIDIRECTIONAL: jh7110_l2_flush_range() + fence
 *                                           (best effort — no true invalidate)
 *
 *                    For peripherals on the TileLink coherency fabric (most
 *                    on-chip DMA masters), this is sufficient. For any device
 *                    that bypasses TileLink coherency, FROM_DEVICE transfers
 *                    require mapping the buffer as non-cacheable in the MMU
 *                    (PBMT_NC), which is outside the scope of hal_dma.
 *
 *   RISC-V (KYX1):  HAL_DMA_TO_DEVICE:   cbo.clean via clean_dcache_range()
 *                   HAL_DMA_FROM_DEVICE:  cbo.inval via invalidate_dcache_range()
 *                   HAL_DMA_BIDIRECTIONAL: cbo.flush via flush_dcache_range()
 *                   All implemented in boot/riscv64/cache.S (Zicbom)
 *                   fence iorw, iorw after each operation
 *
 *   x86_64:         No-op for cache (x86 is cache-coherent for DMA)
 *                   SFENCE for store ordering
 *
 * Sets buf->owner = HAL_DMA_OWNER_DEVICE.
 * Populates buf->dev_addr with the device-visible address.
 *
 * @param buf  DMA buffer descriptor (cpu_addr, size, direction, channel
 *             must be set before calling)
 *
 * @return HAL_SUCCESS or error code
 */
hal_error_t hal_dma_prepare(hal_dma_buf_t *buf);

/*
 * hal_dma_complete() — Transfer buffer ownership back from device to CPU
 *
 * Call this AFTER the device signals transfer completion (polled status
 * register, interrupt flag, or mailbox response). After this call, the
 * CPU may safely read the buffer contents.
 *
 * What this does per platform:
 *   TO_DEVICE:      Memory barrier only (data already in DRAM from prepare)
 *   FROM_DEVICE:    invalidate_dcache_range() to discard stale CPU cache
 *   BIDIRECTIONAL:  flush_dcache_range()
 *
 * Sets buf->owner = HAL_DMA_OWNER_CPU.
 *
 * @param buf  DMA buffer descriptor
 *
 * @return HAL_SUCCESS or error code
 */
hal_error_t hal_dma_complete(hal_dma_buf_t *buf);

/*
 * hal_dma_sync() — Synchronize without changing ownership
 *
 * For long-running transfers where the CPU needs to sample the buffer
 * mid-transfer (e.g., checking a status byte the device updates in-place).
 * Does not change buf->owner.
 *
 * Use sparingly — sharing a buffer between CPU and device mid-transfer
 * is inherently racy without additional synchronization.
 *
 * @param buf  DMA buffer descriptor
 * @param dir  Which direction to sync (may differ from buf->direction
 *             for BIDIRECTIONAL buffers where you only need one side)
 */
void hal_dma_sync(hal_dma_buf_t *buf, hal_dma_dir_t dir);

/* ==========================================================================
 * ADDRESS TRANSLATION
 * ==========================================================================
 *
 * On most platforms, the device-visible (bus) address equals the CPU
 * physical address. BCM2710/2711 is the exception — the VideoCore sees
 * ARM memory through the 0xC0000000 L2 cache-coherent alias.
 *
 * These functions abstract that difference so drivers don't need to
 * know which platform they're on.
 *
 * TEACHING NOTE:
 *   This is exactly why BCM2712 was such a debugging milestone — the VC7
 *   on Pi 5 returns physical addresses directly (no alias), but the code
 *   written for BCM2711 was stripping the alias bits, corrupting the
 *   framebuffer address. hal_dma_to_device_addr() encodes that difference
 *   once so it can never happen again.
 */

/*
 * hal_dma_to_device_addr() — Convert CPU address to device-visible address
 *
 * @param cpu_addr  CPU virtual/physical address
 * @return          Address the device should use for DMA
 *
 * BCM2710/2711: Adds 0xC0000000 bus alias
 * BCM2712:      Returns physical address unchanged (VC7 direct addressing)
 * JH7110:       Returns physical address unchanged
 * KYX1:         Returns physical address unchanged
 * x86_64:       Returns physical address unchanged (IOMMU managed by UEFI)
 */
uintptr_t hal_dma_to_device_addr(void *cpu_addr);

/*
 * hal_dma_to_cpu_addr() — Convert device-visible address to CPU address
 *
 * Inverse of hal_dma_to_device_addr(). Used when a device returns a
 * DMA address (e.g., mailbox framebuffer allocation response) and the
 * CPU needs to access that memory.
 *
 * @param dev_addr  Device (bus) address returned by hardware
 * @return          CPU-accessible address for the same physical memory
 *
 * BCM2710/2711: Strips 0xC0000000 bus alias → BCM_BUS_TO_ARM()
 * BCM2712:      Returns unchanged (no alias)
 * All others:   Returns unchanged
 */
uintptr_t hal_dma_to_cpu_addr(uintptr_t dev_addr);

/* ==========================================================================
 * CHANNEL ORDERING BARRIERS
 * ==========================================================================
 *
 * When multiple DMA channels are active, ordering between them matters.
 * These functions ensure one channel's transfer is visible to the system
 * before another begins.
 *
 * Example: Display channel must see completed audio buffer before the
 * display controller reads its next frame (relevant for overlays or
 * mixed audio/video buffers).
 */

/*
 * hal_dma_channel_barrier() — Ensure channel A's writes are visible before
 *                              channel B starts reading
 *
 * @param from_ch  Channel whose transfer must be complete first
 * @param to_ch    Channel that will start next
 */
void hal_dma_channel_barrier(hal_dma_channel_t from_ch,
                              hal_dma_channel_t to_ch);

/*
 * hal_dma_full_barrier() — Full system DMA ordering barrier
 *
 * Ensures all in-flight DMA from all channels is complete and visible
 * before any subsequent memory access. Equivalent to a DSB SY on ARM64.
 *
 * Use before:
 *   - Transitioning display buffers (framebuffer swap)
 *   - Powering off a DMA-capable peripheral
 *   - Any point where the system must be in a known-clean state
 */
void hal_dma_full_barrier(void);

/* ==========================================================================
 * CONVENIENCE WRAPPERS FOR EXISTING PATTERNS
 * ==========================================================================
 *
 * These wrap the common patterns already used throughout the codebase,
 * giving them a consistent name without requiring every call site to
 * be rewritten immediately.
 */

/*
 * hal_dma_flush_framebuffer() — Flush framebuffer to DRAM before display read
 *
 * Replaces the existing pattern:
 *   JH7110: jh7110_l2_flush_range(fb_phys, fb_size) + fence iorw,iorw
 *           NOTE: When MMU is active and framebuffer VA is mapped as Device
 *           (non-cacheable) in mmu.S, writes already bypass the cache.
 *           This call reduces to a fence-only barrier in that case.
 *           The JH7110 SoC implementation detects this automatically.
 *   KYX1:   clean_dcache_range(fb_addr, fb_size)   [cbo.clean loop]
 *   ARM64:  clean_dcache_range(fb_addr, fb_size)   [dc cvac loop]
 *   x86_64: No-op (UEFI GOP framebuffer is write-combining memory type)
 *
 * @param fb_cpu_addr  CPU address of framebuffer start
 * @param size         Framebuffer size in bytes (pitch * height)
 */
void hal_dma_flush_framebuffer(void *fb_cpu_addr, size_t size);

/*
 * hal_dma_zero_buffer() — Zero a DMA receive buffer efficiently
 *
 * Zeroes a buffer using the fastest available mechanism per platform,
 * leaving it cache-clean and ready for a device to write into.
 *
 * Why not just memset()?
 *   memset() writes through the cache. For a DMA receive buffer, we then
 *   need to invalidate those cache lines anyway before the device writes.
 *   On KYX1 (Zicboz), cbo.zero writes a zero cache line directly to DRAM
 *   without a read-modify-write cycle, then marks the line clean. This is
 *   faster than memset() + invalidate for large buffers (network RX rings,
 *   framebuffer clears).
 *
 * Platform behavior:
 *   KYX1:   zero_dcache_range() using cbo.zero loop (Zicboz, hardware-zeroed)
 *   JH7110: Regular memset() + jh7110_l2_flush_range() (no Zicboz on U74)
 *   ARM64:  Regular memset() + clean_dcache_range() (ARM dc zva is optional)
 *   x86_64: Regular memset() (cache-coherent, no flush needed)
 *
 * After this call, the buffer is device-owned and ready for a FROM_DEVICE
 * transfer. CPU must not write to it until hal_dma_complete() is called.
 *
 * @param buf  DMA buffer descriptor (cpu_addr and size must be set)
 *             Direction is set to HAL_DMA_FROM_DEVICE automatically.
 */
void hal_dma_zero_buffer(hal_dma_buf_t *buf);

/*
 * hal_dma_prepare_mailbox() — Prepare a BCM mailbox buffer for GPU DMA
 *
 * Replaces the existing pattern in bcm_mailbox_call():
 *   clean cache + DSB ST + BCM_ARM_TO_BUS() address translation
 *
 * @param buf      Pointer to mailbox buffer (must be 16-byte aligned)
 * @param size     Size of mailbox buffer in bytes
 *
 * @return Device (bus) address to write to the mailbox register
 */
uintptr_t hal_dma_prepare_mailbox(void *buf, size_t size);

/*
 * hal_dma_complete_mailbox() — Finalize a BCM mailbox response read
 *
 * Replaces the existing DMB after reading the mailbox response register.
 * Ensures the GPU's writes to the response buffer are visible to the CPU.
 *
 * @param buf   Pointer to mailbox buffer
 * @param size  Size of mailbox buffer in bytes
 */
void hal_dma_complete_mailbox(void *buf, size_t size);

/* ==========================================================================
 * DEBUG AND DIAGNOSTICS
 * ==========================================================================
 */

/*
 * hal_dma_zero_buffer() — Zero a DMA buffer using hardware-accelerated cache zero
 *
 * On KYX1 (Zicboz): uses cbo.zero — allocates a clean zero cache line without
 * reading from DRAM first. Significantly faster than a store loop for large
 * receive buffers.
 *
 * On all other platforms: falls back to memset(buf, 0, size).
 *
 * Use before handing a receive buffer to a device (FROM_DEVICE) to ensure
 * no stale data is visible if the transfer completes partially.
 *
 * @param buf   Pointer to buffer to zero (must be HAL_DMA_ALIGN aligned)
 * @param size  Size in bytes (should be a multiple of HAL_DMA_ALIGN for
 *              full hardware acceleration; partial tail uses scalar store)
 */
void hal_dma_zero_buffer(void *buf, size_t size);

/*
 * hal_dma_addr_is_reserved() — Check if an address falls in a reserved DMA region
 *
 * Some platforms have memory regions reserved for specific DMA masters that
 * must NOT be used as general-purpose DMA buffers. Using them causes
 * conflicts with hardware that owns those regions.
 *
 * Known reserved regions by platform:
 *
 *   KYX1 (Orange Pi RV2):
 *     0x3020_6000 + 984 KB  — VDev shared DMA buffer (RCPU ↔ main CPU virtio)
 *     0x2FF4_0000 + 384 KB  — DPU reserved: display MMU tables (256K) +
 *                              command list (128K). The DPU writes here
 *                              autonomously — CPU DMA into this region
 *                              will corrupt display output.
 *
 *   JH7110 (Milk-V Mars): No explicit reserved DMA regions in bare-metal
 *                          context beyond the U-Boot/OpenSBI footprint.
 *
 *   BCM family: VideoCore owns the VC memory region returned by
 *               bcm_mailbox_get_vc_memory(). ARM must not DMA into it.
 *
 * hal_dma_buf_is_valid() calls this internally. Drivers can also call it
 * directly when allocating DMA-capable buffers.
 *
 * @param addr  Physical address to check
 * @param size  Size of the region
 * @return      true if the region is reserved and must not be used
 */
bool hal_dma_addr_is_reserved(uintptr_t addr, size_t size);


 *
 * Checks alignment, size, and ownership state. Useful in debug builds
 * to catch misuse early (e.g., CPU writing a DEVICE_OWNED buffer).
 *
 * @param buf  DMA buffer descriptor to validate
 * @return     true if valid, false if misuse detected
 */
bool hal_dma_buf_is_valid(const hal_dma_buf_t *buf);

/*
 * hal_dma_channel_name() — Human-readable channel name for UART diagnostics
 *
 * @param ch  Channel identifier
 * @return    Null-terminated string, e.g. "DISPLAY", "AUDIO", "NETWORK"
 *
 * NOTE: Uses switch-based return (PC-relative) rather than a const char*[]
 * array to avoid RISC-V static pointer relocation issues in bare-metal.
 * (The same pattern used throughout the RISC-V SoC drivers.)
 */
const char *hal_dma_channel_name(hal_dma_channel_t ch);

/* ==========================================================================
 * PLATFORM IMPLEMENTATION NOTES
 * ==========================================================================
 *
 * Each SoC must implement the above functions in:
 *   soc/<soc_name>/dma.c
 *
 * The implementation must:
 *   1. Implement hal_dma_prepare() using the platform cache flush mechanism
 *   2. Implement hal_dma_complete() with appropriate cache invalidation
 *   3. Implement hal_dma_to_device_addr() with correct address translation
 *   4. Implement hal_dma_to_cpu_addr() as the inverse
 *   5. Implement hal_dma_full_barrier() with the platform memory barrier
 *
 * Per-SoC implementation details:
 *
 * soc/bcm2710/dma.c, soc/bcm2711/dma.c:
 *   hal_dma_prepare()    → clean_dcache_range() + DSB ST
 *   hal_dma_complete()   → DMB SY + invalidate_dcache_range()
 *   hal_dma_to_device_addr() → BCM_ARM_TO_BUS(addr)   [+0xC0000000]
 *   hal_dma_to_cpu_addr()    → BCM_BUS_TO_ARM(addr)   [-0xC0000000]
 *
 * soc/bcm2712/dma.c:
 *   hal_dma_prepare()    → clean_dcache_range() + DSB ST
 *   hal_dma_complete()   → DMB SY + invalidate_dcache_range()
 *   hal_dma_to_device_addr() → identity (VC7 returns physical addr directly)
 *   hal_dma_to_cpu_addr()    → identity (no alias to strip)
 *
 * soc/jh7110/dma.c:
 *   hal_dma_prepare() for TO_DEVICE:
 *     if (mmu_is_active() && fb_is_device_memory(buf->cpu_addr)):
 *         fence iorw, iorw   (device memory writes already bypass cache)
 *     else:
 *         jh7110_l2_flush_range(phys_addr, size)  (SiFive L2 Flush64 MMIO)
 *         fence iorw, iorw
 *   hal_dma_prepare() for FROM_DEVICE:
 *     fence iorw, iorw   (U74 TileLink coherency handles the rest)
 *   hal_dma_complete():
 *     fence iorw, iorw
 *   hal_dma_to_device_addr() → identity
 *   NOTE: cache.S is NOT compiled for JH7110. Do not call
 *         clean_dcache_range() — those symbols do not exist for this target.
 *
 * soc/kyx1/dma.c:
 *   hal_dma_prepare() for TO_DEVICE:
 *     clean_dcache_range(addr, size)    [cbo.clean + fence iorw,iorw]
 *   hal_dma_prepare() for FROM_DEVICE:
 *     invalidate_dcache_range(addr, size) [cbo.inval + fence iorw,iorw]
 *   hal_dma_prepare() for BIDIRECTIONAL:
 *     flush_dcache_range(addr, size)    [cbo.flush + fence iorw,iorw]
 *   hal_dma_complete():
 *     invalidate_dcache_range() for FROM_DEVICE; fence for others
 *   hal_dma_zero_buffer():
 *     zero_dcache_range(addr, size)     [cbo.zero, Zicboz — no fence needed]
 *     sets direction = FROM_DEVICE, owner = DEVICE
 *   hal_dma_to_device_addr() → identity
 *
 * soc/lattepanda/dma.c (x86_64):
 *   hal_dma_prepare()    → SFENCE (store fence only; cache coherent for DMA)
 *   hal_dma_complete()   → LFENCE (load fence)
 *   hal_dma_full_barrier() → MFENCE
 *   All cache ops are no-ops (x86 DMA coherency is hardware-managed)
 *   hal_dma_to_device_addr() → identity (IOMMU managed by UEFI firmware)
 *
 * ==========================================================================
 * FUTURE: NETWORKING
 * ==========================================================================
 *
 * When a network driver is added (USB Ethernet via DWC2, or a dedicated
 * MAC on a future board), it will use HAL_DMA_CH_NETWORK and follow
 * exactly the same prepare/complete lifecycle:
 *
 *   TX path:
 *     fill packet buffer → hal_dma_prepare(&pkt, HAL_DMA_TO_DEVICE) →
 *     hand dev_addr to NIC → wait for TX complete → hal_dma_complete()
 *
 *   RX path:
 *     hal_dma_prepare(&rx_buf, HAL_DMA_FROM_DEVICE) →
 *     hand dev_addr to NIC → wait for RX interrupt/status →
 *     hal_dma_complete(&rx_buf, HAL_DMA_FROM_DEVICE) → read packet
 *
 * The channel ordering system ensures network DMA cannot starve display
 * or audio by giving those channels higher priority in the barrier logic.
 */

#endif /* HAL_DMA_H */