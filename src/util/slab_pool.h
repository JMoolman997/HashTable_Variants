/**
 * @file    slab_pool.h
 * @brief   Fixed-size object pool shared by segmented chaining backends.
 *
 * Provides a minimal slab allocator that reuses fixed-size objects through a
 * free list. It is used for fixed-size bucket/overflow segments and does not
 * attempt to be a general-purpose allocator.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#ifndef HT_UTIL_SLAB_POOL_H
#define HT_UTIL_SLAB_POOL_H

#include <stddef.h>

typedef struct slab_pool_block slab_pool_block;

/**
 * @brief Fixed-size slab allocator state.
 */
typedef struct slab_pool {
    slab_pool_block *blocks; /**< Linked list of allocated blocks. */
    void *free_list; /**< Singly linked list of available slots. */
    size_t slot_size; /**< Aligned size of each slot. */
    size_t objects_per_block; /**< Slots allocated per new block. */
    size_t bytes_owned; /**< Total bytes owned by all blocks. */
    size_t block_count; /**< Number of allocated blocks. */
    size_t alloc_count; /**< Total allocation calls served. */
    size_t free_count; /**< Total frees returned to the pool. */
    size_t live_count; /**< Currently checked-out slots. */
    size_t high_watermark; /**< Largest observed `live_count`. */
    size_t free_slot_count; /**< Current number of available slots. */
} slab_pool;

#define SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK 64u
#define SLAB_POOL_ALIGNMENT 64u

/**
 * @brief Initialize caller-owned slab pool storage.
 *
 * @param pool Pool object to initialize.
 * @param object_size Size of each object the pool will hand out.
 * @param objects_per_block Objects to allocate per slab block, or `0` for the
 *                          default.
 *
 * @return `0` on success, or `-1` for invalid input.
 */
int slab_pool_init(
    slab_pool *pool,
    size_t object_size,
    size_t objects_per_block
);

/**
 * @brief Free every slab block owned by a pool and leave it reusable.
 *
 * @param pool Pool to reset. `NULL` is ignored.
 */
void slab_pool_reset(
    slab_pool *pool
);

/**
 * @brief Destroy caller-owned pool contents.
 *
 * @param pool Pool to destroy. `NULL` is ignored.
 */
void slab_pool_destroy(
    slab_pool *pool
);

/**
 * @brief Allocate one object-sized slot from a pool.
 *
 * @param pool Pool to allocate from.
 *
 * @return Slot pointer on success, or `NULL` if the pool cannot grow.
 */
void *slab_pool_alloc(
    slab_pool *pool
);

/**
 * @brief Return one slot to a pool's free list.
 *
 * @param pool Pool that owns `ptr`.
 * @param ptr Slot previously returned by `slab_pool_alloc`.
 */
void slab_pool_free(
    slab_pool *pool,
    void *ptr
);

/**
 * @brief Return the number of currently available slots.
 *
 * @param pool Pool to inspect.
 *
 * @return Free slot count, or `0` for `NULL`.
 */
size_t slab_pool_available(
    const slab_pool *pool
);

/**
 * @brief Ensure at least object_count slots are available.
 *
 * @param pool Pool to grow.
 * @param object_count Minimum free slots required after the call.
 *
 * @return `0` on success, or `-1` on allocation/overflow failure.
 */
int slab_pool_reserve(
    slab_pool *pool,
    size_t object_count
);

/**
 * @brief Return the number of bytes owned by allocated slab blocks.
 *
 * @param pool Pool to inspect.
 *
 * @return Bytes currently owned by the pool, or `0` for `NULL`.
 */
size_t slab_pool_bytes_owned(
    const slab_pool *pool
);

/**
 * @brief Return the number of slab blocks owned by the pool.
 */
size_t slab_pool_block_count(
    const slab_pool *pool
);

/**
 * @brief Return the total number of slots handed out by the pool.
 */
size_t slab_pool_alloc_count(
    const slab_pool *pool
);

/**
 * @brief Return the total number of slots returned to the pool.
 */
size_t slab_pool_free_count(
    const slab_pool *pool
);

/**
 * @brief Return the number of slots currently checked out.
 */
size_t slab_pool_live_count(
    const slab_pool *pool
);

/**
 * @brief Return the maximum simultaneous live slot count observed.
 */
size_t slab_pool_high_watermark(
    const slab_pool *pool
);

#endif /* HT_UTIL_SLAB_POOL_H */
