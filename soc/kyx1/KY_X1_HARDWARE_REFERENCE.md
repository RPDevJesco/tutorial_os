# Ky X1 SoC — Hardware Reference for Tutorial-OS
## Orange Pi RV2 Board

Extracted from: `x1.dtsi`, `x1_orangepi-rv2.dts`, `x1-hdmi.dtsi`, `x1_defconfig` (U-Boot), schematics, datasheet.

---

## CPU

| Property | Value |
|----------|-------|
| ISA | `rv64imafdcv` (RV64GCV + extensions) |
| Core type | `ky,x60` — 8 cores, 2 clusters of 4 |
| MMU | `riscv,sv39` (39-bit virtual addressing) |
| L1I cache | 32KB per core, 64B line, 128 sets |
| L1D cache | 32KB per core, 64B line, 128 sets |
| L2 cache | 512KB per cluster (2 clusters), 64B line, 512 sets, unified |
| Cache mgmt | **Zicbom** (block size 64B), **Zicboz** (block size 64B) |
| Timebase | **24 MHz** (board DTS overrides dtsi's 10MHz) |

### Notable ISA Extensions
- `zicbom` / `zicboz` — Cache block management/zero (standard, not vendor CSRs!)
- `zba`, `zbb`, `zbc`, `zbs` — Bit manipulation
- `zifencei` — Instruction fence
- `zihintpause` — Pause hint (like ARM `yield`)
- `svinval` — Fine-grained TLB invalidation
- `svnapot`, `svpbmt` — Page table extensions
- `v`, `zvfh` — Vector extension with half-precision float

---

## Memory Map

### DRAM

| Region | Start | End | Size |
|--------|-------|-----|------|
| DRAM bank 0 | `0x0000_0000` | `0x7FFF_FFFF` | 2 GB |
| DRAM bank 1 | `0x1_0000_0000` | `0x1_7FFF_FFFF` | 2 GB |

**Total: 4 GB** (LPDDR4X, Samsung K4UCE3Q4AB-MGCL)

### Reserved Memory

| Region | Address | Size | Purpose |
|--------|---------|------|---------|
| RCPU code/data | `0xC080_0000` | 256 KB | RCPU firmware SRAM area |
| RCPU heap | `0x3000_0000` | 2 MB | RCPU heap |
| VRing 0 | `0x3020_0000` | 12 KB | Virtio ring 0 |
| VRing 1 | `0x3020_3000` | 12 KB | Virtio ring 1 |
| VDev buffer | `0x3020_6000` | 984 KB | Shared DMA buffer |
| Resource table | `0x302F_C000` | 16 KB | RCPU resource table |
| RCPU snapshots | `0x3030_0000` | 256 KB | Suspend snapshots |
| **DPU reserved** | **`0x2FF4_0000`** | **384 KB** | Display MMU tables (256K) + cmdlist (128K) |
| CMA pool | `0x4000_0000` | 384 MB | Contiguous memory allocator |

### On-Chip Memory

| Region | Address | Size | Purpose |
|--------|---------|------|---------|
| Boot ROM | — | 128 KB | Fixed SoC boot code |
| SRAM | — | 256 KB | Shared Main CPU / RCPU |
| TCM core0 | `0xD800_0000` | 128 KB | Tightly-coupled memory, core 0 |
| TCM core1 | `0xD802_0000` | 128 KB | TCM, core 1 |
| TCM core2 | `0xD804_0000` | 128 KB | TCM, core 2 |
| TCM core3 | `0xD806_0000` | 128 KB | TCM, core 3 |

---

## Peripheral Address Map

### Core Infrastructure

| Peripheral | Base Address | Size | Compatible |
|-----------|-------------|------|------------|
| **CLINT** | `0xE400_0000` | 64 KB | `riscv,clint0` |
| **PLIC** | `0xE000_0000` | 64 MB | `riscv,plic0` (159 IRQs, 7 priority levels) |
| Clock controller | `0xD405_0000` | ~8 KB | `ky,x1-clock` |
| Reset controller | `0xD405_0000` | ~8 KB | `ky,x1-reset` |
| Pin controller | `0xD401_E000` | 592 B | `pinconf-single-aib` |
| GPIO | `0xD401_9000` | 2 KB | `ky,x1-gpio` (4 banks) |
| CIU | `0xD428_2C00` | 720 B | `ky,aquila-ciu` (system config) |

### UARTs (all `ky,pxa-uart`, 256 bytes each, reg-shift=2, reg-io-width=4)

| UART | Base Address | IRQ | Status on OPi RV2 |
|------|-------------|-----|--------------------|
| **uart0 (serial0/console)** | **`0xD401_7000`** | 42 | **enabled** |
| uart2 | `0xD401_7100` | 44 | enabled |
| uart3 | `0xD401_7200` | 45 | disabled |
| uart4 | `0xD401_7300` | 46 | disabled |
| uart5 | `0xD401_7400` | 47 | disabled |
| uart6 | `0xD401_7500` | 48 | disabled |
| uart7 | `0xD401_7600` | 49 | disabled |
| uart8 | `0xD401_7700` | 50 | disabled |
| uart9 | `0xD401_7800` | 51 | disabled |
| r_uart1 (RCPU) | `0xC088_D000` | — | enabled |

**UART type: PXA-compatible** (Marvell PXA series register layout, NOT standard 16550)

### Timers

| Timer | Base Address | Size | Frequency |
|-------|-------------|------|-----------|
| timer0 | `0xD401_4000` | 200 B | 12.8 MHz (fast clk), 52 MHz (APB) |
| timer1 | `0xD401_6000` | 200 B | 12.8 MHz (disabled) |
| CLINT mtime | `0xE400_0000+` | — | Via CLINT (standard RISC-V) |
| **rdtime** | **CSR** | — | **24 MHz** (timebase-frequency) |

### Display / HDMI

| Block | Base Address | Size | Compatible |
|-------|-------------|------|------------|
| **DPU (HDMI)** | **`0xC044_0000`** | 168 KB | `ky,saturn-hdmi` / `ky,dpu-online2` |
| HDMI TX | `0xC040_0500` | 512 B | `ky,hdmi` |
| HDMI Audio SSPA | `0xC088_3900` | 768 B | `ky,ky-snd-sspa` |
| HDMI Audio DMA | `0xC088_3800` | 256 B | `ky,x1-adma` |
| HDMI Audio Buf | `0xC08D_0400` | ~15 KB | Audio DMA buffers |
| V2D (2D engine) | `0xC010_0000` | 4 KB | `ky,v2d` |
| VPU | `0xC050_0000` | 64 KB | `arm china,linlon-v5` (Linlon V5) |
| GPU | `0xCAC0_0000` | 1 MB | `img,rgx` (Imagination PowerVR) |
| JPU | `0xC02F_8000` | ~2 KB | JPEG encoder |

DPU reserved memory: `0x2FF4_0000` (256K MMU + 128K cmdlist) — **pre-allocated, no-map**

### Storage

| Block | Base Address | Size | Function on OPi RV2 |
|-------|-------------|------|--------------------|
| SDHCI0 | `0xD428_0000` | 512 B | **SD card** |
| SDHCI1 | `0xD428_0800` | 512 B | SDIO (WiFi) |
| SDHCI2 | `0xD428_1000` | 512 B | **eMMC** (HS400) |
| QSPI | `0xD420_C000` | 4 KB | SPI NOR flash (+ MMAP at `0xB800_0000`) |

### USB

| Block | Base Address | Compatible |
|-------|-------------|------------|
| USB2 PHY 0 | `0xC094_0000` | `ky,usb2-phy` |
| EHCI/OTG 0 | `0xC090_0100` | `ky,mv-ehci` / `ky,mv-otg` |
| USB2 PHY 1 | `0xC09C_0000` | `ky,usb2-phy` |
| EHCI 1 | `0xC098_0100` | `ky,mv-ehci` |
| USB3 PHY | `0xC0B1_0000` | `ky,x1-combphy` |
| USB3 DWC3 | `0xC0A0_0000` | `snps,dwc3` |
| USB2 PHY (USB3) | `0xC0A3_0000` | `ky,usb2-phy` |

### Networking

| Block | Base Address | Compatible |
|-------|-------------|------------|
| ETH0 (GMAC0) | `0xCAC8_0000` | `ky,x1-emac` |
| ETH1 (GMAC1) | `0xCAC8_1000` | `ky,x1-emac` |

### I2C (all `ky,x1-i2c`, APB clock 52 MHz)

| I2C | Base Address | IRQ | Status on OPi RV2 |
|-----|-------------|-----|--------------------|
| i2c0 | `0xD401_0800` | 36 | enabled |
| i2c1 | `0xD401_1000` | 37 | enabled |
| i2c2 | `0xD401_2000` | 38 | enabled (PMIC, codec, touchscreen) |
| i2c8 | `0xD401_D800` | 19 | enabled (PMIC: SPM8821 @ 0x41) |

### Other

| Block | Base Address | Compatible |
|-------|-------------|------------|
| Mailbox | `0xD401_3400` | `ky,x1-mailbox` |
| DMA (PDMA) | `0xD400_0000` | `ky,pdma-1.0` (16 channels) |
| Watchdog | `0xD408_0000` | `ky,soc-wdt` |
| RTC | `0xD401_0000` | `mrvl,mmp-rtc` |
| Thermal | `0xD401_8000` | `ky,x1-tsensor` (5 BJT sensors) |
| IR receiver | `0xD401_7F00` | `ky,x1-irc` |
| Crypto | `0xD860_0000` | `ky,crypto_engine` |
| CRNG | `0xF070_3800` | `ky,hw_crng` |
| eFuse | `0xF070_2800` | `simple-mfd` |
| PCIe0 | `0xCA00_0000` | `x1,dwc-pcie` (1 lane, Gen2) |
| PCIe1 | `0xCA40_0000` | `x1,dwc-pcie` (2 lanes, Gen2) |
| PCIe2 | `0xCA80_0000` | `x1,dwc-pcie` (2 lanes, Gen2) |

---

## Boot Configuration

### Boot Chain

```
BootROM → FSBL (SPL @ 0xC0801000) → OpenSBI (@ 0x0) → U-Boot (@ 0x00200000) → Kernel
```

### U-Boot Configuration (from `x1_defconfig`, branch `v2022.10-ky`)

| Config | Value | Significance |
|--------|-------|-------------|
| `CONFIG_SYS_TEXT_BASE` | **`0x0020_0000`** | U-Boot proper loads at 2 MB |
| `CONFIG_SYS_LOAD_ADDR` | `0x0020_0000` | Default load address |
| `CONFIG_BOOTCOMMAND` | `bootm 0x11000000` | **Kernel FIT image at 272 MB** |
| `CONFIG_SPL_FIT_SIGNATURE` | `y` | SPL verifies FIT signatures |
| `CONFIG_SPL_LOAD_FIT_ADDRESS` | `0x1100_0000` | FIT loaded to 272 MB |
| `CONFIG_SPL_TEXT_BASE` | `0xC080_1000` | SPL runs from SRAM |
| `CONFIG_SPL_STACK` | `0xC084_0000` | SPL stack in SRAM |
| `CONFIG_SPL_BSS_START_ADDR` | `0xC083_7000` | SPL BSS in SRAM |
| `CONFIG_CUSTOM_SYS_INIT_SP_ADDR` | `0x0100_0000` | Initial stack at 16 MB |
| `CONFIG_SPL_OPENSBI_LOAD_ADDR` | `0x0` | **OpenSBI at address 0** |
| `CONFIG_RISCV_SMODE` | `y` | U-Boot runs in S-mode |
| `CONFIG_NR_DRAM_BANKS` | `2` | Two DRAM banks |
| `CONFIG_FDT_SIMPLEFB` | **`y`** | **U-Boot passes SimpleFB in DTB!** |
| `CONFIG_DISPLAY_KY_HDMI` | `y` | U-Boot initializes HDMI display |
| `CONFIG_SPLASH_SCREEN` | `y` | Boot splash = display is live |
| `CONFIG_VIDEO_KY` | `y` | Vendor display driver in U-Boot |
| `CONFIG_SYS_WHITE_ON_BLACK` | `y` | Default console colors |
| `CONFIG_FASTBOOT_BUF_ADDR` | `0x1100_0000` | Fastboot buffer = same as FIT |
| `CONFIG_FASTBOOT_BUF_SIZE` | `0x1000_0000` | 256 MB fastboot buffer |
| `CONFIG_SYS_NS16550_IER` | `0x40` | UART IER init value |

### SPI NOR Flash Partition Layout (from `CONFIG_MTDPARTS_DEFAULT`)

```
d420c000.spi-0:
  0x000000  bootinfo    (64K)
  0x010000  private     (64K)
  0x020000  fsbl        (256K)
  0x060000  env         (64K)
  0x070000  opensbi     (192K)
  0x0A0000  uboot       (remaining)
```

### Orange Pi Build System

```
BOARDFAMILY="ky"
BOOTCONFIG="x1_defconfig"
BOOT_FDT_FILE="ky/x1_orangepi-rv2.dtb"
```

### Kernel Command Line

```
earlycon=sbi console=ttyS0,115200n8 loglevel=8 swiotlb=65536 rdinit=/init
```

Key implications:
- `earlycon=sbi` = OpenSBI provides SBI console (ecall interface)
- Console is uart0 (`ttyS0` = `serial0` alias = `0xD4017000`)
- UART2 at `0xD4017100` is also enabled (debug header on schematic)

### DRAM Layout (Complete)

```
0x0000_0000  ┌──────────────────────┐
             │  OpenSBI (M-mode)    │  SPL_OPENSBI_LOAD_ADDR = 0x0
0x0020_0000  ├──────────────────────┤
             │  U-Boot (S-mode)     │  SYS_TEXT_BASE = 0x00200000
             │                      │
0x0100_0000  │  (initial stack)     │  CUSTOM_SYS_INIT_SP_ADDR = 0x01000000
             │                      │
0x0400_0000  │  (SPL malloc pool)   │  64 MB, CUSTOM_SYS_SPL_MALLOC_ADDR
             │                      │
0x1100_0000  ├──────────────────────┤
             │  *** Tutorial-OS *** │  BOOTCOMMAND target = 0x11000000
             │  (FIT image load)    │  256 MB available before DPU reserved
             │                      │
0x2FF4_0000  ├──────────────────────┤
             │  DPU reserved        │  384 KB (display MMU + cmdlist)
0x3000_0000  ├──────────────────────┤
             │  RCPU heap + vrings  │  ~4 MB reserved
0x3040_0000  │                      │
             │                      │
0x4000_0000  ├──────────────────────┤
             │  CMA pool            │  768 MB (0x40000000 → 0x70000000)
0x7000_0000  ├──────────────────────┤
             │  Free DRAM           │
0x8000_0000  └──────────────────────┘  End of bank 0 (2 GB)

0x1_0000_0000 ┌────────────────────┐
              │  DRAM bank 1       │  2 GB
0x1_8000_0000 └────────────────────┘
```

---

## Key Insights for Tutorial-OS Porting

### 1. Kernel Load Address = 0x11000000
U-Boot's `CONFIG_BOOTCOMMAND` does `bootm 0x11000000`, and `CONFIG_SPL_LOAD_FIT_ADDRESS`
also points to `0x11000000`. This is where Tutorial-OS must be linked.

**Linker script `_start` address: `0x11000000`**

OpenSBI sits at `0x0`, U-Boot at `0x00200000`, so our kernel at `0x11000000` (272 MB)
is safely above both. We have ~497 MB of contiguous space before the DPU reserved
region at `0x2FF40000`.

### 2. SBI Console = Free UART
OpenSBI handles early console. For first boot, we can output via SBI ecalls without
touching UART hardware registers at all:
```asm
# SBI legacy putchar (extension 0x01)
li  a7, 1        # SBI extension: putchar
li  a0, 0x48     # Character 'H'
ecall
```

### 3. UART is PXA-Type, Not 16550
The UARTs are `ky,pxa-uart` — Marvell PXA series UART, NOT standard 16550.
Register layout: reg-shift=2, reg-io-width=4 (32-bit registers at 4-byte offsets).
For bare-metal UART driver, need PXA UART register definitions
(RBR, THR, IER, IIR, FCR, LCR, MCR, LSR at shifted offsets).

Note: U-Boot sets `CONFIG_SYS_NS16550_IER=0x40`, suggesting the PXA UART IER has
a vendor-specific bit 6 that needs to be set (likely UART unit enable or similar).

### 4. Timer: Use rdtime at 24 MHz
The timebase is 24 MHz. Timer implementation:
```c
static inline uint64_t read_time(void) {
    uint64_t t;
    asm volatile ("rdtime %0" : "=r"(t));
    return t;
}
// 24 ticks = 1 microsecond
#define TICKS_PER_US 24
```

### 5. Cache Management Uses Standard Zicbom
No vendor CSR hacks needed! The standard Zicbom extension is supported:
```asm
# Clean cache block (like ARM dc cvac)
cbo.clean (rs1)    # address in rs1

# Invalidate cache block (like ARM dc ivac)
cbo.inval (rs1)

# Clean + invalidate (like ARM dc civac)
cbo.flush (rs1)

# Zero cache block (Zicboz)
cbo.zero (rs1)
```
Block size = 64 bytes.

### 6. Display = DPU "Saturn" + HDMI TX — SimpleFB Handoff Confirmed!
The display pipeline is: DPU (at `0xC0440000`) → HDMI TX (at `0xC0400500`).
DPU has pre-reserved memory at `0x2FF40000` for its MMU tables.

**U-Boot confirms `CONFIG_FDT_SIMPLEFB=y`** — this means U-Boot initializes the HDMI
display (with splash screen support) and injects a `simple-framebuffer` node into the
device tree before booting the kernel. The DTB passed in `a1` will contain:
- Framebuffer physical address
- Width, height, stride
- Pixel format

**This is the fastest path to display**: Parse the DTB `simple-framebuffer` node,
extract the base address, and start writing pixels. No DPU register programming needed
for initial bring-up!

Additional display-related U-Boot configs:
- `CONFIG_DISPLAY_KY_HDMI=y` — HDMI output driver
- `CONFIG_DISPLAY_KY_MIPI=y` — MIPI DSI driver (for LCD panels)
- `CONFIG_DISPLAY_KY_EDP=y` — eDP driver
- `CONFIG_SPLASH_SCREEN=y` — Boot splash (display is live before handoff)
- `CONFIG_VIDEO_PCI_DEFAULT_FB_SIZE=0x1000000` — 16 MB framebuffer allocation

### 7. GPIO Controller
Base: `0xD4019000`, 4 banks (offsets `0x0`, `0x4`, `0x8`, `0x100`).
Heartbeat LED on GPIO 96 (active low). Pin muxing via pinctrl at `0xD401E000`.

### 8. Marvell/SpacemiT Heritage
The `0xD40xxxxx` peripheral range, PXA UART type, MMP RTC, and `mv-otg`/`mv-ehci`
USB controllers all confirm this is a **Marvell MMP-derived design** (likely SpacemiT K1).
Understanding this heritage helps when searching for register documentation —
PXA/MMP datasheets from Marvell may partially apply.

### 9. S-Mode Execution — No EL Drop Needed
`CONFIG_RISCV_SMODE=y` confirms U-Boot runs in Supervisor mode. OpenSBI owns M-mode
and handles the S→M ecall interface. Tutorial-OS runs in S-mode.

Unlike ARM64 where the kernel must drop from EL2/EL3 to EL1, on this platform
OpenSBI already handles the privilege transition. Our `entry.S` enters directly in
S-mode — no privilege level manipulation needed.

### 10. DRAM at Physical Address 0x0
Unlike most RISC-V SoCs where DRAM starts at `0x80000000`, the Ky X1 maps DRAM
starting at `0x0`. This is unusual but confirmed by both the device tree
(`memory@0 { reg = <0x0 0x0 0x0 0x80000000> }`) and U-Boot placing OpenSBI at
`CONFIG_SPL_OPENSBI_LOAD_ADDR=0x0`.

### 11. U-Boot → Kernel Handoff Convention
When U-Boot's `bootm` launches our image at `0x11000000`:
- `a0` = hart ID (current CPU core)
- `a1` = DTB pointer (physical address of flattened device tree)
- The DTB will contain the `simple-framebuffer` node injected by U-Boot
- Execution is in S-mode
- OpenSBI is resident and accessible via ecalls

### 12. SPI NOR Boot Path
The SPI NOR at `0xD420C000` (QSPI, memory-mapped at `0xB8000000`) contains the
early boot stages. SPL loads from either SPI NOR or eMMC (boot partition), then
loads OpenSBI + U-Boot FIT from the `opensbi` and `uboot` partitions. The raw
mode U-Boot sector offset is `0x680` (sector 1664 = 832KB into the device).
