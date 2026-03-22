/*
 * hal/hal_dsi.h — MIPI DSI Display Interface Abstraction
 *
 * Tutorial-OS: HAL Interface Definitions
 *
 * This header abstracts the MIPI Display Serial Interface (DSI) protocol,
 * separating the portable DSI/DCS command layer from SoC-specific PHY
 * and host controller implementations.
 *
 * ============================================================================
 * ARCHITECTURE
 * ============================================================================
 *
 * The MIPI DSI stack has a clean layered structure:
 *
 *   ┌──────────────────────────────────────────────────────┐
 *   │              Panel Init Sequences                    │  ← Per-panel
 *   │         (vendor-specific DCS commands)               │
 *   ├──────────────────────────────────────────────────────┤
 *   │            DCS Command Layer (portable)              │  ← This header
 *   │   (sleep, display on/off, set column/page, etc.)    │
 *   ├──────────────────────────────────────────────────────┤
 *   │            DSI Protocol Layer (portable)             │  ← This header
 *   │    (short/long packets, command/video mode)         │
 *   ├──────────────────────────────────────────────────────┤
 *   │          DSI Host Controller (SoC-specific)          │  ← soc/xxx/
 *   │   (register programming, FIFO management)           │
 *   ├──────────────────────────────────────────────────────┤
 *   │              D-PHY Layer (SoC-specific)              │  ← soc/xxx/
 *   │   (PLL, lane config, timing, pin assignment)        │
 *   └──────────────────────────────────────────────────────┘
 *
 * WHAT'S PORTABLE (defined here):
 *   - DSI packet types and data IDs
 *   - DCS standard command set
 *   - Panel timing parameters
 *   - Configuration structures
 *   - The hal_dsi_host_ops interface contract
 *
 * WHAT'S SOC-SPECIFIC (implemented per-platform):
 *   - D-PHY PLL and clock tree setup
 *   - DSI host controller register programming
 *   - Lane enable/disable and pin mapping
 *   - Interrupt handling
 *
 * ============================================================================
 * SUPPORTED PLATFORMS
 * ============================================================================
 *
 * Each SoC has a different DSI host controller:
 *
 *   BCM2710/BCM2711:  Broadcom UniCam DSI (DSI0/DSI1 peripherals)
 *   BCM2712 (Pi 5):   DSI through RP1 southbridge (via PCIe)
 *   RK3528A:          Rockchip MIPI-DSI2 controller + Innosilicon D-PHY
 *   H618:             Allwinner MIPI-DSI controller + D-PHY
 *   K1 (RISC-V):      SpacemiT MIPI-DSI controller
 *
 * The pin assignment for data/clock lanes is board-specific and depends
 * on the physical PCB routing. The SoC implementation receives lane
 * configuration through hal_dsi_phy_config_t and maps it to the
 * appropriate pinmux/PHY registers.
 *
 * ============================================================================
 * USAGE
 * ============================================================================
 *
 *   // 1. Get the platform's DSI host operations
 *   const hal_dsi_host_ops_t *dsi = hal_dsi_get_host_ops();
 *
 *   // 2. Configure PHY (lanes, clock, timing)
 *   hal_dsi_phy_config_t phy_cfg = {
 *       .lanes = 2,
 *       .lane_rate_mbps = 500,
 *       .mode = HAL_DSI_MODE_VIDEO,
 *   };
 *   dsi->phy_init(dsi->ctx, &phy_cfg);
 *
 *   // 3. Configure the panel timing
 *   hal_dsi_panel_timing_t timing = {
 *       .hactive = 800, .vactive = 480,
 *       .hsync = 48, .hbp = 40, .hfp = 40,
 *       .vsync = 3,  .vbp = 29, .vfp = 13,
 *       .pixel_clock_khz = 30000,
 *       .format = HAL_DSI_FMT_RGB888,
 *   };
 *   dsi->set_timing(dsi->ctx, &timing);
 *
 *   // 4. Send panel init sequence (using portable DCS helpers)
 *   hal_dsi_dcs_exit_sleep(dsi);
 *   hal_delay_ms(120);
 *   hal_dsi_dcs_set_display_on(dsi);
 *
 *   // 5. Switch to video mode for continuous framebuffer streaming
 *   dsi->video_mode_start(dsi->ctx);
 *
 * ============================================================================
 * MIPI DSI PRIMER (for the book)
 * ============================================================================
 *
 * DSI uses differential signaling over 1–4 data lanes plus a clock lane.
 * Each lane is a pair of wires (Dn+/Dn-) that carry data as voltage
 * differences, making it resistant to electromagnetic noise.
 *
 * There are two operating modes:
 *
 *   COMMAND MODE: The host sends individual commands/data packets to the
 *   panel. The panel has its own framebuffer and only needs updates when
 *   content changes. Good for power-efficient displays (watches, e-ink).
 *
 *   VIDEO MODE: The host continuously streams pixel data, just like HDMI
 *   or DPI. The panel has no framebuffer — it displays whatever the host
 *   sends in real-time. Most LCD panels use this mode.
 *
 * DSI packets come in two sizes:
 *   - SHORT PACKET: 4 bytes total (header only). Used for simple commands
 *     like "turn display on" or writing a single register.
 *   - LONG PACKET: Header + payload + CRC. Used for sending pixel data
 *     or multi-byte panel configuration sequences.
 *
 * The Display Command Set (DCS) is a standard set of commands that all
 * MIPI-compliant panels must support. Things like sleep in/out, display
 * on/off, set brightness, etc. Panel vendors add their own commands on
 * top of DCS for panel-specific features (gamma curves, voltage rails).
 */

#ifndef HAL_DSI_H
#define HAL_DSI_H

#include "hal_types.h"

/* =============================================================================
 * DSI ERROR CODES (0x08xx)
 * =============================================================================
 * Extends the HAL error code scheme from hal_types.h.
 * These are defined as standalone constants rather than added to the enum
 * to avoid modifying hal_types.h — add them to the enum when integrating.
 */

