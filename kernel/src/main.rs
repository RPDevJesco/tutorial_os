//! Tutorial-OS: Hardware Inspector (Platform-Portable)
//!
//! Rust parity of kernel/main.c. Static panels drawn once, dynamic
//! panels (PROCESSOR, SYSTEM STATUS) redrawn every UPDATE_INTERVAL_MS.
//! Scales from 640×480 to 4K.

#![no_std]
#![no_main]

use hal::platform::{ClockId, MemoryInfo, Platform, PlatformId, PlatformInfo, ThrottleFlags};

const UPDATE_INTERVAL_MS: u64 = 1000;

// ============================================================================
// Number Formatting (no alloc)
// ============================================================================

fn u64_to_dec(val: u64, buf: &mut [u8; 20]) -> &[u8] {
    if val == 0 { buf[19] = b'0'; return &buf[19..20]; }
    let mut i = 19usize;
    let mut v = val;
    while v > 0 { buf[i] = b'0' + (v % 10) as u8; v /= 10; if i == 0 { break; } i -= 1; }
    &buf[i + 1..20]
}

fn u64_to_hex(val: u64, buf: &mut [u8; 20]) -> &[u8] {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    if val == 0 { buf[16] = b'0'; buf[17] = b'x'; buf[18] = b'0'; return &buf[16..19]; }
    let mut i = 19usize;
    let mut v = val;
    while v > 0 && i > 1 { i -= 1; buf[i] = HEX[(v & 0xF) as usize]; v >>= 4; }
    i -= 1; buf[i] = b'x'; i -= 1; buf[i] = b'0';
    &buf[i..19]
}

fn gcd(mut a: u32, mut b: u32) -> u32 { while b != 0 { let t = b; b = a % b; a = t; } a }

fn u32_to_dec_small(val: u32, buf: &mut [u8; 8]) -> &[u8] {
    if val == 0 { buf[7] = b'0'; return &buf[7..8]; }
    let mut i = 7usize; let mut v = val;
    while v > 0 { buf[i] = b'0' + (v % 10) as u8; v /= 10; if i == 0 { break; } i -= 1; }
    &buf[i + 1..8]
}

fn format_ratio(w: u32, h: u32, buf: &mut [u8; 16]) -> &[u8] {
    let g = gcd(w, h); let (rw, rh) = (w / g, h / g);
    let mut i = 0; let mut tmp = [0u8; 8];
    for &b in u32_to_dec_small(rw, &mut tmp) { buf[i] = b; i += 1; }
    buf[i] = b':'; i += 1;
    for &b in u32_to_dec_small(rh, &mut tmp) { buf[i] = b; i += 1; }
    &buf[..i]
}

// ============================================================================
// Layout
// ============================================================================

struct Layout { sx: u32, sy: u32, font_scale: u32, char_w: u32,
    margin: u32, col_gap: u32, full_w: u32, col_w: u32,
    left_x: u32, right_x: u32, pad: u32, row_h: u32,
    hdr_h: u32, panel_gap: u32, bar_w: u32, bar_h: u32,
    badge_h: u32, toast_w: u32, toast_h: u32 }

fn compute_layout(fw: u32, fh: u32) -> Layout {
    let sx = (fw / 640).max(1); let sy = (fh / 360).max(1);
    let fs = (sx / 2).clamp(1, 3);
    let m = 10*sx; let cg = 10*sx; let full = fw - 2*m; let cw = (full - cg) / 2;
    Layout { sx, sy, font_scale: fs, char_w: 8*fs, margin: m, col_gap: cg,
        full_w: full, col_w: cw, left_x: m, right_x: m + cw + cg,
        pad: 8*sx, row_h: 14*sy, hdr_h: 18*sy, panel_gap: 6*sy,
        bar_w: 80*sx, bar_h: 10*sy, badge_h: 14*sy, toast_w: 60*sx, toast_h: 14*sy }
}

fn ph(l: &Layout, rows: u32) -> u32 { l.hdr_h + rows * l.row_h + l.pad }

