//! SimpleFB display driver for the JH7110.
//!
//! Port of `display_simplefb.c`.  Implements [`hal::display::Display`]
//! by parsing the device tree blob (DTB) for a `simple-framebuffer` node
//! injected by U-Boot.
//!
//! # Strategy (Identical to KyX1)
//!
//! U-Boot initializes the DC8200 display controller + HDMI TX, allocates
//! a framebuffer, and injects a `simple-framebuffer` node into the DTB.
//! We parse that node to find the address, dimensions, and stride, then
//! write pixels directly.  No DC8200 register programming needed.
//!
//! # Fallback
//!
//! Some U-Boot builds don't inject the SimpleFB node.  In that case we
//! use hardcoded values confirmed via `bdinfo`:
//!   - FB base = 0xFE000000
//!   - 1920×1080 @ 32bpp
//!   - Stride = 7680 (probed from DC8200 hardware register)
//!
//! # Bug Fixes (from C version)
//!
//! 1. **buffers[] NULL crash**: Both buffer slots must point to the physical
//!    framebuffer address.  SimpleFB has no double buffering.
//! 2. **Fallback address**: Confirmed 0xFE000000 via U-Boot `bdinfo`.
//! 3. **Stride mismatch**: Probed from DC8200 stride register, not assumed.

use hal::display::*;
use hal::types::{HalError, HalResult};
use crate::{uart, cache};

// ============================================================================
// FDT (Flattened Device Tree) Constants
// ============================================================================

const FDT_MAGIC: u32       = 0xD00DFEED;
const FDT_BEGIN_NODE: u32  = 0x00000001;
const FDT_END_NODE: u32    = 0x00000002;
const FDT_PROP: u32        = 0x00000003;
const FDT_NOP: u32         = 0x00000004;
const FDT_END: u32         = 0x00000009;

/// SimpleFB information extracted from the DTB.
struct SimpleFbInfo {
    base_addr: u64,
    size: u64,
    width: u32,
    height: u32,
    stride: u32,
    bpp: u32,
    found: bool,
}

// ============================================================================
// Minimal FDT Parsing
// ============================================================================

/// Read a big-endian 32-bit value from a byte pointer.
#[inline]
fn fdt_r32(p: *const u8) -> u32 {
    unsafe {
        ((p.read() as u32) << 24)
            | ((p.add(1).read() as u32) << 16)
            | ((p.add(2).read() as u32) << 8)
            | (p.add(3).read() as u32)
    }
}

/// Compare two byte slices up to `n` bytes (like C's strncmp returning bool).
fn fdt_strncmp(a: *const u8, b: &[u8]) -> bool {
    for (i, &expected) in b.iter().enumerate() {
        let actual = unsafe { a.add(i).read() };
        if actual != expected {
            return false;
        }
        if actual == 0 {
            return true;
        }
    }
    true
}

