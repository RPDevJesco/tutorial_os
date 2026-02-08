/*
 * usb_host.h - DWC2 USB Host Controller Driver
 * =============================================
 *
 * This header provides USB host functionality using the DesignWare Core 2
 * (DWC2) USB controller found in BCM283x SoCs (Raspberry Pi Zero 2W, etc.).
 *
 * WHAT IS DWC2?
 * -------------
 * The DesignWare Core 2 (DWC2) is a USB 2.0 controller IP core from Synopsys.
 * Broadcom licensed this IP and integrated it into the BCM283x series chips.
 * It's also found in many other SoCs (Samsung Exynos, Amlogic, Rockchip, etc.)
 *
 * The DWC2 can operate in either host mode (controlling USB devices) or
 * device mode (acting as a USB peripheral). This driver implements HOST MODE
 * only - we want to read input from USB gamepads, not present ourselves as
 * a USB device.
 *
 * WHY THIS DRIVER EXISTS:
 * -----------------------
 * The GPi Case 2W (a GameBoy-style handheld for Pi Zero 2W) presents its
 * built-in controls as an Xbox 360 USB controller. To read button presses,
 * we need to:
 *   1. Initialize the USB host controller
 *   2. Detect when a device is connected
 *   3. Enumerate the device (assign address, read descriptors)
 *   4. Configure the HID endpoint for interrupt transfers
 *   5. Periodically poll for input reports
 *
 * FEATURES:
 * ---------
 *   - USB host mode initialization
 *   - Device enumeration (address assignment, descriptor parsing)
 *   - Control transfers (for setup/configuration)
 *   - Interrupt transfers (for HID input reports)
 *   - Xbox 360 controller input parsing
 *
 * LIMITATIONS:
 * ------------
 * This is a MINIMAL implementation focused on HID input devices. It does NOT:
 *   - Support USB hubs (only direct connections)
 *   - Support isochronous transfers (audio/video streaming)
 *   - Support bulk transfers (mass storage)
 *   - Handle hot-plug robustly
 *   - Support multiple simultaneous devices
 *
 * LEARNING RESOURCES:
 * -------------------
 *   - USB 2.0 Specification: https://www.usb.org/document-library
 *   - DWC2 Programming Guide: (proprietary, but Linux source is helpful)
 *   - Linux DWC2 driver: drivers/usb/dwc2/ in kernel source
 *
 * FILE ORGANIZATION:
 * ------------------
 * This file is organized into clear sections:
 *   1. Register addresses (where the hardware lives)
 *   2. Bit definitions (what each register bit means)
 *   3. USB protocol constants (standard USB values)
 *   4. Data structures (packets, reports, controller state)
 *   5. Public API (functions you actually call)
 */

#ifndef USB_HOST_H
#define USB_HOST_H

#include "types.h"  /* Bare-metal type definitions */

/* =============================================================================
 * DWC2 REGISTER ADDRESSES
 * =============================================================================
 *
 * The DWC2 controller is mapped into the peripheral address space at offset
 * 0x980000 from the peripheral base. All registers are 32-bit and must be
 * accessed as such (no byte or halfword accesses).
 *
 * Register groups:
 *   - Core Global Registers (0x000-0x0FF): Controller-wide settings
 *   - Host Mode Registers (0x400-0x4FF): Host-specific configuration
 *   - Host Channel Registers (0x500+): Per-channel transfer control
 *   - Power/Clock Gating (0xE00): Power management
 *   - FIFOs (0x1000+): Data buffers for transfers
 *
 * The BCM2837 (Pi Zero 2W) maps peripherals at 0x3F000000.
 */

#define PERIPHERAL_BASE     0x3F000000
#define USB_BASE            (PERIPHERAL_BASE + 0x00980000)

/* -----------------------------------------------------------------------------
 * Core Global Registers
 * -----------------------------------------------------------------------------
 * These registers control the overall USB controller operation, regardless
 * of whether we're in host or device mode.
 */

/*
 * GOTGCTL - OTG Control and Status Register
 * 
 * Controls OTG (On-The-Go) features. OTG allows a port to dynamically switch
 * between host and device mode. We don't use OTG, but we might read this
 * register to check current mode status.
 */
#define USB_GOTGCTL         (USB_BASE + 0x000)

/*
 * GSNPSID - Synopsys ID Register
 * 
 * Contains the DWC2 core version. We read this to verify the hardware is
 * actually present and is a DWC2 controller. Expected value has 0x4F542000
 * in the upper bits (ASCII "OT2" for "OTG 2.0").
 */
