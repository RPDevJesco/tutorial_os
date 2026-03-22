//! MIPI Display Serial Interface (DSI) abstraction.
//!
//! This module separates the portable DSI/DCS command layer from the
//! SoC-specific PHY and host controller implementations.
//!
//! # Architecture
//!
//! ```text
//! ┌──────────────────────────────────────────────────────┐
//! │              Panel Init Sequences                    │  ← Per-panel
//! ├──────────────────────────────────────────────────────┤
//! │            DCS Command Layer (portable)              │  ← This module
//! ├──────────────────────────────────────────────────────┤
//! │            DSI Protocol Layer (portable)             │  ← This module
//! ├──────────────────────────────────────────────────────┤
//! │          DSI Host Controller (SoC-specific)          │  ← soc/xxx/
//! ├──────────────────────────────────────────────────────┤
//! │              D-PHY Layer (SoC-specific)              │  ← soc/xxx/
//! └──────────────────────────────────────────────────────┘
//! ```
//!
//! The [`DsiHost`] trait is the Rust equivalent of the C `hal_dsi_host_ops_t`
//! function-pointer table.  Where C uses `void *ctx` for type erasure, Rust
//! simply lets each SoC provide a concrete struct that implements the trait.

use crate::types::HalResult;

// ============================================================================
// DSI Data Type Identifiers
// ============================================================================
//
// Every DSI packet starts with a Data Type (DT) byte.  These are defined by
// the MIPI DSI specification and are identical on every SoC.

/// DSI data type identifiers — Host → Panel short packets.
pub mod dt {
    pub const SHORT_WRITE_0: u8       = 0x05;
    pub const SHORT_WRITE_1: u8       = 0x15;
    pub const SHORT_READ: u8          = 0x06;
    pub const SET_MAX_RETURN: u8      = 0x37;
    pub const NULL_PACKET: u8         = 0x09;
    pub const BLANKING: u8            = 0x19;
    pub const GENERIC_SHORT_W0: u8    = 0x03;
    pub const GENERIC_SHORT_W1: u8    = 0x13;
    pub const GENERIC_SHORT_W2: u8    = 0x23;
    pub const GENERIC_READ_0: u8      = 0x04;
    pub const GENERIC_READ_1: u8      = 0x14;
    pub const GENERIC_READ_2: u8      = 0x24;

    /// Host → Panel long packets.
    pub const LONG_WRITE: u8          = 0x39;
    pub const GENERIC_LONG_WRITE: u8  = 0x29;

    /// Video mode pixel stream packets.
    pub const PACKED_16BIT: u8        = 0x0E;
    pub const PACKED_18BIT: u8        = 0x1E;
    pub const LOOSE_18BIT: u8         = 0x2E;
    pub const PACKED_24BIT: u8        = 0x3E;

    /// Video mode sync/timing packets.
    pub const VSYNC_START: u8         = 0x01;
    pub const VSYNC_END: u8           = 0x11;
    pub const HSYNC_START: u8         = 0x21;
    pub const HSYNC_END: u8           = 0x31;

    /// Panel → Host response packets.
    pub const ACK_ERR_REPORT: u8      = 0x02;
    pub const EOT: u8                 = 0x08;
    pub const GENERIC_SHORT_R1: u8    = 0x11;
    pub const GENERIC_SHORT_R2: u8    = 0x12;
    pub const GENERIC_LONG_READ: u8   = 0x1A;
    pub const DCS_SHORT_R1: u8        = 0x21;
    pub const DCS_SHORT_R2: u8        = 0x22;
    pub const DCS_LONG_READ: u8       = 0x1C;
}

// ============================================================================
// MIPI DCS — Display Command Set
// ============================================================================
//
// Standardized commands that all MIPI-compliant panels must support.

/// MIPI DCS (Display Command Set) command bytes.
pub mod dcs {
    // ---- Basic Control ----
    pub const NOP: u8                     = 0x00;
    pub const SOFT_RESET: u8              = 0x01;
    pub const GET_POWER_MODE: u8          = 0x0A;
    pub const GET_ADDRESS_MODE: u8        = 0x0B;
    pub const GET_PIXEL_FORMAT: u8        = 0x0C;
    pub const GET_DISPLAY_MODE: u8        = 0x0D;
    pub const GET_SIGNAL_MODE: u8         = 0x0E;
    pub const GET_DIAGNOSTIC: u8          = 0x0F;

