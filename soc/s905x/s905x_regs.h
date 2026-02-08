/*
 * soc/s905x/s905x_regs.h - Amlogic S905X (Meson GXL) Register Definitions
 *
 * Tutorial-OS: S905X HAL Implementation
 *
 * S905X is used in: Libre Computer La Potato (AML-S905X-CC),
 *                   Khadas VIM1, various TV boxes
 *
 * KEY DIFFERENCES FROM BCM AND ROCKCHIP:
 *   - DRAM starts at 0x00000000 (not 0x3F000000 or 0x40000000)
 *   - Split peripheral buses: CBUS, AOBUS, PERIPHS, HIU
 *   - Amlogic GPIO controller with AO (Always-On) and EE domains
 *   - VPU (Video Processing Unit) for display (not VOP2, not VideoCore)
 *   - ARM Generic Timer (same as Rockchip, not BCM System Timer)
 *   - GICv2 interrupt controller
 *
 * AMLOGIC BUS ARCHITECTURE:
 *   The S905X has a split bus design with separate address ranges
 *   for different power/clock domains:
 *
 *   EE Domain (Everything Else - powered down in deep sleep):
 *     CBUS     0xC1100000 : General peripherals (timers, IR, etc.)
 *     PERIPHS  0xC8834000 : Pinmux, pad control for EE-domain GPIOs
 *     HIU      0xC883C000 : Clocks, PLLs, system control
 *     VPU      0xD0100000 : Video Processing Unit
 *     HDMI TX  0xC883A000 : HDMI transmitter
 *
 *   AO Domain (Always-On - stays powered in sleep):
 *     AOBUS    0xC8100000 : UART_AO, GPIO_AO, IR remote, RTC
 *
 *   This is very different from BCM (single peripheral base) and
 *   Rockchip (peripherals clustered around a single base).
 *
 * Memory Map:
 *   0x00000000 - ...        : DRAM
 *   0xC1100000 - 0xC11FFFFF : CBUS registers
 *   0xC8100000 - 0xC81FFFFF : AOBUS registers
 *   0xC8834000 - ...        : PERIPHS registers
 *   0xC883C000 - ...        : HIU (HHI) registers
 *   0xD0100000 - ...        : VPU registers
 */

#ifndef S905X_REGS_H
#define S905X_REGS_H

#include "hal/hal_types.h"

/* =============================================================================
 * BASE ADDRESSES
 * =============================================================================
 * Unlike BCM (one base) or Rockchip (one base + offsets), Amlogic scatters
 * peripherals across multiple independently-addressed bus regions.
 */

#define AML_DRAM_BASE           0x00000000

/* EE (Everything Else) domain */
#define AML_CBUS_BASE           0xC1100000  /* General EE peripherals */
#define AML_PERIPHS_BASE        0xC8834000  /* Pin mux, pad ctrl (EE GPIOs) */
#define AML_HIU_BASE            0xC883C000  /* Clocks, PLLs (HHI regs) */

/* AO (Always-On) domain */
#define AML_AOBUS_BASE          0xC8100000  /* AO peripherals */

/* Display */
#define AML_VPU_BASE            0xD0100000  /* Video Processing Unit */
#define AML_HDMI_TX_BASE        0xC883A000  /* HDMI transmitter control */

/* =============================================================================
 * GPIO CONTROLLER
 * =============================================================================
 * S905X has two GPIO domains:
 *
 *   1. AO domain (GPIOAO_0 - GPIOAO_9): Always powered, controlled via AOBUS
 *   2. EE domain (GPIOX, GPIODV, GPIOH, GPIOZ, CARD, BOOT): Via PERIPHS
 *
 * Each GPIO bank has:
 *   - Output Enable (OEN) register (active low: 0=output, 1=input)
 *   - Output Value (OUT) register
 *   - Input Value (IN) register
 *   - Pull-up/down enable and selection registers
 *
 * This is different from BCM (GPFSEL/GPSET/GPCLR) and Rockchip (GRF-based).
 *
 * PIN MUX:
 *   Each pin can be a GPIO or one of several alternate functions.
 *   Mux selection is done through PERIPHS registers (EE) or AOBUS (AO).
 *   Each pin typically has 4 bits for function selection.
 */

/* --- AO GPIO (GPIOAO_0 through GPIOAO_9) --- */
#define AML_AO_GPIO_BASE        (AML_AOBUS_BASE + 0x00)

