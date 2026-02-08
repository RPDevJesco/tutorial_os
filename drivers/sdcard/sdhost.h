/*
 * sdhost.h - SD Card Driver via SDHOST Controller
 * =================================================
 *
 * This driver provides low-level SD card access using the BCM283x SDHOST
 * controller. The Pi has TWO SD card interfaces:
 *   - EMMC (Arasan SDHCI): More complex, used for WiFi on some models
 *   - SDHOST (External Mass Media Controller): Simpler, we use this one
 *
 * WHY SDHOST?
 * -----------
 * The SDHOST controller is simpler to program and more reliable for basic
 * SD card operations. It doesn't have DMA, which makes it slower for large
 * transfers but easier to understand and debug.
 *
 * SD CARD BASICS:
 * ---------------
 * SD cards communicate using a command/response protocol:
 *   1. Host sends a command (CMD0, CMD17, etc.)
 *   2. Card processes command
 *   3. Card sends response (R1, R2, R3, R7, etc.)
 *   4. For reads: card sends data block(s)
 *   5. For writes: host sends data block(s), card sends status
 *
 * SD cards come in different capacities:
 *   - SD (up to 2GB): Byte addressing
 *   - SDHC (2GB-32GB): Block addressing (512-byte blocks)
 *   - SDXC (32GB-2TB): Block addressing, exFAT filesystem
 *
 * The initialization sequence detects card type and sets up communication.
 *
 * GPIO REQUIREMENTS:
 * ------------------
 * SDHOST uses GPIO 48-53 in ALT0 mode:
 *   - GPIO 48: CLK (clock output)
 *   - GPIO 49: CMD (command/response)
 *   - GPIO 50-53: DAT0-DAT3 (data lines)
 *
 * These pins must be configured BEFORE using the SD card.
 *
 * FEATURES:
 * ---------
 *   - SD/SDHC/SDXC card support
 *   - Single block reads (512 bytes)
 *   - Automatic card type detection
 *
 * LIMITATIONS:
 * ------------
 *   - Read-only (no write support)
 *   - Single block transfers only
 *   - No DMA
 *   - No multi-block commands
 */

#ifndef SDHOST_H
#define SDHOST_H

#include "types.h"  /* Bare-metal type definitions */

/* =============================================================================
 * SDHOST REGISTER ADDRESSES
 * =============================================================================
 *
 * The SDHOST controller is at offset 0x202000 from the peripheral base.
 * All registers are 32-bit and should be accessed as such.
 */

#define PERIPHERAL_BASE     0x3F000000
#define SDHOST_BASE         (PERIPHERAL_BASE + 0x00202000)

/*
 * SDHOST_CMD - Command Register
 *
 * Write the command index and flags here to start a command.
 * The NEW flag must be set to initiate the command.
 */
#define SDHOST_CMD          (SDHOST_BASE + 0x00)

/*
 * SDHOST_ARG - Argument Register
 *
 * The 32-bit argument for the command (e.g., block address for reads).
 * Must be written BEFORE writing to CMD register.
 */
#define SDHOST_ARG          (SDHOST_BASE + 0x04)

/*
 * SDHOST_TOUT - Timeout Register
 *
 * Data timeout in clock cycles. Set high enough to avoid spurious timeouts.
 */
#define SDHOST_TOUT         (SDHOST_BASE + 0x08)

/*
 * SDHOST_CDIV - Clock Divisor Register
 *
 * Controls SD clock speed: SD_CLK = Core_CLK / (CDIV + 2)
 * During initialization, use slow clock (~400kHz).
 * After init, switch to fast clock (~25MHz).
 */
#define SDHOST_CDIV         (SDHOST_BASE + 0x0C)

/*
 * SDHOST_RSP0-3 - Response Registers
 *
 * After a command completes, the response is here:
 *   - RSP0 only for R1, R3, R6, R7 responses
 *   - RSP0-3 for R2 (CID/CSD) responses
 */
#define SDHOST_RSP0         (SDHOST_BASE + 0x10)
#define SDHOST_RSP1         (SDHOST_BASE + 0x14)
#define SDHOST_RSP2         (SDHOST_BASE + 0x18)
#define SDHOST_RSP3         (SDHOST_BASE + 0x1C)