/// Parse the DTB for a `simple-framebuffer` node.
fn parse_simplefb_from_dtb(dtb: *const u8) -> SimpleFbInfo {
    let mut info = SimpleFbInfo {
        base_addr: 0,
        size: 0,
        width: 0,
        height: 0,
        stride: 0,
        bpp: 32,
        found: false,
    };

    if dtb.is_null() {
        return info;
    }

    // Validate FDT magic
    let magic = fdt_r32(dtb);
    if magic != FDT_MAGIC {
        uart::puts(b"[simplefb] ERROR: Invalid DTB magic\n");
        return info;
    }

    let struct_off = fdt_r32(unsafe { dtb.add(8) }) as usize;
    let strings_off = fdt_r32(unsafe { dtb.add(12) }) as usize;

    let struct_base = unsafe { dtb.add(struct_off) };
    let strings_base = unsafe { dtb.add(strings_off) };

    let mut p = struct_base;
    let mut in_simplefb = false;
    let mut depth: i32 = 0;

    loop {
        let token = fdt_r32(p);
        p = unsafe { p.add(4) };

        match token {
            FDT_BEGIN_NODE => {
                depth += 1;
                // Skip node name (null-terminated, 4-byte aligned)
                let mut name_len = 0u32;
                while unsafe { p.add(name_len as usize).read() } != 0 {
                    name_len += 1;
                }
                p = unsafe { p.add(((name_len + 4) & !3) as usize) };
            }

            FDT_END_NODE => {
                if in_simplefb && depth == 1 {
                    in_simplefb = false;
                    if info.found {
                        return info;
                    }
                }
                depth -= 1;
                if depth < 0 {
                    return info;
                }
            }

            FDT_PROP => {
                let val_size = fdt_r32(p) as usize;
                p = unsafe { p.add(4) };
                let name_off = fdt_r32(p) as usize;
                p = unsafe { p.add(4) };
                let val = p;
                p = unsafe { p.add((val_size + 3) & !3) };

                let prop_name = unsafe { strings_base.add(name_off) };

                // Check for compatible = "simple-framebuffer"
                if fdt_strncmp(prop_name, b"compatible\0") && val_size >= 18 {
                    if fdt_strncmp(val, b"simple-framebuffer\0") {
                        in_simplefb = true;
                        info.found = false;
                    }
                }

                if !in_simplefb {
                    continue;
                }

                // Extract properties
                if fdt_strncmp(prop_name, b"reg\0") && val_size >= 8 {
                    if val_size >= 16 {
                        // 64-bit address cells
                        info.base_addr = ((fdt_r32(val) as u64) << 32)
                            | (fdt_r32(unsafe { val.add(4) }) as u64);
                        info.size = ((fdt_r32(unsafe { val.add(8) }) as u64) << 32)
                            | (fdt_r32(unsafe { val.add(12) }) as u64);
                    } else {
                        // 32-bit address cells
                        info.base_addr = fdt_r32(val) as u64;
                        info.size = fdt_r32(unsafe { val.add(4) }) as u64;
                    }
                    info.found = info.width > 0 && info.height > 0;
                } else if fdt_strncmp(prop_name, b"width\0") && val_size == 4 {
                    info.width = fdt_r32(val);
                    info.found = info.base_addr > 0 && info.height > 0;
                } else if fdt_strncmp(prop_name, b"height\0") && val_size == 4 {
                    info.height = fdt_r32(val);
                    info.found = info.base_addr > 0 && info.width > 0;
                } else if fdt_strncmp(prop_name, b"stride\0") && val_size == 4 {
                    info.stride = fdt_r32(val);
                } else if fdt_strncmp(prop_name, b"format\0") && val_size > 0 {
                    // Determine BPP from format string
                    if fdt_strncmp(val, b"a8r8g8b8") || fdt_strncmp(val, b"x8r8g8b8") {
                        info.bpp = 32;
                    } else if fdt_strncmp(val, b"r5g6b5") {
                        info.bpp = 16;
                    } else {
                        info.bpp = 32;
                    }
                }
            }

            FDT_NOP => {}

            FDT_END => return info,

            _ => {
                uart::puts(b"[simplefb] ERROR: Unknown FDT token\n");
                return info;
            }
        }
    }
}

// ============================================================================
// JH7110 Display
// ============================================================================

/// JH7110 SimpleFB display implementation.
pub struct Jh7110Display {
    initialized: bool,
    width: u32,
    height: u32,
    pitch: u32,
    base: *mut u32,
    size: u32,
    dtb: *const u8,
}

// Safety: The framebuffer pointer is hardware-mapped memory not aliased by
// Rust-managed allocations, and we're bare-metal with no threads.
unsafe impl Send for Jh7110Display {}

impl Jh7110Display {
    /// Create a new uninitialized display instance.
    ///
    /// The `dtb` pointer comes from `__dtb_ptr` set by `common_init.S`.
    pub const fn new(dtb: *const u8) -> Self {
        Self {
            initialized: false,
            width: 0,
            height: 0,
            pitch: 0,
            base: core::ptr::null_mut(),
            size: 0,
            dtb,
        }
    }
}

