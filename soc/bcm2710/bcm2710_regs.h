/*
 * soc/bcm2710/bcm2710_regs.h - BCM2710/BCM2837 Register Definitions
 *
 * Tutorial-OS: BCM2710 HAL Implementation
 *
 * This header consolidates all hardware register addresses that were
 * previously scattered across driver headers (gpio.h, mailbox.h, etc.)
 *
 * BCM2710 = BCM2837 (same silicon, different names)
 * Used in: Raspberry Pi Zero 2W, Pi 3B, Pi 3B+, CM3
 *
 * Memory Map:
 *   0x00000000 - 0x3EFFFFFF : ARM RAM (size varies, GPU takes some)
 *   0x3F000000 - 0x3FFFFFFF : Peripheral registers (64MB window)
 *   0x40000000 - 0x400FFFFF : ARM local peripherals
 */

#ifndef BCM2710_REGS_H
#define BCM2710_REGS_H

#include "hal/hal_types.h"

/* =============================================================================
 * BASE ADDRESSES
 * =============================================================================
 */

#define BCM_PERIPHERAL_BASE     0x3F000000
#define BCM_LOCAL_BASE          0x40000000

/* =============================================================================
 * SYSTEM TIMER (1MHz free-running counter)
 * =============================================================================
 * From common/mmio.h - SYSTIMER_BASE, SYSTIMER_CLO, SYSTIMER_CHI
 */

#define BCM_SYSTIMER_BASE       (BCM_PERIPHERAL_BASE + 0x00003000)
#define BCM_SYSTIMER_CS         (BCM_SYSTIMER_BASE + 0x00)  /* Control/Status */
#define BCM_SYSTIMER_CLO        (BCM_SYSTIMER_BASE + 0x04)  /* Counter Low 32-bit */
#define BCM_SYSTIMER_CHI        (BCM_SYSTIMER_BASE + 0x08)  /* Counter High 32-bit */
#define BCM_SYSTIMER_C0         (BCM_SYSTIMER_BASE + 0x0C)  /* Compare 0 */
#define BCM_SYSTIMER_C1         (BCM_SYSTIMER_BASE + 0x10)  /* Compare 1 */
#define BCM_SYSTIMER_C2         (BCM_SYSTIMER_BASE + 0x14)  /* Compare 2 */
#define BCM_SYSTIMER_C3         (BCM_SYSTIMER_BASE + 0x18)  /* Compare 3 */

/* =============================================================================
 * GPIO
 * =============================================================================
 * From drivers/gpio/gpio.h
 */

#define BCM_GPIO_BASE           (BCM_PERIPHERAL_BASE + 0x00200000)

/* Function Select Registers (10 pins per register, 3 bits per pin) */
#define BCM_GPFSEL0             (BCM_GPIO_BASE + 0x00)   /* GPIO 0-9 */
#define BCM_GPFSEL1             (BCM_GPIO_BASE + 0x04)   /* GPIO 10-19 */
#define BCM_GPFSEL2             (BCM_GPIO_BASE + 0x08)   /* GPIO 20-29 */
#define BCM_GPFSEL3             (BCM_GPIO_BASE + 0x0C)   /* GPIO 30-39 */
#define BCM_GPFSEL4             (BCM_GPIO_BASE + 0x10)   /* GPIO 40-49 */
#define BCM_GPFSEL5             (BCM_GPIO_BASE + 0x14)   /* GPIO 50-53 */

/* Pin Output Set Registers */
#define BCM_GPSET0              (BCM_GPIO_BASE + 0x1C)   /* GPIO 0-31 */
#define BCM_GPSET1              (BCM_GPIO_BASE + 0x20)   /* GPIO 32-53 */

/* Pin Output Clear Registers */
#define BCM_GPCLR0              (BCM_GPIO_BASE + 0x28)   /* GPIO 0-31 */
#define BCM_GPCLR1              (BCM_GPIO_BASE + 0x2C)   /* GPIO 32-53 */

/* Pin Level Registers (read current state) */
#define BCM_GPLEV0              (BCM_GPIO_BASE + 0x34)   /* GPIO 0-31 */
#define BCM_GPLEV1              (BCM_GPIO_BASE + 0x38)   /* GPIO 32-53 */

/* Event Detect Status */
#define BCM_GPEDS0              (BCM_GPIO_BASE + 0x40)
#define BCM_GPEDS1              (BCM_GPIO_BASE + 0x44)

