/*
 * soc/bcm2712/bcm2712_regs.h - BCM2712 Register Definitions
 * ==========================================================
 */

#ifndef BCM2712_REGS_H
#define BCM2712_REGS_H

#include "hal/hal_types.h"

/*
 * BCM2712 Main Peripheral Base
 */
#define BCM2712_PERI_BASE       0x107c000000ULL

/*
 * Mailbox (for GPU communication)
 */
#define BCM2712_MBOX_BASE       (BCM2712_PERI_BASE + 0x013880)
#define MBOX_READ               0x00
#define MBOX_STATUS             0x18
#define MBOX_WRITE              0x20
#define MBOX_FULL               0x80000000
#define MBOX_EMPTY              0x40000000
#define MBOX_CHANNEL_PROP       8

/*
 * Mailbox Channels
 */
#define BCM_MBOX_CH_PROP        8

/*
 * Mailbox Request/Response
 */
#define BCM_MBOX_REQUEST        0x00000000
#define BCM_MBOX_RESPONSE_OK    0x80000000
#define BCM_MBOX_RESPONSE_ERR   0x80000001

/*
 * System Timer
 */
#define BCM2712_TIMER_BASE      (BCM2712_PERI_BASE + 0x003000)

/*
 * RP1 South Bridge
 */
#define RP1_PERI_BASE           0x1f000d0000ULL
#define RP1_GPIO_BASE           (RP1_PERI_BASE + 0x0d0000)
#define RP1_GPIO_IO_BANK0       (RP1_GPIO_BASE + 0x0000)
#define RP1_GPIO_PADS_BANK0     (RP1_GPIO_BASE + 0x4000)
#define RP1_UART0_BASE          (RP1_PERI_BASE + 0x030000)

/*
 * Mailbox Tags
 */
#define BCM_TAG_END                     0x00000000

/* Board info */
#define BCM_TAG_GET_BOARD_MODEL         0x00010001
#define BCM_TAG_GET_BOARD_REVISION      0x00010002
#define BCM_TAG_GET_BOARD_SERIAL        0x00010004
#define BCM_TAG_GET_ARM_MEMORY          0x00010005
#define BCM_TAG_GET_VC_MEMORY           0x00010006

/* Clocks */
#define BCM_TAG_GET_CLOCK_RATE          0x00030002
#define BCM_TAG_GET_MAX_CLOCK_RATE      0x00030004
#define BCM_TAG_GET_CLOCK_MEASURED      0x00030047
#define BCM_TAG_SET_CLOCK_RATE          0x00038002

/* Temperature */
#define BCM_TAG_GET_TEMPERATURE         0x00030006
#define BCM_TAG_GET_MAX_TEMPERATURE     0x0003000A

/* Throttle */
#define BCM_TAG_GET_THROTTLED           0x00030046

/* Power */
#define BCM_TAG_GET_POWER_STATE         0x00020001
#define BCM_TAG_SET_POWER_STATE         0x00028001

/* Framebuffer */
#define BCM_TAG_ALLOCATE_BUFFER         0x00040001
#define BCM_TAG_RELEASE_BUFFER          0x00048001
#define BCM_TAG_SET_PHYSICAL_SIZE       0x00048003
#define BCM_TAG_GET_PHYSICAL_SIZE       0x00040003
#define BCM_TAG_SET_VIRTUAL_SIZE        0x00048004
#define BCM_TAG_GET_VIRTUAL_SIZE        0x00040004
#define BCM_TAG_SET_DEPTH               0x00048005
#define BCM_TAG_GET_DEPTH               0x00040005
#define BCM_TAG_SET_PIXEL_ORDER         0x00048006
#define BCM_TAG_GET_PIXEL_ORDER         0x00040006
#define BCM_TAG_SET_VIRTUAL_OFFSET      0x00048009
#define BCM_TAG_GET_VIRTUAL_OFFSET      0x00040009
#define BCM_TAG_GET_PITCH               0x00040008
#define BCM_TAG_WAIT_VSYNC              0x0004000E

/* Legacy aliases */
#define TAG_GET_ARM_MEMORY      BCM_TAG_GET_ARM_MEMORY
#define TAG_GET_VC_MEMORY       BCM_TAG_GET_VC_MEMORY
#define TAG_GET_BOARD_MODEL     BCM_TAG_GET_BOARD_MODEL
#define TAG_GET_BOARD_REVISION  BCM_TAG_GET_BOARD_REVISION
#define TAG_GET_BOARD_SERIAL    BCM_TAG_GET_BOARD_SERIAL
#define TAG_GET_MAX_CLOCK_RATE  BCM_TAG_GET_MAX_CLOCK_RATE
#define TAG_SET_CLOCK_RATE      BCM_TAG_SET_CLOCK_RATE
#define TAG_END                 BCM_TAG_END

/*
 * Clock IDs
 */
#define CLOCK_ID_ARM            3
#define CLOCK_ID_CORE           4
#define CLOCK_ID_V3D            5
#define CLOCK_ID_UART           2

/*
 * Power Device IDs
 */
#define BCM_POWER_SD            0
#define BCM_POWER_UART0         1
#define BCM_POWER_UART1         2
#define BCM_POWER_USB           3
#define BCM_POWER_I2C0          4
#define BCM_POWER_I2C1          5
#define BCM_POWER_I2C2          6
#define BCM_POWER_SPI           7
#define BCM_POWER_CCP2TX        8

/*
 * Bus address conversion
 * BCM2712 uses same L2 coherent alias as BCM2711
 */
#define BCM_BUS_TO_ARM(addr)    ((addr) & ~0xC0000000)
#define BCM_ARM_TO_BUS(addr)    ((addr) | 0xC0000000)

#endif /* BCM2712_REGS_H */