/*
 * usb_host.c - DWC2 USB Host Controller Driver Implementation
 * =============================================================
 *
 * This file implements the USB host controller driver for the DWC2 core.
 * See usb_host.h for detailed documentation of the interface and concepts.
 *
 * IMPLEMENTATION NOTES:
 * ---------------------
 * This driver uses POLLING rather than interrupts. While less efficient for
 * CPU usage, polling is simpler to understand and debug, which makes it ideal
 * for learning and bare-metal environments where we control all execution.
 *
 * The driver handles only ONE device at a time and doesn't support USB hubs.
 * This limitation simplifies the code significantly while still being useful
 * for our target use case (reading the GPi Case 2W gamepad).
 *
 * DATA TOGGLE EXPLANATION:
 * ------------------------
 * USB uses a "data toggle" mechanism for error detection. Each DATA packet
 * alternates between DATA0 and DATA1 PIDs. If a packet is corrupted or lost,
 * the sender resends with the same PID. The receiver can detect duplicates
 * by noticing the PID didn't toggle.
 *
 * For control endpoints, the toggle is reset to DATA1 at the start of each
 * transaction. For interrupt/bulk endpoints, we must track the toggle state
 * across transfers.
 *
 * FIFO USAGE:
 * -----------
 * The DWC2 has internal FIFOs (buffers) for data transfer:
 *   - RX FIFO: Receives all incoming data from all channels
 *   - Non-periodic TX FIFO: Sends control and bulk OUT data
 *   - Periodic TX FIFO: Sends interrupt and isochronous OUT data
 *
 * We must size these appropriately based on our maximum packet sizes.
 * A simple allocation that works for basic use:
 *   - RX FIFO: 512 words (2KB)
 *   - Non-periodic TX: 256 words starting at word 512
 *   - Periodic TX: 256 words starting at word 768
 */

#include "usb_host.h"

#include "mmio.h"

/* =============================================================================
 * USB POWER CONTROL VIA MAILBOX
 * =============================================================================
 *
 * The VideoCore GPU controls power to the USB controller. We must request
 * power before the USB hardware will respond to register access.
 *
 * Communication uses a mailbox interface with a specific message format:
 * - Messages are placed in a 16-byte aligned buffer
 * - Buffer address (with channel in low 4 bits) is written to MBOX_WRITE
 * - GPU processes request and writes response back to the buffer
 * - We read MBOX_READ to confirm completion
 */

/*
 * USB power mailbox message buffer
 *
 * Must be 16-byte aligned (the hardware requires this).
 * Placed in static storage with alignment attribute.
 */
static volatile uint32_t __attribute__((aligned(16))) usb_mbox_buffer[8];

/*
 * usb_power_on() - Request USB power via mailbox
 *
 * The message format is a "property tag" message:
 *   [0] Buffer size in bytes
 *   [1] Request/response code (0 = request)
 *   [2] Tag ID (0x28001 = SET_POWER_STATE)
 *   [3] Tag value buffer size
 *   [4] Request/response tag
 *   [5] Device ID (3 = USB HCD)
 *   [6] State (bit 0 = on, bit 1 = wait)
 *   [7] End tag (0)
 *
 * Returns: true if power was granted, false otherwise
 */
static bool usb_power_on(void)
{
    /* Build the mailbox message */
    usb_mbox_buffer[0] = 8 * 4;     /* Buffer size: 8 words = 32 bytes */
    usb_mbox_buffer[1] = 0;          /* Request code (will become response) */
    usb_mbox_buffer[2] = 0x28001;    /* SET_POWER_STATE tag */
    usb_mbox_buffer[3] = 8;          /* Value buffer size (2 words = 8 bytes) */
    usb_mbox_buffer[4] = 8;          /* Request size = response size */
    usb_mbox_buffer[5] = 3;          /* Device ID 3 = USB HCD */
    usb_mbox_buffer[6] = 3;          /* State: bit 0 = on, bit 1 = wait for stable */
    usb_mbox_buffer[7] = 0;          /* End tag */

    dmb();  /* Ensure buffer is written before we signal GPU */

    /*
     * Calculate mailbox message value:
     * - Upper 28 bits = buffer address
     * - Lower 4 bits = channel (8 = ARM-to-VC property)
     */
    uint32_t mbox_addr = (uint32_t)(uintptr_t)usb_mbox_buffer;
    uint32_t mbox_msg = (mbox_addr & ~0xF) | 8;

    /* Wait for mailbox to not be full */
    for (int i = 0; i < 10000; i++) {
        if ((mmio_read(MBOX_STATUS) & MBOX_FULL) == 0) {
            break;
        }
        delay_us(1);
    }

    /* Write our message to the mailbox */
    mmio_write(MBOX_WRITE, mbox_msg);

    /* Wait for a response */
    for (int i = 0; i < 100000; i++) {
        if ((mmio_read(MBOX_STATUS) & MBOX_EMPTY) == 0) {
            uint32_t response = mmio_read(MBOX_READ);
            if (response == mbox_msg) {
                /*
                 * Success if:
                 * - Buffer[1] indicates response (bit 31 set)
                 * - Buffer[6] bit 0 indicates power is on
                 */
                dmb();  /* Ensure we read the updated buffer */
                return (usb_mbox_buffer[6] & 1) == 1;
            }
        }
        delay_us(10);
    }

    return false;  /* Timeout */
}