#define HAL_ERROR_DSI_INIT          0x0800  /* DSI subsystem init failed */
#define HAL_ERROR_DSI_PHY           0x0801  /* D-PHY configuration failed */
#define HAL_ERROR_DSI_PLL           0x0802  /* PLL lock timeout */
#define HAL_ERROR_DSI_LANES         0x0803  /* Invalid lane configuration */
#define HAL_ERROR_DSI_TIMEOUT       0x0804  /* Command/transfer timeout */
#define HAL_ERROR_DSI_ACK_ERR       0x0805  /* Panel returned error in ACK */
#define HAL_ERROR_DSI_FIFO          0x0806  /* FIFO overflow/underflow */
#define HAL_ERROR_DSI_CRC           0x0807  /* CRC mismatch on received data */
#define HAL_ERROR_DSI_ECC           0x0808  /* ECC error in packet header */
#define HAL_ERROR_DSI_CONTENTION    0x0809  /* Bus contention detected */
#define HAL_ERROR_DSI_NOT_VIDEO     0x080A  /* Not in video mode */
#define HAL_ERROR_DSI_NOT_CMD       0x080B  /* Not in command mode */
#define HAL_ERROR_DSI_PANEL         0x080C  /* Panel did not respond */


/* =============================================================================
 * DSI DATA TYPE IDENTIFIERS
 * =============================================================================
 *
 * Every DSI packet starts with a Data Type (DT) byte that identifies
 * the packet format and purpose. These are defined by the MIPI DSI spec
 * and are the same on every SoC.
 *
 * The DT encodes two things:
 *   - Bits 5:4 = Virtual Channel (0–3, for multi-display)
 *   - Bits 3:0 = Packet type
 *
 * We define the packet type portion here. Virtual channel is OR'd in
 * by the host controller at transmit time.
 */

/* ---- Processor-to-Peripheral (Host → Panel) Short Packets ---- */
#define DSI_DT_SHORT_WRITE_0        0x05    /* Short write, no parameter */
#define DSI_DT_SHORT_WRITE_1        0x15    /* Short write, 1 parameter */
#define DSI_DT_SHORT_READ           0x06    /* Read request */
#define DSI_DT_SET_MAX_RETURN       0x37    /* Set Maximum Return Packet Size */
#define DSI_DT_NULL_PACKET          0x09    /* Null packet (timing filler) */
#define DSI_DT_BLANKING             0x19    /* Blanking packet */
#define DSI_DT_GENERIC_SHORT_W0     0x03    /* Generic short write, no param */
#define DSI_DT_GENERIC_SHORT_W1     0x13    /* Generic short write, 1 param */
#define DSI_DT_GENERIC_SHORT_W2     0x23    /* Generic short write, 2 params */
#define DSI_DT_GENERIC_READ_0       0x04    /* Generic read, no param */
#define DSI_DT_GENERIC_READ_1       0x14    /* Generic read, 1 param */
#define DSI_DT_GENERIC_READ_2       0x24    /* Generic read, 2 params */

/* ---- Processor-to-Peripheral (Host → Panel) Long Packets ---- */
#define DSI_DT_LONG_WRITE           0x39    /* DCS long write */
#define DSI_DT_GENERIC_LONG_WRITE   0x29    /* Generic long write */

/* ---- Video Mode Pixel Stream Packets ---- */
#define DSI_DT_PACKED_16BIT         0x0E    /* Pixel stream, 16-bit RGB565 */
#define DSI_DT_PACKED_18BIT         0x1E    /* Pixel stream, 18-bit RGB666 (packed) */
#define DSI_DT_LOOSE_18BIT          0x2E    /* Pixel stream, 18-bit RGB666 (loose) */
#define DSI_DT_PACKED_24BIT         0x3E    /* Pixel stream, 24-bit RGB888 */

/* ---- Video Mode Sync/Timing Packets ---- */
#define DSI_DT_VSYNC_START          0x01    /* Vertical sync start */
#define DSI_DT_VSYNC_END            0x11    /* Vertical sync end */
#define DSI_DT_HSYNC_START          0x21    /* Horizontal sync start */
#define DSI_DT_HSYNC_END            0x31    /* Horizontal sync end */

/* ---- Peripheral-to-Processor (Panel → Host) Response Packets ---- */
#define DSI_DT_ACK_ERR_REPORT       0x02    /* Acknowledge with error */
#define DSI_DT_EOT                  0x08    /* End of transmission */
#define DSI_DT_GENERIC_SHORT_R1     0x11    /* Generic short read, 1 byte */
#define DSI_DT_GENERIC_SHORT_R2     0x12    /* Generic short read, 2 bytes */
#define DSI_DT_GENERIC_LONG_READ    0x1A    /* Generic long read response */
#define DSI_DT_DCS_SHORT_R1         0x21    /* DCS short read, 1 byte */
#define DSI_DT_DCS_SHORT_R2         0x22    /* DCS short read, 2 bytes */
#define DSI_DT_DCS_LONG_READ        0x1C    /* DCS long read response */


/* =============================================================================
 * MIPI DCS — DISPLAY COMMAND SET
 * =============================================================================
 *
 * These are standardized commands that all MIPI-compliant display panels
 * must support. They control fundamental panel operations like sleep,
 * display on/off, pixel format, and addressing.
 *
 * Panel vendors extend DCS with proprietary commands (usually 0xB0+)
 * for features like gamma tuning, voltage adjustment, and internal
 * register configuration. Those go in panel-specific init sequences.
 *
 * Reference: MIPI DCS Specification v1.3
 */

