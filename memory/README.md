# Memory - Dynamic Allocation Without an OS

In bare-metal programming, there's no `malloc()` or `free()` provided for you.
This directory contains a complete memory allocator built from scratch.

## Why Write an Allocator?

When you call `malloc(100)` in a normal program:
1. Your program asks the C library
2. The C library asks the OS kernel
3. The kernel finds free memory
4. The kernel returns a pointer

In bare-metal, **there is no OS**. We must:
1. Know where free memory is
2. Track what's allocated vs free
3. Handle requests efficiently
4. Avoid fragmentation

## Files

### allocator.h
- Public API definitions
- Configuration constants
- Data structures

### allocator.c
- TLSF-inspired allocator implementation
- O(1) allocation and deallocation
- Automatic coalescing of freed blocks

## How It Works

### Memory Layout
```
┌─────────────────────────────────────────────────────────────┐
│                         HEAP                                │
│  ┌─────────┬─────────┬─────────┬─────────┬───────────────┐  │
│  │ Block 1 │ Block 2 │ Block 3 │  FREE   │   Block 4     │  │
│  │  (used) │  (used) │ (FREE)  │         │    (used)     │  │
│  └─────────┴─────────┴─────────┴─────────┴───────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Block Structure
Each block has a header:
```
┌──────────────────────────────────────────┐
│ Header (8 bytes)                         │
│   - Size (with flags in low 2 bits)      │
│   - FREE flag (bit 0)                    │
│   - PREV_FREE flag (bit 1)               │
├──────────────────────────────────────────┤
│ Payload (user data)                      │
│   - If allocated: user's data            │
│   - If free: next/prev pointers          │
├──────────────────────────────────────────┤
│ Footer (8 bytes, only if free)           │
│   - Copy of size for backwards traversal │
└──────────────────────────────────────────┘
```

### Size Classes (Segregated Lists)
Free blocks are organized by size for fast lookup:
```
Class 0: [32, 64)    bytes  →  ■ → ■ → ■ → NULL
Class 1: [64, 128)   bytes  →  ■ → NULL
Class 2: [128, 256)  bytes  →  ■ → ■ → NULL
Class 3: [256, 512)  bytes  →  NULL (empty)
...
```

A bitmap tracks which classes have blocks:
```
Bitmap: 0b00000111  (classes 0, 1, 2 have free blocks)
```

### Allocation Algorithm
1. Calculate required size (payload + header, aligned)
2. Compute minimum size class
3. Check bitmap for classes with blocks ≥ needed size
4. Take first block from smallest suitable class
5. Split if much larger than needed
6. Mark as allocated, return pointer

### Deallocation Algorithm
1. Get block header from user pointer
2. Mark as free
3. Check if neighbors are free (coalesce)
4. Write footer (for backwards coalescing)
5. Insert into appropriate size class

## Complexity

| Operation | Time Complexity |
|-----------|-----------------|
| Allocate  | O(1)            |
| Free      | O(1)            |
| Coalesce  | O(1)            |

The bitmap trick makes finding a suitable block O(1) instead of O(n).

## Usage

```c
#include "allocator.h"

// Initialize from RAM detection
void kernel_main(void) {
    heap_init(ram_base, ram_size);
    
    // Now we can allocate!
    char *buffer = heap_alloc(1024);
    
    // Use the buffer...
    
    // Free when done
    heap_free(buffer);
}
```

## ⚠Important Notes

### Thread Safety
This allocator is **NOT** thread-safe. In a multi-core system, you'd need
to add spinlocks around allocation/deallocation.

### Fragmentation
Even with coalescing, external fragmentation can occur:
```
[used][free][used][free][used][free]  ← Can't allocate large block!
```

### No `realloc` Optimization
Our `realloc` always allocates new memory and copies. A production
allocator might try to grow the block in place.

## Further Reading

- "Two-Level Segregated Fit (TLSF)" - M. Masmano et al.
- "dlmalloc" - Doug Lea's classic allocator
- "jemalloc" - Facebook's production allocator