    // ---- Sleep ----
    pub const ENTER_SLEEP: u8             = 0x10;
    pub const EXIT_SLEEP: u8              = 0x11;

    // ---- Partial / Normal ----
    pub const ENTER_PARTIAL: u8           = 0x12;
    pub const ENTER_NORMAL: u8            = 0x13;

    // ---- Display ----
    pub const DISPLAY_OFF: u8             = 0x28;
    pub const DISPLAY_ON: u8              = 0x29;

    // ---- Addressing ----
    pub const SET_COLUMN_ADDRESS: u8      = 0x2A;
    pub const SET_PAGE_ADDRESS: u8        = 0x2B;
    pub const WRITE_MEMORY_START: u8      = 0x2C;
    pub const WRITE_MEMORY_CONTINUE: u8   = 0x3C;

    // ---- Scrolling ----
    pub const SET_SCROLL_AREA: u8         = 0x33;
    pub const SET_SCROLL_START: u8        = 0x37;

    // ---- Tearing Effect ----
    pub const SET_TEAR_OFF: u8            = 0x34;
    pub const SET_TEAR_ON: u8             = 0x35;
    pub const SET_TEAR_SCANLINE: u8       = 0x44;

    // ---- Pixel Format ----
    pub const SET_PIXEL_FORMAT: u8        = 0x3A;
    pub const PIXEL_FORMAT_16BIT: u8      = 0x55;
    pub const PIXEL_FORMAT_18BIT: u8      = 0x66;
    pub const PIXEL_FORMAT_24BIT: u8      = 0x77;

    // ---- Brightness ----
    pub const SET_BRIGHTNESS: u8          = 0x51;
    pub const GET_BRIGHTNESS: u8          = 0x52;
    pub const SET_CTRL_DISPLAY: u8        = 0x53;
    pub const GET_CTRL_DISPLAY: u8        = 0x54;

    // ---- Inversion ----
    pub const ENTER_INVERT: u8            = 0x20;
    pub const EXIT_INVERT: u8             = 0x21;

    // ---- Idle ----
    pub const ENTER_IDLE: u8              = 0x39;
    pub const EXIT_IDLE: u8               = 0x38;

    // ---- Address Mode (MADCTL) bits ----
    pub const SET_ADDRESS_MODE: u8        = 0x36;
    pub const MADCTL_MY: u8               = 1 << 7;
    pub const MADCTL_MX: u8               = 1 << 6;
    pub const MADCTL_MV: u8               = 1 << 5;
    pub const MADCTL_ML: u8               = 1 << 4;
    pub const MADCTL_BGR: u8              = 1 << 3;
    pub const MADCTL_MH: u8               = 1 << 2;

    // ---- Identification ----
    pub const READ_ID1: u8                = 0xDA;
    pub const READ_ID2: u8                = 0xDB;
    pub const READ_ID3: u8                = 0xDC;
}

// ============================================================================
// Configuration Types
// ============================================================================

/// DSI operating mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Mode {
    /// Panel has its own framebuffer; host sends updates on demand.
    Command = 0,
    /// Host continuously streams pixels — most LCD panels use this.
    Video   = 1,
}

/// Video-mode sync signaling variant.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum VideoSync {
    /// Sync events are timed pulses. Most common.
    SyncPulse = 0,
    /// Sync events are single-packet markers.
    SyncEvent = 1,
    /// Pixels sent in bursts at max lane speed; link idles during blanking.
    Burst     = 2,
}

/// Pixel format on the DSI link (may differ from framebuffer format).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum DsiPixelFormat {
    Rgb565       = 0,
    Rgb666Packed = 1,
    Rgb666Loose  = 2,
    Rgb888       = 3,
}

/// Lane polarity (for boards that swap +/- differential pairs).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Polarity {
    Normal   = 0,
    Inverted = 1,
}

// ============================================================================
// PHY Configuration
// ============================================================================

/// D-PHY and lane configuration.
#[derive(Debug, Clone)]
pub struct PhyConfig {
    /// Number of active data lanes (1–4).
    pub lanes: u8,
    /// Target data rate per lane in Mbps.
    pub lane_rate_mbps: u32,
    /// Command or video mode.
    pub mode: Mode,
    /// Video sync variant (ignored in command mode).
    pub video_sync: VideoSync,
    /// Clock lane enters LP during blanking periods.
    pub clk_lp_during_blanking: bool,
    /// Clock lane polarity.
    pub clk_lane_polarity: Polarity,
    /// Data lane polarities (index 0–3 for lanes 0–3).
    pub data_lane_polarity: [Polarity; 4],
    /// Continuous HS clock mode (required by some panel controllers).
    pub continuous_clock: bool,
}

