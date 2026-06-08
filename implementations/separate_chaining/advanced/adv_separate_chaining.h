/**
 * @file    adv_separate_chaining.h
 * @brief   Advanced separate-chaining backend using in-bucket storage and overflow segments.
 *
 * Implements a "bucketized" separate chaining where the first few entries
 * are stored directly in the bucket array to eliminate one level of indirection.
 * Further entries are stored in overflow segments allocated from a slab pool.
 *
 * @author  J.W. Moolman
 * @date    2026-05-08
 */

#ifndef ADV_SEPARATE_CHAINING_H
#define ADV_SEPARATE_CHAINING_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"
#include "ht_bench.h"
#include "slab_pool.h"

struct ht_vtable;

#define ADV_SEPARATE_CHAINING_DEFAULT_INITIAL_CAPACITY 16u
#define ADV_SEPARATE_CHAINING_DEFAULT_MIN_CAPACITY     16u
#define ADV_SEPARATE_CHAINING_DEFAULT_MAX_LOAD         1.50
#define ADV_SEPARATE_CHAINING_DEFAULT_MIN_LOAD         0.25

/*
 * Field order keeps the hot root bucket in one cache line and overflow
 * segments in two cache lines on the supported 64-bit key/value layout.
 */
#define ADV_BUCKET_CAPACITY 3u

/* Overflow segments can be larger to amortize allocation/traversal. */
#define ADV_SEGMENT_CAPACITY 7u

/**
 * @brief An overflow segment in an advanced bucket chain.
 */
typedef struct adv_segment {
    struct adv_segment *next; /**< Next overflow segment. */
    ht_key_t keys[ADV_SEGMENT_CAPACITY]; /**< Stored keys. */
    ht_val_t values[ADV_SEGMENT_CAPACITY]; /**< Stored values. */
    uint8_t  tags[ADV_SEGMENT_CAPACITY]; /**< Hash fingerprints. */
    uint8_t  used; /**< Number of occupied entries in this segment. */
} adv_segment;

/**
 * @brief A bucket in the main table, containing the first few entries.
 */
typedef struct {
    adv_segment *next; /**< First overflow segment. */
    ht_key_t keys[ADV_BUCKET_CAPACITY]; /**< Stored keys. */
    ht_val_t values[ADV_BUCKET_CAPACITY]; /**< Stored values. */
    uint8_t  tags[ADV_BUCKET_CAPACITY]; /**< Hash fingerprints. */
    uint8_t  used; /**< Number of occupied inline entries. */
} adv_bucket;

_Static_assert(sizeof(adv_bucket) == 64,
               "adv_bucket should fit in one cache line");

_Static_assert(sizeof(adv_segment) == 128,
               "adv_segment should fit in two cache lines");

/**
 * @brief Private state for the advanced separate-chaining backend.
 */
typedef struct {
    adv_bucket *buckets; /**< Root bucket array with inline entries. */
    slab_pool pool; /**< Pool used to allocate overflow segments. */

    size_t capacity; /**< Number of buckets, always a power of two. */
    size_t size; /**< Number of live entries currently stored. */
    size_t min_capacity; /**< Smallest bucket count allowed after shrink. */

    double max_load_factor; /**< Upper load factor threshold for growth. */
    double min_load_factor; /**< Lower load factor threshold for shrink. */
    ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

    ht_hash_fn hash_fn; /**< Hash function used for key lookup. */
    uint64_t hash_seed; /**< Seed passed into `hash_fn`. */

    int collect_stats; /**< Non-zero when statistics are tracked. */
    ht_stats stats; /**< Accumulated operation and memory statistics. */
} adv_separate_chaining_table;

/* --- function prototypes -------------------------------------------------- */

ht_result adv_separate_chaining_create_impl_ex(
    const ht_config *cfg,
    void           **out
);

const struct ht_vtable *adv_separate_chaining_vtable(
    void
);

int adv_separate_chaining_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

#endif /* ADV_SEPARATE_CHAINING_H */
