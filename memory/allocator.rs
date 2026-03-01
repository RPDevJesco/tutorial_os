//! TLSF-Inspired Allocator
//!
//! A fast, low-fragmentation allocator for bare-metal environments.
//! Heap bounds are determined at runtime based on DTB-detected RAM size.

use core::alloc::{GlobalAlloc, Layout};
use core::cell::UnsafeCell;
use core::ptr::{self, NonNull};

// ============================================================================
// Configuration
// ============================================================================

/// Minimum block size: header (8) + next (8) + prev (8) + footer (8) = 32
const MIN_BLOCK_SIZE: usize = 32;

/// Number of size classes (covers 32 bytes to 2GB)
const NUM_SIZE_CLASSES: usize = 26;

/// Minimum alignment
const MIN_ALIGN: usize = 8;

/// Size of block header
const HEADER_SIZE: usize = core::mem::size_of::<usize>();

/// Size of block footer (only present in free blocks)
const FOOTER_SIZE: usize = core::mem::size_of::<usize>();

// ============================================================================
// Block Header
// ============================================================================

/// Block header flags (stored in lower bits of size)
mod flags {
    pub const FREE: usize = 0b01;
    pub const PREV_FREE: usize = 0b10;
    pub const MASK: usize = 0b11;
}

/// Block header - present at start of every block
///
/// Size field stores block size with flags in lower 2 bits.
/// Actual size is always aligned to MIN_ALIGN (8), so lower bits are free.
#[repr(C)]
struct BlockHeader {
    size_and_flags: usize,
}

impl BlockHeader {
    #[inline]
    fn size(&self) -> usize {
        self.size_and_flags & !flags::MASK
    }

    #[inline]
    fn set_size(&mut self, size: usize) {
        self.size_and_flags = size | (self.size_and_flags & flags::MASK);
    }

    #[inline]
    fn is_free(&self) -> bool {
        self.size_and_flags & flags::FREE != 0
    }

    #[inline]
    fn set_free(&mut self, free: bool) {
        if free {
            self.size_and_flags |= flags::FREE;
        } else {
            self.size_and_flags &= !flags::FREE;
        }
    }

    #[inline]
    fn is_prev_free(&self) -> bool {
        self.size_and_flags & flags::PREV_FREE != 0
    }

    #[inline]
    fn set_prev_free(&mut self, prev_free: bool) {
        if prev_free {
            self.size_and_flags |= flags::PREV_FREE;
        } else {
            self.size_and_flags &= !flags::PREV_FREE;
        }
    }

    /// Pointer to payload (data area after header)
    #[inline]
    fn payload_ptr(&self) -> *mut u8 {
        unsafe { (self as *const Self as *mut u8).add(HEADER_SIZE) }
    }

    /// Get header from payload pointer
    #[inline]
    unsafe fn from_payload(payload: *mut u8) -> *mut Self {
        payload.sub(HEADER_SIZE) as *mut Self
    }

    /// Get next block in memory
    #[inline]
    unsafe fn next_block(&self) -> *mut Self {
        (self as *const Self as *mut u8).add(self.size()) as *mut Self
    }

    /// Get previous block using footer (only valid if prev_free flag set)
    #[inline]
    unsafe fn prev_block(&self) -> *mut Self {
        let footer_ptr = (self as *const Self as *const usize).sub(1);
        let prev_size = *footer_ptr;
        (self as *const Self as *mut u8).sub(prev_size) as *mut Self
    }
}

// ============================================================================
// Free Block List Pointers
// ============================================================================

/// Free list node - stored in payload area of free blocks
#[repr(C)]
struct FreeListNode {
    next: Option<NonNull<FreeListNode>>,
    prev: Option<NonNull<FreeListNode>>,
}

// ============================================================================
// Free Block Operations
// ============================================================================

struct FreeBlock;

impl FreeBlock {
    #[inline]
    unsafe fn list_node(header: *mut BlockHeader) -> *mut FreeListNode {
        (*header).payload_ptr() as *mut FreeListNode
    }

    #[inline]
    unsafe fn header_from_node(node: *mut FreeListNode) -> *mut BlockHeader {
        (node as *mut u8).sub(HEADER_SIZE) as *mut BlockHeader
    }

    #[inline]
    unsafe fn write_footer(header: *mut BlockHeader) {
        let size = (*header).size();
        let footer_ptr = (header as *mut u8).add(size).sub(FOOTER_SIZE) as *mut usize;
        *footer_ptr = size;
    }

    #[inline]
    fn size_class(size: usize) -> usize {
        if size <= MIN_BLOCK_SIZE {
            return 0;
        }
        let bits = usize::BITS - size.leading_zeros() - 1;
        let min_bits = usize::BITS - MIN_BLOCK_SIZE.leading_zeros() - 1;
        ((bits - min_bits) as usize).min(NUM_SIZE_CLASSES - 1)
    }
}

// ============================================================================
// Allocator
// ============================================================================