/* =============================================================================
 * USB CHANNEL MANAGEMENT
 * =============================================================================
 *
 * The DWC2 has 8 host channels for concurrent transfers. Each channel can
 * handle one USB endpoint at a time. We use:
 *   - Channel 0: Control transfers (enumeration, configuration)
 *   - Channel 1: Interrupt transfers (HID input polling)
 */

#define USB_CHANNEL_CONTROL     0
#define USB_CHANNEL_INTERRUPT   1

/*
 * disable_channel() - Safely disable a host channel
 *
 * Before reprogramming a channel, we must ensure any pending transfer is
 * stopped. The process is:
 *   1. Set CHDIS (channel disable) bit
 *   2. Wait for CHHLT (channel halted) interrupt
 *   3. Clear all interrupt flags
 *
 * @param ch  Channel number (0-7)
 */
static void disable_channel(uint8_t ch)
{
    uint32_t hcchar = mmio_read(USB_HCCHAR(ch));

    /* If channel is enabled, disable it */
    if (hcchar & HCCHAR_CHEN) {
        mmio_write(USB_HCCHAR(ch), hcchar | HCCHAR_CHDIS);

        /* Wait for channel to halt (with timeout) */
        for (int i = 0; i < 10000; i++) {
            if (mmio_read(USB_HCINT(ch)) & HCINT_CHHLT) {
                break;
            }
            delay_us(1);
        }
    }

    /* Clear all channel interrupts */
    mmio_write(USB_HCINT(ch), 0xFFFFFFFF);
}

/*
 * wait_for_sof() - Wait for Start of Frame
 *
 * Some USB operations must be synchronized to the frame boundary. This
 * function waits until the next SOF interrupt occurs.
 *
 * Full-speed USB has 1ms frames, so this waits up to 1ms.
 */
static void wait_for_sof(void)
{
    /* Clear any pending SOF */
    mmio_write(USB_GINTSTS, GINTSTS_SOF);

    /* Wait for next SOF (up to 3ms timeout) */
    for (int i = 0; i < 3000; i++) {
        if (mmio_read(USB_GINTSTS) & GINTSTS_SOF) {
            mmio_write(USB_GINTSTS, GINTSTS_SOF);  /* Clear it */
            return;
        }
        delay_us(1);
    }
}

/*
 * wait_tx_fifo() - Wait for TX FIFO space
 *
 * Before writing data for an OUT transfer, we must ensure there's room
 * in the non-periodic TX FIFO.
 *
 * @param words  Number of 32-bit words needed
 *
 * Returns: true if space available, false if timeout
 */
static bool wait_tx_fifo(uint32_t words)
{
    for (int i = 0; i < 10000; i++) {
        uint32_t txsts = mmio_read(USB_GNPTXSTS);
        uint32_t available = txsts & 0xFFFF;  /* Lower 16 bits = available words */
        if (available >= words) {
            return true;
        }
        delay_us(1);
    }
    return false;
}


/* =============================================================================
 * TRANSFER EXECUTION
 * =============================================================================
 *
 * USB transfers happen in packets. Each packet exchange follows this pattern:
 *   1. Host sends token (tells device what we want to do)
 *   2. Data phase (either host or device sends data)
 *   3. Handshake (ACK, NAK, or STALL)
 *
 * The DWC2 handles the low-level packet timing automatically. We just
 * configure the channel registers and let hardware do the work.
 */

/*
 * MAX_TRANSFER_RETRIES - How many NAKs to accept before giving up
 *
 * Devices return NAK when they're not ready. We retry several times before
 * declaring failure, especially for interrupt endpoints that may not have
 * new data every poll.
 */
#define MAX_TRANSFER_RETRIES    100

/*
 * do_transfer() - Execute a single USB transfer
 *
 * This is the workhorse function that actually moves data. It configures
 * a host channel and waits for completion.
 *
 * @param host       USB host controller state
 * @param ch         Channel number to use
 * @param ep         Endpoint number on the device
 * @param is_in      true = IN (device to host), false = OUT
 * @param ep_type    Endpoint type (HCCHAR_EPTYPE_CTRL, etc.)
 * @param pid        Packet ID (DATA0, DATA1, SETUP)
 * @param buffer     Data buffer (read for OUT, written for IN)
 * @param length     Number of bytes to transfer
 *
 * Returns: transfer_result_t indicating outcome
 */
