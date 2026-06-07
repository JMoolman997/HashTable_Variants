#define _POSIX_C_SOURCE 200809L

/**
 * @file    slab_pool.c
 * @brief   Simple fixed-size object pool for `mod_separate_chaining`.
 *
 * Implements a minimal slab allocator with block allocation and a free list.
 * The allocator trades generality for low complexity and predictable object
 * reuse.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "slab_pool.h"

/**
 * @brief Free-list node stored inside an unused slab slot.
 */
typedef struct slab_pool_free_slot {
    struct slab_pool_free_slot *next; /**< Next free slot in the pool. */
} slab_pool_free_slot;

/**
 * @brief One allocated slab block owned by a pool.
 */
struct slab_pool_block {
    struct slab_pool_block *next; /**< Next block in allocation order. */
    void *allocation; /**< Original allocation pointer for `free`. */
    unsigned char storage[]; /**< Aligned slot storage. */
};

/**
 * @brief Allocate one slab block and push its slots onto the free list.
 *
 * @param pool Pool that needs more free slots.
 *
 * @return `0` on success, or `-1` on overflow/allocation failure.
 */
static int slab_pool_grow(
    slab_pool *pool
);

/**
 * @brief Round an object size up to the pool's slot alignment.
 *
 * @param size Requested object size.
 *
 * @return Aligned slot size, never smaller than a pointer.
 */
static size_t slab_pool_align_size(
    size_t size
);

static size_t slab_pool_align_up(
    size_t value,
    size_t align
);

int slab_pool_init(
    slab_pool *pool,
    size_t object_size,
    size_t objects_per_block
) {
    if (pool == NULL || object_size == 0) {
        return -1;
    }

    pool->blocks = NULL;
    pool->free_list = NULL;
    pool->slot_size = slab_pool_align_size(object_size);
    if (pool->slot_size == SIZE_MAX) {
        return -1;
    }
    pool->objects_per_block = (objects_per_block > 0)
        ? objects_per_block
        : SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK;
    pool->bytes_owned = 0;
    pool->block_count = 0;
    pool->alloc_count = 0;
    pool->free_count = 0;
    pool->live_count = 0;
    pool->high_watermark = 0;
    pool->free_slot_count = 0;

    return 0;
}

void slab_pool_reset(
    slab_pool *pool
) {
    slab_pool_block *block;

    if (pool == NULL) {
        return;
    }

    block = pool->blocks;
    while (block != NULL) {
        slab_pool_block *next = block->next;
        free(block->allocation);
        block = next;
    }

    pool->blocks = NULL;
    pool->free_list = NULL;
    pool->bytes_owned = 0;
    pool->block_count = 0;
    pool->alloc_count = 0;
    pool->free_count = 0;
    pool->live_count = 0;
    pool->high_watermark = 0;
    pool->free_slot_count = 0;
}

void slab_pool_destroy(
    slab_pool *pool
) {
    slab_pool_reset(pool);
}

void *slab_pool_alloc(
    slab_pool *pool
) {
    slab_pool_free_slot *slot;

    if (pool == NULL) {
        return NULL;
    }

    if (pool->free_list == NULL && slab_pool_grow(pool) != 0) {
        return NULL;
    }

    slot = pool->free_list;
    pool->free_list = slot->next;
    if (pool->free_slot_count > 0) {
        pool->free_slot_count--;
    }
    pool->alloc_count++;
    pool->live_count++;
    if (pool->live_count > pool->high_watermark) {
        pool->high_watermark = pool->live_count;
    }
    return slot;
}

void slab_pool_free(
    slab_pool *pool,
    void *ptr
) {
    slab_pool_free_slot *slot;

    if (pool == NULL || ptr == NULL) {
        return;
    }

    slot = ptr;
    slot->next = pool->free_list;
    pool->free_list = slot;
    pool->free_slot_count++;
    pool->free_count++;
    if (pool->live_count > 0) {
        pool->live_count--;
    }
}

size_t slab_pool_available(
    const slab_pool *pool
) {
    return (pool != NULL)
        ? pool->free_slot_count
        : 0
    ;
}