/* AO GPIO registers (offsets from AML_AOBUS_BASE) */
#define AML_AO_RTI_PIN_MUX_0    (AML_AOBUS_BASE + 0x05 * 4)  /* AO pinmux reg 0 */
#define AML_AO_RTI_PIN_MUX_1    (AML_AOBUS_BASE + 0x06 * 4)  /* AO pinmux reg 1 */
#define AML_AO_GPIO_O_EN_N      (AML_AOBUS_BASE + 0x09 * 4)  /* OEN (low=out) + OUT */
#define AML_AO_GPIO_I           (AML_AOBUS_BASE + 0x0A * 4)  /* Input value */
#define AML_AO_RTI_PULL_UP      (AML_AOBUS_BASE + 0x0B * 4)  /* Pull-up enable + value */
#define AML_AO_RTI_PULL_UP_EN   (AML_AOBUS_BASE + 0x0C * 4)  /* Pull-up/down enable */

#define AML_AO_GPIO_COUNT       10  /* GPIOAO_0 through GPIOAO_9 */

/* --- EE GPIO (GPIOX, GPIODV, GPIOH, GPIOZ, CARD, BOOT) --- */

/*
 * EE GPIO pinmux registers (offsets from PERIPHS_BASE)
 * Each register controls mux for 8 pins (4 bits per pin).
 */
#define AML_PERIPHS_PIN_MUX_0   (AML_PERIPHS_BASE + 0x020 * 4)
#define AML_PERIPHS_PIN_MUX_1   (AML_PERIPHS_BASE + 0x021 * 4)
#define AML_PERIPHS_PIN_MUX_2   (AML_PERIPHS_BASE + 0x022 * 4)
#define AML_PERIPHS_PIN_MUX_3   (AML_PERIPHS_BASE + 0x023 * 4)
#define AML_PERIPHS_PIN_MUX_4   (AML_PERIPHS_BASE + 0x024 * 4)
#define AML_PERIPHS_PIN_MUX_5   (AML_PERIPHS_BASE + 0x025 * 4)
#define AML_PERIPHS_PIN_MUX_6   (AML_PERIPHS_BASE + 0x026 * 4)
#define AML_PERIPHS_PIN_MUX_7   (AML_PERIPHS_BASE + 0x027 * 4)
#define AML_PERIPHS_PIN_MUX_8   (AML_PERIPHS_BASE + 0x028 * 4)
#define AML_PERIPHS_PIN_MUX_9   (AML_PERIPHS_BASE + 0x029 * 4)

/*
 * EE GPIO data registers (offsets from PERIPHS_BASE)
 * OEN = Output Enable Negative (0=output, 1=input, opposite of what you'd expect!)
 * OUT = Output value
 * IN  = Input value
 */

/* GPIOX bank (19 pins: GPIOX_0 - GPIOX_18) */
#define AML_PREG_PAD_GPIO0_EN_N (AML_PERIPHS_BASE + 0x00C * 4)  /* GPIOX OEN */
#define AML_PREG_PAD_GPIO0_O    (AML_PERIPHS_BASE + 0x00D * 4)  /* GPIOX OUT */
#define AML_PREG_PAD_GPIO0_I    (AML_PERIPHS_BASE + 0x00E * 4)  /* GPIOX IN  */

/* GPIODV bank (30 pins: GPIODV_0 - GPIODV_29) */
#define AML_PREG_PAD_GPIO1_EN_N (AML_PERIPHS_BASE + 0x00F * 4)  /* GPIODV OEN */
#define AML_PREG_PAD_GPIO1_O    (AML_PERIPHS_BASE + 0x010 * 4)  /* GPIODV OUT */
#define AML_PREG_PAD_GPIO1_I    (AML_PERIPHS_BASE + 0x011 * 4)  /* GPIODV IN  */

/* GPIOH bank (10 pins: GPIOH_0 - GPIOH_9) */
#define AML_PREG_PAD_GPIO2_EN_N (AML_PERIPHS_BASE + 0x012 * 4)  /* GPIOH OEN */
#define AML_PREG_PAD_GPIO2_O    (AML_PERIPHS_BASE + 0x013 * 4)  /* GPIOH OUT */
#define AML_PREG_PAD_GPIO2_I    (AML_PERIPHS_BASE + 0x014 * 4)  /* GPIOH IN  */

/* BOOT bank (16 pins: BOOT_0 - BOOT_15) */
#define AML_PREG_PAD_GPIO3_EN_N (AML_PERIPHS_BASE + 0x015 * 4)  /* BOOT OEN */
#define AML_PREG_PAD_GPIO3_O    (AML_PERIPHS_BASE + 0x016 * 4)  /* BOOT OUT */
#define AML_PREG_PAD_GPIO3_I    (AML_PERIPHS_BASE + 0x017 * 4)  /* BOOT IN  */

