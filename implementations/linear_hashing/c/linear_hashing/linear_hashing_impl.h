/**
 * @file    linear_hashing_impl.h
 * @brief   Linear hashing backend with fingerprint-accelerated segments.
 *
 * Implements linear hashing to distribute the cost of resizing by growing
 * the table one bucket at a time. Each bucket chain uses segmented
 * storage with hash fingerprints for fast lookups.
 *
 * @author  J.W. Moolman
 * @date    2026-05-08
 */

#ifndef LINEAR_HASHING_IMPL_H
#define LINEAR_HASHING_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"
#include "ht_bench.h"
#include "slab_pool.h"

struct ht_vtable;

#define LINEAR_HASHING_DEFAULT_INITIAL_CAPACITY 16u
#define LINEAR_HASHING_DEFAULT_MIN_CAPACITY     16u
#define LINEAR_HASHING_DEFAULT_MAX_LOAD         1.50
#define LINEAR_HASHING_DEFAULT_MIN_LOAD         0.25

#define LINEAR_HASHING_SEGMENT_CAPACITY 8u
#define LINEAR_HASHING_TAG_MASK 0xFFu

/**
 * @brief A segment in a fingerprint-accelerated bucket chain.
 */
typedef struct linear_hashing_segment {
    struct linear_hashing_segment *next; /**< Next segment in the chain. */
    uint8_t used; /**< Number of occupied entries in this segment. */
    uint8_t tags[LINEAR_HASHING_SEGMENT_CAPACITY]; /**< Hash fingerprints. */
    ht_key_t keys[LINEAR_HASHING_SEGMENT_CAPACITY]; /**< Stored keys. */
    ht_val_t values[LINEAR_HASHING_SEGMENT_CAPACITY]; /**< Stored values. */
} linear_hashing_segment;

/**
 * @brief Private state for the linear hashing backend.
 */
typedef struct {
    linear_hashing_segment **buckets; /**< Array of bucket chain heads. */
    slab_pool pool;                  /**< Pool for segment allocations. */

    size_t initial_buckets; /**< Number of buckets at start of level 0 (N0). */
    size_t split_ptr;      /**< Index of the next bucket to be split (p). */
    size_t level;          /**< Current doubling round (L). */
    size_t total_buckets;  /**< Current number of active buckets. */
    size_t capacity;       /**< Allocated size of the `buckets` array. */

    size_t size;           /**< Total number of entries. */
    
    double max_load_factor; /**< Load factor that triggers a split. */
    double min_load_factor; /**< Load factor that triggers a merge. */
    ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

    ht_hash_fn hash_fn; /**< Hash function used for key lookup. */
    uint64_t hash_seed; /**< Seed passed into `hash_fn`. */

    int collect_stats; /**< Non-zero when statistics are tracked. */
    ht_stats stats;    /**< Accumulated operation and memory statistics. */
} linear_hashing_table;

/* --- function prototypes -------------------------------------------------- */

void *linear_hashing_create_impl(
    const ht_config *cfg
);

const struct ht_vtable *linear_hashing_vtable(
    void
);

int linear_hashing_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

#endif /* LINEAR_HASHING_IMPL_H */
