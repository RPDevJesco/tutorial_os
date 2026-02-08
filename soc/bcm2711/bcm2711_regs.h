/*
 * soc/bcm2711/bcm2711_regs.h - BCM2711 Register Definitions
 *
 * Tutorial-OS: BCM2711 HAL Implementation
 *
 * BCM2711 is used in: Raspberry Pi 4B, CM4, Pi 400
 *
 * KEY DIFFERENCES FROM BCM2710:
 *   - Peripheral base at 0xFE000000 (was 0x3F000000)
 *   - GPIO pull resistors use new registers (GPIO_PUP_PDN_CNTRL)
 *   - Has GICv2 interrupt controller
 *   - Has PCIe (for USB 3.0, NVMe)
 *   - Same mailbox interface
 */

#ifndef BCM2711_REGS_H
#define BCM2711_REGS_H

#include "hal/hal_types.h"

/* =============================================================================
 * BASE ADDRESSES - Different from BCM2710!
 * =============================================================================
 */

#define BCM_PERIPHERAL_BASE     0xFE000000      /* Was 0x3F000000 */
#define BCM_LOCAL_BASE          0xFF800000      /* ARM local peripherals */

/* Legacy mapping for VideoCore (still uses old addresses internally) */
#define BCM_LEGACY_BASE         0x7E000000

/* =============================================================================
 * SYSTEM TIMER
 * =============================================================================
 */

#define BCM_SYSTIMER_BASE       (BCM_PERIPHERAL_BASE + 0x00003000)
#define BCM_SYSTIMER_CS         (BCM_SYSTIMER_BASE + 0x00)
#define BCM_SYSTIMER_CLO        (BCM_SYSTIMER_BASE + 0x04)
#define BCM_SYSTIMER_CHI        (BCM_SYSTIMER_BASE + 0x08)

/* =============================================================================
 * GPIO - Same offsets, different base. Pull registers changed!
 * =============================================================================
 */

#define BCM_GPIO_BASE           (BCM_PERIPHERAL_BASE + 0x00200000)

/* Function Select (same as BCM2710) */
#define BCM_GPFSEL0             (BCM_GPIO_BASE + 0x00)
#define BCM_GPFSEL1             (BCM_GPIO_BASE + 0x04)
#define BCM_GPFSEL2             (BCM_GPIO_BASE + 0x08)
#define BCM_GPFSEL3             (BCM_GPIO_BASE + 0x0C)
#define BCM_GPFSEL4             (BCM_GPIO_BASE + 0x10)
#define BCM_GPFSEL5             (BCM_GPIO_BASE + 0x14)

/* Pin Output Set/Clear (same as BCM2710) */
#define BCM_GPSET0              (BCM_GPIO_BASE + 0x1C)
#define BCM_GPSET1              (BCM_GPIO_BASE + 0x20)
#define BCM_GPCLR0              (BCM_GPIO_BASE + 0x28)
#define BCM_GPCLR1              (BCM_GPIO_BASE + 0x2C)

/* Pin Level (same as BCM2710) */
#define BCM_GPLEV0              (BCM_GPIO_BASE + 0x34)
#define BCM_GPLEV1              (BCM_GPIO_BASE + 0x38)

/*
 * NEW Pull-up/down registers for BCM2711!
 * BCM2710 used GPPUD + GPPUDCLK sequence.
 * BCM2711 uses direct 2-bit fields per pin.
 */
#define BCM_GPIO_PUP_PDN_CNTRL0 (BCM_GPIO_BASE + 0xE4)  /* GPIO 0-15 */
#define BCM_GPIO_PUP_PDN_CNTRL1 (BCM_GPIO_BASE + 0xE8)  /* GPIO 16-31 */
#define BCM_GPIO_PUP_PDN_CNTRL2 (BCM_GPIO_BASE + 0xEC)  /* GPIO 32-47 */
#define BCM_GPIO_PUP_PDN_CNTRL3 (BCM_GPIO_BASE + 0xF0)  /* GPIO 48-57 */

/* BCM2711 pull codes (2 bits per pin) */
#define BCM2711_GPIO_PULL_NONE  0
#define BCM2711_GPIO_PULL_UP    1
#define BCM2711_GPIO_PULL_DOWN  2

/* GPIO function codes (same as BCM2710) */
#define BCM_GPIO_FUNC_INPUT     0
#define BCM_GPIO_FUNC_OUTPUT    1
#define BCM_GPIO_FUNC_ALT0      4
#define BCM_GPIO_FUNC_ALT1      5
#define BCM_GPIO_FUNC_ALT2      6
#define BCM_GPIO_FUNC_ALT3      7
#define BCM_GPIO_FUNC_ALT4      3
#define BCM_GPIO_FUNC_ALT5      2

#define BCM_GPIO_MAX_PIN        57  /* BCM2711 has 58 GPIO pins */

/* =============================================================================
 * MAILBOX (same interface as BCM2710)
 * =============================================================================
 */

#define BCM_MAILBOX_BASE        (BCM_PERIPHERAL_BASE + 0x0000B880)
#define BCM_MBOX_READ           (BCM_MAILBOX_BASE + 0x00)
#define BCM_MBOX_STATUS         (BCM_MAILBOX_BASE + 0x18)
#define BCM_MBOX_WRITE          (BCM_MAILBOX_BASE + 0x20)

