/*
 * allocator.h - TLSF-Inspired Memory Allocator
 * =============================================
 *
 * A fast, low-fragmentation allocator for bare-metal environments.
 * "TLSF" stands for Two-Level Segregated Fit, a well-known O(1) allocator.
 *
 * WHY A CUSTOM ALLOCATOR?
 * -----------------------
 * In bare-metal, there's no OS to provide malloc/free. We must manage
 * memory ourselves. This allocator provides:
 *   - O(1) allocation and deallocation
 *   - Low fragmentation through size-class segregation
 *   - Immediate coalescing of adjacent free blocks
 *   - No external dependencies
 *
 * HOW IT WORKS:
 * -------------
 * The heap is divided into blocks. Each block has a header containing:
 *   - Size (with flags in low bits since sizes are aligned)
 *   - Free/allocated status
 *   - Whether previous block is free (for coalescing)
 *
 * Free blocks are organized into "segregated free lists" by size class:
 *   - Class 0: 32-64 bytes
 *   - Class 1: 64-128 bytes
 *   - Class 2: 128-256 bytes
 *   - ... and so on
 *
 * A bitmap tracks which classes have free blocks, enabling O(1) lookup.
 *
 * ALLOCATION STRATEGY:
 * --------------------
 *   1. Calculate required size (payload + header, aligned)
 *   2. Determine size class
 *   3. Use bitmap to find class with available block
 *   4. Remove block from free list
 *   5. Split if block is much larger than needed
 *   6. Return pointer to payload area
 *
 * DEALLOCATION STRATEGY:
 * ----------------------
 *   1. Get block header from payload pointer
 *   2. Mark block as free
 *   3. Coalesce with adjacent free blocks if possible
 *   4. Insert into appropriate free list
 *
 * MEMORY LAYOUT:
 * --------------
 *   +----------------------------------------------------+
 *   |                      HEAP                          |
 *   |  +---------+---------+---------+---------+-----+  |
 *   |  | Block 1 | Block 2 | Block 3 | Block 4 | ... |  |
 *   |  +---------+---------+---------+---------+-----+  |
 *   +----------------------------------------------------+
 *
 *   Each block:
 *   +----------+---------------------+----------+
 *   |  Header  |      Payload        | [Footer] |
 *   | (8 bytes)|   (user data)       | (if free)|
 *   +----------+---------------------+----------+
 *
 * ALIGNMENT:
 * ----------
 * All blocks are 8-byte aligned. This is required for ARM64 and
 * provides space for 2 flag bits in the size field.
 *
 * THREAD SAFETY:
 * --------------
 * This implementation is NOT thread-safe. In a multi-core system,
 * you'd need to add spinlocks around allocation/deallocation.
 */

#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "types.h"  /* Bare-metal type definitions */

/* =============================================================================
 * CONFIGURATION
 * =============================================================================
 */

/*
 * Minimum block size
 *
 * A block must hold at least:
 *   - Header (8 bytes)
 *   - Free list pointers when free (16 bytes)
 *   - Footer when free (8 bytes)
 * Total: 32 bytes minimum
 */
#define MIN_BLOCK_SIZE      32

/*
 * Number of size classes
 *
 * Each class covers blocks of size [2^(n+5), 2^(n+6))
 *   Class 0: 32-64 bytes
 *   Class 1: 64-128 bytes
 *   ...
 *   Class 25: 1GB-2GB
 *
 * 26 classes covers up to 2GB blocks, more than enough.
 */
#define NUM_SIZE_CLASSES    26

/*
 * Minimum alignment
 *
 * All allocations are aligned to 8 bytes. This:
 *   - Satisfies ARM64 alignment requirements
 *   - Leaves low 3 bits free for flags
 */
#define MIN_ALIGN           8

/*
 * Header size
 *
 * Each block starts with an 8-byte header containing size + flags.
 */
#define HEADER_SIZE         8

/*
 * Footer size
 *
 * Free blocks have a footer (copy of size) for backwards coalescing.
 */
#define FOOTER_SIZE         8


/* =============================================================================
 * BLOCK FLAGS
 * =============================================================================
 *
 * Flags are stored in the low 2 bits of the size field.
 * This works because all sizes are 8-byte aligned.
 */

#define FLAG_FREE           0x01    /* Block is free (not allocated) */
#define FLAG_PREV_FREE      0x02    /* Previous block is free */
#define FLAG_MASK           0x03    /* Mask for flag bits */


/* =============================================================================
 * JIT REGION
 * =============================================================================
 *
 * We reserve space at the end of RAM for JIT-compiled code.
 * The heap ends where the JIT region begins.
 */

#define JIT_SIZE            (4 * 1024 * 1024)   /* 4 MB for JIT */


/* =============================================================================
 * DATA STRUCTURES
 * =============================================================================
 */

/*
 * Allocator state
 *
 * Contains all state needed for the allocator. In the Rust version,
 * this uses UnsafeCell for interior mutability. In C, we just use
 * regular fields (with the understanding that thread safety is the
 * caller's responsibility).
 */