/* ---- Basic Control ---- */
#define DCS_NOP                         0x00
#define DCS_SOFT_RESET                  0x01
#define DCS_GET_POWER_MODE              0x0A
#define DCS_GET_ADDRESS_MODE            0x0B
#define DCS_GET_PIXEL_FORMAT            0x0C
#define DCS_GET_DISPLAY_MODE            0x0D
#define DCS_GET_SIGNAL_MODE             0x0E
#define DCS_GET_DIAGNOSTIC              0x0F

/* ---- Sleep ---- */
#define DCS_ENTER_SLEEP                 0x10
#define DCS_EXIT_SLEEP                  0x11

/* ---- Partial Mode ---- */
#define DCS_ENTER_PARTIAL               0x12
#define DCS_ENTER_NORMAL                0x13

/* ---- Display ---- */
#define DCS_DISPLAY_OFF                 0x28
#define DCS_DISPLAY_ON                  0x29

/* ---- Addressing ---- */
#define DCS_SET_COLUMN_ADDRESS          0x2A    /* 4-byte payload: SC_hi, SC_lo, EC_hi, EC_lo */
#define DCS_SET_PAGE_ADDRESS            0x2B    /* 4-byte payload: SP_hi, SP_lo, EP_hi, EP_lo */
#define DCS_WRITE_MEMORY_START          0x2C    /* Begin pixel data transfer */
#define DCS_WRITE_MEMORY_CONTINUE       0x3C    /* Continue pixel data transfer */

/* ---- Scrolling ---- */
#define DCS_SET_SCROLL_AREA             0x33
#define DCS_SET_SCROLL_START            0x37

/* ---- Tearing Effect ---- */
#define DCS_SET_TEAR_OFF                0x34
#define DCS_SET_TEAR_ON                 0x35    /* 0x00=V-only, 0x01=V+H */
#define DCS_SET_TEAR_SCANLINE           0x44

/* ---- Pixel Format ---- */
#define DCS_SET_PIXEL_FORMAT            0x3A
/*
 * Pixel format parameter byte:
 *   Bits 6:4 = DBI interface (parallel) format
 *   Bits 2:0 = DPI interface (RGB) format
 *
 * Common values:
 *   0x55 = 16-bit/pixel (RGB565) for both interfaces
 *   0x66 = 18-bit/pixel (RGB666)
 *   0x77 = 24-bit/pixel (RGB888)
 */
#define DCS_PIXEL_FORMAT_16BIT          0x55
#define DCS_PIXEL_FORMAT_18BIT          0x66
#define DCS_PIXEL_FORMAT_24BIT          0x77

/* ---- Display Brightness ---- */
#define DCS_SET_BRIGHTNESS              0x51    /* 0x00=off, 0xFF=max */
#define DCS_GET_BRIGHTNESS              0x52
#define DCS_SET_CTRL_DISPLAY            0x53    /* BL/DD/BCTRL bits */
#define DCS_GET_CTRL_DISPLAY            0x54

/* ---- Inversion ---- */
#define DCS_ENTER_INVERT                0x20
#define DCS_EXIT_INVERT                 0x21

/* ---- Idle ---- */
#define DCS_ENTER_IDLE                  0x39
#define DCS_EXIT_IDLE                   0x38

/* ---- Address Mode (MADCTL) ---- */
#define DCS_SET_ADDRESS_MODE            0x36
/*
 * Address mode bits (MADCTL):
 *   Bit 7: MY  — Row address order     (0=top→bottom, 1=bottom→top)
 *   Bit 6: MX  — Column address order  (0=left→right, 1=right→left)
 *   Bit 5: MV  — Row/Column exchange   (0=normal, 1=swap X/Y)
 *   Bit 4: ML  — Vertical refresh dir  (0=top→bottom, 1=bottom→top)
 *   Bit 3: BGR — Color order            (0=RGB, 1=BGR)
 *   Bit 2: MH  — Horizontal refresh    (0=left→right, 1=right→left)
 *
 * Common rotation values:
 *   0x00 = Portrait (0°)
 *   0x60 = Landscape (90°)    — MX + MV
 *   0xC0 = Portrait (180°)    — MY + MX
 *   0xA0 = Landscape (270°)   — MY + MV
 */
#define DCS_MADCTL_MY                   BIT(7)
#define DCS_MADCTL_MX                   BIT(6)
#define DCS_MADCTL_MV                   BIT(5)
#define DCS_MADCTL_ML                   BIT(4)
#define DCS_MADCTL_BGR                  BIT(3)
#define DCS_MADCTL_MH                   BIT(2)

/* ---- Identification ---- */
#define DCS_READ_ID1                    0xDA    /* Manufacturer ID */
#define DCS_READ_ID2                    0xDB    /* Module/Driver version */
#define DCS_READ_ID3                    0xDC    /* Module/Driver ID */


/* =============================================================================
 * CONFIGURATION TYPES
 * =============================================================================
 */

/*
 * DSI operating mode
 */
typedef enum {
    HAL_DSI_MODE_COMMAND    = 0,    /* Panel has its own framebuffer */
    HAL_DSI_MODE_VIDEO      = 1,    /* Host streams pixels continuously */
} hal_dsi_mode_t;

/*
 * DSI video mode sync type
 *
 * VIDEO MODE has three sub-modes that differ in how sync events
 * are signaled to the panel:
 *
 *   NON-BURST SYNC PULSE:
 *     Sync events (HSYNC, VSYNC) are transmitted as timed pulses.
 *     Pixel data fills the exact active region. Most common mode.
 *
 *   NON-BURST SYNC EVENT:
 *     Sync events are single-packet markers. Timing is derived from
 *     the event positions. Slightly simpler than pulse mode.
 *
 *   BURST:
 *     Pixel data is sent in bursts at maximum lane speed, then the
 *     link goes idle (LP mode) during blanking periods. Most power-
 *     efficient for high-res panels. Requires the panel to have a
 *     small line buffer for rate adaptation.
 */