/* Rising Edge Detect Enable */
#define BCM_GPREN0              (BCM_GPIO_BASE + 0x4C)
#define BCM_GPREN1              (BCM_GPIO_BASE + 0x50)

/* Falling Edge Detect Enable */
#define BCM_GPFEN0              (BCM_GPIO_BASE + 0x58)
#define BCM_GPFEN1              (BCM_GPIO_BASE + 0x5C)

/* Pull-up/down Enable */
#define BCM_GPPUD               (BCM_GPIO_BASE + 0x94)
#define BCM_GPPUDCLK0           (BCM_GPIO_BASE + 0x98)   /* GPIO 0-31 */
#define BCM_GPPUDCLK1           (BCM_GPIO_BASE + 0x9C)   /* GPIO 32-53 */

/* BCM GPIO function codes (3-bit values, non-sequential!) */
#define BCM_GPIO_FUNC_INPUT     0   /* 000 */
#define BCM_GPIO_FUNC_OUTPUT    1   /* 001 */
#define BCM_GPIO_FUNC_ALT0      4   /* 100 */
#define BCM_GPIO_FUNC_ALT1      5   /* 101 */
#define BCM_GPIO_FUNC_ALT2      6   /* 110 */
#define BCM_GPIO_FUNC_ALT3      7   /* 111 */
#define BCM_GPIO_FUNC_ALT4      3   /* 011 */
#define BCM_GPIO_FUNC_ALT5      2   /* 010 */

/* Pull resistor codes */
#define BCM_GPIO_PULL_OFF       0
#define BCM_GPIO_PULL_DOWN      1
#define BCM_GPIO_PULL_UP        2

/* Maximum GPIO pin number */
#define BCM_GPIO_MAX_PIN        53

/* =============================================================================
 * MAILBOX (ARM <-> VideoCore communication)
 * =============================================================================
 * From drivers/mailbox/mailbox.h
 */

#define BCM_MAILBOX_BASE        (BCM_PERIPHERAL_BASE + 0x0000B880)
#define BCM_MBOX_READ           (BCM_MAILBOX_BASE + 0x00)
#define BCM_MBOX_STATUS         (BCM_MAILBOX_BASE + 0x18)
#define BCM_MBOX_WRITE          (BCM_MAILBOX_BASE + 0x20)

/* Status register bits */
#define BCM_MBOX_FULL           0x80000000
#define BCM_MBOX_EMPTY          0x40000000

/* Response codes */
#define BCM_MBOX_REQUEST        0x00000000
#define BCM_MBOX_RESPONSE_OK    0x80000000
#define BCM_MBOX_RESPONSE_ERR   0x80000001

/* Channels */
#define BCM_MBOX_CH_POWER       0
#define BCM_MBOX_CH_FB          1
#define BCM_MBOX_CH_VUART       2
#define BCM_MBOX_CH_VCHIQ       3
#define BCM_MBOX_CH_LEDS        4
#define BCM_MBOX_CH_BUTTONS     5
#define BCM_MBOX_CH_TOUCH       6
#define BCM_MBOX_CH_PROP        8   /* Property tags (most common) */

/* =============================================================================
 * MAILBOX PROPERTY TAGS
 * =============================================================================
 * From drivers/mailbox/mailbox.h
 */

/* VideoCore info */
#define BCM_TAG_GET_FIRMWARE_REV    0x00000001
#define BCM_TAG_GET_BOARD_MODEL     0x00010001
#define BCM_TAG_GET_BOARD_REV       0x00010002
#define BCM_TAG_GET_BOARD_MAC       0x00010003
#define BCM_TAG_GET_BOARD_SERIAL    0x00010004
#define BCM_TAG_GET_ARM_MEMORY      0x00010005
#define BCM_TAG_GET_VC_MEMORY       0x00010006

/* Clocks */
#define BCM_TAG_GET_CLOCK_RATE      0x00030002
#define BCM_TAG_GET_MAX_CLOCK_RATE  0x00030004
#define BCM_TAG_GET_MIN_CLOCK_RATE  0x00030007
#define BCM_TAG_SET_CLOCK_RATE      0x00038002
#define BCM_TAG_GET_CLOCK_MEASURED  0x00030047

/* Clock IDs */
#define BCM_CLOCK_EMMC      1
#define BCM_CLOCK_UART      2
#define BCM_CLOCK_ARM       3
#define BCM_CLOCK_CORE      4
#define BCM_CLOCK_V3D       5
#define BCM_CLOCK_H264      6
#define BCM_CLOCK_ISP       7
#define BCM_CLOCK_SDRAM     8
#define BCM_CLOCK_PIXEL     9
#define BCM_CLOCK_PWM       10