int slab_pool_reserve(
    slab_pool *pool,
    size_t object_count
) {
    if (pool == NULL) {
        return -1;
    }

    while (pool->free_slot_count < object_count) {
        if (slab_pool_grow(pool) != 0) {
            return -1;
        }
    }

    return 0;
}

size_t slab_pool_bytes_owned(
    const slab_pool *pool
) {
    return (pool != NULL)
        ? pool->bytes_owned
        : 0
    ;
}

size_t slab_pool_block_count(
    const slab_pool *pool
) {
    return (pool != NULL)
        ? pool->block_count
        : 0
    ;
}

size_t slab_pool_alloc_count(
    const slab_pool *pool
) {
    return (pool != NULL)
        ? pool->alloc_count
        : 0
    ;
}

size_t slab_pool_free_count(
    const slab_pool *pool
) {
    return (pool != NULL)
        ? pool->free_count
        : 0
    ;
}

size_t slab_pool_live_count(
    const slab_pool *pool
) {
    return (pool != NULL)
        ? pool->live_count
        : 0
    ;
}

size_t slab_pool_high_watermark(
    const slab_pool *pool
) {
    return (pool != NULL)
        ? pool->high_watermark
        : 0
    ;
}

static int slab_pool_grow(
    slab_pool *pool
) {
    slab_pool_block *block;
    void *allocation;
    uintptr_t aligned_addr;
    size_t storage_offset;
    size_t block_bytes;
    size_t allocation_bytes;
    unsigned char *storage;
    size_t i;

    if (pool == NULL) {
        return -1;
    }

    storage_offset = slab_pool_align_up(
        offsetof(slab_pool_block, storage),
        SLAB_POOL_ALIGNMENT
    );
    if (storage_offset == SIZE_MAX) {
        return -1;
    }
    if (pool->objects_per_block == 0 ||
        pool->slot_size >
            (SIZE_MAX - storage_offset) / pool->objects_per_block) {
        return -1;
    }

    block_bytes = storage_offset + pool->slot_size * pool->objects_per_block;
    if (block_bytes > SIZE_MAX - pool->bytes_owned) {
        return -1;
    }
    if (block_bytes > SIZE_MAX - (SLAB_POOL_ALIGNMENT - 1u)) {
        return -1;
    }
    allocation_bytes = block_bytes + (SLAB_POOL_ALIGNMENT - 1u);

    allocation = malloc(allocation_bytes);
    if (allocation == NULL) {
        return -1;
    }

    aligned_addr = (uintptr_t)allocation;
    if (aligned_addr % SLAB_POOL_ALIGNMENT != 0) {
        aligned_addr += SLAB_POOL_ALIGNMENT -
            (aligned_addr % SLAB_POOL_ALIGNMENT);
    }
    block = (slab_pool_block *)(void *)aligned_addr;
    block->allocation = allocation;
    block->next = pool->blocks;
    pool->blocks = block;
    pool->bytes_owned += allocation_bytes;
    pool->block_count++;
    pool->free_slot_count += pool->objects_per_block;

    /* Carve the new block into fixed-size slots and prepend each one to the
     * free list, so allocation stays a simple pop. */
    storage = (unsigned char *)(void *)block + storage_offset;
    for (i = 0; i < pool->objects_per_block; i++) {
        slab_pool_free_slot *slot = (slab_pool_free_slot *)(void *)
            (storage + i * pool->slot_size);

        slot->next = pool->free_list;
        pool->free_list = slot;
    }

    return 0;
}

static size_t slab_pool_align_size(
    size_t size
) {
    size_t aligned = (size < sizeof(void *))
        ? sizeof(void *)
        : size
    ;

    return slab_pool_align_up(aligned, SLAB_POOL_ALIGNMENT);
}

static size_t slab_pool_align_up(
    size_t value,
    size_t align
) {
    if (align != 0 && value % align != 0) {
        size_t padding = align - (value % align);

        if (value > SIZE_MAX - padding) {
            return SIZE_MAX;
        }
        value += padding;
    }

    return value;
}