// ============================================================================
// Theme
// ============================================================================

struct Co { bg: u32, pbg: u32, pbrd: u32, tp: u32, ts: u32, acc: u32, abr: u32,
    ok: u32, warn: u32, err: u32, info: u32, brd: u32,
    bbg: u32, btx: u32, tok: u32, terr: u32 }
const T: Co = Co { bg:0xFF0F1117, pbg:0xFF1A1D27, pbrd:0xFF2A2D3A,
    tp:0xFFE8EAED, ts:0xFF8B8FA3, acc:0xFF60A5FA, abr:0xFF93C5FD,
    ok:0xFF4ADE80, warn:0xFFFBBD23, err:0xFFF87171, info:0xFF38BDF8,
    brd:0xFF333746, bbg:0xFF252836, btx:0xFFA5B4FC,
    tok:0xFF14532D, terr:0xFF7F1D1D };

// ============================================================================
// Drawing Helpers
// ============================================================================

type Fb = drivers::framebuffer::Framebuffer;

fn mstr(fb: &mut Fb, l: &Layout, x: u32, y: u32, s: &[u8], c: u32) {
    if l.font_scale <= 1 { fb.draw_string_transparent(x, y, s, c); }
    else { fb.draw_string_scaled(x, y, s, c, T.bg, l.font_scale); }
}
fn mtw(l: &Layout, s: &[u8]) -> u32 {
    let mut n = 0u32; for &c in s { if c == 0 { break; } n += 1; } n * l.char_w
}
fn panel(fb: &mut Fb, x: u32, y: u32, w: u32, h: u32) {
    fb.fill_rect(x, y, w, h, T.pbg); fb.draw_rect(x, y, w, h, T.pbrd);
}
fn hdr(fb: &mut Fb, l: &Layout, px: u32, py: u32, pw: u32, t: &[u8]) -> u32 {
    mstr(fb, l, px+l.pad, py+l.pad/2, t, T.acc);
    let dy = py + l.hdr_h - 2;
    fb.draw_hline(px+l.pad, dy, pw - 2*l.pad, T.brd); dy + l.pad/2
}
fn pbar(fb: &mut Fb, x: u32, y: u32, w: u32, h: u32, pct: u32, fc: u32) {
    fb.fill_rect(x, y, w, h, T.pbrd);
    if pct > 0 { fb.fill_rect(x, y, w * pct.min(100) / 100, h, fc); }
    fb.draw_rect(x, y, w, h, T.brd);
}
fn badge(fb: &mut Fb, l: &Layout, x: u32, y: u32, s: &[u8]) {
    let bw = mtw(l, s) + l.pad;
    fb.fill_rounded_rect(x, y, bw, l.badge_h, 3, T.bbg);
    mstr(fb, l, x+l.pad/2, y+2, s, T.btx);
}
fn toast(fb: &mut Fb, l: &Layout, x: u32, y: u32, s: &[u8], ok: bool) {
    let (bg, fg) = if ok { (T.tok, T.ok) } else { (T.terr, T.err) };
    fb.fill_rounded_rect(x, y, l.toast_w, l.toast_h, 3, bg);
    mstr(fb, l, x+l.pad/2, y+2, s, fg);
}
fn clock_row(fb: &mut Fb, l: &Layout, px: u32, y: u32, lab: &[u8], hz: u32, mhz_max: u32, bc: u32) {
    let mhz = hz / 1_000_000;
    let mx = if mhz_max > 0 { mhz_max / 1_000_000 } else { mhz };
    let pct = if mx > 0 { (mhz*100/mx).min(100) } else { 0 };
    mstr(fb, l, px+l.pad, y, lab, T.ts);
    let bx = px + l.pad + 5*l.char_w;
    pbar(fb, bx, y.wrapping_sub(1), l.bar_w, l.bar_h, pct, bc);
    let vx = bx + l.bar_w + l.pad/2;
    let mut buf = [0u8; 20];
    mstr(fb, l, vx, y, u64_to_dec(mhz as u64, &mut buf), T.tp);
    mstr(fb, l, vx + 5*l.char_w, y, b"MHz", T.ts);
}