#define BCM_MBOX_FULL           0x80000000
#define BCM_MBOX_EMPTY          0x40000000

#define BCM_MBOX_REQUEST        0x00000000
#define BCM_MBOX_RESPONSE_OK    0x80000000
#define BCM_MBOX_CH_PROP        8

/* Mailbox tags (same as BCM2710) */
#define BCM_TAG_GET_FIRMWARE_REV    0x00000001
#define BCM_TAG_GET_BOARD_MODEL     0x00010001
#define BCM_TAG_GET_BOARD_REV       0x00010002
#define BCM_TAG_GET_BOARD_SERIAL    0x00010004
#define BCM_TAG_GET_ARM_MEMORY      0x00010005
#define BCM_TAG_GET_VC_MEMORY       0x00010006
#define BCM_TAG_GET_CLOCK_RATE      0x00030002
#define BCM_TAG_GET_CLOCK_MEASURED  0x00030047
#define BCM_TAG_GET_TEMPERATURE     0x00030006
#define BCM_TAG_GET_MAX_TEMP        0x0003000A
#define BCM_TAG_GET_THROTTLED       0x00030046
#define BCM_TAG_SET_POWER_STATE     0x00028001
#define BCM_TAG_GET_POWER_STATE     0x00020001

#define BCM_TAG_ALLOCATE_BUFFER     0x00040001
#define BCM_TAG_SET_PHYSICAL_SIZE   0x00048003
#define BCM_TAG_SET_VIRTUAL_SIZE    0x00048004
#define BCM_TAG_SET_VIRTUAL_OFFSET  0x00048009
#define BCM_TAG_SET_DEPTH           0x00048005
#define BCM_TAG_SET_PIXEL_ORDER     0x00048006
#define BCM_TAG_GET_PITCH           0x00040008
#define BCM_TAG_WAIT_FOR_VSYNC      0x0004800E

#define BCM_TAG_END                 0x00000000

/* Clock IDs */
#define BCM_CLOCK_EMMC      1
#define BCM_CLOCK_UART      2
#define BCM_CLOCK_ARM       3
#define BCM_CLOCK_CORE      4
#define BCM_CLOCK_EMMC2     12  /* New on BCM2711 */

/* Device IDs for power */
#define BCM_DEVICE_SD       0
#define BCM_DEVICE_UART0    1
#define BCM_DEVICE_UART1    2
#define BCM_DEVICE_USB      3

/* =============================================================================
 * USB (xHCI for USB 3.0, via PCIe)
 * =============================================================================
 * BCM2711 has VL805 USB 3.0 controller on PCIe.
 * Also has DWC2 for internal USB 2.0 hub.
 */

#define BCM_USB_DWC2_BASE       (BCM_PERIPHERAL_BASE + 0x00980000)

/* =============================================================================
 * EMMC2 (New SD/MMC controller on BCM2711)
 * =============================================================================
 */

#define BCM_EMMC2_BASE          (BCM_PERIPHERAL_BASE + 0x00340000)

/* =============================================================================
 * PWM
 * =============================================================================
 */

#define BCM_PWM_BASE            (BCM_PERIPHERAL_BASE + 0x0020C000)
#define BCM_PWM_CTL             (BCM_PWM_BASE + 0x00)
#define BCM_PWM_STA             (BCM_PWM_BASE + 0x04)
#define BCM_PWM_RNG1            (BCM_PWM_BASE + 0x10)
#define BCM_PWM_DAT1            (BCM_PWM_BASE + 0x14)
#define BCM_PWM_FIF1            (BCM_PWM_BASE + 0x18)
#define BCM_PWM_RNG2            (BCM_PWM_BASE + 0x20)
#define BCM_PWM_DAT2            (BCM_PWM_BASE + 0x24)

/* =============================================================================
 * CLOCK MANAGER
 * =============================================================================
 */

#define BCM_CM_BASE             (BCM_PERIPHERAL_BASE + 0x00101000)
#define BCM_CM_PWMCTL           (BCM_CM_BASE + 0xA0)
#define BCM_CM_PWMDIV           (BCM_CM_BASE + 0xA4)
#define BCM_CM_PASSWD           0x5A000000

/* =============================================================================
 * GIC (Generic Interrupt Controller) - New on BCM2711
 * =============================================================================
 */

#define BCM_GIC_BASE            0xFF840000
#define BCM_GICD_BASE           (BCM_GIC_BASE + 0x1000)  /* Distributor */
#define BCM_GICC_BASE           (BCM_GIC_BASE + 0x2000)  /* CPU interface */

/* =============================================================================
 * BUS ADDRESS CONVERSION
 * =============================================================================
 * BCM2711 uses different bus address mapping!
 */

/* For DMA addresses seen by VideoCore */
#define BCM_ARM_TO_BUS(addr)    (((uint32_t)(addr) & 0x3FFFFFFF) | 0xC0000000)
#define BCM_BUS_TO_ARM(addr)    ((uint32_t)(addr) & 0x3FFFFFFF)

#endif /* BCM2711_REGS_H */
