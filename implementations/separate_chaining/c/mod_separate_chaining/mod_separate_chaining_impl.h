/**
 * @file    mod_separate_chaining_impl.h
 * @brief   Core interface for the modified separate-chaining backend.
 *
 * Declares the backend state and entry points for the modified
 * separate-chaining hashtable. Bucket storage is selected from `ht_impl` at
 * creation time, so this header keeps bucket storage opaque to the shared
 * backend core.
 *
 * This header is intended for the core wrapper and modified
 * separate-chaining support code. It is not part of the public user-facing
 * API.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#ifndef MOD_SEPARATE_CHAINING_IMPL_H
#define MOD_SEPARATE_CHAINING_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_bench.h"
#include "ht_types.h"

struct ht_vtable;
typedef struct mod_separate_chaining_bucket_ops
    mod_separate_chaining_bucket_ops;

/**
 * @brief Private state for the modified separate-chaining backend core.
 *
 * The selected bucket implementation owns the concrete bucket-array and
 * context types behind opaque pointers.
 */
typedef struct {
    void *buckets; /**< Opaque bucket array storage. */
    void *bucket_ctx; /**< Opaque bucket state. */
    const mod_separate_chaining_bucket_ops *bucket_ops; /**< Bucket dispatch. */

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
} mod_separate_chaining_table;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Allocate and initialize a modified separate-chaining backend instance.
 *
 * @param cfg Backend configuration used to size and initialize the table.
 *
 * @return An initialized backend object on success, or `NULL` if `cfg` is
 *         invalid or allocation fails.
 */
void *mod_separate_chaining_create_impl(
    const ht_config *cfg
);

/**
 * @brief Return the modified separate-chaining backend vtable used by the core wrapper.
 *
 * @return A pointer to the static modified separate-chaining dispatch table.
 */
const struct ht_vtable *mod_separate_chaining_vtable(
    void
);

/**
 * @brief Populate a benchmark interface with modified separate-chaining direct ops.
 *
 * @param ctx Modified separate-chaining backend instance to expose.
 * @param out Output interface to populate.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
int mod_separate_chaining_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

#endif /* MOD_SEPARATE_CHAINING_IMPL_H */