/// Low-level D-PHY timing parameters (nanoseconds unless noted).
///
/// Most SoC drivers can auto-calculate these from the lane rate.
/// Set `manual_timing = true` to override (e.g., for long flex cables).
#[derive(Debug, Clone, Copy)]
pub struct PhyTiming {
    /// If `false`, the SoC driver auto-derives timing from the lane rate.
    pub manual_timing: bool,

    // HS timing
    pub hs_prepare_ns: u32,
    pub hs_zero_ns: u32,
    pub hs_trail_ns: u32,
    pub hs_exit_ns: u32,

    // Clock lane HS timing
    pub clk_prepare_ns: u32,
    pub clk_zero_ns: u32,
    pub clk_trail_ns: u32,
    pub clk_post_ns: u32,
    pub clk_pre_ns: u32,

    // LP timing
    pub lp_data_rate_mhz: u32,
    pub ta_go_ns: u32,
    pub ta_sure_ns: u32,
    pub ta_get_ns: u32,

    // Init
    pub init_us: u32,
}

/// Default PHY init time per MIPI D-PHY spec.
pub const PHY_INIT_TIME_US: u32 = 100;

impl Default for PhyTiming {
    fn default() -> Self {
        Self {
            manual_timing: false,
            hs_prepare_ns: 0, hs_zero_ns: 0, hs_trail_ns: 0, hs_exit_ns: 0,
            clk_prepare_ns: 0, clk_zero_ns: 0, clk_trail_ns: 0,
            clk_post_ns: 0, clk_pre_ns: 0,
            lp_data_rate_mhz: 10,
            ta_go_ns: 0, ta_sure_ns: 0, ta_get_ns: 0,
            init_us: PHY_INIT_TIME_US,
        }
    }
}

// ============================================================================
// Panel Timing
// ============================================================================

/// Display scan timing (identical concept across DPI, HDMI, DSI).
///
/// ```text
/// ┌── hfp ─┬─ hsync ─┬─ hbp ─┬──── hactive ────┬── hfp ─┐
/// │ front  │  sync   │ back  │  active pixels   │ front  │
/// │ porch  │  pulse  │ porch │                   │ porch  │
/// └────────┴─────────┴───────┴──────────────────┴────────┘
/// ```
#[derive(Debug, Clone, Copy)]
pub struct PanelTiming {
    pub hactive: u32,
    pub vactive: u32,
    pub hsync: u32,
    pub hbp: u32,
    pub hfp: u32,
    pub vsync: u32,
    pub vbp: u32,
    pub vfp: u32,
    pub hsync_active_low: bool,
    pub vsync_active_low: bool,
    pub pixel_clock_khz: u32,
    pub format: DsiPixelFormat,
}

// ============================================================================
// Panel Init Commands
// ============================================================================

/// Type tag for a panel init command entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum CmdType {
    /// DCS short write, no parameter.
    Short0 = 0,
    /// DCS short write, 1 parameter.
    Short1 = 1,
    /// DCS long write, N parameters.
    Long   = 2,
    /// Pure delay (no command sent).
    Delay  = 0xFF,
}

/// A single entry in a panel init command sequence.
///
/// For `Short0`: only `cmd` is meaningful.
/// For `Short1`: `cmd` + `data[0]`.
/// For `Long`:   `cmd` + `data[0..len]`.
/// For `Delay`:  only `delay_ms` is meaningful.
#[derive(Debug, Clone)]
pub struct PanelCmd {
    pub cmd_type: CmdType,
    pub cmd: u8,
    pub len: u8,
    pub delay_ms: u16,
    /// Payload for long writes (first byte is typically the DCS command).
    pub data: [u8; 64],
}

impl PanelCmd {
    /// Sentinel marking the end of a command sequence.
    pub const END: Self = Self {
        cmd_type: CmdType::Delay,
        cmd: 0,
        len: 0,
        delay_ms: 0,
        data: [0; 64],
    };

    /// Returns `true` if this is the end-of-sequence sentinel.
    pub const fn is_end(&self) -> bool {
        matches!(self.cmd_type, CmdType::Delay) && self.delay_ms == 0
    }
}