// ============================================================================
// Platform Strings
// ============================================================================

fn cpu_core(id: PlatformId) -> &'static [u8] { match id {
    PlatformId::RpiZero2W|PlatformId::Rpi3B|PlatformId::Rpi3BPlus => b"Cortex-A53",
    PlatformId::Rpi4B|PlatformId::RpiCm4 => b"Cortex-A72",
    PlatformId::Rpi5|PlatformId::RpiCm5 => b"Cortex-A76",
    PlatformId::MilkVMars => b"SiFive U74", PlatformId::OrangePiRv2 => b"XuanTie C908",
    PlatformId::LattePandaMu|PlatformId::LattePandaIota => b"Alder Lake-N E", _ => b"Unknown" }}

fn arch_n(id: PlatformId) -> &'static [u8] { match id {
    PlatformId::RpiZero2W|PlatformId::Rpi3B|PlatformId::Rpi3BPlus
    |PlatformId::Rpi4B|PlatformId::RpiCm4|PlatformId::Rpi5|PlatformId::RpiCm5 => b"ARM64",
    PlatformId::MilkVMars|PlatformId::OrangePiRv2 => b"RISC-V",
    PlatformId::LattePandaMu|PlatformId::LattePandaIota => b"x86_64", _ => b"Unknown" }}

fn arch_i(id: PlatformId) -> &'static [u8] { match id {
    PlatformId::RpiZero2W|PlatformId::Rpi3B|PlatformId::Rpi3BPlus
    |PlatformId::Rpi4B|PlatformId::RpiCm4|PlatformId::Rpi5|PlatformId::RpiCm5 => b"AArch64",
    PlatformId::MilkVMars|PlatformId::OrangePiRv2 => b"RV64GC",
    PlatformId::LattePandaMu|PlatformId::LattePandaIota => b"AMD64", _ => b"Unknown" }}

fn disp_if(id: PlatformId, w: u32) -> &'static [u8] { match id {
    PlatformId::RpiZero2W|PlatformId::Rpi3B|PlatformId::Rpi3BPlus =>
        if w <= 640 { b"DPI RGB666 Parallel" } else { b"HDMI" },
    PlatformId::Rpi4B|PlatformId::RpiCm4|PlatformId::Rpi5|PlatformId::RpiCm5 => b"HDMI",
    PlatformId::MilkVMars => b"HDMI (SimpleFB)", PlatformId::OrangePiRv2 => b"HDMI (SimpleFB)",
    PlatformId::LattePandaMu|PlatformId::LattePandaIota => b"UEFI GOP", _ => b"Unknown" }}

fn stages(id: PlatformId) -> &'static [&'static [u8]] { match id {
    PlatformId::RpiZero2W|PlatformId::Rpi3B|PlatformId::Rpi3BPlus
    |PlatformId::Rpi4B|PlatformId::RpiCm4|PlatformId::Rpi5|PlatformId::RpiCm5 =>
        &[b"GPU FW",b"ATF (BL31)",b"U-Boot SPL",b"U-Boot",b"Kernel"],
    PlatformId::MilkVMars|PlatformId::OrangePiRv2 => &[b"SPL",b"OpenSBI",b"U-Boot",b"Kernel"],
    PlatformId::LattePandaMu|PlatformId::LattePandaIota => &[b"UEFI",b"BOOTX64.EFI"],
    _ => &[b"Kernel"] }}

// ============================================================================
// State
// ============================================================================

struct SS { pi: PlatformInfo, mi: MemoryInfo, arm_max: u32,
    tmax: i32, have_tmax: bool, cpu: &'static [u8], an: &'static [u8],
    ai: &'static [u8], di: &'static [u8] }

struct DS { arm: u32, core: u32, emmc: u32, temp: i32, have_t: bool, thr: u32, have_thr: bool }

