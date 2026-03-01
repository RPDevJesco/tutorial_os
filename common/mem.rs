// mem.rs — Compiler-Required Memory Intrinsics for Bare-Metal
// ============================================================
//
// WHY THIS FILE EXISTS:
// ---------------------
// Even with #![no_std] and no dependencies, LLVM sometimes generates calls
// to memset/memcpy/memmove/memcmp for operations like:
//   - Struct initialization:  let x = MyStruct::default();
//   - Large struct copies / moves
//   - Array zeroing
//   - Passing large values by value
//
// Normally the `compiler_builtins` crate provides these symbols, but since
// we're dependency-free, we must supply them ourselves. Without these,
// the linker will fail with undefined reference errors.
//
// These implementations are intentionally simple and unoptimized for clarity.
// A production OS would use optimized assembly versions.
//
// IMPORTANT: These are #[no_mangle] extern "C" functions because LLVM emits
// calls using the C ABI — Rust's name mangling would make them invisible
// to the linker.


// =============================================================================
// memset — Fill memory with a byte value
// =============================================================================
//
// THE most commonly auto-generated function. LLVM uses it for:
//   - Zeroing structs/arrays
//   - Initializing large local variables
//   - Any block of memory that needs a uniform value
//
// Parameters:
//   s — Destination pointer
//   c — Byte value to fill (only low 8 bits used)
//   n — Number of bytes to fill
// Returns:  Original destination pointer

#[no_mangle]
pub unsafe extern "C" fn memset(s: *mut u8, c: i32, n: usize) -> *mut u8 {
    let byte = c as u8;
    let mut i = 0;
    while i < n {
        *s.add(i) = byte;
        i += 1;
    }
    s
}

// =============================================================================
// memcpy — Copy memory (non-overlapping regions only)
// =============================================================================
//
// LLVM uses this for struct assignments and passing large values by value.
//
// WARNING: Source and destination must not overlap!
//          Use memmove() for overlapping regions.
//
// Parameters:
//   dest — Destination pointer
//   src  — Source pointer
//   n    — Number of bytes to copy
// Returns:  Original destination pointer

#[no_mangle]
pub unsafe extern "C" fn memcpy(dest: *mut u8, src: *const u8, n: usize) -> *mut u8 {
    let mut i = 0;
    while i < n {
        *dest.add(i) = *src.add(i);
        i += 1;
    }
    dest
}

// =============================================================================
// memmove — Copy memory (handles overlapping regions)
// =============================================================================
//
// Safer than memcpy but slightly slower. LLVM may emit this when it can't
// prove that source and destination don't overlap.
//
// The direction trick: if dest is before src, copy forward (low to high).
// If dest is after src, copy backward (high to low). This prevents
// overwriting source data before it's been read.
//
// Parameters:
//   dest — Destination pointer
//   src  — Source pointer
//   n    — Number of bytes to copy
// Returns:  Original destination pointer

#[no_mangle]
pub unsafe extern "C" fn memmove(dest: *mut u8, src: *const u8, n: usize) -> *mut u8 {
    if (dest as usize) < (src as usize) {
        // Copy forward — dest is before src, no overlap risk going up
        let mut i = 0;
        while i < n {
            *dest.add(i) = *src.add(i);
            i += 1;
        }
    } else if (dest as usize) > (src as usize) {
        // Copy backward — dest is after src, must go high-to-low
        let mut i = n;
        while i > 0 {
            i -= 1;
            *dest.add(i) = *src.add(i);
        }
    }
    // If dest == src, nothing to do
    dest
}

// =============================================================================
// memcmp — Compare memory regions
// =============================================================================
//
// Less commonly auto-generated, but LLVM can emit it for derived PartialEq
// on structs with contiguous layout.
//
// Parameters:
//   s1 — First memory region
//   s2 — Second memory region
//   n  — Number of bytes to compare
// Returns:  0 if equal, <0 if s1<s2, >0 if s1>s2

#[no_mangle]
pub unsafe extern "C" fn memcmp(s1: *const u8, s2: *const u8, n: usize) -> i32 {
    let mut i = 0;
    while i < n {
        let a = *s1.add(i);
        let b = *s2.add(i);
        if a != b {
            return a as i32 - b as i32;
        }
        i += 1;
    }
    0
}