typedef enum {
    HAL_DSI_VIDEO_SYNC_PULSE    = 0,
    HAL_DSI_VIDEO_SYNC_EVENT    = 1,
    HAL_DSI_VIDEO_BURST         = 2,
} hal_dsi_video_sync_t;

/*
 * DSI pixel format for the serial link
 *
 * This is the format used on the DSI lanes, which may differ from the
 * framebuffer pixel format. The DSI host controller handles conversion.
 */
typedef enum {
    HAL_DSI_FMT_RGB565          = 0,    /* 16 bits/pixel, packed */
    HAL_DSI_FMT_RGB666_PACKED   = 1,    /* 18 bits/pixel, packed */
    HAL_DSI_FMT_RGB666_LOOSE    = 2,    /* 18 bits/pixel, loosely packed */
    HAL_DSI_FMT_RGB888          = 3,    /* 24 bits/pixel */
} hal_dsi_pixel_format_t;

/*
 * D-PHY lane polarity
 *
 * Some board layouts swap the +/- differential pairs. The PHY needs
 * to know so it can invert the received signal.
 */
typedef enum {
    HAL_DSI_POLARITY_NORMAL     = 0,
    HAL_DSI_POLARITY_INVERTED   = 1,
} hal_dsi_polarity_t;


/* =============================================================================
 * D-PHY CONFIGURATION
 * =============================================================================
 *
 * Physical layer parameters. These depend on the board's PCB routing
 * and the target panel's specifications.
 *
 * The SoC implementation reads these values and programs its PHY PLL
 * and lane configuration registers accordingly.
 */

typedef struct {
    /*
     * Number of active data lanes (1, 2, 3, or 4).
     *
     * Most small panels use 1–2 lanes. Large/high-res panels use 4.
     * The Raspberry Pi official touchscreen uses 2 lanes.
     * Each lane adds ~1 Gbps of bandwidth (at HS clock rates).
     */
    uint8_t lanes;

    /*
     * Target data rate per lane, in Mbps.
     *
     * The PHY PLL will be configured to produce this bit rate.
     * Typical ranges:
     *   - Low-power panels:   80–200 Mbps/lane
     *   - Standard panels:    200–500 Mbps/lane
     *   - High-res panels:    500–1500 Mbps/lane
     *
     * The minimum rate for a given panel is:
     *   rate = (width × height × bpp × fps) / (lanes × 1e6)
     *
     * Example: 800×480 @ RGB888 @ 60fps, 2 lanes:
     *   (800 × 480 × 24 × 60) / (2 × 1e6) = 276 Mbps/lane
     */
    uint32_t lane_rate_mbps;

    /*
     * Operating mode (command or video).
     */
    hal_dsi_mode_t mode;

    /*
     * Video mode sync type (ignored if mode == HAL_DSI_MODE_COMMAND).
     */
    hal_dsi_video_sync_t video_sync;

    /*
     * Clock lane behavior during Low-Power periods.
     *
     * If true, the clock lane enters LP mode during blanking. This
     * saves power but requires the panel to re-lock to the clock
     * when HS data resumes. Some panels don't support this.
     */
    bool clk_lp_during_blanking;

    /*
     * Lane polarity overrides.
     *
     * Index 0 = clock lane, 1–4 = data lanes 0–3.
     * Set to INVERTED if the board swaps +/- on that lane pair.
     */
    hal_dsi_polarity_t clk_lane_polarity;
    hal_dsi_polarity_t data_lane_polarity[4];

    /*
     * Continuous clock mode.
     *
     * If true, the clock lane stays in HS mode continuously (even
     * during LP data periods). Required by some panel controllers.
     * If false, the clock follows data transitions.
     */
    bool continuous_clock;

} hal_dsi_phy_config_t;


/* =============================================================================
 * D-PHY TIMING PARAMETERS
 * =============================================================================
 *
 * Low-level timing values for the physical layer. These are defined by
 * the MIPI D-PHY specification and depend on the lane data rate.
 *
 * Most SoC implementations can auto-calculate these from the lane rate.
 * This struct allows manual override when needed (e.g., marginal signal
 * integrity, long flex cables, or non-compliant panels).
 *
 * All values are in nanoseconds unless noted otherwise.
 */

typedef struct {
    /*
     * Whether to use these manual values or let the SoC auto-calculate.
     * If false, the PHY driver ignores all fields below and derives
     * timing from the lane rate.
     */
    bool manual_timing;

    /* ---- HS (High-Speed) timing ---- */
    uint32_t hs_prepare_ns;     /* Time from LP→HS transition start to first data bit */
    uint32_t hs_zero_ns;        /* Duration of HS-0 state after HS-Prepare */
    uint32_t hs_trail_ns;       /* Time after last data bit before LP transition */
    uint32_t hs_exit_ns;        /* Time in LP after HS transmission ends */

    /* ---- Clock lane HS timing ---- */
    uint32_t clk_prepare_ns;    /* Clock LP→HS prepare time */
    uint32_t clk_zero_ns;       /* Clock HS-0 duration */
    uint32_t clk_trail_ns;      /* Clock trail after HS */
    uint32_t clk_post_ns;       /* Time after HS clock before LP */
    uint32_t clk_pre_ns;        /* Time before HS clock after LP */

    /* ---- LP (Low-Power) timing ---- */
    uint32_t lp_data_rate_mhz;  /* LP data rate (typically 10 MHz) */
    uint32_t ta_go_ns;          /* Turnaround GO (BTA handshake) */
    uint32_t ta_sure_ns;        /* Turnaround SURE duration */
    uint32_t ta_get_ns;         /* Turnaround GET timeout */

    /* ---- Initialization ---- */
    uint32_t init_us;           /* PHY init time after power-on (min 100µs per spec) */

} hal_dsi_phy_timing_t;

/* Default value: 100µs minimum init time per MIPI D-PHY spec */
#define HAL_DSI_PHY_INIT_TIME_US    100