#define USB_GSNPSID         (USB_BASE + 0x040)

/*
 * GAHBCFG - AHB Configuration Register
 * 
 * Configures how the controller interfaces with the AHB (Advanced High-speed
 * Bus). Most importantly, bit 0 enables/disables global interrupts.
 */
#define USB_GAHBCFG         (USB_BASE + 0x008)

/*
 * GUSBCFG - USB Configuration Register
 *
 * Core USB configuration: PHY selection, timeout calibration, and most
 * importantly, bits to force host or device mode.
 */
#define USB_GUSBCFG         (USB_BASE + 0x00C)

/*
 * GRSTCTL - Reset Control Register
 *
 * Controls various reset operations:
 *   - Core soft reset (reinitialize everything)
 *   - TX/RX FIFO flush (clear data buffers)
 * Also has a bit indicating AHB master idle (safe to reset).
 */
#define USB_GRSTCTL         (USB_BASE + 0x010)

/*
 * GINTSTS - Interrupt Status Register
 *
 * Shows which interrupts have occurred. Write 1 to clear individual bits.
 * In host mode, we care about:
 *   - SOF (Start of Frame) - every 1ms
 *   - RXFLVL (RX FIFO non-empty)
 *   - HPRTINT (Host port interrupt - connect/disconnect)
 *   - HCINT (Host channel interrupt - transfer complete)
 */
#define USB_GINTSTS         (USB_BASE + 0x014)

/*
 * GINTMSK - Interrupt Mask Register
 *
 * Enable/disable individual interrupt sources. Set bits to 1 to enable.
 * We mask most interrupts and poll status instead (simpler for bare-metal).
 */
#define USB_GINTMSK         (USB_BASE + 0x018)

/*
 * GRXSTSR/GRXSTSP - Receive Status Read/Pop Registers
 *
 * Contains information about data received in the RX FIFO:
 *   - Channel number
 *   - Packet status (data, setup complete, etc.)
 *   - Byte count
 * GRXSTSR peeks without removing; GRXSTSP pops the entry.
 */
#define USB_GRXSTSR         (USB_BASE + 0x01C)
#define USB_GRXSTSP         (USB_BASE + 0x020)

/*
 * GRXFSIZ - Receive FIFO Size Register
 *
 * Configures how much of the internal FIFO RAM is allocated for receiving.
 * Must balance between RX, periodic TX, and non-periodic TX FIFOs.
 */
#define USB_GRXFSIZ         (USB_BASE + 0x024)

/*
 * GNPTXFSIZ - Non-Periodic TX FIFO Size Register
 *
 * Configures the non-periodic transmit FIFO (for control and bulk OUT).
 * Lower 16 bits = start address, upper 16 bits = depth.
 */
#define USB_GNPTXFSIZ       (USB_BASE + 0x028)

/*
 * GNPTXSTS - Non-Periodic TX FIFO Status
 *
 * Shows available space in the non-periodic TX FIFO and request queue.
 * We poll this before writing data to ensure space is available.
 */
#define USB_GNPTXSTS        (USB_BASE + 0x02C)

/*
 * HPTXFSIZ - Host Periodic TX FIFO Size
 *
 * Configures the periodic transmit FIFO (for interrupt and isochronous OUT).
 * Same format as GNPTXFSIZ.
 */
#define USB_HPTXFSIZ        (USB_BASE + 0x100)

/* -----------------------------------------------------------------------------
 * Host Mode Registers
 * -----------------------------------------------------------------------------
 * These registers are only meaningful when operating as a USB host.
 */

/*
 * HCFG - Host Configuration Register
 *
 * Configures host mode operation:
 *   - Clock speed selection (FS/LS)
 *   - Frame interval
 * Bit 0 selects full-speed (1) vs high-speed (0) PHY clock.
 */
#define USB_HCFG            (USB_BASE + 0x400)

/*
 * HFIR - Host Frame Interval Register
 *
 * Sets the frame interval in PHY clocks. For full-speed operation at 48MHz,
 * this should be 48000 (1ms frame period x 48MHz = 48000 clocks).
 */
#define USB_HFIR            (USB_BASE + 0x404)

/*
 * HFNUM - Host Frame Number/Time Remaining
 *
 * Lower 16 bits: current frame number (increments every 1ms)
 * Upper 16 bits: time remaining in current frame (in PHY clocks)
 * 
 * We use bit 0 (even/odd frame) for certain protocol requirements.
 */
#define USB_HFNUM           (USB_BASE + 0x408)