static transfer_result_t do_transfer(
    usb_host_t *host,
    uint8_t ch,
    uint8_t ep,
    bool is_in,
    uint32_t ep_type,
    uint32_t pid,
    uint8_t *buffer,
    uint16_t length)
{
    /* Ensure channel is idle before starting */
    disable_channel(ch);

    /*
     * Control transfers should start at frame boundary.
     * This improves timing reliability with some devices.
     */
    if (ep_type == HCCHAR_EPTYPE_CTRL) {
        wait_for_sof();
    }

    /*
     * Determine max packet size for this endpoint.
     * Endpoint 0 uses the device's declared max packet size.
     * HID endpoint uses its own max packet size.
     */
    uint16_t max_pkt = (ep == 0) ? host->ep0_max_packet : host->hid_max_packet;

    /*
     * Build HCCHAR register value
     * This configures the channel for the specific endpoint:
     * - Max packet size (bits 10:0)
     * - Endpoint number (bits 14:11)
     * - Direction (bit 15)
     * - Low-speed flag (bit 17)
     * - Endpoint type (bits 19:18)
     * - Multi-count (bits 21:20) - always 1 for non-split
     * - Device address (bits 28:22)
     * - Odd frame flag (bit 29) - for periodic endpoints
     */
    uint32_t hcchar = (max_pkt & HCCHAR_MPS_MASK)
                    | ((uint32_t)ep << HCCHAR_EPNUM_SHIFT)
                    | (is_in ? HCCHAR_EPDIR_IN : 0)
                    | (host->port_speed == 2 ? HCCHAR_LSDEV : 0)  /* LS device */
                    | ep_type
                    | (1 << HCCHAR_MC_SHIFT)
                    | ((uint32_t)host->device_address << HCCHAR_DEVADDR_SHIFT);

    /* Add odd frame flag for periodic endpoints if in odd frame */
    uint32_t frame = mmio_read(USB_HFNUM) & 1;
    if (frame) {
        hcchar |= HCCHAR_ODDFRM;
    }

    /*
     * For IN transfers, request one max packet worth of data.
     * The actual amount received may be less (short packet).
     *
     * For OUT transfers, we send exactly the requested length.
     */
    uint16_t request_len;
    if (is_in) {
        request_len = max_pkt;
    } else {
        request_len = (length < max_pkt) ? length : max_pkt;
    }

    /*
     * Build HCTSIZ register value
     * - Transfer size (bits 18:0)
     * - Packet count (bits 28:19) - always 1 for simplicity
     * - Packet ID (bits 30:29)
     */
    uint32_t hctsiz = ((uint32_t)request_len << HCTSIZ_XFERSIZE_SHIFT)
                    | (1 << HCTSIZ_PKTCNT_SHIFT)
                    | pid;

    /* Disable split transactions (we don't support hubs) */
    mmio_write(USB_HCSPLT(ch), 0);

    /* Clear any pending interrupts on this channel */
    mmio_write(USB_HCINT(ch), 0xFFFFFFFF);

    /*
     * For OUT transfers, ensure TX FIFO has space before we start.
     * The data is pushed to the FIFO after enabling the channel.
     */
    if (!is_in && request_len > 0) {
        if (!wait_tx_fifo((request_len + 3) / 4)) {
            return TRANSFER_TIMEOUT;
        }
    }

    /* Configure the transfer size */
    mmio_write(USB_HCTSIZ(ch), hctsiz);
    dmb();

    /* Start the transfer by writing HCCHAR with CHEN=1 */
    mmio_write(USB_HCCHAR(ch), hcchar | HCCHAR_CHEN);
    dmb();

    /*
     * For OUT transfers, push the data to the FIFO now.
     * Data must be written as 32-bit words, padding the last word if needed.
     */
    if (!is_in && request_len > 0) {
        uint32_t fifo_addr = USB_FIFO(ch);
        uint32_t words = (request_len + 3) / 4;

        for (uint32_t i = 0; i < words; i++) {
            uint32_t word = 0;
            uint32_t start = i * 4;

            /* Pack bytes into a 32-bit word (little-endian) */
            for (int j = 0; j < 4; j++) {
                if (start + j < length) {
                    word |= (uint32_t)buffer[start + j] << (j * 8);
                }
            }
            mmio_write(fifo_addr, word);
        }
    }

    /*
     * Wait for transfer completion.
     * Poll the channel interrupt register for completion or error.
     */
    uint32_t timeout = 50000;  /* 50ms timeout */
    uint32_t start = micros();

    while ((micros() - start) < timeout) {
        uint32_t hcint = mmio_read(USB_HCINT(ch));

        /* Check for successful completion */
        if (hcint & HCINT_XFERCOMP) {
            /*
             * For IN transfers, read data from RX FIFO.
             * The GRXSTSP register tells us how many bytes were received.
             */
            if (is_in) {
                uint32_t grxsts = mmio_read(USB_GRXSTSP);
                uint32_t byte_count = (grxsts >> 4) & 0x7FF;

                if (byte_count > 0) {
                    uint32_t fifo_addr = USB_FIFO(ch);
                    uint32_t words = (byte_count + 3) / 4;

                    for (uint32_t i = 0; i < words; i++) {
                        uint32_t word = mmio_read(fifo_addr);
                        uint32_t base = i * 4;

                        for (int j = 0; j < 4; j++) {
                            if (base + j < byte_count && base + j < length) {
                                buffer[base + j] = (word >> (j * 8)) & 0xFF;
                            }
                        }
                    }
                }

                return TRANSFER_SUCCESS;
            }

            return TRANSFER_SUCCESS;
        }

        /* Check for NAK (device not ready) */
        if (hcint & HCINT_NAK) {
            mmio_write(USB_HCINT(ch), HCINT_NAK);
            return TRANSFER_NAK;
        }

        /* Check for STALL (endpoint error) */
        if (hcint & HCINT_STALL) {
            return TRANSFER_STALL;
        }

        /* Check for errors */
        if (hcint & HCINT_ERROR_MASK) {
            return TRANSFER_ERROR;
        }

        /* Check for channel halt (might indicate error not caught above) */
        if (hcint & HCINT_CHHLT) {
            return TRANSFER_ERROR;
        }

        delay_us(10);
    }

    return TRANSFER_TIMEOUT;
}


