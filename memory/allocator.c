/*
 * allocator.c - TLSF-Inspired Memory Allocator Implementation
 * ============================================================
 *
 * See allocator.h for design overview and API documentation.
 *
 * IMPLEMENTATION DETAILS:
 * -----------------------
 *
 * BLOCK HEADER STRUCTURE:
 * The size_and_flags field packs the block size and status flags:
 *   Bits [63:2] = Size (always multiple of 8)
 *   Bit 1 = PREV_FREE (previous block in memory is free)
 *   Bit 0 = FREE (this block is free)
 *
 * FREE BLOCK LAYOUT:
 *   +------------------+
 *   | size_and_flags   |  Header (8 bytes)
 *   +------------------+
 *   | next_free        |  Pointer to next in free list (8 bytes)
 *   +------------------+
 *   | prev_free        |  Pointer to prev in free list (8 bytes)
 *   +------------------+
 *   |    ...           |  (unused space)
 *   +------------------+
 *   | footer (size)    |  Copy of size for backwards traversal (8 bytes)
 *   +------------------+
 *
 * ALLOCATED BLOCK LAYOUT:
 *   +------------------+
 *   | size_and_flags   |  Header (8 bytes)
 *   +------------------+
 *   |                  |
 *   |   User Data      |  (payload area)
 *   |                  |
 *   +------------------+
 *   (No footer needed - we only need footer for coalescing with free blocks)
 *
 * SIZE CLASS CALCULATION:
 * Given a block size, we compute its size class as:
 *   class = floor(log2(size)) - floor(log2(MIN_BLOCK_SIZE))
 *
 * This groups blocks into power-of-two buckets:
 *   Class 0: [32, 64)
 *   Class 1: [64, 128)
 *   Class 2: [128, 256)
 *   etc.
 */

#include "allocator.h"
#include "../common/string.h"

/* =============================================================================
 * INTERNAL DATA STRUCTURES
 * =============================================================================
 */

/*
 * Block header
 *
 * Present at the start of every block (allocated or free).
 */
typedef struct block_header {
    uintptr_t size_and_flags;
} block_header_t;

/*
 * Free list node
 *
 * Stored in the payload area of free blocks.
 * Forms a doubly-linked list for each size class.
 */
typedef struct free_node {
    struct free_node *next;
    struct free_node *prev;
} free_node_t;


/* =============================================================================
 * HELPER FUNCTIONS - ALIGNMENT
 * =============================================================================
 */

/*
 * align_up() - Round up to alignment boundary
 */
static inline uintptr_t align_up(uintptr_t addr, size_t align)
{
    return (addr + align - 1) & ~(align - 1);
}


/* =============================================================================
 * HELPER FUNCTIONS - BLOCK HEADER OPERATIONS
 * =============================================================================
 */

/*
 * block_size() - Get block size (excluding flags)
 */
static inline size_t block_size(const block_header_t *block)
{
    return block->size_and_flags & ~(uintptr_t)FLAG_MASK;
}

/*
 * block_set_size() - Set block size (preserving flags)
 */
static inline void block_set_size(block_header_t *block, size_t size)
{
    block->size_and_flags = size | (block->size_and_flags & FLAG_MASK);
}

/*
 * block_is_free() - Check if block is free
 */
static inline bool block_is_free(const block_header_t *block)
{
    return (block->size_and_flags & FLAG_FREE) != 0;
}

/*
 * block_set_free() - Mark block as free or allocated
 */
static inline void block_set_free(block_header_t *block, bool free)
{
    if (free) {
        block->size_and_flags |= FLAG_FREE;
    } else {
        block->size_and_flags &= ~(uintptr_t)FLAG_FREE;
    }
}

/*
 * block_is_prev_free() - Check if previous block is free
 */
static inline bool block_is_prev_free(const block_header_t *block)
{
    return (block->size_and_flags & FLAG_PREV_FREE) != 0;
}

/*
 * block_set_prev_free() - Mark whether previous block is free
 */