// ============================================================================
// Panel Descriptor
// ============================================================================

/// Complete description of a DSI panel: PHY config, timing, and init sequence.
///
/// Board support packages define one of these for each supported panel.
pub struct PanelDescriptor {
    pub name: &'static str,
    pub phy: PhyConfig,
    pub phy_timing: PhyTiming,
    pub timing: PanelTiming,
    pub init_cmds: &'static [PanelCmd],
}

// ============================================================================
// DSI Host Trait (the SoC-specific contract)
// ============================================================================

/// DSI host controller contract — implemented by each SoC's DSI driver.
///
/// This is the Rust equivalent of `hal_dsi_host_ops_t`.  Where C uses a
/// `void *ctx` and a function-pointer struct, Rust uses a trait on a
/// concrete SoC-specific struct.
pub trait DsiHost {
    // ---- PHY ----

    /// Initialize the D-PHY and DSI host controller (PLL, lanes, timing).
    fn phy_init(&mut self, cfg: &PhyConfig) -> HalResult<()>;

    /// Apply manual PHY timing overrides.
    fn phy_set_timing(&mut self, timing: &PhyTiming) -> HalResult<()>;

    /// Shut down the D-PHY (disable lanes, stop PLL).
    fn phy_shutdown(&mut self) -> HalResult<()>;

    // ---- Display Timing ----

    /// Configure display timing parameters.
    fn set_timing(&mut self, timing: &PanelTiming) -> HalResult<()>;

    // ---- Packet Interface ----

    /// Send a short write packet (0 or 1 parameter).
    fn short_write(
        &self,
        channel: u8,
        data_type: u8,
        data0: u8,
        data1: u8,
    ) -> HalResult<()>;

    /// Send a long write packet (multi-byte payload).
    fn long_write(
        &self,
        channel: u8,
        data_type: u8,
        data: &[u8],
    ) -> HalResult<()>;

    /// Read data from the panel.
    fn read(
        &self,
        channel: u8,
        cmd: u8,
        buf: &mut [u8],
    ) -> HalResult<()>;

    // ---- Video Mode ----

    /// Start continuous pixel streaming.
    fn video_mode_start(&mut self) -> HalResult<()>;

    /// Stop video streaming, return to command mode.
    fn video_mode_stop(&mut self) -> HalResult<()>;

    // ---- Status ----

    /// Returns `true` if the DSI host is initialized and ready.
    fn is_initialized(&self) -> bool;

    /// Returns `true` if the PHY PLL is locked.
    fn is_pll_locked(&self) -> bool;

    /// Dump host controller state to debug console.
    fn debug_dump(&self) {}
}

// ============================================================================
// Portable DCS Helpers
// ============================================================================
//
// These build on the DsiHost trait to provide named, type-safe wrappers
// for the most common DCS operations.  All use virtual channel 0.

/// Send a DCS command with no parameters.
pub fn dcs_write_0(host: &dyn DsiHost, cmd: u8) -> HalResult<()> {
    host.short_write(0, dt::SHORT_WRITE_0, cmd, 0x00)
}

/// Send a DCS command with 1 parameter.
pub fn dcs_write_1(host: &dyn DsiHost, cmd: u8, param: u8) -> HalResult<()> {
    host.short_write(0, dt::SHORT_WRITE_1, cmd, param)
}

/// Send a DCS long write (first byte of `data` is the command byte).
pub fn dcs_write_long(host: &dyn DsiHost, data: &[u8]) -> HalResult<()> {
    host.long_write(0, dt::LONG_WRITE, data)
}

/// Read from a DCS register.
pub fn dcs_read(host: &dyn DsiHost, cmd: u8, buf: &mut [u8]) -> HalResult<()> {
    host.read(0, cmd, buf)
}

// Named convenience wrappers