/*
 * HAINT - Host All Channels Interrupt Register
 *
 * Shows which host channels have pending interrupts (one bit per channel).
 * The DWC2 has 8 channels, so bits 0-7 are meaningful.
 */
#define USB_HAINT           (USB_BASE + 0x414)

/*
 * HAINTMSK - Host All Channels Interrupt Mask
 *
 * Enable/disable per-channel interrupts. We enable all channels we use.
 */
#define USB_HAINTMSK        (USB_BASE + 0x418)

/*
 * HPRT - Host Port Control and Status Register
 *
 * THE MOST IMPORTANT HOST REGISTER! Controls and monitors the USB port:
 *   - Connection status (is a device plugged in?)
 *   - Port enable/disable
 *   - Port reset (required before enumeration)
 *   - Port power
 *   - Port speed detection (FS/LS after reset)
 *
 * CRITICAL: Some bits are "write 1 to clear" (W1C). When writing to this
 * register, you must read-modify-write carefully to avoid accidentally
 * clearing status bits you wanted to preserve!
 */
#define USB_HPRT            (USB_BASE + 0x440)

/* -----------------------------------------------------------------------------
 * Host Channel Registers
 * -----------------------------------------------------------------------------
 * The DWC2 has 8 host channels, each capable of handling one USB endpoint.
 * Each channel has its own set of registers at a fixed stride (0x20 bytes).
 *
 * We use Channel 0 for control transfers (enumeration) and Channel 1 for
 * interrupt transfers (HID input polling).
 */

/* Channel 0 base registers */
#define USB_HCCHAR0         (USB_BASE + 0x500)  /* Channel Characteristics */
#define USB_HCSPLT0         (USB_BASE + 0x504)  /* Split Transaction Control */
#define USB_HCINT0          (USB_BASE + 0x508)  /* Channel Interrupt Status */
#define USB_HCINTMSK0       (USB_BASE + 0x50C)  /* Channel Interrupt Mask */
#define USB_HCTSIZ0         (USB_BASE + 0x510)  /* Transfer Size */

/* Stride between channel register sets */
#define USB_HC_STRIDE       0x20

/*
 * Macro to calculate channel N register address
 * Example: USB_HCCHAR(2) = USB_HCCHAR0 + 2 * 0x20 = 0x540
 */
#define USB_HCCHAR(n)       (USB_HCCHAR0 + (n) * USB_HC_STRIDE)
#define USB_HCSPLT(n)       (USB_HCSPLT0 + (n) * USB_HC_STRIDE)
#define USB_HCINT(n)        (USB_HCINT0 + (n) * USB_HC_STRIDE)
#define USB_HCINTMSK(n)     (USB_HCINTMSK0 + (n) * USB_HC_STRIDE)
#define USB_HCTSIZ(n)       (USB_HCTSIZ0 + (n) * USB_HC_STRIDE)

/* -----------------------------------------------------------------------------
 * Power and Clock Gating Register
 * -----------------------------------------------------------------------------
 */

/*
 * PCGCCTL - Power and Clock Gating Control
 *
 * Controls power-saving features. We generally disable all gating during
 * operation to ensure the controller is always responsive.
 */
#define USB_PCGCCTL         (USB_BASE + 0xE00)

/* -----------------------------------------------------------------------------
 * FIFO Addresses
 * -----------------------------------------------------------------------------
 * Each channel has its own FIFO region for data transfer. Writing to a
 * channel's FIFO address pushes data for OUT transfers; reading pulls
 * data for IN transfers.
 */
#define USB_FIFO_BASE       (USB_BASE + 0x1000)
#define USB_FIFO(n)         (USB_FIFO_BASE + (n) * 0x1000)


/* =============================================================================
 * REGISTER BIT DEFINITIONS
 * =============================================================================
 *
 * Each register has specific bit fields. Using named constants makes code
 * much more readable than magic numbers like 0x80000000.
 */

/* -----------------------------------------------------------------------------
 * GAHBCFG - AHB Configuration Register Bits
 * -----------------------------------------------------------------------------
 */

/*
 * GAHBCFG_GLBL_INTR_EN - Global Interrupt Enable
 *
 * When set, the DWC2 can generate interrupts. When clear, all interrupts
 * are masked at the top level regardless of individual mask settings.
 * We enable this after configuration so we can poll GINTSTS.
 */
#define GAHBCFG_GLBL_INTR_EN    (1 << 0)

/* -----------------------------------------------------------------------------
 * GUSBCFG - USB Configuration Register Bits
 * -----------------------------------------------------------------------------
 */