static inline void block_set_prev_free(block_header_t *block, bool prev_free)
{
    if (prev_free) {
        block->size_and_flags |= FLAG_PREV_FREE;
    } else {
        block->size_and_flags &= ~(uintptr_t)FLAG_PREV_FREE;
    }
}

/*
 * block_payload() - Get pointer to payload area
 */
static inline void *block_payload(block_header_t *block)
{
    return (uint8_t *)block + HEADER_SIZE;
}

/*
 * block_from_payload() - Get header from payload pointer
 */
static inline block_header_t *block_from_payload(void *payload)
{
    return (block_header_t *)((uint8_t *)payload - HEADER_SIZE);
}

/*
 * block_next() - Get next block in memory
 */
static inline block_header_t *block_next(block_header_t *block)
{
    return (block_header_t *)((uint8_t *)block + block_size(block));
}

/*
 * block_prev() - Get previous block (only valid if prev_free flag set)
 *
 * When a block is free, it has a footer (copy of size) at its end.
 * We can use this to find where the previous block started.
 */
static inline block_header_t *block_prev(block_header_t *block)
{
    /*
     * Footer is at (block - 8 bytes), contains size of previous block.
     * Previous block starts at (block - prev_size).
     */
    uintptr_t *footer = (uintptr_t *)block - 1;
    size_t prev_size = *footer;
    return (block_header_t *)((uint8_t *)block - prev_size);
}


/* =============================================================================
 * HELPER FUNCTIONS - FREE BLOCK OPERATIONS
 * =============================================================================
 */

/*
 * free_list_node() - Get free list node from block header
 */
static inline free_node_t *free_list_node(block_header_t *block)
{
    return (free_node_t *)block_payload(block);
}

/*
 * block_from_node() - Get block header from free list node
 */
static inline block_header_t *block_from_node(free_node_t *node)
{
    return (block_header_t *)((uint8_t *)node - HEADER_SIZE);
}

/*
 * write_footer() - Write size to block's footer
 *
 * The footer is used to find the previous block during coalescing.
 */
static inline void write_footer(block_header_t *block)
{
    size_t size = block_size(block);
    uintptr_t *footer = (uintptr_t *)((uint8_t *)block + size - FOOTER_SIZE);
    *footer = size;
}

/*
 * size_class() - Calculate size class for a block size
 *
 * Uses the position of the highest set bit to determine class.
 * Equivalent to floor(log2(size)) - floor(log2(MIN_BLOCK_SIZE))
 */
static inline size_t size_class(size_t size)
{
    if (size <= MIN_BLOCK_SIZE) {
        return 0;
    }

    /*
     * Count leading zeros to find highest bit position.
     * class = (bit_position(size) - bit_position(MIN_BLOCK_SIZE))
     *
     * MIN_BLOCK_SIZE = 32 = 2^5, so its highest bit is at position 5.
     * For size 64 (2^6), highest bit at 6, class = 6-5 = 1.
     * For size 128 (2^7), highest bit at 7, class = 7-5 = 2.
     */
    unsigned int bits = 0;
    size_t temp = size;
    while (temp > 1) {
        temp >>= 1;
        bits++;
    }

    unsigned int min_bits = 5;  /* log2(32) = 5 */
    size_t class = (bits > min_bits) ? (bits - min_bits) : 0;

    /* Clamp to valid range */
    if (class >= NUM_SIZE_CLASSES) {
        class = NUM_SIZE_CLASSES - 1;
    }

    return class;
}


/* =============================================================================
 * ALLOCATOR OPERATIONS - FREE LIST MANAGEMENT
 * =============================================================================
 */

/*
 * remove_from_free_list() - Remove a block from its free list
 */
static void remove_from_free_list(allocator_t *alloc, block_header_t *block)
{
    size_t size = block_size(block);
    size_t class = size_class(size);
    free_node_t *node = free_list_node(block);

    /* Update previous node's next pointer (or list head) */
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        /* This was the head of the list */
        alloc->free_lists[class] = node->next;
    }

    /* Update next node's prev pointer */
    if (node->next) {
        node->next->prev = node->prev;
    }

    /* If list is now empty, clear bitmap bit */
    if (alloc->free_lists[class] == NULL) {
        alloc->free_bitmap &= ~(1u << class);
    }
}