impl Display for Jh7110Display {
    fn init(&mut self, _config: Option<&DisplayConfig>) -> HalResult<FramebufferInfo> {
        if self.initialized {
            return Err(HalError::AlreadyInitialized);
        }

        uart::puts(b"[simplefb] Parsing DTB for simple-framebuffer...\n");

        let mut info = parse_simplefb_from_dtb(self.dtb);

        if !info.found {
            uart::puts(b"[simplefb] No simple-framebuffer node in DTB\n");
            uart::puts(b"[simplefb] Falling back to hardcoded values\n");

            // Fallback confirmed via U-Boot `bdinfo` on Milk-V Mars 8GB
            info.base_addr = 0xFE00_0000;
            info.width = 1920;
            info.height = 1080;
            info.bpp = 32;
            info.size = 1920 * 1080 * 4;
            info.stride = 7680;
            info.found = true;
        }

        // Sanity checks
        if info.base_addr == 0 || info.width == 0 || info.height == 0 {
            uart::puts(b"[simplefb] ERROR: Invalid framebuffer parameters\n");
            return Err(HalError::HardwareFault);
        }

        // Final stride fallback
        if info.stride == 0 {
            info.stride = info.width * (info.bpp / 8);
        }

        // Log parameters
        uart::puts(b"[simplefb] Framebuffer:\n");
        uart::puts(b"  Address: ");
        uart::puthex(info.base_addr);
        uart::putc(b'\n');
        uart::puts(b"  ");
        uart::putdec(info.width);
        uart::putc(b'x');
        uart::putdec(info.height);
        uart::puts(b" @ ");
        uart::putdec(info.bpp);
        uart::puts(b"bpp  stride=");
        uart::putdec(info.stride);
        uart::putc(b'\n');

        // Store in our state
        self.base = info.base_addr as *mut u32;
        self.width = info.width;
        self.height = info.height;
        self.pitch = info.stride;
        self.size = info.stride * info.height;
        self.initialized = true;

        uart::puts(b"[simplefb] framebuffer OK\n");

        Ok(FramebufferInfo {
            width: info.width,
            height: info.height,
            pitch: info.stride,
            bits_per_pixel: info.bpp,
            display_type: DisplayType::Hdmi,
            format: PixelFormat::Argb8888,
            base: self.base,
            size: self.size,
            // SimpleFB has no double buffering — single buffer
            buffer_count: 1,
        })
    }

    fn shutdown(&mut self) -> HalResult<()> {
        // SimpleFB has nothing to shut down — the DC8200 keeps scanning.
        self.initialized = false;
        Ok(())
    }

    fn is_initialized(&self) -> bool {
        self.initialized
    }

    fn width(&self) -> u32 {
        self.width
    }

    fn height(&self) -> u32 {
        self.height
    }

    fn pitch(&self) -> u32 {
        self.pitch
    }

    fn present(&mut self) -> HalResult<u32> {
        if !self.initialized {
            return Err(HalError::NotInitialized);
        }

        // Flush the framebuffer from L2 cache to DRAM so the DC8200
        // display controller DMA can see our writes.
        cache::l2_flush_range(self.base as usize, self.size as usize);

        // Fence to ensure all stores are visible
        crate::cpu::dsb();

        // SimpleFB single buffer — always return buffer index 0
        Ok(0)
    }

    fn present_immediate(&mut self) -> HalResult<u32> {
        // No vsync on SimpleFB — present is always immediate
        self.present()
    }

    fn default_config(&self) -> DisplayConfig {
        DisplayConfig {
            width: 1920,
            height: 1080,
            display_type: DisplayType::Hdmi,
            format: PixelFormat::Argb8888,
            buffer_count: 1,
            vsync: false,
        }
    }
}
