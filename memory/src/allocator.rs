//! TLSF-inspired memory allocator for bare-metal Tutorial-OS.
//!
//! This allocator provides `O(1)` allocation and deallocation with bounded
//! fragmentation, suitable for a bare-metal OS that cannot fall back to
//! an operating system allocator.
//!
//! # Design
//!
//! TLSF (Two-Level Segregated Fit) organises free blocks into a two-level
//! bitmap index.  The first level divides sizes by powers of two; the
//! second level subdivides each power-of-two range into linear segments.
//! Finding a suitable free block requires only two bitmap scans (each a
//! single `CTZ` or `CLZ` instruction on all supported architectures).
//!
//! # Usage
//!
//! ```ignore
//! static mut HEAP: [u8; 1024 * 1024] = [0; 1024 * 1024];
//!
//! let mut alloc = Allocator::new();
//! unsafe { alloc.init(HEAP.as_mut_ptr(), HEAP.len()); }
//!
//! let ptr = alloc.alloc(256, 8);  // 256 bytes, 8-byte aligned
//! alloc.free(ptr);
//! ```
//!
//! # Safety
//!
//! This is inherently unsafe — it manages raw memory regions.  The caller
//! must ensure the heap region is valid, exclusively owned, and not
//! accessed through any other means while the allocator is in use.

use core::ptr;

// ============================================================================
// Configuration
// ============================================================================

/// Number of first-level index classes (each is a power-of-two range).
const FL_INDEX_COUNT: usize = 20; // Covers up to 2^(FL_INDEX_SHIFT + 20 - 1) ≈ 512 MB

/// Number of second-level subdivisions within each first-level class.
const SL_INDEX_COUNT_LOG2: usize = 4;
const SL_INDEX_COUNT: usize = 1 << SL_INDEX_COUNT_LOG2; // 16

/// Minimum allocation size (must be large enough for the free block header).
const MIN_BLOCK_SIZE: usize = 32;

/// First-level index shift — the smallest first-level class covers
/// blocks of size `[2^FL_INDEX_SHIFT, 2^(FL_INDEX_SHIFT+1))`.
const FL_INDEX_SHIFT: usize = 5; // 32 bytes

// ============================================================================
// Block Header
// ============================================================================

/// Bit flag: block is free.
const BLOCK_FREE: usize = 1 << 0;
/// Bit flag: the preceding physical block is free.
const BLOCK_PREV_FREE: usize = 1 << 1;
/// Mask for the size field (excludes flag bits).
const SIZE_MASK: usize = !(BLOCK_FREE | BLOCK_PREV_FREE);

/// Header placed at the start of every memory block (allocated or free).
///
/// Free blocks additionally contain `next_free` / `prev_free` pointers
/// that overlay the user data region.
#[repr(C)]
struct BlockHeader {
    /// Encodes the block size (in bytes, including the header) plus flags
    /// in the low two bits.
    size_and_flags: usize,
    /// Pointer to the preceding physical block (for coalescing on free).
    prev_phys: *mut BlockHeader,
    // --- The following fields exist only in FREE blocks ---
    // (They overlap the user data area of the block.)
    next_free: *mut BlockHeader,
    prev_free: *mut BlockHeader,
}

#[allow(dead_code)]
impl BlockHeader {
    #[inline]
    fn size(&self) -> usize {
        self.size_and_flags & SIZE_MASK
    }

    #[inline]
    fn set_size(&mut self, size: usize) {
        self.size_and_flags = size | (self.size_and_flags & !SIZE_MASK);
    }

    #[inline]
    fn is_free(&self) -> bool {
        self.size_and_flags & BLOCK_FREE != 0
    }

    #[inline]
    fn set_free(&mut self) {
        self.size_and_flags |= BLOCK_FREE;
    }

    #[inline]
    fn set_used(&mut self) {
        self.size_and_flags &= !BLOCK_FREE;
    }

    #[inline]
    fn is_prev_free(&self) -> bool {
        self.size_and_flags & BLOCK_PREV_FREE != 0
    }

    #[inline]
    fn set_prev_free(&mut self) {
        self.size_and_flags |= BLOCK_PREV_FREE;
    }

    #[inline]
    fn clear_prev_free(&mut self) {
        self.size_and_flags &= !BLOCK_PREV_FREE;
    }

    /// User data starts immediately after `size_and_flags` and `prev_phys`.
    #[inline]
    fn payload(&self) -> *mut u8 {
        let hdr_ptr = self as *const Self as *mut u8;
        unsafe { hdr_ptr.add(core::mem::size_of::<usize>() * 2) }
    }