pub struct TlsfAllocator {
    /// Bitmap: bit N set means free_lists[N] is non-empty
    free_bitmap: UnsafeCell<u32>,

    /// Segregated free lists by size class
    free_lists: UnsafeCell<[Option<NonNull<FreeListNode>>; NUM_SIZE_CLASSES]>,

    /// Heap bounds
    heap_start: UnsafeCell<usize>,
    heap_end: UnsafeCell<usize>,

    /// Statistics
    allocated: UnsafeCell<usize>,
    free: UnsafeCell<usize>,

    /// Initialization flag
    initialized: UnsafeCell<bool>,
}

unsafe impl Sync for TlsfAllocator {}

impl TlsfAllocator {
    pub const fn new() -> Self {
        Self {
            free_bitmap: UnsafeCell::new(0),
            free_lists: UnsafeCell::new([None; NUM_SIZE_CLASSES]),
            heap_start: UnsafeCell::new(0),
            heap_end: UnsafeCell::new(0),
            allocated: UnsafeCell::new(0),
            free: UnsafeCell::new(0),
            initialized: UnsafeCell::new(false),
        }
    }

    /// Initialize with a memory region
    ///
    /// # Safety
    /// Must be called once before any allocations. Region must be valid.
    pub unsafe fn init(&self, heap_start: usize, heap_end: usize) {
        // Align heap bounds
        let start = align_up(heap_start, MIN_ALIGN);
        let end = heap_end & !(MIN_ALIGN - 1);
        let size = end - start;

        *self.heap_start.get() = start;
        *self.heap_end.get() = end;
        *self.free.get() = size;
        *self.allocated.get() = 0;

        // Create one large free block
        let header = start as *mut BlockHeader;
        (*header).size_and_flags = size | flags::FREE;

        let node = FreeBlock::list_node(header);
        (*node).next = None;
        (*node).prev = None;

        FreeBlock::write_footer(header);

        // Add to free list
        let class = FreeBlock::size_class(size);
        (*self.free_lists.get())[class] = NonNull::new(node);
        *self.free_bitmap.get() = 1 << class;

        *self.initialized.get() = true;
    }

    /// Check if allocator is initialized
    pub fn is_initialized(&self) -> bool {
        unsafe { *self.initialized.get() }
    }

    /// Find a free block >= requested size
    unsafe fn find_block(&self, size: usize) -> Option<*mut BlockHeader> {
        let min_class = FreeBlock::size_class(size);
        let bitmap = *self.free_bitmap.get();

        let available = bitmap & !((1 << min_class) - 1);
        if available == 0 {
            return None;
        }

        let class = available.trailing_zeros() as usize;
        let node = (*self.free_lists.get())[class]?;

        Some(FreeBlock::header_from_node(node.as_ptr()))
    }

    /// Remove a block from its free list
    unsafe fn remove_free(&self, header: *mut BlockHeader) {
        let size = (*header).size();
        let class = FreeBlock::size_class(size);
        let node = FreeBlock::list_node(header);

        if let Some(prev) = (*node).prev {
            (*prev.as_ptr()).next = (*node).next;
        } else {
            (*self.free_lists.get())[class] = (*node).next;
        }

        if let Some(next) = (*node).next {
            (*next.as_ptr()).prev = (*node).prev;
        }

        if (*self.free_lists.get())[class].is_none() {
            *self.free_bitmap.get() &= !(1 << class);
        }
    }

    /// Add a block to its free list
    unsafe fn insert_free(&self, header: *mut BlockHeader) {
        let size = (*header).size();
        let class = FreeBlock::size_class(size);
        let node = FreeBlock::list_node(header);

        let old_head = (*self.free_lists.get())[class];
        (*node).prev = None;
        (*node).next = old_head;

        if let Some(old) = old_head {
            (*old.as_ptr()).prev = NonNull::new(node);
        }

        (*self.free_lists.get())[class] = NonNull::new(node);
        *self.free_bitmap.get() |= 1 << class;
    }

    /// Split a block, returning remainder as a new free block (if large enough)
    unsafe fn split(&self, header: *mut BlockHeader, needed: usize) {
        let block_size = (*header).size();
        let remainder = block_size - needed;

        if remainder < MIN_BLOCK_SIZE {
            return;
        }

        (*header).set_size(needed);

        let rem_header = (header as *mut u8).add(needed) as *mut BlockHeader;
        (*rem_header).size_and_flags = remainder | flags::FREE;
        (*rem_header).set_prev_free(false);

        let rem_node = FreeBlock::list_node(rem_header);
        (*rem_node).next = None;
        (*rem_node).prev = None;

        FreeBlock::write_footer(rem_header);
        self.insert_free(rem_header);

        let next = (*rem_header).next_block() as usize;
        if next < *self.heap_end.get() {
            (*(next as *mut BlockHeader)).set_prev_free(true);
        }
    }