/*
 * insert_into_free_list() - Add a block to its free list
 */
static void insert_into_free_list(allocator_t *alloc, block_header_t *block)
{
    size_t size = block_size(block);
    size_t class = size_class(size);
    free_node_t *node = free_list_node(block);

    /* Insert at head of list */
    free_node_t *old_head = (free_node_t *)alloc->free_lists[class];
    node->prev = NULL;
    node->next = old_head;

    if (old_head) {
        old_head->prev = node;
    }

    alloc->free_lists[class] = node;

    /* Set bitmap bit */
    alloc->free_bitmap |= (1u << class);
}


/* =============================================================================
 * ALLOCATOR OPERATIONS - BLOCK SPLITTING AND COALESCING
 * =============================================================================
 */

/*
 * split_block() - Split a block if it's larger than needed
 *
 * If the remainder after allocation is large enough to be a valid block,
 * create a new free block with the leftover space.
 */
static void split_block(allocator_t *alloc, block_header_t *block, size_t needed)
{
    size_t current_size = block_size(block);
    size_t remainder = current_size - needed;

    /* Only split if remainder can form a valid block */
    if (remainder < MIN_BLOCK_SIZE) {
        return;
    }

    /* Shrink original block */
    block_set_size(block, needed);

    /* Create remainder block */
    block_header_t *rem = (block_header_t *)((uint8_t *)block + needed);
    rem->size_and_flags = remainder | FLAG_FREE;
    block_set_prev_free(rem, false);

    /* Initialize free list node for remainder */
    free_node_t *rem_node = free_list_node(rem);
    rem_node->next = NULL;
    rem_node->prev = NULL;

    /* Write footer for remainder block */
    write_footer(rem);

    /* Add remainder to free list */
    insert_into_free_list(alloc, rem);

    /* Update next block's prev_free flag */
    uintptr_t next_addr = (uintptr_t)block_next(rem);
    if (next_addr < alloc->heap_end) {
        block_header_t *next = (block_header_t *)next_addr;
        block_set_prev_free(next, true);
    }
}

/*
 * coalesce() - Merge adjacent free blocks
 *
 * After freeing a block, check if neighbors are also free and merge them.
 * This prevents fragmentation from accumulating over time.
 */
static block_header_t *coalesce(allocator_t *alloc, block_header_t *block)
{
    /*
     * Check next block
     *
     * If the next block in memory is free, absorb it into this block.
     */
    uintptr_t next_addr = (uintptr_t)block_next(block);
    if (next_addr < alloc->heap_end) {
        block_header_t *next = (block_header_t *)next_addr;
        if (block_is_free(next)) {
            /* Remove next from free list */
            remove_from_free_list(alloc, next);

            /* Absorb next into current block */
            size_t new_size = block_size(block) + block_size(next);
            block_set_size(block, new_size);
        }
    }

    /*
     * Check previous block
     *
     * If the previous block is free (indicated by our prev_free flag),
     * we should merge into it instead.
     */
    if (block_is_prev_free(block)) {
        block_header_t *prev = block_prev(block);

        /* Remove prev from free list */
        remove_from_free_list(alloc, prev);

        /* Absorb current block into prev */
        size_t new_size = block_size(prev) + block_size(block);
        block_set_size(prev, new_size);

        /* Continue with prev as the block to insert */
        block = prev;
    }

    /* Write footer for the (possibly merged) block */
    write_footer(block);

    /* Update next block's prev_free flag */
    next_addr = (uintptr_t)block_next(block);
    if (next_addr < alloc->heap_end) {
        block_header_t *next = (block_header_t *)next_addr;
        block_set_prev_free(next, true);
    }

    return block;
}

/*
 * find_free_block() - Find a free block of at least the given size
 *
 * Uses the bitmap to quickly find a size class with available blocks.
 */