/* =============================================================================
 * CONTROL TRANSFER
 * =============================================================================
 *
 * Control transfers are used for configuration and status requests.
 * They have three phases:
 *   1. SETUP phase: Send 8-byte setup packet (always DATA0)
 *   2. DATA phase: Optional, direction determined by setup packet
 *   3. STATUS phase: Opposite direction of DATA, zero-length packet
 *
 * The data toggle is reset at the start of each control transfer.
 */

/*
 * usb_control_transfer() - Execute a USB control transfer
 *
 * @param host       USB host controller state
 * @param setup      Setup packet (8 bytes)
 * @param buffer     Data buffer for DATA stage (NULL for no data)
 * @param length     Buffer length (or 0 for no data)
 *
 * Returns: Number of bytes transferred in DATA stage (may be less than
 *          requested), or negative value on error
 */
int usb_control_transfer(
    usb_host_t *host,
    const usb_setup_packet_t *setup,
    uint8_t *buffer,
    uint16_t length)
{
    transfer_result_t result;
    int transferred = 0;

    /*
     * SETUP STAGE
     *
     * Send the 8-byte setup packet. This always uses:
     * - DATA0 PID (toggle is reset for SETUP)
     * - OUT direction (host to device)
     * - SETUP packet type (special PID indicating setup, not regular data)
     */
    uint8_t setup_data[8];
    setup_data[0] = setup->bm_request_type;
    setup_data[1] = setup->b_request;
    setup_data[2] = setup->w_value & 0xFF;
    setup_data[3] = (setup->w_value >> 8) & 0xFF;
    setup_data[4] = setup->w_index & 0xFF;
    setup_data[5] = (setup->w_index >> 8) & 0xFF;
    setup_data[6] = setup->w_length & 0xFF;
    setup_data[7] = (setup->w_length >> 8) & 0xFF;

    /* Try SETUP with retries for NAK */
    for (int retry = 0; retry < MAX_TRANSFER_RETRIES; retry++) {
        result = do_transfer(host, USB_CHANNEL_CONTROL, 0, false,
                            HCCHAR_EPTYPE_CTRL, HCTSIZ_PID_SETUP,
                            setup_data, 8);

        if (result == TRANSFER_SUCCESS) {
            break;
        } else if (result == TRANSFER_NAK) {
            delay_ms(1);
            continue;
        } else {
            return -1;  /* Error */
        }
    }
    if (result != TRANSFER_SUCCESS) {
        return -1;
    }

    /*
     * DATA STAGE (optional)
     *
     * If w_length > 0, there's a data stage.
     * Direction is determined by bit 7 of bm_request_type:
     *   - Set (0x80) = IN (device to host)
     *   - Clear (0x00) = OUT (host to device)
     *
     * Data toggle starts at DATA1 after SETUP.
     */
    if (setup->w_length > 0 && buffer != NULL) {
        bool is_in = (setup->bm_request_type & USB_REQTYPE_DIR_IN) != 0;
        uint32_t data_toggle = HCTSIZ_PID_DATA1;
        uint16_t remaining = (length < setup->w_length) ? length : setup->w_length;
        uint16_t offset = 0;

        while (offset < remaining) {
            uint16_t chunk = remaining - offset;
            if (chunk > host->ep0_max_packet) {
                chunk = host->ep0_max_packet;
            }

            for (int retry = 0; retry < MAX_TRANSFER_RETRIES; retry++) {
                result = do_transfer(host, USB_CHANNEL_CONTROL, 0, is_in,
                                    HCCHAR_EPTYPE_CTRL, data_toggle,
                                    buffer + offset, chunk);

                if (result == TRANSFER_SUCCESS) {
                    offset += chunk;
                    transferred = offset;

                    /* Toggle DATA0/DATA1 for next packet */
                    data_toggle = (data_toggle == HCTSIZ_PID_DATA1)
                                ? HCTSIZ_PID_DATA0 : HCTSIZ_PID_DATA1;

                    /*
                     * If we received a short packet (less than max packet),
                     * that signals end of data stage.
                     */
                    if (is_in && chunk < host->ep0_max_packet) {
                        remaining = offset;  /* Exit the loop */
                    }
                    break;

                } else if (result == TRANSFER_NAK) {
                    delay_ms(1);
                    continue;
                } else {
                    return -1;  /* Error */
                }
            }

            if (result != TRANSFER_SUCCESS) {
                return -1;
            }
        }
    }

    /*
     * STATUS STAGE
     *
     * A zero-length packet in the OPPOSITE direction of the data stage.
     * - If DATA was IN (or no data), STATUS is OUT
     * - If DATA was OUT, STATUS is IN
     *
     * STATUS always uses DATA1 PID.
     */
    bool status_in;
    if (setup->w_length == 0) {
        /* No data stage: STATUS is IN (device confirms our request) */
        status_in = true;
    } else {
        /* Has data stage: STATUS is opposite direction */
        status_in = (setup->bm_request_type & USB_REQTYPE_DIR_IN) == 0;
    }

    uint8_t status_buf[8];  /* We don't expect any data, but need a buffer */

    for (int retry = 0; retry < MAX_TRANSFER_RETRIES; retry++) {
        result = do_transfer(host, USB_CHANNEL_CONTROL, 0, status_in,
                            HCCHAR_EPTYPE_CTRL, HCTSIZ_PID_DATA1,
                            status_buf, 0);

        if (result == TRANSFER_SUCCESS) {
            return transferred;
        } else if (result == TRANSFER_NAK) {
            delay_ms(1);
            continue;
        } else {
            return -1;  /* Error */
        }
    }

    return -1;  /* Timeout on STATUS */
}


