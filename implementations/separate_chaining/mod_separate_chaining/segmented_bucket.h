/**
 * @file    segmented_bucket.h
 * @brief   Segmented chaining buckets for `mod_separate_chaining`.
 *
 * Declares the bucket-local operations used when modified separate-chaining
 * stores multiple entries inside linked segments instead of per-entry nodes.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#ifndef MOD_SEPARATE_CHAINING_SEGMENTED_BUCKET_H
#define MOD_SEPARATE_CHAINING_SEGMENTED_BUCKET_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

#define SEGMENT_CAPACITY 8u

typedef struct bucket_segment {
    struct bucket_segment *next; /**< Next segment in the bucket chain. */
    uint8_t used; /**< Number of occupied entries in this segment. */
    ht_key_t keys[SEGMENT_CAPACITY]; /**< Stored keys. */
    ht_val_t values[SEGMENT_CAPACITY]; /**< Stored values. */
} bucket_segment_t;

/**
 * @brief Root bucket for segmented modified chaining.
 */
typedef struct {
    bucket_segment_t *head; /**< First segment in the bucket chain. */
} segmented_bucket;

typedef struct segmented_bucket_ctx segmented_bucket_ctx;

/**
 * @brief Allocate segmented bucket context and initialize its segment slab.
 *
 * @return New context on success, or `NULL` on allocation failure.
 */
segmented_bucket_ctx *segmented_bucket_ctx_create(
    void
);

/**
 * @brief Destroy a segmented bucket context and its segment slab.
 *
 * @param ctx Context to destroy. `NULL` is ignored.
 */
void segmented_bucket_ctx_destroy(
    segmented_bucket_ctx *ctx
);

/**
 * @brief Allocate a zero-filled root bucket array.
 *
 * @param capacity Number of root buckets.
 *
 * @return Bucket array on success, or `NULL` on allocation failure.
 */
segmented_bucket *segmented_bucket_array_alloc(
    size_t capacity
);

/**
 * @brief Free only the root bucket array after entries were moved elsewhere.
 *
 * @param buckets Bucket array to release. `NULL` is ignored.
 */
void segmented_bucket_array_release(
    segmented_bucket *buckets
);

/**
 * @brief Free all segment chains in a bucket array and then free the array.
 *
 * @param ctx Context whose slab owns segment storage.
 * @param buckets Bucket array to destroy.
 * @param capacity Number of root buckets in `buckets`.
 */
void segmented_bucket_array_destroy(
    segmented_bucket_ctx *ctx,
    segmented_bucket *buckets,
    size_t capacity
);

/**
 * @brief Insert a key/value pair into one segmented bucket.
 *
 * @param ctx Context used to allocate segments.
 * @param buckets Root bucket array.
 * @param bucket_index Bucket index selected by the caller.
 * @param key Key to insert.
 * @param value Value to associate with `key`.
 * @param probe_len_out Optional output for slots examined plus insert slot.
 *
 * @return `HT_OK`, `HT_ERR_EXISTS`, `HT_ERR_INVALID`, or `HT_ERR_OOM`.
 */
ht_result segmented_bucket_insert(
    segmented_bucket_ctx *ctx,
    segmented_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
);

/**
 * @brief Look up a key in one segmented bucket.
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
ht_result segmented_bucket_get(
    segmented_bucket_ctx *ctx,
    const segmented_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
);

/**
 * @brief Remove a key from one segmented bucket.
 *
 * @param ctx Context whose slab receives emptied segments.
 * @param buckets Root bucket array.
 * @param bucket_index Bucket index selected by the caller.
 * @param key Key to remove.
 * @param probe_len_out Optional output for slots examined.
 *
 * @return `HT_OK`, `HT_ERR_NOT_FOUND`, or `HT_ERR_INVALID`.
 */
ht_result segmented_bucket_remove(
    segmented_bucket_ctx *ctx,
    segmented_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
);

/**
 * @brief Copy all entries into a new bucket array during resize.
 *
 * @param ctx Context used for destination segment allocations.
 * @param old_buckets Source bucket array.
 * @param old_capacity Number of source root buckets.
 * @param new_buckets Destination bucket array.
 * @param new_capacity Number of destination root buckets.
 * @param hash_fn Hash function used to recompute bucket indexes.
 * @param hash_seed Seed passed to `hash_fn`.
 *
 * @return `HT_OK`, or an insertion/allocation error.
 */
ht_result segmented_bucket_rehash_all(
    segmented_bucket_ctx *ctx,
    segmented_bucket *old_buckets,
    size_t old_capacity,
    segmented_bucket *new_buckets,
    size_t new_capacity,
    ht_hash_fn hash_fn,
    uint64_t hash_seed
);

/**
 * @brief Report segmented-bucket memory outside the root bucket array.
 *
 * @param ctx Bucket context containing slab accounting.
 * @param buckets Unused root bucket array parameter.
 * @param capacity Unused bucket count parameter.
 * @param size Unused live entry count parameter.
 *
 * @return Extra bytes owned, or `SIZE_MAX` for invalid input.
 */
size_t segmented_bucket_extra_bytes(
    const segmented_bucket_ctx *ctx,
    const segmented_bucket *buckets,
    size_t capacity,
    size_t size
);

#endif /* MOD_SEPARATE_CHAINING_SEGMENTED_BUCKET_H */