static block_header_t *find_free_block(allocator_t *alloc, size_t size)
{
    size_t min_class = size_class(size);

    /*
     * Mask off classes smaller than we need, then find first set bit.
     * This gives us the smallest class that might have a large enough block.
     */
    uint32_t available = alloc->free_bitmap & ~((1u << min_class) - 1);

    if (available == 0) {
        return NULL;  /* No suitable blocks */
    }

    /*
     * Find first set bit (lowest class with blocks)
     */
    size_t class = 0;
    while ((available & (1u << class)) == 0) {
        class++;
    }

    /* Get first block from that class */
    free_node_t *node = (free_node_t *)alloc->free_lists[class];
    if (node == NULL) {
        return NULL;  /* Shouldn't happen if bitmap is correct */
    }

    return block_from_node(node);
}


/* =============================================================================
 * PUBLIC API IMPLEMENTATION
 * =============================================================================
 */

/*
 * allocator_init() - Initialize allocator with memory region
 */
void allocator_init(allocator_t *alloc, uintptr_t heap_start, uintptr_t heap_end)
{
    /* Align heap bounds */
    uintptr_t start = align_up(heap_start, MIN_ALIGN);
    uintptr_t end = heap_end & ~(uintptr_t)(MIN_ALIGN - 1);
    size_t size = end - start;

    /* Initialize allocator state */
    alloc->heap_start = start;
    alloc->heap_end = end;
    alloc->free_space = size;
    alloc->allocated = 0;
    alloc->free_bitmap = 0;

    /* Clear free lists */
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        alloc->free_lists[i] = NULL;
    }

    /*
     * Create one large free block spanning the entire heap
     */
    block_header_t *initial = (block_header_t *)start;
    initial->size_and_flags = size | FLAG_FREE;

    /* Initialize free list node */
    free_node_t *node = free_list_node(initial);
    node->next = NULL;
    node->prev = NULL;

    /* Write footer */
    write_footer(initial);

    /* Add to free list */
    size_t class = size_class(size);
    alloc->free_lists[class] = node;
    alloc->free_bitmap = 1u << class;

    alloc->initialized = true;
}

/*
 * allocator_init_from_ram() - Initialize from RAM detection results
 */
void allocator_init_from_ram(allocator_t *alloc,
                             uintptr_t ram_base, size_t ram_size,
                             uintptr_t heap_symbol,
                             uintptr_t *heap_start_out, uintptr_t *heap_end_out,
                             uintptr_t *jit_start_out, uintptr_t *jit_end_out)
{
    uintptr_t ram_end = ram_base + ram_size;

    /* JIT region at end of RAM */
    uintptr_t jit_start = ram_end - JIT_SIZE;
    uintptr_t jit_end = ram_end;

    /* Heap ends where JIT begins */
    uintptr_t heap_start = heap_symbol;
    uintptr_t heap_end = jit_start;

    /* Initialize allocator */
    allocator_init(alloc, heap_start, heap_end);

    /* Return bounds */
    if (heap_start_out) *heap_start_out = heap_start;
    if (heap_end_out) *heap_end_out = heap_end;
    if (jit_start_out) *jit_start_out = jit_start;
    if (jit_end_out) *jit_end_out = jit_end;
}

/*
 * allocator_alloc() - Allocate memory
 */
