/*
 * sdhost.c - SD Card Driver Implementation
 * =========================================
 *
 * This file implements the SD card driver using the SDHOST controller.
 * See sdhost.h for detailed interface documentation.
 *
 * SD CARD INITIALIZATION SEQUENCE:
 * --------------------------------
 * The SD specification requires a specific sequence to initialize cards:
 *
 *   1. Power on and wait 1ms
 *   2. Send at least 74 clock cycles with CS high
 *   3. CMD0 (GO_IDLE_STATE) - Enter SPI/idle mode
 *   4. CMD8 (SEND_IF_COND) - Check voltage, detect SD v2
 *   5. ACMD41 loop - Wait for card to be ready, detect SDHC
 *   6. CMD2 (ALL_SEND_CID) - Get card identification
 *   7. CMD3 (SEND_RELATIVE_ADDR) - Get relative card address
 *   8. CMD7 (SELECT_CARD) - Select the card for data transfer
 *   9. Optionally: Set bus width, speed mode
 *
 * RESPONSE TYPES:
 * ---------------
 *   R1:  48 bits - Standard response with card status
 *   R1b: 48 bits + busy - Same as R1 but card may be busy
 *   R2:  136 bits - CID or CSD register contents
 *   R3:  48 bits - OCR register (ACMD41)
 *   R6:  48 bits - RCA response (CMD3)
 *   R7:  48 bits - Card interface condition (CMD8)
 *
 * SDHC vs SD ADDRESSING:
 * ----------------------
 * Regular SD cards (up to 2GB) use BYTE addressing:
 *   Address for sector 100 = 100 * 512 = 51200
 *
 * SDHC/SDXC cards use BLOCK addressing:
 *   Address for sector 100 = 100
 *
 * We detect card type during initialization and adjust addressing accordingly.
 */

#include "sdhost.h"
#include "gpio.h"
#include "mailbox.h"

/* =============================================================================
 * INTERNAL FUNCTIONS
 * =============================================================================
 */

/*
 * clear_status() - Clear error flags in status register
 *
 * The HSTS register has bits that are "write 1 to clear".
 * We clear all error bits to prepare for the next command.
 */
static void clear_status(void)
{
    mmio_write(SDHOST_HSTS, SDHOST_HSTS_ERROR_MASK);
}

/*
 * reset_controller() - Reset the SDHOST controller
 *
 * Initializes all registers to known values and powers on the card.
 * Uses slow clock (~400kHz) for identification phase.
 */
static void reset_controller(void)
{
    /* Clear all registers to safe values */
    mmio_write(SDHOST_CMD, 0);
    mmio_write(SDHOST_ARG, 0);
    mmio_write(SDHOST_TOUT, 0xF00000);      /* Long timeout */
    mmio_write(SDHOST_CDIV, 0);              /* Will set clock later */
    mmio_write(SDHOST_HSTS, SDHOST_HSTS_ERROR_MASK);  /* Clear errors */
    mmio_write(SDHOST_HCFG, 0);
    mmio_write(SDHOST_HBCT, 0);
    mmio_write(SDHOST_HBLC, 0);

    /*
     * Power on VDD to the card
     * 
     * The VDD register controls power to the SD card slot.
     * Set to 1 to power on.
     */
    mmio_write(SDHOST_VDD, 1);
    delay_ms(10);

    /*
     * Configure for slow card during identification
     *
     * SLOW_CARD: Use slower timing for initial communication
     * INTBUS: Internal bus width setting
     */
    mmio_write(SDHOST_HCFG, SDHOST_HCFG_SLOW_CARD | SDHOST_HCFG_INTBUS);

    /*
     * Set clock divider for ~400kHz identification mode
     *
     * SD cards must be initialized at a slow clock (100-400kHz).
     * The base clock is typically around 250MHz, so:
     * 250MHz / (0x148 + 2) = ~750kHz (close enough)
     *
     * The exact formula depends on the SoC's base clock.
     */
    mmio_write(SDHOST_CDIV, 0x148);
    delay_ms(10);
}

