/*
 * soc/rk3528a/rk3528a_regs.h - Rockchip RK3528A Register Definitions
 *
 * Tutorial-OS: RK3528A HAL Implementation
 *
 * RK3528A is used in: Radxa Rock 2A, various TV boxes
 *
 * KEY DIFFERENCES FROM BROADCOM:
 *   - ARM Generic Timer (not BCM System Timer)
 *   - Rockchip GPIO controller (5 banks, 32 pins each)
 *   - Separate pinmux/iomux registers
 *   - VOP2 display controller (not VideoCore mailbox)
 *   - GRF (General Register Files) for system config
 *   - CRU (Clock and Reset Unit) for clock management
 *
 * Memory Map:
 *   0x00000000 - 0x00200000 : Boot ROM
 *   0x00200000 - 0x01200000 : SRAM
 *   0x02000000 - ...        : Peripherals
 *   0x40000000 - ...        : DRAM
 */

#ifndef RK3528A_REGS_H
#define RK3528A_REGS_H

#include "hal/hal_types.h"

/* =============================================================================
 * BASE ADDRESSES
 * =============================================================================
 */

#define RK_SRAM_BASE            0x00200000
#define RK_PERIPHERAL_BASE      0x02000000
#define RK_DRAM_BASE            0x40000000

/* =============================================================================
 * GPIO CONTROLLER
 * =============================================================================
 * RK3528A has 5 GPIO banks (GPIO0-GPIO4), each with 32 pins.
 * Each bank has its own register block.
 *
 * GPIO pins are named GPIO<bank>_<letter><num>, e.g., GPIO1_A3
 *   A = pins 0-7
 *   B = pins 8-15
 *   C = pins 16-23
 *   D = pins 24-31
 */

#define RK_GPIO0_BASE           (RK_PERIPHERAL_BASE + 0x02230000)
#define RK_GPIO1_BASE           (RK_PERIPHERAL_BASE + 0x02240000)
#define RK_GPIO2_BASE           (RK_PERIPHERAL_BASE + 0x02250000)
#define RK_GPIO3_BASE           (RK_PERIPHERAL_BASE + 0x02260000)
#define RK_GPIO4_BASE           (RK_PERIPHERAL_BASE + 0x02270000)

/* GPIO register offsets (same for all banks) */
#define RK_GPIO_SWPORT_DR_L     0x0000  /* Data Register Low (pins 0-15) */
#define RK_GPIO_SWPORT_DR_H     0x0004  /* Data Register High (pins 16-31) */
#define RK_GPIO_SWPORT_DDR_L    0x0008  /* Direction Low (0=in, 1=out) */
#define RK_GPIO_SWPORT_DDR_H    0x000C  /* Direction High */
#define RK_GPIO_INT_EN_L        0x0010  /* Interrupt Enable Low */
#define RK_GPIO_INT_EN_H        0x0014  /* Interrupt Enable High */
#define RK_GPIO_INT_MASK_L      0x0018  /* Interrupt Mask Low */
#define RK_GPIO_INT_MASK_H      0x001C  /* Interrupt Mask High */
#define RK_GPIO_INT_TYPE_L      0x0020  /* Interrupt Type Low */
#define RK_GPIO_INT_TYPE_H      0x0024  /* Interrupt Type High */
#define RK_GPIO_INT_POLARITY_L  0x0028  /* Interrupt Polarity Low */
#define RK_GPIO_INT_POLARITY_H  0x002C  /* Interrupt Polarity High */
#define RK_GPIO_INT_STATUS      0x0050  /* Interrupt Status */
#define RK_GPIO_INT_RAWSTATUS   0x0058  /* Raw Interrupt Status */
#define RK_GPIO_DEBOUNCE        0x0060  /* Debounce Enable */
#define RK_GPIO_PORTA_EOI       0x0068  /* End of Interrupt */
#define RK_GPIO_EXT_PORT        0x0070  /* External Port (read pin level) */
#define RK_GPIO_VER_ID          0x0078  /* Version ID */

/* Number of GPIO banks and pins */
#define RK_GPIO_BANKS           5
#define RK_GPIO_PINS_PER_BANK   32
#define RK_GPIO_MAX_PIN         (RK_GPIO_BANKS * RK_GPIO_PINS_PER_BANK - 1)

/* Macro to get GPIO bank base address */
static inline uintptr_t rk_gpio_bank_base(uint32_t bank) {
    const uintptr_t bases[] = {
        RK_GPIO0_BASE, RK_GPIO1_BASE, RK_GPIO2_BASE,
        RK_GPIO3_BASE, RK_GPIO4_BASE
    };
    return (bank < RK_GPIO_BANKS) ? bases[bank] : 0;
}

/* =============================================================================
 * IOMUX / PINMUX (Pin Function Selection)
 * =============================================================================
 * Rockchip uses GRF (General Register Files) for pin muxing.
 * Each pin has multiple possible functions.
 */

#define RK_GRF_BASE             (RK_PERIPHERAL_BASE + 0x02020000)
#define RK_PMU_GRF_BASE         (RK_PERIPHERAL_BASE + 0x02000000)

/* IOMUX registers for GPIO0 (in PMU_GRF) */
#define RK_PMU_GRF_GPIO0A_IOMUX (RK_PMU_GRF_BASE + 0x0000)
#define RK_PMU_GRF_GPIO0B_IOMUX (RK_PMU_GRF_BASE + 0x0008)