/* =============================================================================
 * PANEL TIMING (Display Timing Parameters)
 * =============================================================================
 *
 * These describe the display's scan timing and are specified in the
 * panel's datasheet. They're the same parameters used for any display
 * interface (HDMI, DPI, DSI) — just delivered over different wires.
 *
 *   ┌─── hfp ──┬── hsync ──┬── hbp ──┬──── hactive ────┬─── hfp ──┐
 *   │  front    │   sync    │  back   │   active pixel   │  front   │
 *   │  porch    │   pulse   │  porch  │   region         │  porch   │
 *   └──────────────────────────────────────────────────────────────────┘
 *
 * Same structure vertically with vfp/vsync/vbp/vactive.
 */

typedef struct {
    /* ---- Active region ---- */
    uint32_t hactive;           /* Horizontal active pixels */
    uint32_t vactive;           /* Vertical active lines */

    /* ---- Horizontal blanking ---- */
    uint32_t hsync;             /* Horizontal sync pulse width (pixels) */
    uint32_t hbp;               /* Horizontal back porch (pixels) */
    uint32_t hfp;               /* Horizontal front porch (pixels) */

    /* ---- Vertical blanking ---- */
    uint32_t vsync;             /* Vertical sync pulse width (lines) */
    uint32_t vbp;               /* Vertical back porch (lines) */
    uint32_t vfp;               /* Vertical front porch (lines) */

    /* ---- Sync polarity ---- */
    bool hsync_active_low;      /* true = active-low HSYNC (most panels) */
    bool vsync_active_low;      /* true = active-low VSYNC (most panels) */

    /* ---- Clock ---- */
    uint32_t pixel_clock_khz;   /* Pixel clock in kHz */

    /* ---- Pixel format on the DSI link ---- */
    hal_dsi_pixel_format_t format;

} hal_dsi_panel_timing_t;


/* =============================================================================
 * PANEL INIT COMMAND
 * =============================================================================
 *
 * Panel initialization typically involves sending a sequence of DCS
 * and vendor-specific commands. These are documented in the panel's
 * datasheet and usually look like:
 *
 *   1. Soft reset → wait 10ms
 *   2. Write vendor magic sequence (unlock registers)
 *   3. Configure gamma, voltage, timing
 *   4. Set pixel format (0x3A, 0x77)
 *   5. Exit sleep (0x11) → wait 120ms
 *   6. Display on (0x29) → wait 20ms
 *
 * We represent this as an array of hal_dsi_panel_cmd_t. The init
 * function walks the array and sends each command.
 */

typedef struct {
    /*
     * Command type:
     *   0 = DCS short write (0 params) — just the command byte
     *   1 = DCS short write (1 param)  — command + 1 data byte
     *   2 = DCS long write             — command + N data bytes
     *   0xFF = delay marker            — wait `delay_ms` milliseconds
     */
    uint8_t type;

    /*
     * DCS command byte (ignored for delay markers).
     */
    uint8_t cmd;

    /*
     * Number of data bytes following the command (0 for short writes).
     */
    uint8_t len;

    /*
     * Delay in milliseconds after sending this command.
     * Many panels require a delay after sleep exit (120ms) and
     * display on (20ms) for internal power sequencing.
     */
    uint16_t delay_ms;

    /*
     * Data payload for long writes (up to 64 bytes covers most panels).
     * For short writes with 1 param, only data[0] is used.
     */
    uint8_t data[64];

} hal_dsi_panel_cmd_t;

/* Panel command type constants */
#define HAL_DSI_CMD_SHORT_0     0       /* DCS short write, no parameter */
#define HAL_DSI_CMD_SHORT_1     1       /* DCS short write, 1 parameter */
#define HAL_DSI_CMD_LONG        2       /* DCS long write, N parameters */
#define HAL_DSI_CMD_DELAY       0xFF    /* Delay marker (no command sent) */

/*
 * Panel command sequence terminator
 *
 * End an init command array with this sentinel.
 */
#define HAL_DSI_CMD_END         { .type = HAL_DSI_CMD_DELAY, .delay_ms = 0 }


/* =============================================================================
 * PANEL DESCRIPTOR
 * =============================================================================
 *
 * Complete description of a DSI panel: PHY configuration, display timing,
 * and initialization command sequence. Board support packages define one
 * of these for each supported panel.
 *
 * Example:
 *
 *   static const hal_dsi_panel_cmd_t my_panel_init[] = {
 *       { HAL_DSI_CMD_SHORT_0, DCS_SOFT_RESET, 0, 10, {} },
 *       { HAL_DSI_CMD_SHORT_1, DCS_SET_PIXEL_FORMAT, 1, 0, {0x77} },
 *       { HAL_DSI_CMD_SHORT_0, DCS_EXIT_SLEEP, 0, 120, {} },
 *       { HAL_DSI_CMD_SHORT_0, DCS_DISPLAY_ON, 0, 20, {} },
 *       HAL_DSI_CMD_END,
 *   };
 *
 *   static const hal_dsi_panel_desc_t my_panel = {
 *       .name = "Generic 800x480 DSI Panel",
 *       .phy = { .lanes = 2, .lane_rate_mbps = 400, ... },
 *       .timing = { .hactive = 800, .vactive = 480, ... },
 *       .init_cmds = my_panel_init,
 *       .init_cmd_count = ARRAY_SIZE(my_panel_init) - 1,
 *   };
 */

typedef struct {
    const char *name;                       /* Human-readable panel name */
    hal_dsi_phy_config_t phy;               /* PHY/lane configuration */
    hal_dsi_phy_timing_t phy_timing;        /* PHY timing (manual_timing=false for auto) */
    hal_dsi_panel_timing_t timing;          /* Display timing */
    const hal_dsi_panel_cmd_t *init_cmds;   /* Init command sequence */
    uint32_t init_cmd_count;                /* Number of init commands */
} hal_dsi_panel_desc_t;