// ============================================================================
// Static Panels
// ============================================================================
fn draw_static(fb: &mut Fb, l: &Layout, s: &SS) {
    let mut b = [0u8; 20]; let mut rb = [0u8; 16];
    let mut cy = l.margin;
    let (p2, p3, p4) = (ph(l,2), ph(l,3), ph(l,4));

    // TITLE
    { panel(fb, l.left_x, cy, l.full_w, p2);
        let t = b"Tutorial-OS: Hardware Inspector";
        mstr(fb, l, l.left_x+(l.full_w-mtw(l,t))/2, cy+l.pad, t, T.tp);
        let sub = s.pi.board_name.as_bytes();
        mstr(fb, l, l.left_x+(l.full_w-mtw(l,sub))/2, cy+p2-l.row_h, sub, T.ts);
        cy += p2 + l.panel_gap; }

    // BOARD
    { let (px,pw) = (l.left_x, l.col_w);
        panel(fb, px, cy, pw, p3);
        let mut y = hdr(fb, l, px, cy, pw, b"BOARD");
        mstr(fb,l,px+l.pad,y,b"Model:",T.ts);
        let soc = s.pi.soc_name.as_bytes();
        mstr(fb,l,px+l.pad+7*l.char_w,y,soc,T.tp);
        badge(fb,l,px+l.pad+7*l.char_w+mtw(l,soc)+l.char_w,y.wrapping_sub(2),s.cpu);
        y += l.row_h;
        mstr(fb,l,px+l.pad,y,b"Serial:",T.ts);
        if s.pi.serial_number != 0 {
            mstr(fb,l,px+l.pad+8*l.char_w,y,u64_to_hex(s.pi.serial_number,&mut b),T.tp);
        } else { mstr(fb,l,px+l.pad+8*l.char_w,y,b"N/A",T.ts); }
        y += l.row_h;
        mstr(fb,l,px+l.pad,y,b"Arch:",T.ts);
        let ax = px+l.pad+6*l.char_w;
        badge(fb,l,ax,y.wrapping_sub(2),s.an);
        badge(fb,l,ax+mtw(l,s.an)+2*l.char_w,y.wrapping_sub(2),s.ai); }

    // DISPLAY
    { let (px,pw) = (l.right_x, l.col_w);
        panel(fb, px, cy, pw, p3);
        let mut y = hdr(fb, l, px, cy, pw, b"DISPLAY");
        mstr(fb,l,px+l.pad,y,b"Resolution:",T.ts);
        let mut rx = px+l.pad+12*l.char_w;
        let ws = u64_to_dec(fb.width as u64, &mut b);
        mstr(fb,l,rx,y,ws,T.tp); rx += mtw(l,ws);
        mstr(fb,l,rx,y,b"x",T.ts); rx += l.char_w;
        let hs = u64_to_dec(fb.height as u64, &mut b);
        mstr(fb,l,rx,y,hs,T.tp); rx += mtw(l,hs);
        badge(fb,l,rx+l.char_w,y.wrapping_sub(2),format_ratio(fb.width,fb.height,&mut rb));
        y += l.row_h;
        mstr(fb,l,px+l.pad,y,b"Format:",T.ts);
        badge(fb,l,px+l.pad+8*l.char_w,y.wrapping_sub(2),b"ARGB8888");
        mstr(fb,l,px+l.pad+18*l.char_w,y,b"32-bit",T.ts);
        y += l.row_h;
        mstr(fb,l,px+l.pad,y,b"Interface:",T.ts);
        mstr(fb,l,px+l.pad+11*l.char_w,y,s.di,T.tp); }

    cy += p3 + l.panel_gap;

    // PERIPHERALS
    { let (px,pw) = (l.right_x, l.col_w);
        panel(fb, px, cy, pw, p4);
        let mut y = hdr(fb, l, px, cy, pw, b"PERIPHERALS");
        mstr(fb,l,px+l.pad,y,b"USB Host:",T.ts);
        toast(fb,l,px+l.pad+10*l.char_w,y.wrapping_sub(3),b"Active",true);
        y += l.row_h + 4*l.sy;
        mstr(fb,l,px+l.pad,y,b"SD Card:",T.ts);
        toast(fb,l,px+l.pad+10*l.char_w,y.wrapping_sub(3),b"Active",true);
        y += l.row_h + 4*l.sy;
        mstr(fb,l,px+l.pad,y,b"I2C:",T.ts);
        toast(fb,l,px+l.pad+10*l.char_w,y.wrapping_sub(3),b"Active",true); }

    cy += p4 + l.panel_gap;

    // MEMORY
    { let (px,pw) = (l.left_x, l.col_w);
        panel(fb, px, cy, pw, p3);
        let mut y = hdr(fb, l, px, cy, pw, b"MEMORY");
        let (arm, gpu) = (s.mi.arm_size, s.mi.gpu_size);
        let total = arm + gpu;
        if total > 0 && gpu > 0 {
            let (am,gm,tm) = ((arm>>20) as u32, (gpu>>20) as u32, (total>>20) as u32);
            let pct = if tm>0 { am*100/tm } else { 0 };
            mstr(fb,l,px+l.pad,y,b"Total:",T.ts);
            mstr(fb,l,px+l.pad+7*l.char_w,y,u64_to_dec(tm as u64,&mut b),T.tp);
            mstr(fb,l,px+l.pad+12*l.char_w,y,b"MB",T.ts);
            pbar(fb,px+l.pad+15*l.char_w,y.wrapping_sub(1),l.bar_w,l.bar_h,pct,T.info);
            y += l.row_h;
            mstr(fb,l,px+l.pad,y,b"RAM:",T.ts);
            mstr(fb,l,px+l.pad+5*l.char_w,y,u64_to_dec(am as u64,&mut b),T.tp);
            mstr(fb,l,px+l.pad+10*l.char_w,y,b"MB",T.ts);
            y += l.row_h;
            mstr(fb,l,px+l.pad,y,b"GPU:",T.ts);
            mstr(fb,l,px+l.pad+5*l.char_w,y,u64_to_dec(gm as u64,&mut b),T.tp);
            mstr(fb,l,px+l.pad+10*l.char_w,y,b"MB",T.ts);
        } else if arm > 0 {
            let mb = (arm>>20) as u32;
            mstr(fb,l,px+l.pad,y,b"Total:",T.ts);
            mstr(fb,l,px+l.pad+7*l.char_w,y,u64_to_dec(mb as u64,&mut b),T.tp);
            mstr(fb,l,px+l.pad+12*l.char_w,y,b"MB",T.ts);
            y += l.row_h;
            mstr(fb,l,px+l.pad,y,b"RAM:",T.ts);
            mstr(fb,l,px+l.pad+5*l.char_w,y,u64_to_dec(mb as u64,&mut b),T.tp);
            mstr(fb,l,px+l.pad+14*l.char_w,y,b"(unified)",T.ts);
        } else { mstr(fb,l,px+l.pad,y,b"Total: Unknown",T.ts); } }

    cy += p3 + l.panel_gap;

    // BOOT SEQUENCE
    { panel(fb, l.left_x, cy, l.full_w, p2);
        let y = hdr(fb, l, l.left_x, cy, l.full_w, b"BOOT SEQUENCE");
        let st = stages(s.pi.platform_id);
        let mut bx = l.left_x + l.pad;
        for (i, &s) in st.iter().enumerate() {
            if i > 0 { mstr(fb,l,bx,y,b"->",T.acc); bx += 3*l.char_w; }
            let c = if i == st.len()-1 { T.abr } else { T.tp };
            mstr(fb,l,bx,y,s,c); bx += mtw(l,s) + l.char_w;
        }
        mstr(fb,l,bx+l.char_w/2,y,b"(this code!)",T.ts); }
}

