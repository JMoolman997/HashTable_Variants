/**
 * @file    fingerprint_impl.h
 * @brief   Fingerprint-accelerated separate-chaining hashtable backend.
 *
 * Implements a variant of separate chaining where chains are stored in
 * segments containing an array of small hash fingerprints and a separate 
 * array of key/value pairs. Lookups scan the fingerprints first to avoid
 * expensive full-key comparisons.
 *
 * @author  J.W. Moolman
 * @date    2026-05-08
 */

#ifndef FINGERPRINT_IMPL_H
#define FINGERPRINT_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"
#include "ht_bench.h"
#include "slab_pool.h"

struct ht_vtable;

#define FINGERPRINT_DEFAULT_INITIAL_CAPACITY 16u
#define FINGERPRINT_DEFAULT_MIN_CAPACITY     16u
#define FINGERPRINT_DEFAULT_MAX_LOAD         1.50
#define FINGERPRINT_DEFAULT_MIN_LOAD         0.25

#define FINGERPRINT_SEGMENT_CAPACITY 8u
#define FINGERPRINT_TAG_MASK 0xFFu

/**
 * @brief A segment in a fingerprint-accelerated bucket chain.
 *
 * Storing fingerprints in a separate array allows scanning them with high
 * locality (or SIMD) before touching the larger key/value arrays.
 */
typedef struct fingerprint_segment {
    struct fingerprint_segment *next;     /**< Next segment in the chain. */
    uint8_t used;                         /**< Number of occupied slots. */
    uint8_t tags[FINGERPRINT_SEGMENT_CAPACITY]; /**< Small hash fingerprints. */
    ht_key_t keys[FINGERPRINT_SEGMENT_CAPACITY];  /**< Stored keys. */
    ht_val_t values[FINGERPRINT_SEGMENT_CAPACITY]; /**< Stored values. */
} fingerprint_segment;

/**
 * @brief Private state for the fingerprint-accelerated chaining backend.
 */
typedef struct {
    fingerprint_segment **buckets; /**< Bucket array of segment chain heads. */
    slab_pool pool;                /**< Pool used to allocate segments. */

    size_t capacity;      /**< Number of buckets, always a power of two. */
    size_t min_capacity;  /**< Smallest bucket count the table may shrink to. */
    size_t size;          /**< Number of live entries currently stored. */

    double max_load_factor; /**< Upper load factor threshold for growth. */
    double min_load_factor; /**< Lower load factor threshold for shrink. */
    ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

    ht_hash_fn hash_fn;   /**< Hash function used for all key lookups. */
    uint64_t hash_seed;   /**< Seed passed into `hash_fn`. */

    int collect_stats;  /**< Non-zero when benchmark statistics are tracked. */
    ht_stats stats;     /**< Accumulated operation and memory statistics. */
} fingerprint_table;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Allocate and initialize a fingerprint-accelerated backend instance.
 */
void *fingerprint_create_impl(
    const ht_config *cfg
);

/**
 * @brief Return the fingerprint backend vtable used by the core wrapper.
 */
const struct ht_vtable *fingerprint_vtable(
    void
);

/**
 * @brief Populate a benchmark interface with fingerprint direct ops.
 */
int fingerprint_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

#endif /* FINGERPRINT_IMPL_H */
