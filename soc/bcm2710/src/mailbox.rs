//! BCM2710 VideoCore mailbox interface.
//!
//! The mailbox is the ARM CPU's communication channel to the VideoCore GPU.
//! Property tag messages are built in a 16-byte-aligned buffer, sent via
//! the mailbox write register, and responses are read back from the same
//! buffer after the GPU fills them in.
//!
//! This is an **internal** module — not part of the public HAL trait surface.
//! [`soc_init`](crate::soc_init) and [`display_dpi`](crate::display_dpi)
//! call these functions to implement the HAL traits.

use crate::regs;

// ============================================================================
// Mailbox Buffer
// ============================================================================

/// 16-byte-aligned mailbox buffer (low 4 bits encode the channel number).
#[repr(C, align(16))]
pub struct MailboxBuffer {
    pub data: [u32; 36],
}

impl MailboxBuffer {
    pub const fn zeroed() -> Self {
        Self { data: [0; 36] }
    }
}

// ============================================================================
// Core Mailbox Call
// ============================================================================

/// Send a mailbox message and wait for the response.
///
/// Returns `true` if the GPU responded with `RESPONSE_OK`.
pub fn call(buf: &mut MailboxBuffer, channel: u8) -> bool {
    let addr = buf as *const MailboxBuffer as u32;
    let bus_addr = regs::arm_to_bus(addr);

    // Wait for mailbox not full
    while (unsafe { common::mmio::read32(regs::MBOX_STATUS) } & regs::MBOX_FULL) != 0 {
        hal::cpu::nop();
    }

    // Write address with channel in low 4 bits
    unsafe { common::mmio::write32(regs::MBOX_WRITE, (bus_addr & !0xF) | (channel as u32 & 0xF)) };

    // Wait for response
    loop {
        while (unsafe { common::mmio::read32(regs::MBOX_STATUS) } & regs::MBOX_EMPTY) != 0 {
            hal::cpu::nop();
        }

        let response = unsafe { common::mmio::read32(regs::MBOX_READ) };
        if (response & 0xF) == channel as u32 {
            return buf.data[1] == regs::MBOX_RESPONSE_OK;
        }
    }
}

// ============================================================================
// Convenience Functions
// ============================================================================

/// Query ARM-accessible memory base and size.
pub fn get_arm_memory() -> Option<(u32, u32)> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_ARM_MEMORY;
    mbox.data[3] = 8;
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some((mbox.data[5], mbox.data[6]))
}

/// Query VideoCore GPU memory base and size.
pub fn get_vc_memory() -> Option<(u32, u32)> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_VC_MEMORY;
    mbox.data[3] = 8;
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some((mbox.data[5], mbox.data[6]))
}

/// Query a clock rate by BCM clock ID.
pub fn get_clock_rate(clock_id: u32) -> Option<u32> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_CLOCK_RATE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = clock_id;
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some(mbox.data[6])
}

/// Query measured (actual) clock rate.
pub fn get_clock_measured(clock_id: u32) -> Option<u32> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_CLOCK_MEASURED;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = clock_id;
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some(mbox.data[6])
}

/// Query SoC temperature in millidegrees Celsius.
pub fn get_temperature() -> Option<u32> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_TEMPERATURE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = 0; // Temperature ID 0 = SoC
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some(mbox.data[6])
}

/// Query maximum temperature before throttling.
pub fn get_max_temperature() -> Option<u32> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_MAX_TEMP;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some(mbox.data[6])
}

/// Query throttle status flags.
pub fn get_throttled() -> Option<u32> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_THROTTLED;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some(mbox.data[6])
}

/// Query board revision code.
pub fn get_board_revision() -> Option<u32> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 7 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_BOARD_REV;
    mbox.data[3] = 4;
    mbox.data[6] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some(mbox.data[5])
}

/// Query board serial number.
pub fn get_board_serial() -> Option<u64> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_BOARD_SERIAL;
    mbox.data[3] = 8;
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some(((mbox.data[6] as u64) << 32) | (mbox.data[5] as u64))
}

/// Set device power state. Returns `true` on success.
pub fn set_power_state(device_id: u32, on: bool) -> bool {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::SET_POWER_STATE;
    mbox.data[3] = 8;
    mbox.data[4] = 8;
    mbox.data[5] = device_id;
    mbox.data[6] = if on { 3 } else { 2 }; // bit 0=on, bit 1=wait for stable
    mbox.data[7] = regs::tag::END;

    call(&mut mbox, regs::MBOX_CH_PROP)
}

/// Query device power state.
pub fn get_power_state(device_id: u32) -> Option<bool> {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::GET_POWER_STATE;
    mbox.data[3] = 8;
    mbox.data[4] = 4;
    mbox.data[5] = device_id;
    mbox.data[7] = regs::tag::END;

    if !call(&mut mbox, regs::MBOX_CH_PROP) { return None; }
    Some((mbox.data[6] & 1) != 0)
}

/// Wait for vertical sync.
pub fn wait_vsync() -> bool {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 7 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::WAIT_FOR_VSYNC;
    mbox.data[3] = 4;
    mbox.data[4] = 4;
    mbox.data[6] = regs::tag::END;

    call(&mut mbox, regs::MBOX_CH_PROP)
}

/// Set the virtual framebuffer offset (for double-buffering page flip).
pub fn set_virtual_offset(x: u32, y: u32) -> bool {
    let mut mbox = MailboxBuffer::zeroed();
    mbox.data[0] = 8 * 4;
    mbox.data[1] = regs::MBOX_REQUEST;
    mbox.data[2] = regs::tag::SET_VIRTUAL_OFFSET;
    mbox.data[3] = 8;
    mbox.data[4] = 8;
    mbox.data[5] = x;
    mbox.data[6] = y;
    mbox.data[7] = regs::tag::END;

    call(&mut mbox, regs::MBOX_CH_PROP)
}