// ============================================================================
// Dynamic Panels
// ============================================================================
fn draw_dynamic(fb: &mut Fb, l: &Layout, s: &SS, d: &DS) {
    let mut b = [0u8; 20];
    let (p2,p3,p4) = (ph(l,2), ph(l,3), ph(l,4));
    let r1 = l.margin + p2 + l.panel_gap;
    let r2 = r1 + p3 + l.panel_gap;
    let r3 = r2 + p4 + l.panel_gap;

    // PROCESSOR
    { let (px,pw) = (l.left_x, l.col_w);
        panel(fb, px, r2, pw, p4);
        let mut y = hdr(fb, l, px, r2, pw, b"PROCESSOR");
        if d.arm > 0 { let mx = if s.arm_max>0 {s.arm_max} else {d.arm};
            clock_row(fb,l,px,y,b"CPU: ",d.arm,mx,T.acc);
        } else { mstr(fb,l,px+l.pad,y,b"CPU:  N/A",T.ts); } y += l.row_h;
        if d.core > 0 { clock_row(fb,l,px,y,b"Core:",d.core,d.core,T.ok);
        } else { mstr(fb,l,px+l.pad,y,b"Core: N/A",T.ts); } y += l.row_h;
        if d.emmc > 0 { clock_row(fb,l,px,y,b"eMMC:",d.emmc,d.emmc,T.warn);
        } else { mstr(fb,l,px+l.pad,y,b"eMMC: N/A",T.ts); } y += l.row_h;
        mstr(fb,l,px+l.pad,y,b"Frame:",T.ts);
        mstr(fb,l,px+l.pad+7*l.char_w,y,u64_to_dec(fb.frame_count,&mut b),T.ts); }

    // SYSTEM STATUS
    { let (px,pw) = (l.right_x, l.col_w);
        panel(fb, px, r3, pw, p3);
        let mut y = hdr(fb, l, px, r3, pw, b"SYSTEM STATUS");
        mstr(fb,l,px+l.pad,y,b"Temp:",T.ts);
        if d.have_t {
            let cel = d.temp / 1000; let hot = d.temp > 80000;
            if s.have_tmax && s.tmax > 0 {
                let pct = ((d.temp as u32)*100 / s.tmax as u32).min(100);
                let bx = px+l.pad+6*l.char_w;
                pbar(fb,bx,y.wrapping_sub(1),l.bar_w,l.bar_h,pct, if hot{T.err}else{T.ok});
                let vx = bx+l.bar_w+l.pad/2;
                mstr(fb,l,vx,y,u64_to_dec(cel as u64,&mut b), if hot{T.err}else{T.tp});
                mstr(fb,l,vx+4*l.char_w,y,b"C",T.ts);
            } else {
                mstr(fb,l,px+l.pad+6*l.char_w,y,u64_to_dec(cel as u64,&mut b),
                     if hot{T.err}else{T.tp});
                mstr(fb,l,px+l.pad+10*l.char_w,y,b"C",T.ts);
            }
        } else { mstr(fb,l,px+l.pad+6*l.char_w,y,b"(no sensor)",T.ts); }
        y += l.row_h;
        if d.have_thr {
            let th = d.thr & ThrottleFlags::THROTTLED_NOW != 0;
            let ca = d.thr & ThrottleFlags::ARM_FREQ_CAPPED != 0;
            mstr(fb,l,px+l.pad,y,b"Throttle:",T.ts);
            mstr(fb,l,px+l.pad+10*l.char_w,y, if th { b"YES" as &[u8] } else { b"NO" },
                 if th{T.err}else{T.ok});
            mstr(fb,l,px+l.pad+15*l.char_w,y,b"Cap:",T.ts);
            mstr(fb,l,px+l.pad+19*l.char_w,y, if ca { b"YES" as &[u8] } else { b"NO" },
                 if ca{T.err}else{T.ok});
        } else { mstr(fb,l,px+l.pad,y,b"Throttle: N/A",T.ts); } }
}

