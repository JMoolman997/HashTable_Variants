/**
 * @file    segmented_bucket.c
 * @brief   Segmented chaining buckets for `mod_separate_chaining`.
 *
 * Implements segmented chaining where each segment stores multiple entries
 * contiguously and segments are linked only when a bucket runs out of room.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "backend_config.h"
#include "capacity_util.h"
#include "hash_util.h"
#include "memory_util.h"
#include "resize_stats.h"
#include "stats_util.h"
#include "segmented_bucket.h"
#include "slab_pool.h"

/**
 * @brief Allocation context for segmented bucket chains.
 */
struct segmented_bucket_ctx {
    slab_pool pool; /**< Slab pool that owns bucket segments. */
};

/**
 * @brief Allocate one segment from the context slab.
 *
 * @param ctx Context whose slab owns segment storage.
 *
 * @return Empty segment on success, or `NULL` on allocation failure.
 */
static bucket_segment_t *segmented_bucket_segment_alloc(
    segmented_bucket_ctx *ctx
);

/**
 * @brief Return one segment to the context slab.
 *
 * @param ctx Context whose slab owns `segment`.
 * @param segment Segment to free. `NULL` is ignored.
 */
static void segmented_bucket_segment_free(
    segmented_bucket_ctx *ctx,
    bucket_segment_t *segment
);