/* Power */
#define BCM_TAG_GET_POWER_STATE     0x00020001
#define BCM_TAG_SET_POWER_STATE     0x00028001
#define BCM_TAG_GET_TIMING          0x00020002

/* Device IDs for power control (mailbox) */
#define BCM_DEVICE_SD       0
#define BCM_DEVICE_UART0    1
#define BCM_DEVICE_UART1    2
#define BCM_DEVICE_USB      3

/* Temperature */
#define BCM_TAG_GET_TEMPERATURE     0x00030006
#define BCM_TAG_GET_MAX_TEMP        0x0003000A

/* Throttle status */
#define BCM_TAG_GET_THROTTLED       0x00030046

/* Framebuffer */
#define BCM_TAG_ALLOCATE_BUFFER     0x00040001
#define BCM_TAG_RELEASE_BUFFER      0x00048001
#define BCM_TAG_BLANK_SCREEN        0x00040002
#define BCM_TAG_GET_PHYSICAL_SIZE   0x00040003
#define BCM_TAG_TEST_PHYSICAL_SIZE  0x00044003
#define BCM_TAG_SET_PHYSICAL_SIZE   0x00048003
#define BCM_TAG_GET_VIRTUAL_SIZE    0x00040004
#define BCM_TAG_TEST_VIRTUAL_SIZE   0x00044004
#define BCM_TAG_SET_VIRTUAL_SIZE    0x00048004
#define BCM_TAG_GET_DEPTH           0x00040005
#define BCM_TAG_TEST_DEPTH          0x00044005
#define BCM_TAG_SET_DEPTH           0x00048005
#define BCM_TAG_GET_PIXEL_ORDER     0x00040006
#define BCM_TAG_TEST_PIXEL_ORDER    0x00044006
#define BCM_TAG_SET_PIXEL_ORDER     0x00048006
#define BCM_TAG_GET_ALPHA_MODE      0x00040007
#define BCM_TAG_TEST_ALPHA_MODE     0x00044007
#define BCM_TAG_SET_ALPHA_MODE      0x00048007
#define BCM_TAG_GET_PITCH           0x00040008
#define BCM_TAG_GET_VIRTUAL_OFFSET  0x00040009
#define BCM_TAG_TEST_VIRTUAL_OFFSET 0x00044009
#define BCM_TAG_SET_VIRTUAL_OFFSET  0x00048009
#define BCM_TAG_GET_OVERSCAN        0x0004000A
#define BCM_TAG_TEST_OVERSCAN       0x0004400A
#define BCM_TAG_SET_OVERSCAN        0x0004800A
#define BCM_TAG_GET_PALETTE         0x0004000B
#define BCM_TAG_TEST_PALETTE        0x0004400B
#define BCM_TAG_SET_PALETTE         0x0004800B
#define BCM_TAG_WAIT_FOR_VSYNC      0x0004800E

#define BCM_TAG_END                 0x00000000

/* =============================================================================
 * USB (DWC2 OTG Controller)
 * =============================================================================
 * From drivers/usb/usb_host.h
 */

#define BCM_USB_BASE                (BCM_PERIPHERAL_BASE + 0x00980000)

/* Core Global Registers */
#define BCM_USB_GOTGCTL             (BCM_USB_BASE + 0x000)
#define BCM_USB_GSNPSID             (BCM_USB_BASE + 0x040)
#define BCM_USB_GAHBCFG             (BCM_USB_BASE + 0x008)
#define BCM_USB_GUSBCFG             (BCM_USB_BASE + 0x00C)
#define BCM_USB_GRSTCTL             (BCM_USB_BASE + 0x010)
#define BCM_USB_GINTSTS             (BCM_USB_BASE + 0x014)
#define BCM_USB_GINTMSK             (BCM_USB_BASE + 0x018)
#define BCM_USB_GRXSTSR             (BCM_USB_BASE + 0x01C)
#define BCM_USB_GRXSTSP             (BCM_USB_BASE + 0x020)
#define BCM_USB_GRXFSIZ             (BCM_USB_BASE + 0x024)
#define BCM_USB_GNPTXFSIZ           (BCM_USB_BASE + 0x028)
#define BCM_USB_GHWCFG1             (BCM_USB_BASE + 0x044)
#define BCM_USB_GHWCFG2             (BCM_USB_BASE + 0x048)
#define BCM_USB_GHWCFG3             (BCM_USB_BASE + 0x04C)
#define BCM_USB_GHWCFG4             (BCM_USB_BASE + 0x050)