/* =============================================================================
 * DSI HOST OPERATIONS — THE PLATFORM CONTRACT
 * =============================================================================
 *
 * This is the interface that each SoC must implement. It follows the same
 * vtable/function-pointer pattern used in ui_canvas.h for the same reason:
 * C doesn't have traits, so we use a struct of function pointers.
 *
 * The ctx field is an opaque pointer to the SoC's private driver state
 * (controller base address, PLL config, allocated resources, etc.).
 *
 * A SoC implementation looks like this:
 *
 *   // soc/bcm2711/dsi_bcm2711.c
 *   typedef struct {
 *       uintptr_t dsi_base;      // DSI0 or DSI1 register base
 *       uintptr_t phy_base;      // D-PHY register base
 *       uint8_t   active_lanes;
 *       bool      initialized;
 *   } bcm2711_dsi_ctx_t;
 *
 *   static hal_error_t bcm2711_dsi_phy_init(void *ctx, const hal_dsi_phy_config_t *cfg) {
 *       bcm2711_dsi_ctx_t *dsi = (bcm2711_dsi_ctx_t *)ctx;
 *       // Program BCM2711 DSI PLL, lane enables, timing...
 *       return HAL_SUCCESS;
 *   }
 *
 *   static const hal_dsi_host_ops_t bcm2711_dsi_ops = {
 *       .ctx = &bcm2711_dsi_state,
 *       .phy_init = bcm2711_dsi_phy_init,
 *       .phy_shutdown = bcm2711_dsi_phy_shutdown,
 *       // ... all function pointers
 *   };
 */

typedef struct {
    /*
     * Opaque context pointer — points to SoC-specific driver state.
     * Passed as the first argument to every operation.
     */
    void *ctx;

    /* =================================================================
     * PHY INITIALIZATION / SHUTDOWN
     * =================================================================
     */

    /*
     * Initialize the D-PHY and DSI host controller.
     *
     * This is where the SoC-specific magic happens:
     *   - Configure PLL for the requested lane rate
     *   - Enable the requested number of data lanes
     *   - Set lane polarities
     *   - Apply PHY timing parameters (or auto-calculate)
     *   - Bring PHY out of reset
     *
     * @param ctx       SoC driver context
     * @param phy_cfg   PHY configuration (lanes, rate, mode)
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*phy_init)(void *ctx, const hal_dsi_phy_config_t *phy_cfg);

    /*
     * Apply manual PHY timing overrides.
     *
     * Called after phy_init() if custom timing is needed (e.g., long
     * flex cables or out-of-spec panels). If phy_timing->manual_timing
     * is false, this function may be a no-op.
     *
     * @param ctx       SoC driver context
     * @param timing    PHY timing parameters
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*phy_set_timing)(void *ctx, const hal_dsi_phy_timing_t *timing);

    /*
     * Shut down the D-PHY and DSI host.
     *
     * Disables lanes, stops PLL, puts PHY in reset.
     *
     * @param ctx       SoC driver context
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*phy_shutdown)(void *ctx);

    /* =================================================================
     * DISPLAY TIMING
     * =================================================================
     */

    /*
     * Configure display timing parameters.
     *
     * Programs the DSI host controller with the panel's horizontal
     * and vertical timing. Must be called after phy_init() and before
     * video_mode_start().
     *
     * @param ctx       SoC driver context
     * @param timing    Panel timing parameters
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*set_timing)(void *ctx, const hal_dsi_panel_timing_t *timing);

    /* =================================================================
     * LOW-LEVEL PACKET INTERFACE
     * =================================================================
     *
     * These send raw DSI packets. The DCS helper functions (below)
     * use these internally. SoC implementations must provide all of
     * them; the portable layer calls them for command sequences.
     */

    /*
     * Send a short write packet (0 or 1 parameter).
     *
     * Short packets are 4 bytes total on the wire:
     *   [DT] [Data0] [Data1] [ECC]
     *
     * For DCS short write with 0 params: DT=0x05, Data0=cmd, Data1=0
     * For DCS short write with 1 param:  DT=0x15, Data0=cmd, Data1=param
     *
     * @param ctx       SoC driver context
     * @param channel   Virtual channel (0–3, usually 0)
     * @param dt        Data type identifier
     * @param data0     First data byte (usually the DCS command)
     * @param data1     Second data byte (parameter, or 0)
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*short_write)(void *ctx, uint8_t channel,
                               uint8_t dt, uint8_t data0, uint8_t data1);

    /*
     * Send a long write packet (multi-byte payload).
     *
     * Long packets have a header, payload, and CRC:
     *   [DT] [WC_lo] [WC_hi] [ECC] [payload...] [CRC_lo] [CRC_hi]
     *
     * The host controller typically handles ECC and CRC generation.
     *
     * @param ctx       SoC driver context
     * @param channel   Virtual channel
     * @param dt        Data type identifier
     * @param data      Payload buffer (first byte is usually the DCS command)
     * @param len       Payload length in bytes
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*long_write)(void *ctx, uint8_t channel,
                              uint8_t dt, const uint8_t *data, uint32_t len);

    /*
     * Read data from the panel (DCS read or generic read).
     *
     * Sends a read request, then performs a Bus Turn-Around (BTA) to
     * receive the panel's response. The response may be a short (1–2
     * bytes) or long read depending on the data requested.
     *
     * @param ctx       SoC driver context
     * @param channel   Virtual channel
     * @param cmd       DCS command or register to read
     * @param buf       Buffer for received data
     * @param len       Expected number of bytes to read
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*read)(void *ctx, uint8_t channel,
                        uint8_t cmd, uint8_t *buf, uint32_t len);

    /* =================================================================
     * VIDEO MODE CONTROL
     * =================================================================
     */

    /*
     * Start video mode streaming.
     *
     * After calling this, the DSI host continuously reads from the
     * framebuffer and streams pixels to the panel. The framebuffer
     * address must already be configured (via hal_display_init).
     *
     * @param ctx       SoC driver context
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*video_mode_start)(void *ctx);

    /*
     * Stop video mode streaming and return to command mode.
     *
     * @param ctx       SoC driver context
     * @return          HAL_SUCCESS or DSI error code
     */
    hal_error_t (*video_mode_stop)(void *ctx);

    /* =================================================================
     * STATUS / DEBUG
     * =================================================================
     */

    /*
     * Check if DSI host is initialized and PHY is locked.
     *
     * @param ctx       SoC driver context
     * @return          true if DSI is ready for commands
     */
    bool (*is_initialized)(void *ctx);

    /*
     * Get PHY PLL lock status.
     *
     * @param ctx       SoC driver context
     * @return          true if PLL is locked and stable
     */
    bool (*is_pll_locked)(void *ctx);

    /*
     * Dump DSI host controller state to debug console.
     *
     * Prints register values, PHY status, error counters, etc.
     * Useful for bring-up debugging with UART.
     *
     * @param ctx       SoC driver context
     */
    void (*debug_dump)(void *ctx);

} hal_dsi_host_ops_t;