/*
 * GUSBCFG_PHYSEL - USB 2.0 High-Speed PHY or USB 1.1 Full-Speed Interface
 *
 * Selects the PHY interface. On BCM283x, we use the internal full-speed PHY,
 * so this should be set to 1.
 */
#define GUSBCFG_PHYSEL          (1 << 6)

/*
 * GUSBCFG_FORCE_HOST - Force Host Mode
 *
 * Overrides OTG negotiation and forces the controller into host mode.
 * Since we always want to be a host (not a device), we set this bit.
 */
#define GUSBCFG_FORCE_HOST      (1 << 29)

/*
 * GUSBCFG_FORCE_DEV - Force Device Mode
 *
 * Opposite of FORCE_HOST. We ensure this is CLEAR when forcing host mode.
 */
#define GUSBCFG_FORCE_DEV       (1 << 30)

/* -----------------------------------------------------------------------------
 * GRSTCTL - Reset Control Register Bits
 * -----------------------------------------------------------------------------
 */

/*
 * GRSTCTL_CSRST - Core Soft Reset
 *
 * Writing 1 initiates a soft reset. All registers return to default values.
 * Poll this bit - it clears automatically when reset completes.
 */
#define GRSTCTL_CSRST           (1 << 0)

/*
 * GRSTCTL_RXFFLSH - RX FIFO Flush
 *
 * Flushes the entire receive FIFO. Use this after reset or when recovering
 * from errors.
 */
#define GRSTCTL_RXFFLSH         (1 << 4)

/*
 * GRSTCTL_TXFFLSH - TX FIFO Flush
 *
 * Flushes transmit FIFOs. Which FIFO(s) are flushed depends on TXFNUM bits.
 */
#define GRSTCTL_TXFFLSH         (1 << 5)

/*
 * GRSTCTL_TXFNUM_ALL - Flush All TX FIFOs
 *
 * When combined with TXFFLSH, flushes all transmit FIFOs.
 * Bits [10:6] = 0x10 means "all TXFIFOs".
 */
#define GRSTCTL_TXFNUM_ALL      (0x10 << 6)

/*
 * GRSTCTL_AHB_IDLE - AHB Master Idle
 *
 * Indicates the AHB master state machine is idle. Wait for this before
 * performing a soft reset to ensure no bus transactions are in progress.
 */
#define GRSTCTL_AHB_IDLE        (1 << 31)

/* -----------------------------------------------------------------------------
 * GINTSTS - Interrupt Status Register Bits
 * -----------------------------------------------------------------------------
 */

/*
 * GINTSTS_CURMOD - Current Mode of Operation
 *
 * 0 = Device mode, 1 = Host mode.
 * We poll this after forcing host mode to verify it took effect.
 */
#define GINTSTS_CURMOD          (1 << 0)

/*
 * GINTSTS_SOF - Start of Frame
 *
 * Set every 1ms (full-speed) or 125us (high-speed) at the start of each
 * USB frame/microframe. We use this for timing-sensitive operations like
 * control transfers.
 */
#define GINTSTS_SOF             (1 << 3)

/*
 * GINTSTS_RXFLVL - RX FIFO Non-Empty
 *
 * Indicates data is waiting in the receive FIFO. We poll this during
 * IN transfers.
 */
#define GINTSTS_RXFLVL          (1 << 4)

/*
 * GINTSTS_HPRTINT - Host Port Interrupt
 *
 * Indicates a host port event occurred (connect, disconnect, enable change,
 * overcurrent). Read HPRT to determine the specific event.
 */
#define GINTSTS_HPRTINT         (1 << 24)

/*
 * GINTSTS_HCINT - Host Channels Interrupt
 *
 * Indicates at least one host channel has a pending interrupt. Read HAINT
 * to see which channels, then read the channel's HCINT register.
 */
#define GINTSTS_HCINT           (1 << 25)

/* -----------------------------------------------------------------------------
 * HPRT - Host Port Control and Status Register Bits
 * -----------------------------------------------------------------------------
 * WARNING: Bits 1, 2, 3, 5 are "write 1 to clear" (W1C). When writing to
 * this register, always read first, AND out the W1C bits, then OR in your
 * changes. This prevents accidentally clearing status bits.
 *
 * Safe write pattern:
 *   val = mmio_read(USB_HPRT);
 *   val &= ~HPRT_W1C_MASK;     // Clear W1C bits in our copy
 *   val |= bits_to_set;        // Set what we want
 *   val &= ~bits_to_clear;     // Clear what we want
 *   mmio_write(USB_HPRT, val);
 */

