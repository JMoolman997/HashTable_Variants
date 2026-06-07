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

#include "mod_separate_chaining_impl.h"

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

extern const mod_separate_chaining_bucket_ops segmented_bucket_ops;

#endif /* MOD_SEPARATE_CHAINING_SEGMENTED_BUCKET_H */