/*
 * wait_cmd() - Wait for command to complete
 *
 * Polls the CMD register until the NEW flag clears (command complete)
 * or FAIL flag sets (error occurred).
 *
 * Returns: true on success, false on error/timeout
 */
static bool wait_cmd(void)
{
    for (int i = 0; i < 50000; i++) {
        uint32_t cmd = mmio_read(SDHOST_CMD);

        /* NEW flag clears when command is complete */
        if ((cmd & SDHOST_CMD_NEW) == 0) {
            uint32_t hsts = mmio_read(SDHOST_HSTS);

            /* Check for timeout (bit 6) */
            if (hsts & 0x40) {
                clear_status();
                return false;
            }

            /* Check for CRC error (bit 4) */
            if (hsts & 0x10) {
                clear_status();
                return false;
            }

            return true;  /* Success */
        }

        /* FAIL flag indicates hard error */
        if (cmd & SDHOST_CMD_FAIL) {
            clear_status();
            return false;
        }
    }

    return false;  /* Timeout waiting for completion */
}

/*
 * send_cmd() - Send a command and get response
 *
 * @param cmd_idx  Command index (0-63)
 * @param arg      32-bit command argument
 * @param flags    Additional flags (BUSY, LONG_RSP, etc.)
 * @param response Pointer to receive response (or NULL)
 *
 * Returns: true on success, false on failure
 */
static bool send_cmd(uint32_t cmd_idx, uint32_t arg, uint32_t flags, uint32_t *response)
{
    clear_status();

    /* Write argument first (must be before command) */
    mmio_write(SDHOST_ARG, arg);

    /* Write command with flags */
    mmio_write(SDHOST_CMD, (cmd_idx & 0x3F) | flags | SDHOST_CMD_NEW);

    /* Wait for completion */
    if (!wait_cmd()) {
        return false;
    }

    /* Read response if requested */
    if (response) {
        *response = mmio_read(SDHOST_RSP0);
    }

    return true;
}


/* =============================================================================
 * PUBLIC API IMPLEMENTATION
 * =============================================================================
 */

/*
 * sdcard_create() - Create SD card driver instance
 */
sdcard_t sdcard_create(void)
{
    sdcard_t card = {
        .initialized = false,
        .is_sdhc = true,   /* Assume SDHC until proven otherwise */
        .rca = 0
    };
    return card;
}

/*
 * sdcard_init() - Initialize the SD card
 *
 * This implements the full SD card initialization sequence per the
 * SD specification.
 */