/* CARD bank (7 pins: CARD_0 - CARD_6) */
#define AML_PREG_PAD_GPIO4_EN_N (AML_PERIPHS_BASE + 0x018 * 4)  /* CARD OEN */
#define AML_PREG_PAD_GPIO4_O    (AML_PERIPHS_BASE + 0x019 * 4)  /* CARD OUT */
#define AML_PREG_PAD_GPIO4_I    (AML_PERIPHS_BASE + 0x01A * 4)  /* CARD IN  */

/* GPIOZ bank (16 pins: GPIOZ_0 - GPIOZ_15) */
#define AML_PREG_PAD_GPIO5_EN_N (AML_PERIPHS_BASE + 0x01B * 4)  /* GPIOZ OEN */
#define AML_PREG_PAD_GPIO5_O    (AML_PERIPHS_BASE + 0x01C * 4)  /* GPIOZ OUT */
#define AML_PREG_PAD_GPIO5_I    (AML_PERIPHS_BASE + 0x01D * 4)  /* GPIOZ IN  */

/* Pull-up/down registers for EE GPIOs */
#define AML_PAD_PULL_UP_EN_0    (AML_PERIPHS_BASE + 0x048 * 4)  /* Pull enable GPIOX */
#define AML_PAD_PULL_UP_EN_1    (AML_PERIPHS_BASE + 0x049 * 4)  /* Pull enable GPIODV */
#define AML_PAD_PULL_UP_EN_2    (AML_PERIPHS_BASE + 0x04A * 4)  /* Pull enable GPIOH */
#define AML_PAD_PULL_UP_EN_3    (AML_PERIPHS_BASE + 0x04B * 4)  /* Pull enable BOOT */
#define AML_PAD_PULL_UP_EN_4    (AML_PERIPHS_BASE + 0x04C * 4)  /* Pull enable CARD */
#define AML_PAD_PULL_UP_EN_5    (AML_PERIPHS_BASE + 0x04D * 4)  /* Pull enable GPIOZ */

#define AML_PAD_PULL_UP_0       (AML_PERIPHS_BASE + 0x03A * 4)  /* Pull value GPIOX */
#define AML_PAD_PULL_UP_1       (AML_PERIPHS_BASE + 0x03B * 4)  /* Pull value GPIODV */
#define AML_PAD_PULL_UP_2       (AML_PERIPHS_BASE + 0x03C * 4)  /* Pull value GPIOH */
#define AML_PAD_PULL_UP_3       (AML_PERIPHS_BASE + 0x03D * 4)  /* Pull value BOOT */
#define AML_PAD_PULL_UP_4       (AML_PERIPHS_BASE + 0x03E * 4)  /* Pull value CARD */
#define AML_PAD_PULL_UP_5       (AML_PERIPHS_BASE + 0x03F * 4)  /* Pull value GPIOZ */

/* =============================================================================
 * ARM GENERIC TIMER (same concept as RK3528A)
 * =============================================================================
 * The ARM Generic Timer is accessed via system registers, not MMIO.
 * Frequency is typically 24 MHz on Amlogic (same as Rockchip!).
 *
 * If you've read timer.c for the RK3528A, this will look very familiar.
 * The S905X timer implementation is essentially identical because both
 * SoCs use the standard ARM Generic Timer.
 */

#define AML_TIMER_FREQ_HZ       24000000    /* 24 MHz */

/* System register access macros for ARM Generic Timer */
#define AML_READ_CNTFRQ()       ({ uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); v; })
#define AML_READ_CNTPCT()       ({ uint64_t v; __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v)); v; })
#define AML_READ_CNTVCT()       ({ uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); v; })

/* =============================================================================
 * HIU / HHI (Host/Hub Interface Unit) - Clock Control
 * =============================================================================
 * Controls PLLs, clock dividers, and system-level configuration.
 * Equivalent in function to Rockchip's CRU.
 */

#define AML_HHI_GP0_PLL_CNTL    (AML_HIU_BASE + 0x010 * 4)
#define AML_HHI_SYS_CPU_CLK_CNTL (AML_HIU_BASE + 0x067 * 4)
#define AML_HHI_GCLK_MPEG0      (AML_HIU_BASE + 0x050 * 4)
#define AML_HHI_GCLK_MPEG1      (AML_HIU_BASE + 0x051 * 4)
#define AML_HHI_GCLK_MPEG2      (AML_HIU_BASE + 0x052 * 4)

/* Temperature sensor (SAR ADC based, different from BCM mailbox or RK TSADC) */
#define AML_SAR_ADC_BASE        (AML_AOBUS_BASE + 0x680 * 4)