/* =============================================================================
 * SETUP PACKET BUILDERS
 * =============================================================================
 *
 * Helper functions to create common setup packets.
 */

/*
 * Create a GET_DESCRIPTOR request
 *
 * @param desc_type   Descriptor type (USB_DESC_DEVICE, etc.)
 * @param desc_index  Descriptor index (usually 0)
 * @param length      Number of bytes to request
 */
static usb_setup_packet_t make_get_descriptor(
    uint8_t desc_type, uint8_t desc_index, uint16_t length)
{
    usb_setup_packet_t setup = {
        .bm_request_type = USB_REQTYPE_DIR_IN | USB_REQTYPE_TYPE_STANDARD |
                          USB_REQTYPE_RECIP_DEVICE,
        .b_request = USB_REQ_GET_DESCRIPTOR,
        .w_value = ((uint16_t)desc_type << 8) | desc_index,
        .w_index = 0,
        .w_length = length
    };
    return setup;
}

/*
 * Create a SET_ADDRESS request
 *
 * @param addr  New device address (1-127)
 */
static usb_setup_packet_t make_set_address(uint8_t addr)
{
    usb_setup_packet_t setup = {
        .bm_request_type = USB_REQTYPE_DIR_OUT | USB_REQTYPE_TYPE_STANDARD |
                          USB_REQTYPE_RECIP_DEVICE,
        .b_request = USB_REQ_SET_ADDRESS,
        .w_value = addr,
        .w_index = 0,
        .w_length = 0
    };
    return setup;
}

/*
 * Create a SET_CONFIGURATION request
 *
 * @param config  Configuration value (from configuration descriptor)
 */
static usb_setup_packet_t make_set_configuration(uint8_t config)
{
    usb_setup_packet_t setup = {
        .bm_request_type = USB_REQTYPE_DIR_OUT | USB_REQTYPE_TYPE_STANDARD |
                          USB_REQTYPE_RECIP_DEVICE,
        .b_request = USB_REQ_SET_CONFIGURATION,
        .w_value = config,
        .w_index = 0,
        .w_length = 0
    };
    return setup;
}


/* =============================================================================
 * DESCRIPTOR PARSING
 * =============================================================================
 *
 * USB descriptors describe the device's capabilities. The configuration
 * descriptor is especially important - it contains embedded interface
 * and endpoint descriptors that tell us how to communicate with the device.
 */

/*
 * parse_config_descriptor() - Find HID interrupt IN endpoint
 *
 * Walks through the configuration descriptor looking for an interrupt IN
 * endpoint, which is what we need for HID input reports.
 *
 * @param host  USB host controller state
 * @param data  Configuration descriptor data
 * @param len   Data length
 *
 * Returns: true if endpoint found, false otherwise
 */
static bool parse_config_descriptor(usb_host_t *host, const uint8_t *data, uint16_t len)
{
    uint16_t pos = 0;

    while (pos + 2 <= len) {
        uint8_t desc_len = data[pos];
        uint8_t desc_type = data[pos + 1];

        if (desc_len == 0 || pos + desc_len > len) {
            break;  /* Invalid descriptor */
        }

        /*
         * Endpoint Descriptor (type 0x05)
         * Format:
         *   [0] bLength = 7
         *   [1] bDescriptorType = 5
         *   [2] bEndpointAddress (bit 7 = direction, bits 3:0 = number)
         *   [3] bmAttributes (bits 1:0 = transfer type)
         *   [4-5] wMaxPacketSize (little-endian)
         *   [6] bInterval (polling interval)
         */
        if (desc_type == USB_DESC_ENDPOINT && desc_len >= 7) {
            uint8_t ep_addr = data[pos + 2];
            uint8_t ep_attr = data[pos + 3];
            uint16_t ep_max_pkt = data[pos + 4] | ((uint16_t)data[pos + 5] << 8);

            bool is_in = (ep_addr & 0x80) != 0;
            uint8_t ep_type = ep_attr & 0x03;  /* 0=ctrl, 1=isoc, 2=bulk, 3=intr */

            /*
             * We're looking for an INTERRUPT IN endpoint.
             * This is where HID devices send their input reports.
             */
            if (is_in && ep_type == 3) {
                host->hid_endpoint = ep_addr & 0x0F;
                host->hid_max_packet = ep_max_pkt;
                return true;
            }
        }

        pos += desc_len;
    }

    return false;  /* No suitable endpoint found */
}