/* =============================================================================
 * PLATFORM BINDING
 * =============================================================================
 *
 * Each SoC provides this function to return its DSI host operations.
 * Returns NULL if the platform has no DSI controller.
 *
 * Implemented in: soc/bcm2711/dsi_bcm2711.c, soc/bcm2712/dsi_bcm2712.c, etc.
 */

/*
 * Get the platform's DSI host operations.
 *
 * @return  Pointer to the host ops struct, or NULL if DSI is not available.
 */
const hal_dsi_host_ops_t *hal_dsi_get_host_ops(void);


/* =============================================================================
 * PORTABLE DCS HELPER FUNCTIONS
 * =============================================================================
 *
 * These are convenience functions that build on the host ops interface.
 * They handle the DCS command encoding so callers don't need to know
 * about DSI data types and packet formats.
 *
 * All functions send on virtual channel 0 (the default for single-panel
 * configurations).
 */

/*
 * Send a DCS command with no parameters (short write, DT=0x05).
 */
HAL_INLINE hal_error_t hal_dsi_dcs_write_0(
    const hal_dsi_host_ops_t *host, uint8_t cmd)
{
    return host->short_write(host->ctx, 0, DSI_DT_SHORT_WRITE_0, cmd, 0x00);
}

/*
 * Send a DCS command with 1 parameter (short write, DT=0x15).
 */
HAL_INLINE hal_error_t hal_dsi_dcs_write_1(
    const hal_dsi_host_ops_t *host, uint8_t cmd, uint8_t param)
{
    return host->short_write(host->ctx, 0, DSI_DT_SHORT_WRITE_1, cmd, param);
}

/*
 * Send a DCS long write (command byte + data payload).
 *
 * The first byte of the buffer must be the DCS command.
 * Example: buf = { DCS_SET_COLUMN_ADDRESS, SC_hi, SC_lo, EC_hi, EC_lo }
 */
HAL_INLINE hal_error_t hal_dsi_dcs_write_long(
    const hal_dsi_host_ops_t *host, const uint8_t *buf, uint32_t len)
{
    return host->long_write(host->ctx, 0, DSI_DT_LONG_WRITE, buf, len);
}

/*
 * Read from a DCS register.
 */
HAL_INLINE hal_error_t hal_dsi_dcs_read(
    const hal_dsi_host_ops_t *host, uint8_t cmd, uint8_t *buf, uint32_t len)
{
    return host->read(host->ctx, 0, cmd, buf, len);
}

/* ---- Named DCS commands for readability ---- */

HAL_INLINE hal_error_t hal_dsi_dcs_soft_reset(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_SOFT_RESET);
}

HAL_INLINE hal_error_t hal_dsi_dcs_enter_sleep(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_ENTER_SLEEP);
}

HAL_INLINE hal_error_t hal_dsi_dcs_exit_sleep(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_EXIT_SLEEP);
}

HAL_INLINE hal_error_t hal_dsi_dcs_display_off(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_DISPLAY_OFF);
}

HAL_INLINE hal_error_t hal_dsi_dcs_display_on(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_DISPLAY_ON);
}

HAL_INLINE hal_error_t hal_dsi_dcs_enter_normal(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_ENTER_NORMAL);
}

HAL_INLINE hal_error_t hal_dsi_dcs_enter_invert(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_ENTER_INVERT);
}

HAL_INLINE hal_error_t hal_dsi_dcs_exit_invert(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_EXIT_INVERT);
}

HAL_INLINE hal_error_t hal_dsi_dcs_set_pixel_format(
    const hal_dsi_host_ops_t *host, uint8_t format)
{
    return hal_dsi_dcs_write_1(host, DCS_SET_PIXEL_FORMAT, format);
}

HAL_INLINE hal_error_t hal_dsi_dcs_set_address_mode(
    const hal_dsi_host_ops_t *host, uint8_t madctl)
{
    return hal_dsi_dcs_write_1(host, DCS_SET_ADDRESS_MODE, madctl);
}

HAL_INLINE hal_error_t hal_dsi_dcs_set_brightness(
    const hal_dsi_host_ops_t *host, uint8_t brightness)
{
    return hal_dsi_dcs_write_1(host, DCS_SET_BRIGHTNESS, brightness);
}

HAL_INLINE hal_error_t hal_dsi_dcs_set_tear_on(
    const hal_dsi_host_ops_t *host, uint8_t mode)
{
    return hal_dsi_dcs_write_1(host, DCS_SET_TEAR_ON, mode);
}

HAL_INLINE hal_error_t hal_dsi_dcs_set_tear_off(const hal_dsi_host_ops_t *host)
{
    return hal_dsi_dcs_write_0(host, DCS_SET_TEAR_OFF);
}