typedef struct {
    /* Bitmap: bit N set means free_lists[N] is non-empty */
    uint32_t free_bitmap;

    /* Segregated free lists by size class */
    void *free_lists[NUM_SIZE_CLASSES];

    /* Heap bounds */
    uintptr_t heap_start;
    uintptr_t heap_end;

    /* Statistics */
    size_t allocated;
    size_t free_space;

    /* Initialization flag */
    bool initialized;
} allocator_t;


/* =============================================================================
 * PUBLIC API
 * =============================================================================
 */

/*
 * allocator_init() - Initialize the allocator with memory region
 *
 * Must be called once before any allocations.
 *
 * @param alloc       Pointer to allocator state
 * @param heap_start  Start address of heap region
 * @param heap_end    End address of heap region (exclusive)
 */
void allocator_init(allocator_t *alloc, uintptr_t heap_start, uintptr_t heap_end);

/*
 * allocator_init_from_ram() - Initialize with detected RAM bounds
 *
 * Convenience function that sets up heap and JIT regions based on
 * RAM detected at boot time.
 *
 * @param alloc      Pointer to allocator state
 * @param ram_base   Physical address where RAM starts
 * @param ram_size   Total RAM size in bytes
 * @param heap_symbol  Address of __heap_start linker symbol
 *
 * Returns: Heap bounds as (heap_start, heap_end, jit_start, jit_end)
 *          in the provided output parameters
 */
void allocator_init_from_ram(allocator_t *alloc,
                             uintptr_t ram_base, size_t ram_size,
                             uintptr_t heap_symbol,
                             uintptr_t *heap_start_out, uintptr_t *heap_end_out,
                             uintptr_t *jit_start_out, uintptr_t *jit_end_out);

/*
 * allocator_alloc() - Allocate memory
 *
 * @param alloc  Pointer to allocator state
 * @param size   Number of bytes to allocate
 * @param align  Alignment requirement (must be power of 2, >= 8)
 *
 * Returns: Pointer to allocated memory, or NULL if allocation failed
 */
void *allocator_alloc(allocator_t *alloc, size_t size, size_t align);

/*
 * allocator_free() - Free previously allocated memory
 *
 * @param alloc  Pointer to allocator state
 * @param ptr    Pointer returned by allocator_alloc() (NULL is safe)
 */
void allocator_free(allocator_t *alloc, void *ptr);

/*
 * allocator_realloc() - Reallocate memory
 *
 * @param alloc     Pointer to allocator state
 * @param ptr       Previous allocation (or NULL for new allocation)
 * @param new_size  New size in bytes
 *
 * Returns: Pointer to reallocated memory, or NULL if failed
 *          (original pointer still valid on failure)
 */
void *allocator_realloc(allocator_t *alloc, void *ptr, size_t new_size);

/*
 * allocator_stats() - Get allocation statistics
 *
 * @param alloc           Pointer to allocator state
 * @param allocated_out   Output: bytes currently allocated
 * @param free_out        Output: bytes currently free
 */
void allocator_stats(const allocator_t *alloc, size_t *allocated_out, size_t *free_out);

/*
 * allocator_bounds() - Get heap bounds
 *
 * @param alloc       Pointer to allocator state
 * @param start_out   Output: heap start address
 * @param end_out     Output: heap end address
 */
void allocator_bounds(const allocator_t *alloc, uintptr_t *start_out, uintptr_t *end_out);

/*
 * allocator_is_initialized() - Check if allocator is ready
 *
 * @param alloc  Pointer to allocator state
 *
 * Returns: true if init has been called
 */
bool allocator_is_initialized(const allocator_t *alloc);


/* =============================================================================
 * GLOBAL ALLOCATOR
 * =============================================================================
 *
 * For convenience, we provide a global allocator instance and
 * malloc/free-style wrappers.
 */

/*
 * Global allocator instance
 *
 * Must be initialized before use with heap_init().
 */
extern allocator_t g_allocator;

/*
 * heap_init() - Initialize global allocator from RAM bounds
 *
 * @param ram_base   Physical address where RAM starts
 * @param ram_size   Total RAM size in bytes
 */
void heap_init(uintptr_t ram_base, size_t ram_size);

/*
 * heap_alloc() - Allocate from global allocator
 *
 * @param size  Number of bytes to allocate
 *
 * Returns: Pointer to allocated memory, or NULL on failure
 */
void *heap_alloc(size_t size);

/*
 * heap_alloc_aligned() - Allocate with specific alignment
 *
 * @param size   Number of bytes to allocate
 * @param align  Alignment requirement (power of 2)
 *
 * Returns: Pointer to aligned memory, or NULL on failure
 */
void *heap_alloc_aligned(size_t size, size_t align);

/*
 * heap_free() - Free memory from global allocator
 *
 * @param ptr  Pointer to free (NULL is safe)
 */
void heap_free(void *ptr);

/*
 * heap_realloc() - Reallocate from global allocator
 *
 * @param ptr   Previous allocation (or NULL)
 * @param size  New size in bytes
 *
 * Returns: Pointer to reallocated memory, or NULL on failure
 */
void *heap_realloc(void *ptr, size_t size);

#endif /* ALLOCATOR_H */