/* =============================================================================
 * PUBLIC API IMPLEMENTATION
 * =============================================================================
 */

/*
 * usb_host_create() - Create USB host controller instance
 */
usb_host_t usb_host_create(void)
{
    usb_host_t host = {
        .device_address = 0,
        .ep0_max_packet = 8,     /* Start with 8, device descriptor tells us real value */
        .hid_endpoint = 0,
        .hid_max_packet = 0,
        .hid_data_toggle = false,
        .enumerated = false,
        .port_speed = 1          /* Assume full-speed until reset tells us otherwise */
    };
    return host;
}

/*
 * usb_host_init() - Initialize USB host controller hardware
 */
bool usb_host_init(usb_host_t *host)
{
    (void)host;  /* Not used yet in init */

    /*
     * Step 1: Request USB power from the VideoCore
     *
     * The GPU controls power to peripherals. USB won't respond until
     * we explicitly request power.
     */
    usb_power_on();
    delay_ms(50);

    /*
     * Step 2: Verify DWC2 core is present
     *
     * Read the Synopsys ID register. The upper bits should contain
     * 0x4F542 (ASCII "OT2" for OTG 2.0).
     */
    uint32_t snpsid = mmio_read(USB_GSNPSID);
    if ((snpsid & 0xFFFFF000) != 0x4F542000) {
        return false;  /* DWC2 not found */
    }

    /*
     * Step 3: Disable interrupts and DMA during configuration
     *
     * We'll enable what we need after setup is complete.
     */
    mmio_write(USB_GINTMSK, 0);
    mmio_write(USB_GAHBCFG, 0);

    /*
     * Step 4: Wait for AHB master idle
     *
     * Before resetting, ensure no bus transactions are in progress.
     */
    for (int i = 0; i < 100000; i++) {
        if (mmio_read(USB_GRSTCTL) & GRSTCTL_AHB_IDLE) {
            break;
        }
        delay_us(1);
    }

    /*
     * Step 5: Perform core soft reset
     *
     * This returns all registers to default values. The CSRST bit
     * clears automatically when reset is complete.
     */
    mmio_write(USB_GRSTCTL, GRSTCTL_CSRST);
    for (int i = 0; i < 100000; i++) {
        if ((mmio_read(USB_GRSTCTL) & GRSTCTL_CSRST) == 0) {
            break;
        }
        delay_us(1);
    }
    delay_ms(100);

    /*
     * Step 6: Disable power gating
     *
     * Keep the controller fully powered during operation.
     */
    mmio_write(USB_PCGCCTL, 0);
    delay_ms(10);

    /*
     * Step 7: Force host mode
     *
     * The DWC2 supports OTG (automatic host/device negotiation), but
     * we always want to be a host, so we force it.
     */
    uint32_t gusbcfg = mmio_read(USB_GUSBCFG);
    gusbcfg &= ~GUSBCFG_FORCE_DEV;   /* Clear device mode force */
    gusbcfg |= GUSBCFG_FORCE_HOST;   /* Set host mode force */
    gusbcfg |= GUSBCFG_PHYSEL;       /* Select full-speed PHY */
    mmio_write(USB_GUSBCFG, gusbcfg);
    delay_ms(50);

    /*
     * Step 8: Wait for host mode confirmation
     *
     * The CURMOD bit in GINTSTS indicates current mode.
     */
    for (int i = 0; i < 100000; i++) {
        if (mmio_read(USB_GINTSTS) & GINTSTS_CURMOD) {
            break;  /* Host mode confirmed */
        }
        delay_us(1);
    }

    /*
     * Step 9: Configure FIFOs
     *
     * The DWC2 has internal FIFO RAM that must be divided between:
     * - RX FIFO (receives all incoming data)
     * - Non-periodic TX FIFO (control/bulk OUT)
     * - Periodic TX FIFO (interrupt/isochronous OUT)
     *
     * Values are in 32-bit words. Our allocation:
     * - RX: 512 words (2KB) at address 0
     * - Non-periodic TX: 256 words (1KB) starting at word 512
     * - Periodic TX: 256 words (1KB) starting at word 768
     */
    mmio_write(USB_GRXFSIZ, 512);
    mmio_write(USB_GNPTXFSIZ, (256 << 16) | 512);   /* Size << 16 | Start addr */
    mmio_write(USB_HPTXFSIZ, (256 << 16) | 768);

    /*
     * Step 10: Flush all FIFOs
     *
     * Clear any stale data after reset.
     */
    mmio_write(USB_GRSTCTL, GRSTCTL_TXFFLSH | GRSTCTL_TXFNUM_ALL);
    for (int i = 0; i < 10000; i++) {
        if ((mmio_read(USB_GRSTCTL) & GRSTCTL_TXFFLSH) == 0) {
            break;
        }
        delay_us(1);
    }

    mmio_write(USB_GRSTCTL, GRSTCTL_RXFFLSH);
    for (int i = 0; i < 10000; i++) {
        if ((mmio_read(USB_GRSTCTL) & GRSTCTL_RXFFLSH) == 0) {
            break;
        }
        delay_us(1);
    }

    /*
     * Step 11: Configure host mode
     *
     * HCFG: Set PHY clock speed. Value 1 = 48MHz (full-speed).
     * HFIR: Frame interval in PHY clocks. 48000 = 1ms at 48MHz.
     */
    mmio_write(USB_HCFG, 1);
    mmio_write(USB_HFIR, 48000);

    /*
     * Step 12: Initialize host channels
     *
     * Disable all 8 channels and enable interrupts for them.
     */
    for (int ch = 0; ch < 8; ch++) {
        disable_channel(ch);

        /* Enable important interrupts for this channel */
        mmio_write(USB_HCINTMSK(ch),
                  HCINT_XFERCOMP | HCINT_CHHLT | HCINT_STALL |
                  HCINT_NAK | HCINT_ACK | HCINT_XACTERR |
                  HCINT_BBLERR | HCINT_DATATGLERR);
    }

    /*
     * Step 13: Enable global interrupts
     *
     * Configure interrupt mask and enable global interrupt.
     */
    mmio_write(USB_HAINTMSK, 0xFF);  /* All 8 channels */
    mmio_write(USB_GINTSTS, 0xFFFFFFFF);  /* Clear pending interrupts */
    mmio_write(USB_GINTMSK,
              GINTSTS_SOF | GINTSTS_RXFLVL | GINTSTS_HPRTINT | GINTSTS_HCINT);
    mmio_write(USB_GAHBCFG, GAHBCFG_GLBL_INTR_EN);

    /*
     * Step 14: Power on the USB port
     *
     * Set the HPRT_PWR bit to supply power to connected devices.
     * Wait for power to stabilize.
     */
    uint32_t hprt = mmio_read(USB_HPRT);
    hprt &= ~HPRT_W1C_MASK;  /* Don't accidentally clear status bits */
    hprt |= HPRT_PWR;
    mmio_write(USB_HPRT, hprt);
    delay_ms(100);

    return true;
}