bool sdcard_init(sdcard_t *card)
{
    uint32_t response;

    /*
     * Step 1: Configure GPIO pins for SDHOST
     *
     * The SDHOST controller uses GPIO 48-53 in ALT0 mode.
     * This must be done before any SD communication.
     */
    gpio_configure_for_sd();

    /*
     * Step 2: Power on SD card via mailbox
     *
     * The VideoCore GPU controls power to peripherals.
     * We must request power before the SD card will respond.
     */
    mailbox_set_power_state(DEVICE_SD_CARD, true);

    /*
     * Step 3: Reset the SDHOST controller
     *
     * Initialize all registers and set up slow clock for identification.
     */
    reset_controller();

    /*
     * Step 4: CMD0 - GO_IDLE_STATE
     *
     * This command resets the card to idle state. It has no response.
     * All cards must respond to this command.
     */
    mmio_write(SDHOST_ARG, 0);
    mmio_write(SDHOST_CMD, SD_CMD_GO_IDLE_STATE | SDHOST_CMD_NO_RSP | SDHOST_CMD_NEW);
    delay_ms(50);
    clear_status();

    /*
     * Step 5: CMD8 - SEND_IF_COND
     *
     * This command detects SD v2.0+ cards and verifies voltage compatibility.
     * Argument: 0x1AA = check pattern for 2.7-3.6V operation
     *
     * Response:
     *   - If card responds with 0x1AA in low byte, it's SD v2+
     *   - If no response (timeout), it's SD v1 or MMC
     *   - If different response, voltage mismatch
     */
    if (send_cmd(SD_CMD_SEND_IF_COND, 0x1AA, 0, &response)) {
        /* Card responded - check the check pattern */
        card->is_sdhc = ((response & 0xFF) == 0xAA);
    } else {
        /*
         * No response - this is an older SD v1 card or MMC.
         * SD v1 cards are never SDHC.
         */
        card->is_sdhc = false;
        clear_status();  /* CMD8 timeout is expected for SD v1 */
    }

    /*
     * Step 6: ACMD41 loop - SD_SEND_OP_COND
     *
     * This is the main initialization command. We repeat it until:
     *   - Bit 31 (busy) of OCR is set (card is ready)
     *   - Or timeout (card is dead/incompatible)
     *
     * ACMD41 is an "application command", so it must be preceded by CMD55.
     *
     * Argument:
     *   - Bit 30 (HCS): Set if host supports SDHC. Card returns this bit
     *     in OCR to indicate if it IS SDHC.
     *   - Bits 23:0: Voltage window (0xFF8000 = 2.7-3.6V)
     */
    for (int retry = 0; retry < 50; retry++) {
        /* CMD55 - APP_CMD (tells card next command is application-specific) */
        if (!send_cmd(SD_CMD_APP_CMD, 0, 0, NULL)) {
            /* CMD55 failed - try again */
            delay_ms(10);
            continue;
        }

        /* ACMD41 - SD_SEND_OP_COND */
        uint32_t hcs = card->is_sdhc ? 0x40000000 : 0;  /* High Capacity Support */
        if (send_cmd(SD_ACMD_SD_SEND_OP_COND, 0x00FF8000 | hcs, 0, &response)) {
            /*
             * Check if card is ready (busy bit set = ready, inverted logic!)
             * Also check HCS bit to confirm SDHC status.
             */
            if (response & 0x80000000) {
                /* Card is ready! Check if it's actually SDHC */
                card->is_sdhc = (response & 0x40000000) != 0;
                break;
            }
        }

        delay_ms(10);
    }

    /*
     * Step 7: CMD2 - ALL_SEND_CID
     *
     * Gets the Card Identification (CID) register.
     * This is a 136-bit response containing manufacturer info,
     * serial number, etc. We don't use it but it's required.
     */
    if (!send_cmd(SD_CMD_ALL_SEND_CID, 0, SDHOST_CMD_LONG_RSP, NULL)) {
        return false;
    }

    /*
     * Step 8: CMD3 - SEND_RELATIVE_ADDR
     *
     * The card assigns itself a relative address (RCA).
     * We use this address for all subsequent communication.
     * The RCA is in the upper 16 bits of the response.
     */
    if (!send_cmd(SD_CMD_SEND_RELATIVE_ADDR, 0, 0, &response)) {
        return false;
    }
    card->rca = response & 0xFFFF0000;

    /*
     * Step 9: CMD7 - SELECT_CARD
     *
     * Selects this card for data transfer. Only one card can be
     * selected at a time on a bus (though we only have one).
     * Uses BUSY flag because card may take time to transition.
     */
    if (!send_cmd(SD_CMD_SELECT_CARD, card->rca, SDHOST_CMD_BUSY, NULL)) {
        return false;
    }

    /*
     * Step 10: Switch to high-speed clock
     *
     * Now that initialization is complete, we can use a faster clock.
     * CDIV=4 gives roughly 25MHz, which is close to the SD card's
     * "default speed" of 25MHz.
     */
    mmio_write(SDHOST_CDIV, 4);

    /*
     * Step 11: Set block size
     *
     * Configure the controller for 512-byte blocks.
     */
    mmio_write(SDHOST_HBCT, SECTOR_SIZE);

    card->initialized = true;
    return true;
}