/* Host Mode Registers */
#define BCM_USB_HCFG                (BCM_USB_BASE + 0x400)
#define BCM_USB_HFIR                (BCM_USB_BASE + 0x404)
#define BCM_USB_HFNUM               (BCM_USB_BASE + 0x408)
#define BCM_USB_HPTXSTS             (BCM_USB_BASE + 0x410)
#define BCM_USB_HAINT               (BCM_USB_BASE + 0x414)
#define BCM_USB_HAINTMSK            (BCM_USB_BASE + 0x418)
#define BCM_USB_HPRT                (BCM_USB_BASE + 0x440)

/* Host Channel Registers (offset by channel * 0x20) */
#define BCM_USB_HCCHAR(n)           (BCM_USB_BASE + 0x500 + (n) * 0x20)
#define BCM_USB_HCSPLT(n)           (BCM_USB_BASE + 0x504 + (n) * 0x20)
#define BCM_USB_HCINT(n)            (BCM_USB_BASE + 0x508 + (n) * 0x20)
#define BCM_USB_HCINTMSK(n)         (BCM_USB_BASE + 0x50C + (n) * 0x20)
#define BCM_USB_HCTSIZ(n)           (BCM_USB_BASE + 0x510 + (n) * 0x20)
#define BCM_USB_HCDMA(n)            (BCM_USB_BASE + 0x514 + (n) * 0x20)

/* Power and Clock Gating */
#define BCM_USB_PCGCCTL             (BCM_USB_BASE + 0xE00)

/* FIFOs */
#define BCM_USB_FIFO(n)             (BCM_USB_BASE + 0x1000 + (n) * 0x1000)

/* =============================================================================
 * PWM (Pulse Width Modulation - Audio)
 * =============================================================================
 * From drivers/audio/audio.h
 */

#define BCM_PWM_BASE                (BCM_PERIPHERAL_BASE + 0x0020C000)
#define BCM_PWM_CTL                 (BCM_PWM_BASE + 0x00)
#define BCM_PWM_STA                 (BCM_PWM_BASE + 0x04)
#define BCM_PWM_DMAC                (BCM_PWM_BASE + 0x08)
#define BCM_PWM_RNG1                (BCM_PWM_BASE + 0x10)
#define BCM_PWM_DAT1                (BCM_PWM_BASE + 0x14)
#define BCM_PWM_FIF1                (BCM_PWM_BASE + 0x18)
#define BCM_PWM_RNG2                (BCM_PWM_BASE + 0x20)
#define BCM_PWM_DAT2                (BCM_PWM_BASE + 0x24)

/* PWM Control bits */
#define BCM_PWM_CTL_PWEN1           (1 << 0)
#define BCM_PWM_CTL_MODE1           (1 << 1)
#define BCM_PWM_CTL_RPTL1           (1 << 2)
#define BCM_PWM_CTL_SBIT1           (1 << 3)
#define BCM_PWM_CTL_POLA1           (1 << 4)
#define BCM_PWM_CTL_USEF1           (1 << 5)
#define BCM_PWM_CTL_CLRF            (1 << 6)
#define BCM_PWM_CTL_MSEN1           (1 << 7)
#define BCM_PWM_CTL_PWEN2           (1 << 8)
#define BCM_PWM_CTL_MODE2           (1 << 9)
#define BCM_PWM_CTL_RPTL2           (1 << 10)
#define BCM_PWM_CTL_SBIT2           (1 << 11)
#define BCM_PWM_CTL_POLA2           (1 << 12)
#define BCM_PWM_CTL_USEF2           (1 << 13)
#define BCM_PWM_CTL_MSEN2           (1 << 15)

/* =============================================================================
 * CLOCK MANAGER
 * =============================================================================
 * From drivers/audio/audio.h
 */

#define BCM_CM_BASE                 (BCM_PERIPHERAL_BASE + 0x00101000)
#define BCM_CM_PWMCTL               (BCM_CM_BASE + 0xA0)
#define BCM_CM_PWMDIV               (BCM_CM_BASE + 0xA4)

/* Clock manager password (must be ORed into writes) */
#define BCM_CM_PASSWD               0x5A000000

/* =============================================================================
 * SDHOST (SD Card Controller)
 * =============================================================================
 * From drivers/sdcard/sdhost.c (if you have one)
 */