void *allocator_alloc(allocator_t *alloc, size_t size, size_t align)
{
    if (!alloc->initialized) {
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    /* Ensure minimum alignment */
    if (align < MIN_ALIGN) {
        align = MIN_ALIGN;
    }

    /*
     * Calculate total block size needed
     *
     * Payload must be large enough for free list node when freed.
     */
    size_t payload_size = size;
    if (payload_size < sizeof(free_node_t)) {
        payload_size = sizeof(free_node_t);
    }

    size_t block_size_needed = align_up(HEADER_SIZE + payload_size, MIN_ALIGN);
    if (block_size_needed < MIN_BLOCK_SIZE) {
        block_size_needed = MIN_BLOCK_SIZE;
    }

    /* Find a suitable free block */
    block_header_t *block = find_free_block(alloc, block_size_needed);
    if (block == NULL) {
        return NULL;  /* Out of memory */
    }

    /* Remove from free list */
    remove_from_free_list(alloc, block);

    /* Split if block is much larger than needed */
    split_block(alloc, block, block_size_needed);

    /* Mark as allocated */
    block_set_free(block, false);

    /* Update next block's prev_free flag */
    size_t final_size = block_size(block);
    uintptr_t next_addr = (uintptr_t)block_next(block);
    if (next_addr < alloc->heap_end) {
        block_header_t *next = (block_header_t *)next_addr;
        block_set_prev_free(next, false);
    }

    /* Update statistics */
    alloc->allocated += final_size;
    alloc->free_space -= final_size;

    return block_payload(block);
}

/*
 * allocator_free() - Free previously allocated memory
 */
void allocator_free(allocator_t *alloc, void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    if (!alloc->initialized) {
        return;
    }

    /* Get block header */
    block_header_t *block = block_from_payload(ptr);
    size_t size = block_size(block);

    /* Update statistics */
    alloc->allocated -= size;
    alloc->free_space += size;

    /* Mark as free */
    block_set_free(block, true);

    /* Coalesce with neighbors */
    block = coalesce(alloc, block);

    /* Insert into free list */
    insert_into_free_list(alloc, block);
}

/*
 * allocator_realloc() - Reallocate memory
 */
void *allocator_realloc(allocator_t *alloc, void *ptr, size_t new_size)
{
    if (ptr == NULL) {
        return allocator_alloc(alloc, new_size, MIN_ALIGN);
    }

    if (new_size == 0) {
        allocator_free(alloc, ptr);
        return NULL;
    }

    /* Get current block info */
    block_header_t *block = block_from_payload(ptr);
    size_t current_size = block_size(block) - HEADER_SIZE;

    /* If shrinking or same size, just return current pointer */
    if (new_size <= current_size) {
        /* Could potentially split here, but keeping it simple */
        return ptr;
    }

    /* Need to grow - allocate new block and copy */
    void *new_ptr = allocator_alloc(alloc, new_size, MIN_ALIGN);
    if (new_ptr == NULL) {
        return NULL;  /* Original pointer still valid */
    }

    /* Copy old data */
    memcpy(new_ptr, ptr, current_size);

    /* Free old block */
    allocator_free(alloc, ptr);

    return new_ptr;
}

/*
 * allocator_stats() - Get statistics
 */
void allocator_stats(const allocator_t *alloc, size_t *allocated_out, size_t *free_out)
{
    if (allocated_out) *allocated_out = alloc->allocated;
    if (free_out) *free_out = alloc->free_space;
}

/*
 * allocator_bounds() - Get heap bounds
 */
void allocator_bounds(const allocator_t *alloc, uintptr_t *start_out, uintptr_t *end_out)
{
    if (start_out) *start_out = alloc->heap_start;
    if (end_out) *end_out = alloc->heap_end;
}

/*
 * allocator_is_initialized() - Check initialization
 */
bool allocator_is_initialized(const allocator_t *alloc)
{
    return alloc->initialized;
}


/* =============================================================================
 * GLOBAL ALLOCATOR
 * =============================================================================
 */

/* Global allocator instance */
allocator_t g_allocator = {0};

/* Linker symbol for heap start - must be defined in linker script */
extern uint8_t __heap_start[];

void heap_init(uintptr_t ram_base, size_t ram_size)
{
    uintptr_t heap_start, heap_end, jit_start, jit_end;
    allocator_init_from_ram(&g_allocator,
                            ram_base, ram_size,
                            (uintptr_t)__heap_start,
                            &heap_start, &heap_end,
                            &jit_start, &jit_end);
}

void *heap_alloc(size_t size)
{
    return allocator_alloc(&g_allocator, size, MIN_ALIGN);
}

void *heap_alloc_aligned(size_t size, size_t align)
{
    return allocator_alloc(&g_allocator, size, align);
}

void heap_free(void *ptr)
{
    allocator_free(&g_allocator, ptr);
}

void *heap_realloc(void *ptr, size_t size)
{
    return allocator_realloc(&g_allocator, ptr, size);
}