/* IOMUX registers for GPIO1-4 (in GRF) */
#define RK_GRF_GPIO1A_IOMUX     (RK_GRF_BASE + 0x0000)
#define RK_GRF_GPIO1B_IOMUX     (RK_GRF_BASE + 0x0008)
#define RK_GRF_GPIO1C_IOMUX     (RK_GRF_BASE + 0x0010)
#define RK_GRF_GPIO1D_IOMUX     (RK_GRF_BASE + 0x0018)
/* ... similar for GPIO2, GPIO3, GPIO4 */

/* Pull-up/down configuration in GRF */
#define RK_GRF_GPIO1A_P         (RK_GRF_BASE + 0x0100)
/* ... etc */

/* IOMUX function values (4 bits per pin, varies by pin) */
#define RK_IOMUX_GPIO           0   /* Function 0 = GPIO mode */
#define RK_IOMUX_FUNC1          1
#define RK_IOMUX_FUNC2          2
#define RK_IOMUX_FUNC3          3
/* etc - up to 15 functions per pin */

/* Write enable mask - Rockchip registers use write-enable bits in upper 16 bits */
#define RK_WRITE_MASK(bits)     ((bits) << 16)

/* =============================================================================
 * CRU (Clock and Reset Unit)
 * =============================================================================
 */

#define RK_CRU_BASE             (RK_PERIPHERAL_BASE + 0x02010000)

/* Some important clock gates */
#define RK_CRU_CLKGATE_CON0     (RK_CRU_BASE + 0x0300)
/* ... many more */

/* Clock sources */
#define RK_CRU_CLKSEL_CON0      (RK_CRU_BASE + 0x0100)
/* ... many more */

/* =============================================================================
 * ARM GENERIC TIMER (used instead of BCM System Timer)
 * =============================================================================
 * The ARM Generic Timer is accessed via system registers, not MMIO.
 * Frequency is typically 24 MHz on Rockchip.
 */

#define RK_TIMER_FREQ_HZ        24000000

/* System register access macros for ARM Generic Timer */
#define RK_READ_CNTFRQ()        ({ uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); v; })
#define RK_READ_CNTPCT()        ({ uint64_t v; __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v)); v; })
#define RK_READ_CNTVCT()        ({ uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); v; })

/* =============================================================================
 * VOP2 (Video Output Processor 2) - Display Controller
 * =============================================================================
 * Rockchip's display engine. Much more complex than BCM mailbox!
 */

#define RK_VOP2_BASE            (RK_PERIPHERAL_BASE + 0x02040000)

/* VOP2 register offsets */
#define RK_VOP2_REG_CFG_DONE    0x0000
#define RK_VOP2_VERSION         0x0004
#define RK_VOP2_SYS_CTRL        0x0008
#define RK_VOP2_DSP_CTRL0       0x000C
#define RK_VOP2_DSP_BG          0x0014
#define RK_VOP2_WIN0_CTRL0      0x0030
#define RK_VOP2_WIN0_CTRL1      0x0034
#define RK_VOP2_WIN0_VIR        0x003C
#define RK_VOP2_WIN0_YRGB_MST   0x0040
/* ... many more */

/* =============================================================================
 * HDMI TX
 * =============================================================================
 */

#define RK_HDMI_BASE            (RK_PERIPHERAL_BASE + 0x020A0000)

/* =============================================================================
 * UART
 * =============================================================================
 */

#define RK_UART0_BASE           (RK_PERIPHERAL_BASE + 0x02100000)
#define RK_UART1_BASE           (RK_PERIPHERAL_BASE + 0x02110000)
#define RK_UART2_BASE           (RK_PERIPHERAL_BASE + 0x02120000)

/* UART is 8250/16550 compatible */
#define RK_UART_RBR             0x00    /* Receive Buffer */
#define RK_UART_THR             0x00    /* Transmit Holding */
#define RK_UART_IER             0x04    /* Interrupt Enable */
#define RK_UART_FCR             0x08    /* FIFO Control */
#define RK_UART_LCR             0x0C    /* Line Control */
#define RK_UART_MCR             0x10    /* Modem Control */
#define RK_UART_LSR             0x14    /* Line Status */
#define RK_UART_USR             0x7C    /* Status */
#define RK_UART_DLL             0x00    /* Divisor Latch Low (when DLAB=1) */
#define RK_UART_DLH             0x04    /* Divisor Latch High */

/* =============================================================================
 * SD/MMC (DWMMC)
 * =============================================================================
 */

#define RK_SDMMC0_BASE          (RK_PERIPHERAL_BASE + 0x02100000)
#define RK_SDMMC1_BASE          (RK_PERIPHERAL_BASE + 0x02110000)

/* =============================================================================
 * USB
 * =============================================================================
 */

#define RK_USB2_OTG_BASE        (RK_PERIPHERAL_BASE + 0x02180000)

/* =============================================================================
 * GIC (Generic Interrupt Controller)
 * =============================================================================
 */

#define RK_GIC_BASE             (RK_PERIPHERAL_BASE + 0x02600000)
#define RK_GICD_BASE            (RK_GIC_BASE + 0x1000)
#define RK_GICC_BASE            (RK_GIC_BASE + 0x2000)

#endif /* RK3528A_REGS_H */
