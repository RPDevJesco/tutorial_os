//! BCM2710 display implementation (DPI / HDMI via VideoCore mailbox).
//!
//! Only initialization and buffer presentation are platform-specific.
//! All drawing primitives live in the portable `drivers::framebuffer` crate.

use hal::display::{Display, DisplayConfig, DisplayType, FramebufferInfo, PixelFormat};
use hal::gpio::Peripheral;
use hal::types::{HalError, HalResult};
use crate::{mailbox, regs};

const BITS_PER_PIXEL: u32 = 32;
const BUFFER_COUNT: u32   = 2;
const DEFAULT_WIDTH: u32  = 640;
const DEFAULT_HEIGHT: u32 = 480;

/// BCM2710 display controller.
pub struct Bcm2710Display {
    info: Option<FramebufferInfo>,
    /// Index of the buffer currently shown on screen.
    front: u32,
    /// Index of the buffer the CPU is drawing into.
    back: u32,
    height: u32,
    vsync: bool,
}

impl Bcm2710Display {
    pub const fn new() -> Self {
        Self {
            info: None,
            front: 0,
            back: 1,
            height: 0,
            vsync: true,
        }
    }

    /// Allocate a framebuffer through the VideoCore mailbox.
    fn allocate(&mut self, width: u32, height: u32) -> HalResult<FramebufferInfo> {
        let virtual_height = height * BUFFER_COUNT;

        let mut mbox = mailbox::MailboxBuffer::zeroed();
        let mut i = 0usize;

        // Build the multi-tag request.
        mbox.data[i] = 0;                      i += 1; // total size (filled at end)
        mbox.data[i] = regs::MBOX_REQUEST;      i += 1;

        // Set physical size
        mbox.data[i] = regs::tag::SET_PHYSICAL_SIZE; i += 1;
        mbox.data[i] = 8;  i += 1;
        mbox.data[i] = 0;  i += 1;
        mbox.data[i] = width;  i += 1;
        mbox.data[i] = height; i += 1;

        // Set virtual size
        mbox.data[i] = regs::tag::SET_VIRTUAL_SIZE; i += 1;
        mbox.data[i] = 8;  i += 1;
        mbox.data[i] = 0;  i += 1;
        mbox.data[i] = width;          i += 1;
        mbox.data[i] = virtual_height; i += 1;

        // Set virtual offset
        mbox.data[i] = regs::tag::SET_VIRTUAL_OFFSET; i += 1;
        mbox.data[i] = 8;  i += 1;
        mbox.data[i] = 0;  i += 1;
        mbox.data[i] = 0;  i += 1;
        mbox.data[i] = 0;  i += 1;

        // Set depth
        mbox.data[i] = regs::tag::SET_DEPTH; i += 1;
        mbox.data[i] = 4;  i += 1;
        mbox.data[i] = 0;  i += 1;
        mbox.data[i] = BITS_PER_PIXEL; i += 1;

        // Set pixel order (BGR)
        mbox.data[i] = regs::tag::SET_PIXEL_ORDER; i += 1;
        mbox.data[i] = 4;  i += 1;
        mbox.data[i] = 0;  i += 1;
        mbox.data[i] = 0;  i += 1; // 0 = BGR

        // Allocate buffer
        mbox.data[i] = regs::tag::ALLOCATE_BUFFER; i += 1;
        mbox.data[i] = 8;  i += 1;
        mbox.data[i] = 0;  i += 1;
        let alloc_idx = i;
        mbox.data[i] = 16; i += 1; // alignment
        mbox.data[i] = 0;  i += 1; // size (output)

        // Get pitch
        mbox.data[i] = regs::tag::GET_PITCH; i += 1;
        mbox.data[i] = 4;  i += 1;
        mbox.data[i] = 0;  i += 1;
        let pitch_idx = i;
        mbox.data[i] = 0;  i += 1; // pitch (output)

        // End tag
        mbox.data[i] = regs::tag::END; i += 1;

        // Fill total size
        mbox.data[0] = (i as u32) * 4;

        if !mailbox::call(&mut mbox, regs::MBOX_CH_PROP) {
            return Err(HalError::DisplayMailbox);
        }

        let fb_addr = mbox.data[alloc_idx];
        let fb_size = mbox.data[alloc_idx + 1];
        let pitch   = mbox.data[pitch_idx];

        if fb_addr == 0 || fb_size == 0 {
            return Err(HalError::DisplayNoFramebuffer);
        }

        // Convert bus address → ARM physical address
        let fb_addr = regs::bus_to_arm(fb_addr);

        self.height = height;
        self.front = 0;
        self.back = if BUFFER_COUNT > 1 { 1 } else { 0 };

        let info = FramebufferInfo {
            width,
            height,
            pitch,
            bits_per_pixel: BITS_PER_PIXEL,
            display_type: DisplayType::Dpi,
            format: PixelFormat::Argb8888,
            base: fb_addr as *mut u32,
            size: fb_size,
            buffer_count: BUFFER_COUNT,
        };

        self.info = Some(info);
        Ok(info)
    }
}

impl Display for Bcm2710Display {
    fn init(&mut self, config: Option<&DisplayConfig>) -> HalResult<FramebufferInfo> {
        if self.info.is_some() {
            return Err(HalError::AlreadyInitialized);
        }

        let (w, h) = match config {
            Some(c) => (c.width, c.height),
            None    => (DEFAULT_WIDTH, DEFAULT_HEIGHT),
        };

        // Configure GPIO for DPI (best-effort — HDMI-only boards skip this)
        let gpio = crate::gpio::Bcm2710Gpio;
        let _ = hal::gpio::Gpio::configure_peripheral(&gpio, Peripheral::Dpi);

        self.allocate(w, h)
    }

    fn shutdown(&mut self) -> HalResult<()> {
        if self.info.is_none() {
            return Err(HalError::NotInitialized);
        }
        // Can't deallocate the framebuffer on BCM — just mark uninitialised.
        self.info = None;
        Ok(())
    }

    fn is_initialized(&self) -> bool { self.info.is_some() }

    fn width(&self)  -> u32 { self.info.map(|i| i.width).unwrap_or(0) }
    fn height(&self) -> u32 { self.info.map(|i| i.height).unwrap_or(0) }
    fn pitch(&self)  -> u32 { self.info.map(|i| i.pitch).unwrap_or(0) }

    fn present(&mut self) -> HalResult<u32> {
        if self.info.is_none() { return Err(HalError::NotInitialized); }

        if self.vsync {
            mailbox::wait_vsync();
        }

        // Swap front ↔ back
        if BUFFER_COUNT > 1 {
            core::mem::swap(&mut self.front, &mut self.back);
        }

        // Tell the GPU to display the front buffer
        let y_offset = self.front * self.height;
        mailbox::set_virtual_offset(0, y_offset);

        Ok(self.back)
    }

    fn present_immediate(&mut self) -> HalResult<u32> {
        let saved = self.vsync;
        self.vsync = false;
        let result = self.present();
        self.vsync = saved;
        result
    }

    fn set_vsync(&mut self, enabled: bool) -> HalResult<()> {
        self.vsync = enabled;
        Ok(())
    }

    fn wait_vsync(&self) -> HalResult<()> {
        if mailbox::wait_vsync() { Ok(()) } else { Err(HalError::HardwareFault) }
    }

    fn default_config(&self) -> DisplayConfig {
        DisplayConfig {
            width: DEFAULT_WIDTH,
            height: DEFAULT_HEIGHT,
            display_type: DisplayType::Dpi,
            format: PixelFormat::Argb8888,
            buffer_count: BUFFER_COUNT,
            vsync: true,
        }
    }
}