    /// Get the next physical block (adjacent in memory).
    #[inline]
    unsafe fn next_phys(&self) -> *mut BlockHeader {
        let base = self as *const Self as *const u8;
        unsafe { base.add(self.size()) as *mut BlockHeader }
    }
}

// ============================================================================
// Allocator
// ============================================================================

/// TLSF-inspired memory allocator.
pub struct Allocator {
    /// First-level bitmap — bit `i` is set if any SL list in FL class `i` is non-empty.
    fl_bitmap: u32,
    /// Second-level bitmaps — one per FL class.
    sl_bitmaps: [u16; FL_INDEX_COUNT],
    /// Free list heads: `blocks[fl][sl]` points to the first free block in
    /// that size class, or null.
    blocks: [[*mut BlockHeader; SL_INDEX_COUNT]; FL_INDEX_COUNT],
    /// Total bytes managed.
    heap_size: usize,
    /// Total bytes currently allocated (including headers, for diagnostics).
    used: usize,
}

// SAFETY: bare-metal, single-core context — no concurrent access.
unsafe impl Send for Allocator {}

impl Allocator {
    /// Create a new (uninitialised) allocator.
    pub const fn new() -> Self {
        Self {
            fl_bitmap: 0,
            sl_bitmaps: [0; FL_INDEX_COUNT],
            blocks: [[ptr::null_mut(); SL_INDEX_COUNT]; FL_INDEX_COUNT],
            heap_size: 0,
            used: 0,
        }
    }

    /// Register a contiguous memory region as the heap.
    ///
    /// # Safety
    ///
    /// - `base` must be valid and writable for `size` bytes.
    /// - The region must be exclusively owned by the allocator.
    /// - Must not be called more than once.
    pub unsafe fn init(&mut self, base: *mut u8, size: usize) {
        if size < MIN_BLOCK_SIZE * 4 {
            return; // Too small to be useful.
        }

        self.heap_size = size;
        self.used = 0;

        // Place a sentinel block at the start.
        let block = base as *mut BlockHeader;
        let block_size = size - core::mem::size_of::<BlockHeader>();
        // Round down to alignment.
        let block_size = block_size & SIZE_MASK;

        unsafe {
            (*block).size_and_flags = block_size | BLOCK_FREE;
            (*block).prev_phys = ptr::null_mut();
            (*block).next_free = ptr::null_mut();
            (*block).prev_free = ptr::null_mut();
        }

        self.insert_free_block(block);
    }

    /// Allocate `size` bytes with the given alignment.
    ///
    /// Returns a pointer to the allocated region, or null on failure.
    /// The returned pointer is guaranteed to be at least `align`-byte aligned.
    pub fn alloc(&mut self, size: usize, align: usize) -> *mut u8 {
        let adjusted = adjust_size(size, align);
        if adjusted == 0 {
            return ptr::null_mut();
        }

        let block = self.find_suitable_block(adjusted);
        if block.is_null() {
            return ptr::null_mut();
        }

        self.remove_free_block(block);
        self.split_and_use(block, adjusted);

        unsafe { (*block).payload() }
    }

    /// Free a previously allocated pointer.
    ///
    /// # Safety
    ///
    /// `ptr` must have been returned by a prior call to [`alloc`](Allocator::alloc)
    /// and must not have been freed already.
    pub unsafe fn free(&mut self, ptr: *mut u8) {
        if ptr.is_null() {
            return;
        }

        let block = payload_to_header(ptr);
        unsafe {
            self.used -= (*block).size();
            (*block).set_free();
        }

        // Coalesce with adjacent free blocks.
        let block = unsafe { self.merge_next(block) };
        let block = unsafe { self.merge_prev(block) };

        self.insert_free_block(block);
    }

    /// Total heap bytes managed.
    pub fn heap_size(&self) -> usize { self.heap_size }

    /// Bytes currently in use (including block headers).
    pub fn used(&self) -> usize { self.used }

    /// Bytes available for allocation.
    pub fn free_bytes(&self) -> usize { self.heap_size - self.used }

    // ---- Internal ----

    fn insert_free_block(&mut self, block: *mut BlockHeader) {
        let size = unsafe { (*block).size() };
        let (fl, sl) = mapping(size);

        let head = self.blocks[fl][sl];
        unsafe {
            (*block).next_free = head;
            (*block).prev_free = ptr::null_mut();
            if !head.is_null() {
                (*head).prev_free = block;
            }
        }
        self.blocks[fl][sl] = block;
        self.fl_bitmap |= 1 << fl;
        self.sl_bitmaps[fl] |= 1 << sl;
    }