/*
 * SDHOST_HSTS - Host Status Register
 *
 * Shows command completion, errors, and data availability.
 * Error bits should be cleared by writing 1 to them.
 */
#define SDHOST_HSTS         (SDHOST_BASE + 0x20)

/*
 * SDHOST_VDD - VDD Control Register
 *
 * Controls power to the SD card. Set to 1 to power on.
 */
#define SDHOST_VDD          (SDHOST_BASE + 0x30)

/*
 * SDHOST_HCFG - Host Configuration Register
 *
 * Various host settings including slow card mode and bus width.
 */
#define SDHOST_HCFG         (SDHOST_BASE + 0x38)

/*
 * SDHOST_HBCT - Host Block Count Register
 *
 * Number of bytes per block (usually 512).
 */
#define SDHOST_HBCT         (SDHOST_BASE + 0x3C)

/*
 * SDHOST_DATA - Data Port Register
 *
 * Read/write data here. For reads, poll HSTS for data availability,
 * then read 32 bits at a time.
 */
#define SDHOST_DATA         (SDHOST_BASE + 0x40)

/*
 * SDHOST_HBLC - Host Block Count Register
 *
 * Number of blocks to transfer.
 */
#define SDHOST_HBLC         (SDHOST_BASE + 0x50)


/* =============================================================================
 * SDHOST COMMAND FLAGS
 * =============================================================================
 *
 * These flags are OR'd with the command index when writing to CMD register.
 */

/*
 * SDHOST_CMD_NEW - New Command Flag
 *
 * Must be set to start a new command. Hardware clears this when done.
 */
#define SDHOST_CMD_NEW      0x8000

/*
 * SDHOST_CMD_FAIL - Command Failed
 *
 * Set by hardware if the command failed. Check HSTS for error details.
 */
#define SDHOST_CMD_FAIL     0x4000

/*
 * SDHOST_CMD_BUSY - Busy Flag
 *
 * Set if the command expects a "busy" signal from the card.
 * Used for commands like CMD7 (SELECT_CARD).
 */
#define SDHOST_CMD_BUSY     0x0800

/*
 * SDHOST_CMD_NO_RSP - No Response Expected
 *
 * Set for commands that don't expect a response (like CMD0).
 */
#define SDHOST_CMD_NO_RSP   0x0400

/*
 * SDHOST_CMD_LONG_RSP - Long Response Expected
 *
 * Set for R2 responses (136 bits, like CID/CSD).
 */
#define SDHOST_CMD_LONG_RSP 0x0200

/*
 * SDHOST_CMD_READ - Data Read Command
 *
 * Set for commands that read data (like CMD17).
 */
#define SDHOST_CMD_READ     0x0040


/* =============================================================================
 * SDHOST STATUS FLAGS
 * =============================================================================
 */

/*
 * SDHOST_HSTS_DATA_FLAG - Data Available
 *
 * Set when data is available in the DATA register.
 */
#define SDHOST_HSTS_DATA_FLAG   0x0001

/*
 * SDHOST_HSTS_ERROR_MASK - Error Bits Mask
 *
 * Mask for all error conditions in HSTS register.
 */
#define SDHOST_HSTS_ERROR_MASK  0x7F8


/* =============================================================================
 * SDHOST CONFIGURATION FLAGS
 * =============================================================================
 */

/* Slow card mode - use during initialization */
#define SDHOST_HCFG_SLOW_CARD   0x0002

/* Internal bus width setting */
#define SDHOST_HCFG_INTBUS      0x0001


/* =============================================================================
 * SD CARD COMMANDS
 * =============================================================================
 *
 * Standard SD card command indices. Each command has a specific function
 * and expected response type.
 */