#define BCM_SDHOST_BASE             (BCM_PERIPHERAL_BASE + 0x00202000)

/* =============================================================================
 * UART
 * =============================================================================
 */

/* UART0 - PL011 */
#define BCM_UART0_BASE              (BCM_PERIPHERAL_BASE + 0x00201000)
#define BCM_UART0_DR                (BCM_UART0_BASE + 0x00)
#define BCM_UART0_FR                (BCM_UART0_BASE + 0x18)
#define BCM_UART0_IBRD              (BCM_UART0_BASE + 0x24)
#define BCM_UART0_FBRD              (BCM_UART0_BASE + 0x28)
#define BCM_UART0_LCRH              (BCM_UART0_BASE + 0x2C)
#define BCM_UART0_CR                (BCM_UART0_BASE + 0x30)
#define BCM_UART0_ICR               (BCM_UART0_BASE + 0x44)

/* UART1 - Mini UART (auxiliary peripheral) */
#define BCM_AUX_BASE                (BCM_PERIPHERAL_BASE + 0x00215000)
#define BCM_AUX_ENABLES             (BCM_AUX_BASE + 0x04)
#define BCM_AUX_MU_IO               (BCM_AUX_BASE + 0x40)
#define BCM_AUX_MU_IER              (BCM_AUX_BASE + 0x44)
#define BCM_AUX_MU_IIR              (BCM_AUX_BASE + 0x48)
#define BCM_AUX_MU_LCR              (BCM_AUX_BASE + 0x4C)
#define BCM_AUX_MU_MCR              (BCM_AUX_BASE + 0x50)
#define BCM_AUX_MU_LSR              (BCM_AUX_BASE + 0x54)
#define BCM_AUX_MU_CNTL             (BCM_AUX_BASE + 0x60)
#define BCM_AUX_MU_BAUD             (BCM_AUX_BASE + 0x68)

/* =============================================================================
 * INTERRUPT CONTROLLER
 * =============================================================================
 */

#define BCM_IRQ_BASE                (BCM_PERIPHERAL_BASE + 0x0000B000)
#define BCM_IRQ_BASIC_PENDING       (BCM_IRQ_BASE + 0x200)
#define BCM_IRQ_PENDING1            (BCM_IRQ_BASE + 0x204)
#define BCM_IRQ_PENDING2            (BCM_IRQ_BASE + 0x208)
#define BCM_IRQ_FIQ_CTRL            (BCM_IRQ_BASE + 0x20C)
#define BCM_IRQ_ENABLE1             (BCM_IRQ_BASE + 0x210)
#define BCM_IRQ_ENABLE2             (BCM_IRQ_BASE + 0x214)
#define BCM_IRQ_ENABLE_BASIC        (BCM_IRQ_BASE + 0x218)
#define BCM_IRQ_DISABLE1            (BCM_IRQ_BASE + 0x21C)
#define BCM_IRQ_DISABLE2            (BCM_IRQ_BASE + 0x220)
#define BCM_IRQ_DISABLE_BASIC       (BCM_IRQ_BASE + 0x224)

/* =============================================================================
 * ARM LOCAL PERIPHERALS
 * =============================================================================
 */

#define BCM_LOCAL_CONTROL           (BCM_LOCAL_BASE + 0x00)
#define BCM_LOCAL_PRESCALER         (BCM_LOCAL_BASE + 0x08)
#define BCM_LOCAL_GPU_INT_ROUTE     (BCM_LOCAL_BASE + 0x0C)

/* Per-core mailboxes (for multicore) */
#define BCM_LOCAL_MBOX_SET(core)    (BCM_LOCAL_BASE + 0x80 + (core) * 0x10)
#define BCM_LOCAL_MBOX_CLR(core)    (BCM_LOCAL_BASE + 0xC0 + (core) * 0x10)

/* =============================================================================
 * BUS ADDRESS CONVERSION
 * =============================================================================
 * VideoCore sees memory differently than ARM. When passing addresses
 * to the GPU via mailbox, use these macros.
 */

/* Convert ARM physical address to VideoCore bus address */
#define BCM_ARM_TO_BUS(addr)        ((uint32_t)(addr) | 0xC0000000)

/* Convert VideoCore bus address to ARM physical address */
#define BCM_BUS_TO_ARM(addr)        ((uint32_t)(addr) & 0x3FFFFFFF)

/* =============================================================================
 * CACHE
 * =============================================================================
 */

#define BCM_CACHE_LINE_SIZE         64

#endif /* BCM2710_REGS_H */