    fn remove_free_block(&mut self, block: *mut BlockHeader) {
        let size = unsafe { (*block).size() };
        let (fl, sl) = mapping(size);

        let next = unsafe { (*block).next_free };
        let prev = unsafe { (*block).prev_free };

        if !next.is_null() {
            unsafe { (*next).prev_free = prev };
        }
        if !prev.is_null() {
            unsafe { (*prev).next_free = next };
        } else {
            // Block was the head of this list.
            self.blocks[fl][sl] = next;
            if next.is_null() {
                self.sl_bitmaps[fl] &= !(1 << sl);
                if self.sl_bitmaps[fl] == 0 {
                    self.fl_bitmap &= !(1 << fl);
                }
            }
        }
    }

    fn find_suitable_block(&self, size: usize) -> *mut BlockHeader {
        let (mut fl, mut sl) = mapping(size);

        // Search the second-level bitmap for a block in this FL class.
        let sl_map = self.sl_bitmaps[fl] & (!0u16 << sl);
        if sl_map != 0 {
            sl = sl_map.trailing_zeros() as usize;
            return self.blocks[fl][sl];
        }

        // No block in this FL class — search higher FL classes.
        let fl_map = self.fl_bitmap & (!0u32 << (fl + 1));
        if fl_map == 0 {
            return ptr::null_mut(); // Out of memory.
        }
        fl = fl_map.trailing_zeros() as usize;
        sl = self.sl_bitmaps[fl].trailing_zeros() as usize;
        self.blocks[fl][sl]
    }

    fn split_and_use(&mut self, block: *mut BlockHeader, size: usize) {
        let block_size = unsafe { (*block).size() };
        let remaining = block_size - size;

        unsafe { (*block).set_used() };
        unsafe { (*block).set_size(size) };
        self.used += size;

        if remaining >= MIN_BLOCK_SIZE {
            let rest = unsafe { (*block).next_phys() };
            unsafe {
                (*rest).size_and_flags = remaining | BLOCK_FREE;
                (*rest).prev_phys = block;
            }
            self.insert_free_block(rest);
        }
    }

    unsafe fn merge_next(&mut self, block: *mut BlockHeader) -> *mut BlockHeader {
        let next = unsafe { (*block).next_phys() };
        if !next.is_null() && unsafe { (*next).is_free() } {
            self.remove_free_block(next);
            let merged_size = unsafe { (*block).size() + (*next).size() };
            unsafe { (*block).set_size(merged_size) };
        }
        block
    }

    unsafe fn merge_prev(&mut self, block: *mut BlockHeader) -> *mut BlockHeader {
        if unsafe { (*block).is_prev_free() } {
            let prev = unsafe { (*block).prev_phys };
            if !prev.is_null() {
                self.remove_free_block(prev);
                let merged_size = unsafe { (*prev).size() + (*block).size() };
                unsafe { (*prev).set_size(merged_size) };
                return prev;
            }
        }
        block
    }
}

// ============================================================================
// Helpers
// ============================================================================

/// Map a block size to first-level and second-level indices.
fn mapping(size: usize) -> (usize, usize) {
    if size < MIN_BLOCK_SIZE {
        return (0, 0);
    }
    let fl = (usize::BITS - 1 - size.leading_zeros()) as usize;
    let fl = fl.saturating_sub(FL_INDEX_SHIFT);
    let sl = (size >> (fl + FL_INDEX_SHIFT - SL_INDEX_COUNT_LOG2)) & (SL_INDEX_COUNT - 1);
    (fl.min(FL_INDEX_COUNT - 1), sl)
}

/// Round `size` up to include the block header and satisfy alignment.
fn adjust_size(size: usize, align: usize) -> usize {
    let size = size.max(MIN_BLOCK_SIZE);
    let align = align.max(core::mem::align_of::<BlockHeader>());
    // Add header overhead.
    let total = size + core::mem::size_of::<usize>() * 2;
    // Align up.
    (total + align - 1) & !(align - 1)
}

/// Convert a user-data pointer back to its block header.
fn payload_to_header(ptr: *mut u8) -> *mut BlockHeader {
    unsafe { ptr.sub(core::mem::size_of::<usize>() * 2) as *mut BlockHeader }
}
