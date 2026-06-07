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

#include "mod_separate_chaining_impl.h"

#define INLINE_BUCKET_CAPACITY 8u

typedef struct inline_array_bucket {
    struct inline_array_bucket *next; /**< Next overflow bucket in chain. */
    uint8_t used; /**< Number of occupied entries in this bucket. */
    ht_key_t keys[INLINE_BUCKET_CAPACITY]; /**< Stored keys. */
    ht_val_t values[INLINE_BUCKET_CAPACITY]; /**< Stored values. */
} inline_array_bucket;

typedef struct inline_array_bucket_ctx inline_array_bucket_ctx;

extern const mod_separate_chaining_bucket_ops inline_array_bucket_ops;

#endif /* MOD_SEPARATE_CHAINING_INLINE_ARRAY_BUCKET_H */