/* Basic Commands */
#define SD_CMD_GO_IDLE_STATE        0   /* CMD0: Reset to idle state */
#define SD_CMD_SEND_IF_COND         8   /* CMD8: Voltage check (SD v2+) */
#define SD_CMD_SEND_CSD             9   /* CMD9: Get Card Specific Data */
#define SD_CMD_SEND_CID             10  /* CMD10: Get Card ID */
#define SD_CMD_STOP_TRANSMISSION    12  /* CMD12: Stop multi-block transfer */
#define SD_CMD_SET_BLOCKLEN         16  /* CMD16: Set block length */
#define SD_CMD_READ_SINGLE_BLOCK    17  /* CMD17: Read one block */
#define SD_CMD_READ_MULTIPLE_BLOCK  18  /* CMD18: Read multiple blocks */
#define SD_CMD_APP_CMD              55  /* CMD55: Prefix for ACMD commands */

/* SD-Specific Commands (must be preceded by CMD55) */
#define SD_ACMD_SD_SEND_OP_COND     41  /* ACMD41: Send operating conditions */

/* Card Identification Commands */
#define SD_CMD_ALL_SEND_CID         2   /* CMD2: Get CID from all cards */
#define SD_CMD_SEND_RELATIVE_ADDR   3   /* CMD3: Get/set relative address */
#define SD_CMD_SELECT_CARD          7   /* CMD7: Select/deselect card */


/* =============================================================================
 * CONSTANTS
 * =============================================================================
 */

/* Standard sector size */
#define SECTOR_SIZE                 512


/* =============================================================================
 * DATA STRUCTURES
 * =============================================================================
 */

/*
 * SD Card Driver State
 *
 * Maintains state for the SD card after initialization.
 */
typedef struct {
    bool initialized;       /* Card has been successfully initialized */
    bool is_sdhc;           /* Card is SDHC/SDXC (block addressing) */
    uint32_t rca;           /* Relative Card Address (upper 16 bits) */
} sdcard_t;


/* =============================================================================
 * PUBLIC API
 * =============================================================================
 */

/*
 * sdcard_create() - Create an SD card driver instance
 *
 * Initializes the structure to default values. Does NOT initialize hardware.
 *
 * Returns: Initialized sdcard_t structure
 */
sdcard_t sdcard_create(void);

/*
 * sdcard_init() - Initialize the SD card
 *
 * Performs the complete initialization sequence:
 *   1. Configure GPIO pins for SDHOST
 *   2. Power on SD card via mailbox
 *   3. Reset the SDHOST controller
 *   4. Send CMD0 (GO_IDLE_STATE)
 *   5. Send CMD8 to detect SD v2 cards
 *   6. ACMD41 loop to wait for card ready
 *   7. Get CID and RCA
 *   8. Select card and switch to high speed
 *
 * @param card  Pointer to sdcard_t structure
 *
 * Returns: true on success, false on failure
 */
bool sdcard_init(sdcard_t *card);

/*
 * sdcard_is_initialized() - Check if card is initialized
 *
 * @param card  Pointer to sdcard_t structure
 *
 * Returns: true if card is ready for use
 */
bool sdcard_is_initialized(const sdcard_t *card);

/*
 * sdcard_is_sdhc() - Check if card is SDHC/SDXC
 *
 * SDHC cards use block addressing (sector numbers).
 * Regular SD cards use byte addressing.
 *
 * @param card  Pointer to sdcard_t structure
 *
 * Returns: true if card is SDHC/SDXC
 */
bool sdcard_is_sdhc(const sdcard_t *card);

/*
 * sdcard_read_sector() - Read a single 512-byte sector
 *
 * @param card    Pointer to sdcard_t structure
 * @param lba     Logical Block Address (sector number)
 * @param buffer  Buffer to read into (must be exactly 512 bytes)
 *
 * Returns: true on success, false on failure
 */
bool sdcard_read_sector(sdcard_t *card, uint32_t lba, uint8_t buffer[SECTOR_SIZE]);

/*
 * sdcard_read_sectors() - Read multiple sectors
 *
 * Convenience function that calls sdcard_read_sector() multiple times.
 *
 * @param card          Pointer to sdcard_t structure
 * @param start_lba     Starting sector number
 * @param buffer        Buffer to read into
 * @param buffer_size   Size of buffer (should be multiple of 512)
 *
 * Returns: Number of bytes read, or 0 on failure
 */
size_t sdcard_read_sectors(sdcard_t *card, uint32_t start_lba,
                           uint8_t *buffer, size_t buffer_size);

#endif /* SDHOST_H */