/* Connection Status (read-only) - 1 = device connected */
#define HPRT_CONN_STS           (1 << 0)

/* Connection Detected (W1C) - Set when connect/disconnect occurs */
#define HPRT_CONN_DET           (1 << 1)

/* Port Enable (W1C to disable) - Set by hardware after reset completes */
#define HPRT_ENA                (1 << 2)

/* Port Enable Changed (W1C) - Set when port enable status changes */
#define HPRT_ENA_CHNG           (1 << 3)

/* Overcurrent Change (W1C) - Set when overcurrent status changes */
#define HPRT_OVRCUR_CHNG        (1 << 5)

/* Port Reset - Set to 1 to start reset, clear to end reset */
#define HPRT_RST                (1 << 8)

/* Port Power - Set to power on the port */
#define HPRT_PWR                (1 << 12)

/* Port Speed - Read after reset to determine device speed */
#define HPRT_SPD_SHIFT          17
#define HPRT_SPD_MASK           (0x3 << HPRT_SPD_SHIFT)
/* Speed values: 0 = High (480Mbps), 1 = Full (12Mbps), 2 = Low (1.5Mbps) */

/*
 * W1C Mask - All bits that are "write 1 to clear"
 *
 * Use this when doing read-modify-write:
 *   val = read(HPRT);
 *   val &= ~HPRT_W1C_MASK;  // Preserve W1C bits by writing 0
 *   val |= HPRT_RST;        // Set reset
 *   write(HPRT, val);
 */
#define HPRT_W1C_MASK           (HPRT_CONN_DET | HPRT_ENA | HPRT_ENA_CHNG | HPRT_OVRCUR_CHNG)

/* -----------------------------------------------------------------------------
 * HCCHAR - Host Channel Characteristics Register Bits
 * -----------------------------------------------------------------------------
 * Configures a channel for a specific endpoint. Set before starting transfer.
 */

/* Maximum Packet Size (bits [10:0]) */
#define HCCHAR_MPS_MASK         0x7FF

/* Endpoint Number (bits [14:11]) */
#define HCCHAR_EPNUM_SHIFT      11

/* Endpoint Direction - 1 = IN (device to host) */
#define HCCHAR_EPDIR_IN         (1 << 15)

/* Low-Speed Device - Set if target is a low-speed device */
#define HCCHAR_LSDEV            (1 << 17)

/* Endpoint Type (bits [19:18]) */
#define HCCHAR_EPTYPE_CTRL      (0 << 18)   /* Control */
#define HCCHAR_EPTYPE_ISOC      (1 << 18)   /* Isochronous */
#define HCCHAR_EPTYPE_BULK      (2 << 18)   /* Bulk */
#define HCCHAR_EPTYPE_INTR      (3 << 18)   /* Interrupt */

/* Multi Count / Error Count (bits [21:20]) - packets per frame */
#define HCCHAR_MC_SHIFT         20

/* Device Address (bits [28:22]) */
#define HCCHAR_DEVADDR_SHIFT    22

/* Odd Frame - Set for transfers that should occur in odd frames */
#define HCCHAR_ODDFRM           (1 << 29)

/* Channel Disable - Set to disable a running channel */
#define HCCHAR_CHDIS            (1 << 30)

/* Channel Enable - Set to start a transfer */
#define HCCHAR_CHEN             (1 << 31)

/* -----------------------------------------------------------------------------
 * HCTSIZ - Host Channel Transfer Size Register Bits
 * -----------------------------------------------------------------------------
 * Configures the transfer size and packet ID for a channel.
 */

/* Transfer Size (bits [18:0]) - total bytes to transfer */
#define HCTSIZ_XFERSIZE_SHIFT   0

/* Packet Count (bits [28:19]) - number of packets */
#define HCTSIZ_PKTCNT_SHIFT     19

/* Packet ID (bits [30:29]) - DATA0/DATA1/SETUP/MDATA */
#define HCTSIZ_PID_DATA0        (0 << 29)
#define HCTSIZ_PID_DATA1        (2 << 29)
#define HCTSIZ_PID_MDATA        (1 << 29)
#define HCTSIZ_PID_SETUP        (3 << 29)

/* -----------------------------------------------------------------------------
 * HCINT - Host Channel Interrupt Register Bits
 * -----------------------------------------------------------------------------
 * Status bits for transfer completion and errors. All bits are W1C.
 */

/* Transfer Completed - All data transferred successfully */
#define HCINT_XFERCOMP          (1 << 0)

/* Channel Halted - Channel has stopped (after disable or completion) */
#define HCINT_CHHLT             (1 << 1)