/*
 * sdcard_is_initialized() - Check if card is initialized
 */
bool sdcard_is_initialized(const sdcard_t *card)
{
    return card->initialized;
}

/*
 * sdcard_is_sdhc() - Check if card is SDHC/SDXC
 */
bool sdcard_is_sdhc(const sdcard_t *card)
{
    return card->is_sdhc;
}

/*
 * sdcard_read_sector() - Read a single sector
 *
 * This performs a single-block read operation using CMD17.
 */
bool sdcard_read_sector(sdcard_t *card, uint32_t lba, uint8_t buffer[SECTOR_SIZE])
{
    if (!card->initialized) {
        return false;
    }

    /* Set up for single block transfer */
    mmio_write(SDHOST_HBCT, SECTOR_SIZE);
    mmio_write(SDHOST_HBLC, 1);

    /*
     * Calculate address based on card type
     *
     * SDHC: Block address (sector number)
     * SD: Byte address (sector number x 512)
     */
    uint32_t addr = card->is_sdhc ? lba : (lba * SECTOR_SIZE);

    /*
     * Send CMD17 - READ_SINGLE_BLOCK
     *
     * This initiates a read of one 512-byte block.
     * The READ flag tells the controller to expect data.
     */
    clear_status();
    mmio_write(SDHOST_ARG, addr);
    mmio_write(SDHOST_CMD, SD_CMD_READ_SINGLE_BLOCK | SDHOST_CMD_READ | SDHOST_CMD_NEW);

    if (!wait_cmd()) {
        return false;
    }

    /*
     * Read data from FIFO
     *
     * The DATA register provides 32 bits at a time. We poll the
     * status register until data is available, then read it.
     */
    size_t idx = 0;

    for (int timeout = 0; timeout < 500000; timeout++) {
        if (idx >= SECTOR_SIZE) {
            break;  /* All data received */
        }

        /* Check if data is available */
        uint32_t hsts = mmio_read(SDHOST_HSTS);
        if (hsts & SDHOST_HSTS_DATA_FLAG) {
            /*
             * Read 32-bit word and split into bytes
             * SD cards use little-endian byte order
             */
            uint32_t word = mmio_read(SDHOST_DATA);
            buffer[idx + 0] = (word >> 0) & 0xFF;
            buffer[idx + 1] = (word >> 8) & 0xFF;
            buffer[idx + 2] = (word >> 16) & 0xFF;
            buffer[idx + 3] = (word >> 24) & 0xFF;
            idx += 4;
        }
    }

    clear_status();

    /* Verify we got all the data */
    if (idx < SECTOR_SIZE) {
        return false;  /* Data timeout */
    }

    return true;
}

/*
 * sdcard_read_sectors() - Read multiple sectors
 *
 * This is a convenience wrapper that reads multiple sectors by
 * calling sdcard_read_sector() repeatedly.
 *
 * For better performance, a proper implementation would use
 * CMD18 (READ_MULTIPLE_BLOCK) with DMA.
 */
size_t sdcard_read_sectors(sdcard_t *card, uint32_t start_lba,
                           uint8_t *buffer, size_t buffer_size)
{
    size_t num_sectors = buffer_size / SECTOR_SIZE;
    uint8_t sector_buf[SECTOR_SIZE];

    for (size_t i = 0; i < num_sectors; i++) {
        if (!sdcard_read_sector(card, start_lba + i, sector_buf)) {
            return i * SECTOR_SIZE;  /* Return bytes read so far */
        }

        /* Copy to output buffer */
        for (size_t j = 0; j < SECTOR_SIZE; j++) {
            buffer[i * SECTOR_SIZE + j] = sector_buf[j];
        }
    }

    return num_sectors * SECTOR_SIZE;
}