pub fn dcs_soft_reset(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::SOFT_RESET)
}
pub fn dcs_enter_sleep(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::ENTER_SLEEP)
}
pub fn dcs_exit_sleep(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::EXIT_SLEEP)
}
pub fn dcs_display_off(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::DISPLAY_OFF)
}
pub fn dcs_display_on(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::DISPLAY_ON)
}
pub fn dcs_enter_normal(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::ENTER_NORMAL)
}
pub fn dcs_enter_invert(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::ENTER_INVERT)
}
pub fn dcs_exit_invert(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::EXIT_INVERT)
}
pub fn dcs_set_pixel_format(host: &dyn DsiHost, format: u8) -> HalResult<()> {
    dcs_write_1(host, dcs::SET_PIXEL_FORMAT, format)
}
pub fn dcs_set_address_mode(host: &dyn DsiHost, madctl: u8) -> HalResult<()> {
    dcs_write_1(host, dcs::SET_ADDRESS_MODE, madctl)
}
pub fn dcs_set_brightness(host: &dyn DsiHost, brightness: u8) -> HalResult<()> {
    dcs_write_1(host, dcs::SET_BRIGHTNESS, brightness)
}
pub fn dcs_set_tear_on(host: &dyn DsiHost, mode: u8) -> HalResult<()> {
    dcs_write_1(host, dcs::SET_TEAR_ON, mode)
}
pub fn dcs_set_tear_off(host: &dyn DsiHost) -> HalResult<()> {
    dcs_write_0(host, dcs::SET_TEAR_OFF)
}

/// Set column address range (for command-mode partial updates).
pub fn dcs_set_column_address(host: &dyn DsiHost, start: u16, end: u16) -> HalResult<()> {
    let buf = [
        dcs::SET_COLUMN_ADDRESS,
        (start >> 8) as u8, (start & 0xFF) as u8,
        (end >> 8) as u8,   (end & 0xFF) as u8,
    ];
    dcs_write_long(host, &buf)
}

/// Set page (row) address range.
pub fn dcs_set_page_address(host: &dyn DsiHost, start: u16, end: u16) -> HalResult<()> {
    let buf = [
        dcs::SET_PAGE_ADDRESS,
        (start >> 8) as u8, (start & 0xFF) as u8,
        (end >> 8) as u8,   (end & 0xFF) as u8,
    ];
    dcs_write_long(host, &buf)
}

/// Read panel identification bytes.
pub fn dcs_read_id(host: &dyn DsiHost) -> HalResult<(u8, u8, u8)> {
    let mut id1 = [0u8];
    let mut id2 = [0u8];
    let mut id3 = [0u8];
    dcs_read(host, dcs::READ_ID1, &mut id1)?;
    dcs_read(host, dcs::READ_ID2, &mut id2)?;
    dcs_read(host, dcs::READ_ID3, &mut id3)?;
    Ok((id1[0], id2[0], id3[0]))
}

// ============================================================================
// Panel Init Sequence Executor
// ============================================================================

/// Walk a panel command sequence and send each command through the host.
///
/// Stops at the first [`PanelCmd::END`] sentinel or after `cmds.len()` entries.
/// `delay_fn` is called for inter-command delays (typically wired to
/// `timer.delay_ms()`).
pub fn panel_init(
    host: &dyn DsiHost,
    cmds: &[PanelCmd],
    mut delay_fn: impl FnMut(u16),
) -> HalResult<()> {
    for cmd in cmds {
        if cmd.is_end() {
            break;
        }

        match cmd.cmd_type {
            CmdType::Short0 => {
                dcs_write_0(host, cmd.cmd)?;
            }
            CmdType::Short1 => {
                dcs_write_1(host, cmd.cmd, cmd.data[0])?;
            }
            CmdType::Long => {
                let len = cmd.len as usize + 1; // +1 for the command byte
                let mut buf = [0u8; 65];
                buf[0] = cmd.cmd;
                buf[1..len].copy_from_slice(&cmd.data[..cmd.len as usize]);
                dcs_write_long(host, &buf[..len])?;
            }
            CmdType::Delay => { /* delay only, no command */ }
        }

        if cmd.delay_ms > 0 {
            delay_fn(cmd.delay_ms);
        }
    }
    Ok(())
}

/// High-level DSI display init from a panel descriptor.
///
/// Performs the complete sequence: PHY init → timing → panel commands → video mode.
pub fn display_init(
    host: &mut dyn DsiHost,
    panel: &PanelDescriptor,
    delay_fn: impl FnMut(u16),
) -> HalResult<()> {
    host.phy_init(&panel.phy)?;

    if panel.phy_timing.manual_timing {
        host.phy_set_timing(&panel.phy_timing)?;
    }

    host.set_timing(&panel.timing)?;

    panel_init(host, panel.init_cmds, delay_fn)?;

    if panel.phy.mode == Mode::Video {
        host.video_mode_start()?;
    }

    Ok(())
}