/* AHB Error - Bus error during DMA (we don't use DMA, rarely seen) */
#define HCINT_AHBERR            (1 << 2)

/* STALL Response - Endpoint returned STALL (error or not supported) */
#define HCINT_STALL             (1 << 3)

/* NAK Response - Endpoint has no data ready (try again later) */
#define HCINT_NAK               (1 << 4)

/* ACK Response - Packet acknowledged (good!) */
#define HCINT_ACK               (1 << 5)

/* Transaction Error - CRC error, timeout, bit stuff error, etc. */
#define HCINT_XACTERR           (1 << 7)

/* Babble Error - Endpoint sent more data than expected */
#define HCINT_BBLERR            (1 << 8)

/* Data Toggle Error - PID doesn't match expected toggle */
#define HCINT_DATATGLERR        (1 << 10)

/* Combined error mask for quick error checking */
#define HCINT_ERROR_MASK        (HCINT_AHBERR | HCINT_STALL | HCINT_XACTERR | HCINT_BBLERR)


/* =============================================================================
 * USB PROTOCOL CONSTANTS
 * =============================================================================
 *
 * Standard USB protocol values from the USB 2.0 specification.
 * These are the same regardless of what USB controller is used.
 */

/* -----------------------------------------------------------------------------
 * Standard Request Codes (bRequest field in SETUP packet)
 * -----------------------------------------------------------------------------
 */

#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05    /* Assign device address 1-127 */
#define USB_REQ_GET_DESCRIPTOR      0x06    /* Get device/config/string desc */
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09    /* Activate a configuration */
#define USB_REQ_GET_INTERFACE       0x0A
#define USB_REQ_SET_INTERFACE       0x0B

/* -----------------------------------------------------------------------------
 * Descriptor Types (wValue high byte in GET_DESCRIPTOR)
 * -----------------------------------------------------------------------------
 */

#define USB_DESC_DEVICE             0x01    /* 18 bytes, one per device */
#define USB_DESC_CONFIGURATION      0x02    /* Variable, includes interfaces */
#define USB_DESC_STRING             0x03    /* Unicode strings */
#define USB_DESC_INTERFACE          0x04    /* Embedded in config descriptor */
#define USB_DESC_ENDPOINT           0x05    /* Embedded in config descriptor */
#define USB_DESC_HID                0x21    /* HID class descriptor */
#define USB_DESC_HID_REPORT         0x22    /* HID report descriptor */

/* -----------------------------------------------------------------------------
 * Request Type Bits (bmRequestType field)
 * -----------------------------------------------------------------------------
 * Format: D7=direction, D6-5=type, D4-0=recipient
 */

/* Direction */
#define USB_REQTYPE_DIR_OUT         0x00    /* Host to device */
#define USB_REQTYPE_DIR_IN          0x80    /* Device to host */

/* Type */
#define USB_REQTYPE_TYPE_STANDARD   0x00    /* Standard USB request */
#define USB_REQTYPE_TYPE_CLASS      0x20    /* Class-specific request */
#define USB_REQTYPE_TYPE_VENDOR     0x40    /* Vendor-specific request */

/* Recipient */
#define USB_REQTYPE_RECIP_DEVICE    0x00    /* Request to device */
#define USB_REQTYPE_RECIP_INTERFACE 0x01    /* Request to interface */
#define USB_REQTYPE_RECIP_ENDPOINT  0x02    /* Request to endpoint */


/* =============================================================================
 * MAILBOX CONSTANTS (for USB power control)
 * =============================================================================
 *
 * The Raspberry Pi uses a mailbox interface to communicate with the VideoCore
 * GPU, which controls power to various peripherals including USB.
 * We must request USB power before the controller will work.
 */

#define MBOX_BASE                   (PERIPHERAL_BASE + 0x0000B880)
#define MBOX_READ                   (MBOX_BASE + 0x00)
#define MBOX_STATUS                 (MBOX_BASE + 0x18)
#define MBOX_WRITE                  (MBOX_BASE + 0x20)

#define MBOX_FULL                   0x80000000  /* Mailbox full, can't write */
#define MBOX_EMPTY                  0x40000000  /* Mailbox empty, nothing to read */


/* =============================================================================
 * DATA STRUCTURES
 * =============================================================================
 */

/*
 * USB Setup Packet (8 bytes)
 * --------------------------
 * Every USB control transfer begins with a SETUP packet containing these
 * 8 bytes. This tells the device what we want to do.
 *
 * The packet is sent in the SETUP stage, followed by optional DATA stage(s),
 * and finally a STATUS stage to confirm completion.
 */