// ============================================================================
// Entry
// ============================================================================


#[no_mangle]
pub extern "C" fn kernel_main(_: usize, _: usize, _: usize) -> ! {
    #[cfg(feature = "board-rpi-zero2w")]
    boot_os(soc_bcm2710::Bcm2710Platform::new());

    #[cfg(feature = "board-milkv-mars")]
    boot_os(soc_jh7110::Jh7110Platform::new());

    idle()
}

fn boot_os<T: Platform>(mut platform: T) -> ! {
    use hal::display::Display;
    use hal::timer::Timer;

    if platform.init().is_err() { idle(); }

    let mut disp = platform.create_display();
    let fbi = match disp.init(None) { Ok(i) => i, Err(_) => idle() };

    let mut fb = drivers::framebuffer::Framebuffer::new();
    fb.width = fbi.width;
    fb.height = fbi.height;
    fb.pitch = fbi.pitch;
    fb.virtual_height = fbi.height * fbi.buffer_count;
    fb.buffer_size = (fbi.pitch * fbi.height) as usize;
    fb.buffers[0] = fbi.base;
    if fbi.buffer_count > 1 {
        fb.buffers[1] = unsafe { fbi.base.add((fb.buffer_size / 4) as usize) };
        fb.back_buffer = 1;
    } else {
        fb.buffers[1] = fbi.base;
    }
    fb.addr = fb.buffers[fb.back_buffer as usize];
    fb.clip_stack[0] = drivers::framebuffer::ClipRect {
        x: 0, y: 0, w: fbi.width, h: fbi.height,
    };
    fb.vsync_enabled = fbi.buffer_count > 1;
    fb.initialized = true;

    let l = compute_layout(fb.width, fb.height);
    let pid = platform.platform_id();
    let ss = SS {
        pi: platform.info(),
        mi: platform.memory_info().unwrap_or(MemoryInfo {
            arm_base: 0, arm_size: 0, peripheral_base: 0, gpu_base: 0, gpu_size: 0,
        }),
        arm_max: platform.arm_freq_hz(),
        tmax: platform.max_temperature_mc().unwrap_or(0),
        have_tmax: platform.max_temperature_mc().is_ok(),
        cpu: cpu_core(pid),
        an: arch_n(pid),
        ai: arch_i(pid),
        di: disp_if(pid, fb.width),
    };

    // --- First buffer ---
    fb.clear(T.bg);
    draw_static(&mut fb, &l, &ss);

    let poll = |p: &T, prev: &DS| {
        let arm  = p.arm_freq_hz();
        let core = p.clock_rate(ClockId::Core);
        let emmc = p.clock_rate(ClockId::Emmc);
        let temp = p.temperature_mc();
        let thr  = p.throttle_status();

        DS {
            arm:  if arm  != 0 { arm }  else { prev.arm },
            core: if core != 0 { core } else { prev.core },
            emmc: if emmc != 0 { emmc } else { prev.emmc },
            temp: temp.unwrap_or(prev.temp),
            have_t: temp.is_ok() || prev.have_t,
            thr: thr.map(|f| f.0).unwrap_or(prev.thr),
            have_thr: thr.is_ok() || prev.have_thr,
        }
    };

    let zero = DS { arm: 0, core: 0, emmc: 0, temp: 0, have_t: false, thr: 0, have_thr: false };

    let mut ds = poll(&platform, &zero);
    draw_dynamic(&mut fb, &l, &ss, &ds);
    if let Ok(back) = disp.present() { fb.swap_to(back); }

    // Double-buffered platforms need static content on both buffers.
    // Single-buffer: both slots point to the same address, so skip.
    if fbi.buffer_count > 1 {
        fb.clear(T.bg);
        draw_static(&mut fb, &l, &ss);
        draw_dynamic(&mut fb, &l, &ss, &ds);
        if let Ok(back) = disp.present() { fb.swap_to(back); }
    }

    // --- Main loop: poll dynamic data, redraw, present ---
    let tmr = platform.timer();
    loop {
        tmr.delay_ms(UPDATE_INTERVAL_MS);
        ds = poll(&platform, &ds);
        draw_dynamic(&mut fb, &l, &ss, &ds);
        if let Ok(back) = disp.present() { fb.swap_to(back); }
    }
}

fn idle() -> ! { loop { hal::cpu::wfi(); } }
#[panic_handler] fn panic(_: &core::panic::PanicInfo) -> ! { idle() }
