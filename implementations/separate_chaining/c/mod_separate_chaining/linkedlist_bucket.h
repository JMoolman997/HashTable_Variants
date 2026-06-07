/**
 * @file    linkedlist_bucket.h
 * @brief   Linked-list bucket implementation for `mod_separate_chaining`.
 *
 * Declares the bucket-local operations used by the modified separate-chaining
 * backend when `linked_mod_separate_chaining` is selected. Nodes are private
 * to the implementation and allocated from a slab pool.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#ifndef MOD_SEPARATE_CHAINING_LINKEDLIST_BUCKET_H
#define MOD_SEPARATE_CHAINING_LINKEDLIST_BUCKET_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

typedef struct linkedlist_bucket_node *linkedlist_bucket;
typedef struct linkedlist_bucket_ctx linkedlist_bucket_ctx;

/**
 * @brief Allocate linked-list bucket context and initialize its node slab.
 *
 * @return New context on success, or `NULL` on allocation failure.
 */
linkedlist_bucket_ctx *linkedlist_bucket_ctx_create(
    void
);

/**
 * @brief Destroy a linked-list bucket context and its slab storage.
 *
 * @param ctx Context to destroy. `NULL` is ignored.
 */
void linkedlist_bucket_ctx_destroy(
    linkedlist_bucket_ctx *ctx
);

/**
 * @brief Allocate a zero-filled root bucket pointer array.
 *
 * @param capacity Number of root buckets.
 *
 * @return Bucket array on success, or `NULL` on allocation failure.
 */
linkedlist_bucket *linkedlist_bucket_array_alloc(
    size_t capacity
);

/**
 * @brief Free only the root bucket array after entries were moved elsewhere.
 *
 * @param buckets Bucket array to release. `NULL` is ignored.
 */
void linkedlist_bucket_array_release(
    linkedlist_bucket *buckets
);

/**
 * @brief Free all nodes in a bucket array and then free the array itself.
 *
 * @param ctx Context whose slab owns the nodes.
 * @param buckets Bucket array to destroy.
 * @param capacity Number of root buckets in `buckets`.
 */
void linkedlist_bucket_array_destroy(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket *buckets,
    size_t capacity
);

/**
 * @brief Insert a key/value pair into one linked-list bucket.
 *
 * @param ctx Bucket context used to allocate a node.
 * @param buckets Root bucket array.
 * @param bucket_index Bucket index selected by the caller.
 * @param key Key to insert.
 * @param value Value to associate with `key`.
 * @param probe_len_out Optional output for nodes examined plus insert slot.
 *
 * @return `HT_OK`, `HT_ERR_EXISTS`, `HT_ERR_INVALID`, or `HT_ERR_OOM`.
 */
ht_result linkedlist_bucket_insert(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
);

/**
 * @brief Look up a key in one linked-list bucket.
 *
 * @param ctx Unused context parameter kept for bucket interface symmetry.
 * @param buckets Root bucket array.
 * @param bucket_index Bucket index selected by the caller.
 * @param key Key to search for.
 * @param value_out Output value on success.
 * @param probe_len_out Optional output for nodes examined.
 *
 * @return `HT_OK`, `HT_ERR_NOT_FOUND`, or `HT_ERR_INVALID`.
 */
ht_result linkedlist_bucket_get(
    linkedlist_bucket_ctx *ctx,
    const linkedlist_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
);

/**
 * @brief Remove a key from one linked-list bucket.
 *
 * @param ctx Bucket context whose slab receives the freed node.
 * @param buckets Root bucket array.
 * @param bucket_index Bucket index selected by the caller.
 * @param key Key to remove.
 * @param probe_len_out Optional output for nodes examined.
 *
 * @return `HT_OK`, `HT_ERR_NOT_FOUND`, or `HT_ERR_INVALID`.
 */
ht_result linkedlist_bucket_remove(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
);

/**
 * @brief Move all nodes from one bucket array into another during resize.
 *
 * @param ctx Unused context parameter kept for bucket interface symmetry.
 * @param old_buckets Source bucket array.
 * @param old_capacity Number of source root buckets.
 * @param new_buckets Destination bucket array.
 * @param new_capacity Number of destination root buckets.
 * @param hash_fn Hash function used to recompute bucket indexes.
 * @param hash_seed Seed passed to `hash_fn`.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID`.
 */
ht_result linkedlist_bucket_rehash_all(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket *old_buckets,
    size_t old_capacity,
    linkedlist_bucket *new_buckets,
    size_t new_capacity,
    ht_hash_fn hash_fn,
    uint64_t hash_seed
);

/**
 * @brief Report linked-list bucket memory outside the root bucket array.
 *
 * @param ctx Bucket context containing slab accounting.
 * @param buckets Unused root bucket array parameter.
 * @param capacity Unused bucket count parameter.
 * @param size Unused live entry count parameter.
 *
 * @return Extra bytes owned, or `SIZE_MAX` for invalid input.
 */
size_t linkedlist_bucket_extra_bytes(
    const linkedlist_bucket_ctx *ctx,
    const linkedlist_bucket *buckets,
    size_t capacity,
    size_t size
);

#endif /* MOD_SEPARATE_CHAINING_LINKEDLIST_BUCKET_H */