static void *segmented_bucket_ctx_create(
    void
) {
    segmented_bucket_ctx *ctx;

    ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    if (slab_pool_init(
            &ctx->pool,
            sizeof(bucket_segment_t),
            SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK
        ) != 0) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

static void segmented_bucket_ctx_destroy(
    void *ctx_in
) {
    segmented_bucket_ctx *ctx = ctx_in;

    if (ctx == NULL) {
        return;
    }

    slab_pool_destroy(&ctx->pool);
    free(ctx);
}

static void *segmented_bucket_array_alloc(
    size_t capacity
) {
    return calloc(capacity, sizeof(segmented_bucket));
}

static void segmented_bucket_array_release(
    void *buckets
) {
    free(buckets);
}

static void segmented_bucket_array_destroy(
    void *ctx_in,
    void *buckets_in,
    size_t capacity
) {
    segmented_bucket_ctx *ctx = ctx_in;
    segmented_bucket *buckets = buckets_in;
    size_t i;

    if (ctx == NULL || buckets == NULL) {
        return;
    }

    for (i = 0; i < capacity; i++) {
        bucket_segment_t *segment = buckets[i].head;

        while (segment != NULL) {
            bucket_segment_t *next = segment->next;
            segmented_bucket_segment_free(ctx, segment);
            segment = next;
        }
    }

    free(buckets);
}

static ht_result segmented_bucket_insert_absent(
    void *ctx_in,
    void *buckets_in,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
) {
    segmented_bucket_ctx *ctx = ctx_in;
    segmented_bucket *buckets = buckets_in;
    bucket_segment_t *segment;
    bucket_segment_t *tail;
    uint64_t probe_len = 1;

    if (ctx == NULL || buckets == NULL) {
        return HT_ERR_INVALID;
    }

    /* Segmented buckets allocate their first segment lazily so empty buckets
     * cost only one head pointer in the root array. */
    if (buckets[bucket_index].head == NULL) {
        buckets[bucket_index].head = segmented_bucket_segment_alloc(ctx);
        if (buckets[bucket_index].head == NULL) {
            if (probe_len_out != NULL) {
                *probe_len_out = probe_len;
            }
            return HT_ERR_OOM;
        }
    }

    segment = buckets[bucket_index].head;
    tail = segment;

    while (segment != NULL) {
        if (segment->used < SEGMENT_CAPACITY) {
            probe_len += segment->used;
            segment->keys[segment->used] = key;
            segment->values[segment->used] = value;
            segment->used++;
            if (probe_len_out != NULL) {
                *probe_len_out = probe_len;
            }
            return HT_OK;
        }

        probe_len += segment->used;
        tail = segment;
        segment = segment->next;
    }

    segment = segmented_bucket_segment_alloc(ctx);
    if (segment == NULL) {
        if (probe_len_out != NULL) {
            *probe_len_out = probe_len;
        }
        return HT_ERR_OOM;
    }

    segment->keys[0] = key;
    segment->values[0] = value;
    segment->used = 1;
    tail->next = segment;

    if (probe_len_out != NULL) {
        *probe_len_out = probe_len;
    }

    return HT_OK;
}

static ht_result segmented_bucket_get(
    void *ctx,
    const void *buckets_in,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
) {
    const segmented_bucket *buckets = buckets_in;
    const bucket_segment_t *segment;
    uint64_t probe_len = 1;

    (void)ctx;

    if (buckets == NULL || value_out == NULL) {
        return HT_ERR_INVALID;
    }

    for (segment = buckets[bucket_index].head;
         segment != NULL;
         segment = segment->next) {
        uint8_t i;

        for (i = 0; i < segment->used; i++) {
            if (segment->keys[i] == key) {
                if (probe_len_out != NULL) {
                    *probe_len_out = probe_len;
                }
                *value_out = segment->values[i];
                return HT_OK;
            }
            probe_len++;
        }
    }

    if (probe_len_out != NULL) {
        *probe_len_out = probe_len;
    }

    return HT_ERR_NOT_FOUND;
}

static ht_result segmented_bucket_remove(
    void *ctx_in,
    void *buckets_in,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
) {
    segmented_bucket_ctx *ctx = ctx_in;
    segmented_bucket *buckets = buckets_in;
    bucket_segment_t *segment;
    bucket_segment_t *prev = NULL;
    uint64_t probe_len = 1;

    if (ctx == NULL || buckets == NULL) {
        return HT_ERR_INVALID;
    }

    for (segment = buckets[bucket_index].head;
         segment != NULL;
         prev = segment, segment = segment->next) {
        uint8_t i;

        for (i = 0; i < segment->used; i++) {
            if (segment->keys[i] == key) {
                uint8_t last = (uint8_t)(segment->used - 1u);

                segment->keys[i] = segment->keys[last];
                segment->values[i] = segment->values[last];
                segment->used--;

                /* Empty segments are removed from the chain immediately,
                 * including the head segment referenced by the root bucket. */
                if (segment->used == 0) {
                    if (prev == NULL) {
                        buckets[bucket_index].head = segment->next;
                    } else {
                        prev->next = segment->next;
                    }
                    segmented_bucket_segment_free(ctx, segment);
                }

                if (probe_len_out != NULL) {
                    *probe_len_out = probe_len;
                }
                return HT_OK;
            }
            probe_len++;
        }
    }

    if (probe_len_out != NULL) {
        *probe_len_out = probe_len;
    }

    return HT_ERR_NOT_FOUND;
}

static ht_result segmented_bucket_rehash_all(
    void *ctx,
    void *old_buckets_in,
    size_t old_capacity,
    void *new_buckets_in,
    size_t new_capacity,
    ht_hash_fn hash_fn,
    uint64_t hash_seed
) {
    segmented_bucket *old_buckets = old_buckets_in;
    segmented_bucket *new_buckets = new_buckets_in;
    size_t i;

    if (ctx == NULL || old_buckets == NULL || new_buckets == NULL ||
        hash_fn == NULL) {
        return HT_ERR_INVALID;
    }

    for (i = 0; i < old_capacity; i++) {
        bucket_segment_t *segment = old_buckets[i].head;

        /* Reinsert into the new array so each destination bucket can choose
         * compact segment chains for the new capacity. */
        while (segment != NULL) {
            uint8_t j;

            for (j = 0; j < segment->used; j++) {
                size_t bucket_index = HT_INDEX_FOR_U64(
                    hash_fn(segment->keys[j], hash_seed),
                    new_capacity
                );
                ht_result rc = segmented_bucket_insert_absent(
                    ctx,
                    new_buckets,
                    bucket_index,
                    segment->keys[j],
                    segment->values[j],
                    NULL
                );

                if (rc != HT_OK) {
                    return rc;
                }
            }

            segment = segment->next;
        }
    }

    for (i = 0; i < old_capacity; i++) {
        bucket_segment_t *segment = old_buckets[i].head;

        while (segment != NULL) {
            bucket_segment_t *next = segment->next;
            segmented_bucket_segment_free(ctx, segment);
            segment = next;
        }

        old_buckets[i].head = NULL;
    }

    return HT_OK;
}

static size_t segmented_bucket_extra_bytes(
    const void *ctx_in,
    const void *buckets,
    size_t capacity,
    size_t size
) {
    const segmented_bucket_ctx *ctx = ctx_in;

    (void)buckets;
    (void)capacity;
    (void)size;

    if (ctx == NULL) {
        return SIZE_MAX;
    }

    return sizeof(*ctx) + slab_pool_bytes_owned(&ctx->pool);
}

const mod_separate_chaining_bucket_ops segmented_bucket_ops = {
    .bucket_size = sizeof(segmented_bucket),
    .ctx_create = segmented_bucket_ctx_create,
    .ctx_destroy = segmented_bucket_ctx_destroy,
    .array_alloc = segmented_bucket_array_alloc,
    .array_release = segmented_bucket_array_release,
    .array_destroy = segmented_bucket_array_destroy,
    .insert_absent = segmented_bucket_insert_absent,
    .get = segmented_bucket_get,
    .remove = segmented_bucket_remove,
    .rehash_all = segmented_bucket_rehash_all,
    .extra_bytes = segmented_bucket_extra_bytes
};

static bucket_segment_t *segmented_bucket_segment_alloc(
    segmented_bucket_ctx *ctx
) {
    bucket_segment_t *segment;

    if (ctx == NULL) {
        return NULL;
    }

    segment = slab_pool_alloc(&ctx->pool);
    if (segment == NULL) {
        return NULL;
    }

    segment->next = NULL;
    segment->used = 0;
    return segment;
}

static void segmented_bucket_segment_free(
    segmented_bucket_ctx *ctx,
    bucket_segment_t *segment
) {
    if (ctx == NULL || segment == NULL) {
        return;
    }

    slab_pool_free(&ctx->pool, segment);
}