/*
 * usb_host_wait_connection() - Wait for device connection
 */
bool usb_host_wait_connection(usb_host_t *host, uint32_t timeout_ms)
{
    (void)host;

    uint32_t start = micros();

    while ((micros() - start) < (timeout_ms * 1000)) {
        if (mmio_read(USB_HPRT) & HPRT_CONN_STS) {
            return true;  /* Device connected */
        }
        delay_ms(10);
    }

    return false;  /* Timeout */
}

/*
 * usb_host_reset_port() - Reset the USB port
 */
bool usb_host_reset_port(usb_host_t *host)
{
    uint32_t hprt;

    /* Verify device is still connected */
    hprt = mmio_read(USB_HPRT);
    if ((hprt & HPRT_CONN_STS) == 0) {
        return false;  /* No device */
    }

    /*
     * Clear any pending status bits.
     * Remember: Writing 1 to W1C bits clears them!
     */
    hprt &= ~HPRT_ENA;  /* Don't accidentally disable port */
    hprt |= HPRT_CONN_DET | HPRT_ENA_CHNG | HPRT_OVRCUR_CHNG;
    mmio_write(USB_HPRT, hprt);
    delay_ms(10);

    /*
     * Start USB reset
     *
     * Hold HPRT_RST high for at least 10ms (USB spec requires minimum 10ms).
     * We use 60ms for reliability with slow devices.
     */
    hprt = mmio_read(USB_HPRT);
    hprt &= ~HPRT_W1C_MASK;  /* Preserve status bits */
    hprt |= HPRT_RST;
    mmio_write(USB_HPRT, hprt);
    delay_ms(60);

    /* End reset by clearing HPRT_RST */
    hprt = mmio_read(USB_HPRT);
    hprt &= ~HPRT_W1C_MASK;
    hprt &= ~HPRT_RST;
    mmio_write(USB_HPRT, hprt);
    delay_ms(20);

    /*
     * Wait for port to be enabled
     *
     * After reset, the hardware determines device speed and enables
     * the port if enumeration is possible.
     */
    for (int i = 0; i < 50; i++) {
        hprt = mmio_read(USB_HPRT);

        /* Clear enable change if set */
        if (hprt & HPRT_ENA_CHNG) {
            uint32_t clear = hprt & ~HPRT_ENA;  /* Don't clear enable */
            clear |= HPRT_ENA_CHNG;
            mmio_write(USB_HPRT, clear);
        }

        /* Check if port is enabled */
        if (hprt & HPRT_ENA) {
            /*
             * Read port speed:
             * 0 = High-speed (480 Mbps)
             * 1 = Full-speed (12 Mbps)
             * 2 = Low-speed (1.5 Mbps)
             */
            host->port_speed = (hprt & HPRT_SPD_MASK) >> HPRT_SPD_SHIFT;

            /* Reset device state for new enumeration */
            host->device_address = 0;
            host->ep0_max_packet = 8;
            host->enumerated = false;

            return true;
        }

        delay_ms(10);
    }

    return false;  /* Port enable timeout */
}