typedef struct __attribute__((packed)) {
    uint8_t  bm_request_type;   /* Direction, type, and recipient */
    uint8_t  b_request;         /* Specific request code */
    uint16_t w_value;           /* Request-specific parameter */
    uint16_t w_index;           /* Request-specific index (often 0) */
    uint16_t w_length;          /* Number of bytes in DATA stage */
} usb_setup_packet_t;

/*
 * Xbox 360 Controller Input Report (20 bytes)
 * -------------------------------------------
 * The GPi Case 2W presents itself as an Xbox 360 controller and sends
 * input reports in this format via USB HID interrupt transfers.
 *
 * This structure matches the HID report descriptor the device provides.
 * All button states and analog values are in this single report.
 */
typedef struct __attribute__((packed)) {
    uint8_t  report_id;         /* Always 0x00 for this report type */
    uint8_t  report_length;     /* Always 0x14 (20 bytes) */
    uint8_t  buttons_low;       /* D-pad and Start/Back buttons */
    uint8_t  buttons_high;      /* Shoulder buttons and face buttons */
    uint8_t  left_trigger;      /* Left analog trigger (0-255) */
    uint8_t  right_trigger;     /* Right analog trigger (0-255) */
    int16_t  left_stick_x;      /* Left stick horizontal (-32768 to 32767) */
    int16_t  left_stick_y;      /* Left stick vertical */
    int16_t  right_stick_x;     /* Right stick horizontal */
    int16_t  right_stick_y;     /* Right stick vertical */
    uint8_t  reserved[6];       /* Padding to 20 bytes */
} xbox360_input_report_t;

/*
 * Button bit definitions for Xbox 360 report
 *
 * buttons_low (byte 2):
 *   bit 0 = D-Pad Up
 *   bit 1 = D-Pad Down
 *   bit 2 = D-Pad Left
 *   bit 3 = D-Pad Right
 *   bit 4 = Start
 *   bit 5 = Back
 *
 * buttons_high (byte 3):
 *   bit 0 = Left Bumper (LB)
 *   bit 1 = Right Bumper (RB)
 *   bit 2 = Guide/Xbox button
 *   bit 4 = A
 *   bit 5 = B
 *   bit 6 = X
 *   bit 7 = Y
 */
#define XBOX_BTN_DPAD_UP        (1 << 0)
#define XBOX_BTN_DPAD_DOWN      (1 << 1)
#define XBOX_BTN_DPAD_LEFT      (1 << 2)
#define XBOX_BTN_DPAD_RIGHT     (1 << 3)
#define XBOX_BTN_START          (1 << 4)
#define XBOX_BTN_BACK           (1 << 5)

#define XBOX_BTN_LB             (1 << 0)
#define XBOX_BTN_RB             (1 << 1)
#define XBOX_BTN_GUIDE          (1 << 2)
#define XBOX_BTN_A              (1 << 4)
#define XBOX_BTN_B              (1 << 5)
#define XBOX_BTN_X              (1 << 6)
#define XBOX_BTN_Y              (1 << 7)

/*
 * Transfer Result
 * ---------------
 * Result of a USB transfer operation. Multiple outcomes are possible
 * depending on device response and hardware status.
 */
typedef enum {
    TRANSFER_SUCCESS,       /* Transfer completed successfully */
    TRANSFER_NAK,           /* Device returned NAK (no data ready) */
    TRANSFER_STALL,         /* Device returned STALL (error/unsupported) */
    TRANSFER_ERROR,         /* Hardware error (CRC, timeout, etc.) */
    TRANSFER_TIMEOUT        /* Transfer timed out waiting */
} transfer_result_t;

/*
 * USB Host Controller State
 * -------------------------
 * Maintains state for the USB host controller and the currently
 * connected device. This is the main structure used by the driver.
 *
 * In a more complete driver, you'd have separate structures for the
 * controller and each connected device. We simplify since we only
 * support one device.
 */
typedef struct {
    uint8_t  device_address;    /* Assigned address (1-127), 0 = default */
    uint16_t ep0_max_packet;    /* Control endpoint max packet size */
    uint8_t  hid_endpoint;      /* Interrupt IN endpoint number for HID */
    uint16_t hid_max_packet;    /* HID endpoint max packet size */
    bool     hid_data_toggle;   /* DATA0/DATA1 toggle for HID endpoint */
    bool     enumerated;        /* True if device successfully enumerated */
    uint8_t  port_speed;        /* 0=high, 1=full, 2=low speed */
} usb_host_t;


