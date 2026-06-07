/**
 * @file    inline_array_bucket.h
 * @brief   Fixed-size inline bucket arrays for `mod_separate_chaining`.
 *
 * Declares the bucket-local operations used when modified separate-chaining
 * stores multiple entries contiguously inside each bucket and allocates
 * overflow buckets only when required.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#ifndef MOD_SEPARATE_CHAINING_INLINE_ARRAY_BUCKET_H
#define MOD_SEPARATE_CHAINING_INLINE_ARRAY_BUCKET_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

#define INLINE_BUCKET_CAPACITY 8u

typedef struct inline_array_bucket {
    struct inline_array_bucket *next; /**< Next overflow bucket in chain. */
    uint8_t used; /**< Number of occupied entries in this bucket. */
    ht_key_t keys[INLINE_BUCKET_CAPACITY]; /**< Stored keys. */
    ht_val_t values[INLINE_BUCKET_CAPACITY]; /**< Stored values. */
} inline_array_bucket;

typedef struct inline_array_bucket_ctx inline_array_bucket_ctx;

/**
 * @brief Allocate inline-array bucket context and initialize overflow slab.
 *
 * @return New context on success, or `NULL` on allocation failure.
 */
inline_array_bucket_ctx *inline_array_bucket_ctx_create(
    void
);

/**
 * @brief Destroy an inline-array bucket context and its overflow slab.
 *
 * @param ctx Context to destroy. `NULL` is ignored.
 */
void inline_array_bucket_ctx_destroy(
    inline_array_bucket_ctx *ctx
);

/**
 * @brief Allocate a zero-filled root bucket array.
 *
 * @param capacity Number of root buckets.
 *
 * @return Bucket array on success, or `NULL` on allocation failure.
 */
inline_array_bucket *inline_array_bucket_array_alloc(
    size_t capacity
);

/**
 * @brief Free only the root bucket array after entries were moved elsewhere.
 *
 * @param buckets Bucket array to release. `NULL` is ignored.
 */
void inline_array_bucket_array_release(
    inline_array_bucket *buckets
);

/**
 * @brief Free overflow chains in a root bucket array, then free the array.
 *
 * @param ctx Context whose slab owns overflow buckets.
 * @param buckets Bucket array to destroy.
 * @param capacity Number of root buckets in `buckets`.
 */
void inline_array_bucket_array_destroy(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *buckets,
    size_t capacity
);

/**
 * @brief Insert a key/value pair into one bucket chain.
 *
 * @param ctx Context used to allocate overflow buckets.
 * @param buckets Root bucket array.
 * @param bucket_index Bucket index selected by the caller.
 * @param key Key to insert.
 * @param value Value to associate with `key`.
 * @param probe_len_out Optional output for slots examined plus insert slot.
 *
 * @return `HT_OK`, `HT_ERR_EXISTS`, `HT_ERR_INVALID`, or `HT_ERR_OOM`.
 */
ht_result inline_array_bucket_insert(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
);

/**
 * @brief Look up a key in one bucket chain.
 *
 * @param ctx Unused context parameter kept for bucket interface symmetry.
 * @param buckets Root bucket array.
 * @param bucket_index Bucket index selected by the caller.
 * @param key Key to search for.
 * @param value_out Output value on success.
 * @param probe_len_out Optional output for slots examined.
 *
 * @return `HT_OK`, `HT_ERR_NOT_FOUND`, or `HT_ERR_INVALID`.
 */
ht_result inline_array_bucket_get(
    inline_array_bucket_ctx *ctx,
    const inline_array_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
);

/**
 * @brief Remove a key from one bucket chain.
 *
 * @param ctx Context whose slab receives empty overflow buckets.
 * @param buckets Root bucket array.
 * @param bucket_index Bucket index selected by the caller.
 * @param key Key to remove.
 * @param probe_len_out Optional output for slots examined.
 *
 * @return `HT_OK`, `HT_ERR_NOT_FOUND`, or `HT_ERR_INVALID`.
 */
ht_result inline_array_bucket_remove(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
);

/**
 * @brief Copy all entries into a new bucket array during resize.
 *
 * @param ctx Context used for destination overflow allocations.
 * @param old_buckets Source bucket array.
 * @param old_capacity Number of source root buckets.
 * @param new_buckets Destination bucket array.
 * @param new_capacity Number of destination root buckets.
 * @param hash_fn Hash function used to recompute bucket indexes.
 * @param hash_seed Seed passed to `hash_fn`.
 *
 * @return `HT_OK`, or an insertion/allocation error.
 */
ht_result inline_array_bucket_rehash_all(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *old_buckets,
    size_t old_capacity,
    inline_array_bucket *new_buckets,
    size_t new_capacity,
    ht_hash_fn hash_fn,
    uint64_t hash_seed
);

/**
 * @brief Report inline-array memory outside the root bucket array.
 *
 * @param ctx Bucket context containing slab accounting.
 * @param buckets Unused root bucket array parameter.
 * @param capacity Unused bucket count parameter.
 * @param size Unused live entry count parameter.
 *
 * @return Extra bytes owned, or `SIZE_MAX` for invalid input.
 */
size_t inline_array_bucket_extra_bytes(
    const inline_array_bucket_ctx *ctx,
    const inline_array_bucket *buckets,
    size_t capacity,
    size_t size
);

#endif /* MOD_SEPARATE_CHAINING_INLINE_ARRAY_BUCKET_H */
