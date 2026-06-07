/**
 * @file    separate_chaining_impl.h
 * @brief   Internal separate-chaining backend interface and representation.
 *
 * Declares the bucket-node layouts, backend entry points, and shared
 * constants for the separate-chaining hashtable implementation.
 *
 * This header is intended for the core wrapper and separate-chaining support
 * code. It is not part of the public user-facing API.
 *
 * @author  J.W. Moolman
 * @date    2026-03-30
 */

#ifndef SEPARATE_CHAINING_IMPL_H
#define SEPARATE_CHAINING_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"
#include "ht_bench.h"

struct ht_vtable;

#define SEPARATE_CHAINING_DEFAULT_INITIAL_CAPACITY 16u
#define SEPARATE_CHAINING_DEFAULT_MIN_CAPACITY     16u
#define SEPARATE_CHAINING_DEFAULT_MAX_LOAD         0.85
#define SEPARATE_CHAINING_DEFAULT_MIN_LOAD         0.10

/**
 * @brief One node in a separate-chaining bucket list.
 */
typedef struct separate_chaining_node {
    ht_key_t key;                        /**< Stored key. */
    ht_val_t value;                     /**< Stored value. */
    struct separate_chaining_node *next; /**< Next node in the same bucket chain. */
} separate_chaining_node;

/**
 * @brief Private state for the separate-chaining backend.
 */
typedef struct {
    separate_chaining_node **buckets; /**< Bucket array of chain head pointers. */

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
} separate_chaining_table;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Allocate and initialize a separate-chaining backend instance.
 *
 * @param cfg Backend configuration used to size and initialize the table.
 *
 * @return An initialized backend object on success, or `NULL` if `cfg` is
 *         invalid or allocation fails.
 */
ht_result separate_chaining_create_impl_ex(
    const ht_config *cfg,
    void           **out
);

/**
 * @brief Return the separate-chaining backend vtable used by the core wrapper.
 *
 * @return A pointer to the static separate-chaining dispatch table.
 */
const struct ht_vtable *separate_chaining_vtable(
    void
);

/**
 * @brief Populate a benchmark interface with separate-chaining direct ops.
 *
 * @param ctx Separate-chaining backend instance to expose.
 * @param out Output interface to populate.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
int separate_chaining_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

#endif /* SEPARATE_CHAINING_IMPL_H */