    /// Coalesce a free block with adjacent free blocks
    unsafe fn coalesce(&self, mut header: *mut BlockHeader) -> *mut BlockHeader {
        let heap_end = *self.heap_end.get();

        // Merge with next block if free
        let next_addr = (*header).next_block() as usize;
        if next_addr < heap_end {
            let next = next_addr as *mut BlockHeader;
            if (*next).is_free() {
                self.remove_free(next);
                let new_size = (*header).size() + (*next).size();
                (*header).set_size(new_size);
            }
        }

        // Merge with previous block if free
        if (*header).is_prev_free() {
            let prev = (*header).prev_block();
            self.remove_free(prev);
            let new_size = (*prev).size() + (*header).size();
            (*prev).set_size(new_size);
            header = prev;
        }

        FreeBlock::write_footer(header);

        let next_addr = (*header).next_block() as usize;
        if next_addr < heap_end {
            (*(next_addr as *mut BlockHeader)).set_prev_free(true);
        }

        header
    }

    /// Get usage statistics: (allocated_bytes, free_bytes)
    pub fn stats(&self) -> (usize, usize) {
        unsafe { (*self.allocated.get(), *self.free.get()) }
    }

    /// Get heap bounds: (start, end)
    pub fn bounds(&self) -> (usize, usize) {
        unsafe { (*self.heap_start.get(), *self.heap_end.get()) }
    }
}

unsafe impl GlobalAlloc for TlsfAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // Return null if not initialized
        if !*self.initialized.get() {
            return ptr::null_mut();
        }

        let align = layout.align().max(MIN_ALIGN);
        let payload_size = layout.size().max(core::mem::size_of::<FreeListNode>());

        let size = align_up(HEADER_SIZE + payload_size, MIN_ALIGN);
        let size = size.max(MIN_BLOCK_SIZE);

        let header = match self.find_block(size) {
            Some(h) => h,
            None => return ptr::null_mut(),
        };

        self.remove_free(header);
        self.split(header, size);

        (*header).set_free(false);

        let final_size = (*header).size();
        let next_addr = (*header).next_block() as usize;
        if next_addr < *self.heap_end.get() {
            (*(next_addr as *mut BlockHeader)).set_prev_free(false);
        }

        *self.allocated.get() += final_size;
        *self.free.get() -= final_size;

        (*header).payload_ptr()
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        if ptr.is_null() {
            return;
        }

        let header = BlockHeader::from_payload(ptr);
        let size = (*header).size();

        *self.allocated.get() -= size;
        *self.free.get() += size;

        (*header).set_free(true);

        let header = self.coalesce(header);
        self.insert_free(header);
    }
}

// ============================================================================
// Helpers
// ============================================================================

#[inline]
const fn align_up(addr: usize, align: usize) -> usize {
    (addr + align - 1) & !(align - 1)
}

// ============================================================================
// Global Instance
// ============================================================================

#[global_allocator]
pub static ALLOCATOR: TlsfAllocator = TlsfAllocator::new();

/// JIT region size (4MB) - must match linker script
pub const JIT_SIZE: usize = 0x0040_0000;

/// Initialize the heap with detected RAM bounds
///
/// # Safety
/// Call once before any allocations. `ram_base` and `ram_size` must be
/// the actual values detected from DTB parsing.
///
/// # Arguments
/// * `ram_base` - Physical address where RAM starts (from DTB)
/// * `ram_size` - Total RAM size in bytes (from DTB)
///
/// # Returns
/// Tuple of (heap_start, heap_end, jit_start, jit_end)
pub unsafe fn init(ram_base: usize, ram_size: usize) -> (usize, usize, usize, usize) {
    unsafe extern "C" {
        static __heap_start: u8;
    }

    let heap_start = &__heap_start as *const u8 as usize;

    // RAM end is base + size
    let ram_end = ram_base + ram_size;

    // JIT region at end of RAM
    let jit_start = ram_end - JIT_SIZE;
    let jit_end = ram_end;

    // Heap ends where JIT begins
    let heap_end = jit_start;

    ALLOCATOR.init(heap_start, heap_end);

    (heap_start, heap_end, jit_start, jit_end)
}

/// Initialize with explicit bounds (alternative API for custom layouts)
///
/// # Safety
/// Call once before any allocations. Does NOT set up JIT region -
/// caller is responsible for managing that separately.
pub unsafe fn init_with_bounds(heap_start: usize, heap_end: usize) {
    ALLOCATOR.init(heap_start, heap_end);
}

/// Get the JIT region size constant
pub const fn jit_size() -> usize {
    JIT_SIZE
}

/// Get heap statistics
pub fn stats() -> (usize, usize) {
    ALLOCATOR.stats()
}

/// Get heap bounds
pub fn bounds() -> (usize, usize) {
    ALLOCATOR.bounds()
}

/// Check if heap is initialized
pub fn is_initialized() -> bool {
    ALLOCATOR.is_initialized()
}