/* =============================================================================
 * UART
 * =============================================================================
 * S905X has multiple UARTs:
 *   UART_AO_A: Primary debug UART (on AO domain, always available)
 *   UART_AO_B: Secondary AO UART
 *   UART_A/B/C: EE-domain UARTs
 *
 * UART_AO_A is the standard debug console at 115200 baud.
 * This is more like Pi's 115200 and unlike Rock 2A's unusual 1.5Mbaud.
 *
 * The UART is NOT 16550-compatible (unlike Rockchip).
 * Amlogic uses its own UART register layout.
 */

#define AML_UART_AO_A_BASE      (AML_AOBUS_BASE + 0x4C0)
#define AML_UART_AO_B_BASE      (AML_AOBUS_BASE + 0x4E0)

/* UART register offsets (Amlogic-specific layout, NOT 16550!) */
#define AML_UART_WFIFO          0x00    /* Write FIFO */
#define AML_UART_RFIFO          0x04    /* Read FIFO */
#define AML_UART_CONTROL        0x08    /* Control register */
#define AML_UART_STATUS         0x0C    /* Status register */
#define AML_UART_MISC           0x10    /* Misc control */
#define AML_UART_REG5           0x14    /* Additional control */

/* UART status bits */
#define AML_UART_TX_FULL        (1 << 21)
#define AML_UART_TX_EMPTY       (1 << 22)
#define AML_UART_RX_EMPTY       (1 << 20)

/* =============================================================================
 * VPU (Video Processing Unit) - Display Controller
 * =============================================================================
 * Amlogic's display engine. Different from both VOP2 and VideoCore.
 *
 * The VPU handles:
 *   - OSD (On-Screen Display) layers - used as our framebuffer
 *   - Video scaling and processing
 *   - Output to HDMI, CVBS, etc.
 *
 * For bare-metal display, we use OSD1 as our framebuffer canvas.
 * Like the Rockchip approach, we assume U-Boot has done initial
 * display setup and we just update the framebuffer address.
 */

/* VPU OSD registers */
#define AML_VIU_OSD1_CTRL_STAT  (AML_VPU_BASE + 0x1A10 * 4)
#define AML_VIU_OSD1_BLK0_CFG_W0 (AML_VPU_BASE + 0x1A1B * 4)
#define AML_VIU_OSD1_BLK0_CFG_W1 (AML_VPU_BASE + 0x1A1C * 4)
#define AML_VIU_OSD1_BLK0_CFG_W2 (AML_VPU_BASE + 0x1A1D * 4)
#define AML_VIU_OSD1_BLK0_CFG_W3 (AML_VPU_BASE + 0x1A1E * 4)
#define AML_VIU_OSD1_BLK0_CFG_W4 (AML_VPU_BASE + 0x1A13 * 4)

/* Canvas (memory address mapping for VPU) */
#define AML_DMC_CAV_LUT_DATAL   (AML_VPU_BASE + 0x0048 * 4)
#define AML_DMC_CAV_LUT_DATAH   (AML_VPU_BASE + 0x0049 * 4)
#define AML_DMC_CAV_LUT_ADDR    (AML_VPU_BASE + 0x004A * 4)

/* VPP (Video Post-Processing) */
#define AML_VPP_POSTBLEND_H_SIZE (AML_VPU_BASE + 0x1D21 * 4)

/* =============================================================================
 * HDMI TX
 * =============================================================================
 */

#define AML_HDMI_TX_TOP_BASE    (AML_HDMI_TX_BASE + 0x000)

/* =============================================================================
 * SD/MMC
 * =============================================================================
 */

#define AML_SD_EMMC_A_BASE      (AML_CBUS_BASE + 0x70000)  /* SDIO */
#define AML_SD_EMMC_B_BASE      (AML_CBUS_BASE + 0x72000)  /* SD card */
#define AML_SD_EMMC_C_BASE      (AML_CBUS_BASE + 0x74000)  /* eMMC */

/* =============================================================================
 * USB
 * =============================================================================
 */

#define AML_USB0_BASE           0xC9000000  /* USB OTG */
#define AML_USB1_BASE           0xC9100000  /* USB Host */

/* =============================================================================
 * GIC (Generic Interrupt Controller v2)
 * =============================================================================
 */

#define AML_GIC_BASE            0xC4300000
#define AML_GICD_BASE           (AML_GIC_BASE + 0x1000)
#define AML_GICC_BASE           (AML_GIC_BASE + 0x2000)

/* =============================================================================
 * RESET CONTROLLER
 * =============================================================================
 */

#define AML_RESET0_REGISTER     (AML_CBUS_BASE + 0x01100 * 4)
#define AML_RESET1_REGISTER     (AML_CBUS_BASE + 0x01101 * 4)
#define AML_RESET2_REGISTER     (AML_CBUS_BASE + 0x01102 * 4)

#endif /* S905X_REGS_H */