/* =============================================================================
 * PUBLIC API
 * =============================================================================
 *
 * These functions provide the interface for using the USB host controller.
 * Call them in this typical sequence:
 *
 *   1. usb_host_init()           - Initialize the controller hardware
 *   2. usb_host_wait_connection() - Wait for a device to be plugged in
 *   3. usb_host_reset_port()      - Reset the port (required by USB spec)
 *   4. usb_host_enumerate()       - Enumerate the device (get descriptors)
 *   5. usb_host_read_input()      - Poll for HID input (call repeatedly)
 */

/*
 * usb_host_create() - Create a USB host controller instance
 *
 * Initializes the usb_host_t structure to default values. Does NOT
 * initialize the hardware - call usb_host_init() for that.
 *
 * Returns: Initialized usb_host_t structure
 */
usb_host_t usb_host_create(void);

/*
 * usb_host_init() - Initialize the USB host controller hardware
 *
 * Performs the following sequence:
 *   1. Request USB power via mailbox
 *   2. Verify DWC2 core is present
 *   3. Perform core soft reset
 *   4. Configure for host mode
 *   5. Set up FIFOs
 *   6. Configure host channels
 *   7. Power on the port
 *
 * @param host  Pointer to usb_host_t structure
 *
 * Returns: true on success, false on failure
 */
bool usb_host_init(usb_host_t *host);

/*
 * usb_host_wait_connection() - Wait for a device to connect
 *
 * Polls the host port status register until a device is detected or
 * timeout expires.
 *
 * @param host       Pointer to usb_host_t structure
 * @param timeout_ms Maximum time to wait in milliseconds
 *
 * Returns: true if device connected, false if timeout
 */
bool usb_host_wait_connection(usb_host_t *host, uint32_t timeout_ms);

/*
 * usb_host_reset_port() - Reset the USB port
 *
 * Performs a USB bus reset on the connected device. This is REQUIRED
 * by the USB specification before communicating with a newly connected
 * device. The reset also determines the device's speed capability.
 *
 * After reset, the device is in the "Default" state and responds to
 * address 0.
 *
 * @param host  Pointer to usb_host_t structure
 *
 * Returns: true on success, false on failure
 */
bool usb_host_reset_port(usb_host_t *host);

/*
 * usb_host_enumerate() - Enumerate the connected device
 *
 * Performs USB enumeration sequence:
 *   1. Get device descriptor (first 8 bytes to learn max packet size)
 *   2. Reset port again (required by some devices)
 *   3. Set device address to 1
 *   4. Get full device descriptor
 *   5. Get configuration descriptor
 *   6. Parse to find HID interrupt IN endpoint
 *   7. Set configuration to activate the device
 *
 * After successful enumeration, the device is ready for HID polling.
 *
 * @param host  Pointer to usb_host_t structure
 *
 * Returns: true on success, false on failure
 */
bool usb_host_enumerate(usb_host_t *host);

/*
 * usb_host_is_enumerated() - Check if a device is enumerated
 *
 * @param host  Pointer to usb_host_t structure
 *
 * Returns: true if device is enumerated and ready
 */
bool usb_host_is_enumerated(usb_host_t *host);

/*
 * usb_host_read_input() - Read HID input report
 *
 * Performs an interrupt IN transfer on the HID endpoint to read the
 * current input state. Call this periodically (e.g., once per frame).
 *
 * @param host    Pointer to usb_host_t structure
 * @param report  Pointer to xbox360_input_report_t to receive data
 *
 * Returns:
 *   TRANSFER_SUCCESS  - Report received, data in *report
 *   TRANSFER_NAK      - No new data (device hasn't updated)
 *   TRANSFER_ERROR    - Transfer failed
 *   TRANSFER_TIMEOUT  - Transfer timed out
 */
transfer_result_t usb_host_read_input(usb_host_t *host, xbox360_input_report_t *report);


/* =============================================================================
 * HELPER FUNCTION DECLARATIONS
 * =============================================================================
 *
 * These are internal functions used by the driver. They're declared here
 * so implementation can be split across files if needed.
 */

/*
 * Control transfer - Perform a USB control transfer
 *
 * @param host    USB host controller
 * @param setup   Setup packet describing the request
 * @param buffer  Data buffer for DATA stage (or NULL for no data)
 * @param length  Length of buffer
 *
 * Returns: Number of bytes transferred, or negative on error
 */
int usb_control_transfer(usb_host_t *host, const usb_setup_packet_t *setup,
                         uint8_t *buffer, uint16_t length);

#endif /* USB_HOST_H */