/*
 * Set column address range (for command-mode partial updates).
 *
 * @param start     Start column (inclusive)
 * @param end       End column (inclusive)
 */
HAL_INLINE hal_error_t hal_dsi_dcs_set_column_address(
    const hal_dsi_host_ops_t *host, uint16_t start, uint16_t end)
{
    uint8_t buf[5] = {
        DCS_SET_COLUMN_ADDRESS,
        (uint8_t)(start >> 8), (uint8_t)(start & 0xFF),
        (uint8_t)(end >> 8),   (uint8_t)(end & 0xFF),
    };
    return hal_dsi_dcs_write_long(host, buf, 5);
}

/*
 * Set page (row) address range.
 *
 * @param start     Start row (inclusive)
 * @param end       End row (inclusive)
 */
HAL_INLINE hal_error_t hal_dsi_dcs_set_page_address(
    const hal_dsi_host_ops_t *host, uint16_t start, uint16_t end)
{
    uint8_t buf[5] = {
        DCS_SET_PAGE_ADDRESS,
        (uint8_t)(start >> 8), (uint8_t)(start & 0xFF),
        (uint8_t)(end >> 8),   (uint8_t)(end & 0xFF),
    };
    return hal_dsi_dcs_write_long(host, buf, 5);
}

/*
 * Read panel identification bytes.
 *
 * @param id1   Output: manufacturer ID
 * @param id2   Output: module/driver version
 * @param id3   Output: module/driver ID
 * @return      HAL_SUCCESS or DSI error code
 */
HAL_INLINE hal_error_t hal_dsi_dcs_read_id(
    const hal_dsi_host_ops_t *host,
    uint8_t *id1, uint8_t *id2, uint8_t *id3)
{
    hal_error_t err;
    err = hal_dsi_dcs_read(host, DCS_READ_ID1, id1, 1);
    if (HAL_FAILED(err)) return err;
    err = hal_dsi_dcs_read(host, DCS_READ_ID2, id2, 1);
    if (HAL_FAILED(err)) return err;
    err = hal_dsi_dcs_read(host, DCS_READ_ID3, id3, 1);
    return err;
}


/* =============================================================================
 * PANEL INIT SEQUENCE EXECUTOR
 * =============================================================================
 *
 * Walks a hal_dsi_panel_cmd_t array and sends each command. This is the
 * portable function that every panel init calls — the only SoC-specific
 * part is the host ops that it dispatches through.
 */

/*
 * Execute a panel initialization command sequence.
 *
 * Iterates through the command array, sending each DCS command through
 * the host interface and inserting delays as specified. Stops at the
 * first HAL_DSI_CMD_END sentinel (type=0xFF, delay_ms=0).
 *
 * @param host      DSI host operations (from hal_dsi_get_host_ops())
 * @param cmds      Array of panel init commands
 * @param count     Number of commands in the array
 * @return          HAL_SUCCESS, or the first error encountered
 */
hal_error_t hal_dsi_panel_init(
    const hal_dsi_host_ops_t *host,
    const hal_dsi_panel_cmd_t *cmds,
    uint32_t count
);


/* =============================================================================
 * HIGH-LEVEL DSI DISPLAY INIT
 * =============================================================================
 *
 * Combines PHY init, timing config, and panel init into a single call.
 * This is the function most board support packages will use.
 */

/*
 * Initialize a DSI display from a panel descriptor.
 *
 * Performs the complete init sequence:
 *   1. Get platform DSI host ops
 *   2. Initialize D-PHY (PLL, lanes)
 *   3. Apply PHY timing overrides (if manual_timing=true)
 *   4. Configure display timing
 *   5. Execute panel init command sequence
 *   6. Start video mode (if panel is video-mode)
 *
 * @param panel     Complete panel descriptor
 * @return          HAL_SUCCESS or the first error encountered
 */
hal_error_t hal_dsi_display_init(const hal_dsi_panel_desc_t *panel);


/* =============================================================================
 * PLATFORM-SPECIFIC IMPLEMENTATION NOTES
 * =============================================================================
 *
 * BCM2710/BCM2711 (Pi Zero 2W, Pi 3, Pi 4, CM4):
 *   - Two DSI controllers: DSI0 (display connector) and DSI1
 *   - Broadcom UniCam DSI host controller
 *   - PHY PLL configurable via DSI_PHY_AFEC registers
 *   - Official Pi touchscreen uses DSI0, 2 lanes, 400 Mbps
 *   - DMA-capable: can read framebuffer automatically in video mode
 *
 * BCM2712 (Pi 5, CM5):
 *   - DSI routed through RP1 southbridge (accessed via PCIe BAR)
 *   - Different register layout from BCM2711 DSI
 *   - Two DSI ports on RP1
 *   - Higher lane rates supported (up to 1.5 Gbps/lane)
 *
 * RK3528A (Radxa Rock 2A):
 *   - Rockchip MIPI-DSI2 controller
 *   - Innosilicon MIPI D-PHY
 *   - Configured via RK3528 CRU (Clock & Reset Unit) + GRF registers
 *   - Integrates with VOP2 (Video Output Processor) for framebuffer DMA
 *
 * H618 (KICKPI K2B):
 *   - Allwinner MIPI-DSI controller
 *   - Allwinner D-PHY with separate PLL
 *   - Integrates with DE3 (Display Engine) for framebuffer DMA
 *
 * K1 RISC-V (Orange Pi RV2):
 *   - SpacemiT MIPI-DSI controller
 *   - SpacemiT D-PHY
 *   - Less documented than ARM SoCs — may require register-level reverse engineering
 *
 * LattePanda IOTA (x86_64):
 *   - No native MIPI-DSI (Intel N150 uses eDP for panels)
 *   - Could potentially use a USB-to-DSI bridge (e.g., TC358762)
 *   - DSI support on this platform is unlikely / low priority
 */

#endif /* HAL_DSI_H */