/*
 * usb_host_enumerate() - Enumerate the connected device
 */
bool usb_host_enumerate(usb_host_t *host)
{
    usb_setup_packet_t setup;
    uint8_t desc_buf[18];
    uint8_t config_buf[64];
    int result;

    /*
     * Step 1: Get first 8 bytes of device descriptor
     *
     * The device descriptor is 18 bytes, but we first request only 8
     * to learn the endpoint 0 max packet size (byte 7). Some devices
     * can't handle a request for more bytes than their max packet size.
     */
    setup = make_get_descriptor(USB_DESC_DEVICE, 0, 8);
    result = usb_control_transfer(host, &setup, desc_buf, 8);
    if (result < 0) {
        return false;
    }

    /*
     * Update max packet size from device descriptor
     *
     * Byte 7 (bMaxPacketSize0) contains the max packet size for endpoint 0.
     * Valid values are 8, 16, 32, or 64 for full-speed devices.
     */
    uint16_t max_pkt = desc_buf[7];
    if (max_pkt == 0 || max_pkt > 64) {
        max_pkt = 8;  /* Use safe default if invalid */
    }
    host->ep0_max_packet = max_pkt;

    /*
     * Step 2: Reset port again
     *
     * Some devices require a reset after reading the initial descriptor.
     * This ensures we're in a known state before assigning an address.
     */
    if (!usb_host_reset_port(host)) {
        return false;
    }
    delay_ms(20);

    /*
     * Step 3: Set device address
     *
     * The device starts at address 0. We assign it address 1.
     * After this, all communication uses the new address.
     */
    setup = make_set_address(1);
    result = usb_control_transfer(host, &setup, NULL, 0);
    if (result < 0) {
        return false;
    }
    host->device_address = 1;
    delay_ms(10);

    /*
     * Step 4: Get full device descriptor (18 bytes)
     *
     * Now we can request the full descriptor. This contains:
     * - USB version, device class/subclass/protocol
     * - Vendor ID, Product ID
     * - Manufacturer, product, serial number string indices
     * - Number of configurations
     */
    setup = make_get_descriptor(USB_DESC_DEVICE, 0, 18);
    result = usb_control_transfer(host, &setup, desc_buf, 18);
    if (result < 0) {
        return false;
    }

    /*
     * Step 5: Get configuration descriptor
     *
     * The configuration descriptor contains embedded interface and
     * endpoint descriptors. We request up to 64 bytes, which is
     * usually enough for simple devices.
     */
    setup = make_get_descriptor(USB_DESC_CONFIGURATION, 0, 64);
    result = usb_control_transfer(host, &setup, config_buf, 64);
    if (result < 0) {
        return false;
    }

    /*
     * Step 6: Parse configuration to find HID endpoint
     *
     * We need to find an interrupt IN endpoint for receiving HID
     * input reports.
     */
    if (!parse_config_descriptor(host, config_buf, result)) {
        return false;  /* No suitable endpoint */
    }

    /*
     * Step 7: Set configuration
     *
     * Activate the device's configuration. The configuration value
     * is in byte 5 of the configuration descriptor (bConfigurationValue).
     */
    uint8_t config_val = (result >= 6) ? config_buf[5] : 1;
    setup = make_set_configuration(config_val);
    result = usb_control_transfer(host, &setup, NULL, 0);
    if (result < 0) {
        return false;
    }

    host->enumerated = true;
    return true;
}

/*
 * usb_host_is_enumerated() - Check if device is enumerated
 */
bool usb_host_is_enumerated(usb_host_t *host)
{
    return host->enumerated;
}

/*
 * usb_host_read_input() - Read HID input report
 */
transfer_result_t usb_host_read_input(usb_host_t *host, xbox360_input_report_t *report)
{
    if (!host->enumerated || host->hid_endpoint == 0) {
        return TRANSFER_ERROR;
    }

    /*
     * Determine PID based on data toggle state
     *
     * Interrupt endpoints maintain data toggle across transfers.
     * We flip the toggle after each successful transfer.
     */
    uint32_t pid = host->hid_data_toggle ? HCTSIZ_PID_DATA1 : HCTSIZ_PID_DATA0;

    /*
     * Calculate transfer length
     *
     * Request up to max packet size, or the size of our report structure,
     * whichever is smaller.
     */
    uint16_t len = sizeof(xbox360_input_report_t);
    if (len > host->hid_max_packet) {
        len = host->hid_max_packet;
    }

    /*
     * Perform interrupt IN transfer
     */
    transfer_result_t result = do_transfer(
        host,
        USB_CHANNEL_INTERRUPT,
        host->hid_endpoint,
        true,  /* IN transfer */
        HCCHAR_EPTYPE_INTR,
        pid,
        (uint8_t *)report,
        len
    );

    /*
     * On success, toggle the data PID for next transfer
     */
    if (result == TRANSFER_SUCCESS) {
        host->hid_data_toggle = !host->hid_data_toggle;
    }

    return result;
}
