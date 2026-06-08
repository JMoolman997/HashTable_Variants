/**
 * @file    inline_array_bucket.c
 * @brief   Fixed-size inline bucket arrays for `mod_separate_chaining`.
 *
 * Implements bucketized chaining where each root bucket stores multiple
 * entries contiguously and only allocates same-shaped overflow buckets when a
 * bucket fills up.
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
#include "inline_array_bucket.h"
#include "slab_pool.h"

/**
 * @brief Allocation context for inline-array overflow buckets.
 */
struct inline_array_bucket_ctx {
    slab_pool pool; /**< Slab pool that owns overflow buckets. */
};

/**
 * @brief Allocate one overflow bucket from the context slab.
 *
 * @param ctx Context whose slab owns overflow buckets.
 *
 * @return Empty overflow bucket on success, or `NULL` on allocation failure.
 */
static inline_array_bucket *inline_array_bucket_overflow_alloc(
    inline_array_bucket_ctx *ctx
);

/**
 * @brief Return one overflow bucket to the context slab.
 *
 * @param ctx Context whose slab owns `bucket`.
 * @param bucket Overflow bucket to free. `NULL` is ignored.
 */
static void inline_array_bucket_overflow_free(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *bucket
);

/**
 * @brief Free every overflow bucket in a chain.
 *
 * @param ctx Context whose slab owns overflow buckets.
 * @param bucket First overflow bucket in the chain.
 */
static void inline_array_bucket_destroy_overflow_chain(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *bucket
);

static void *inline_array_bucket_ctx_create(
    void
) {
    inline_array_bucket_ctx *ctx;

    ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    if (slab_pool_init(
            &ctx->pool,
            sizeof(inline_array_bucket),
            SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK
        ) != 0) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

static void inline_array_bucket_ctx_destroy(
    void *ctx_in
) {
    inline_array_bucket_ctx *ctx = ctx_in;

    if (ctx == NULL) {
        return;
    }

    slab_pool_destroy(&ctx->pool);
    free(ctx);
}

static void *inline_array_bucket_array_alloc(
    size_t capacity
) {
    return calloc(capacity, sizeof(inline_array_bucket));
}

static void inline_array_bucket_array_release(
    void *buckets
) {
    free(buckets);
}

static void inline_array_bucket_array_destroy(
    void *ctx_in,
    void *buckets_in,
    size_t capacity
) {
    inline_array_bucket_ctx *ctx = ctx_in;
    inline_array_bucket *buckets = buckets_in;
    size_t i;

    if (ctx == NULL || buckets == NULL) {
        return;
    }

    for (i = 0; i < capacity; i++) {
        inline_array_bucket_destroy_overflow_chain(ctx, buckets[i].next);
        buckets[i].next = NULL;
        buckets[i].used = 0;
    }

    free(buckets);
}

static ht_result inline_array_bucket_insert_known_absent(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t value
) {
    inline_array_bucket *bucket;
    inline_array_bucket *tail;

    if (ctx == NULL || buckets == NULL) {
        return HT_ERR_INVALID;
    }

    bucket = &buckets[bucket_index];
    tail = bucket;

    while (bucket != NULL) {
        if (bucket->used < INLINE_BUCKET_CAPACITY) {
            bucket->keys[bucket->used] = key;
            bucket->values[bucket->used] = value;
            bucket->used++;
            return HT_OK;
        }

        tail = bucket;
        bucket = bucket->next;
    }

    bucket = inline_array_bucket_overflow_alloc(ctx);
    if (bucket == NULL) {
        return HT_ERR_OOM;
    }

    bucket->keys[0] = key;
    bucket->values[0] = value;
    bucket->used = 1;
    tail->next = bucket;

    return HT_OK;
}

static ht_result inline_array_bucket_insert(
    void *ctx_in,
    void *buckets_in,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
) {
    inline_array_bucket_ctx *ctx = ctx_in;
    inline_array_bucket *buckets = buckets_in;
    inline_array_bucket *bucket;
    inline_array_bucket *tail;
    inline_array_bucket *first_free = NULL;
    uint64_t probe_len = 1;

    if (ctx == NULL || buckets == NULL) {
        return HT_ERR_INVALID;
    }

    tail = &buckets[bucket_index];
    for (bucket = tail; bucket != NULL; bucket = bucket->next) {
        uint8_t i;

        if (first_free == NULL && bucket->used < INLINE_BUCKET_CAPACITY) {
            first_free = bucket;
        }

        for (i = 0; i < bucket->used; i++) {
            if (bucket->keys[i] == key) {
                if (probe_len_out != NULL) {
                    *probe_len_out = probe_len;
                }
                return HT_ERR_EXISTS;
            }
            probe_len++;
        }

        tail = bucket;
    }

    if (first_free == NULL) {
        first_free = inline_array_bucket_overflow_alloc(ctx);
        if (first_free == NULL) {
            if (probe_len_out != NULL) {
                *probe_len_out = probe_len;
            }
            return HT_ERR_OOM;
        }
        tail->next = first_free;
    }

    first_free->keys[first_free->used] = key;
    first_free->values[first_free->used] = value;
    first_free->used++;

    if (probe_len_out != NULL) {
        *probe_len_out = probe_len;
    }

    return HT_OK;
}

static ht_result inline_array_bucket_get(
    void *ctx,
    const void *buckets_in,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
) {
    const inline_array_bucket *buckets = buckets_in;
    const inline_array_bucket *bucket;
    uint64_t probe_len = 1;

    (void)ctx;

    if (buckets == NULL || value_out == NULL) {
        return HT_ERR_INVALID;
    }

    for (bucket = &buckets[bucket_index];
         bucket != NULL;
         bucket = bucket->next) {
        uint8_t i;

        for (i = 0; i < bucket->used; i++) {
            if (bucket->keys[i] == key) {
                if (probe_len_out != NULL) {
                    *probe_len_out = probe_len;
                }
                *value_out = bucket->values[i];
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

static ht_result inline_array_bucket_remove(
    void *ctx_in,
    void *buckets_in,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
) {
    inline_array_bucket_ctx *ctx = ctx_in;
    inline_array_bucket *buckets = buckets_in;
    inline_array_bucket *bucket;
    inline_array_bucket *prev = NULL;
    uint64_t probe_len = 1;

    if (ctx == NULL || buckets == NULL) {
        return HT_ERR_INVALID;
    }

    for (bucket = &buckets[bucket_index];
         bucket != NULL;
         prev = bucket, bucket = bucket->next) {
        uint8_t i;

        for (i = 0; i < bucket->used; i++) {
            if (bucket->keys[i] == key) {
                uint8_t last = (uint8_t)(bucket->used - 1u);

                bucket->keys[i] = bucket->keys[last];
                bucket->values[i] = bucket->values[last];
                bucket->used--;

                /* Root buckets are embedded in the array, so an emptied root
                 * adopts its first overflow bucket instead of being freed. */
                if (bucket != &buckets[bucket_index] && bucket->used == 0) {
                    prev->next = bucket->next;
                    inline_array_bucket_overflow_free(ctx, bucket);
                } else if (bucket == &buckets[bucket_index] &&
                           bucket->used == 0 &&
                           bucket->next != NULL) {
                    inline_array_bucket *next = bucket->next;

                    *bucket = *next;
                    inline_array_bucket_overflow_free(ctx, next);
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

static ht_result inline_array_bucket_rehash_all(
    void *ctx,
    void *old_buckets_in,
    size_t old_capacity,
    void *new_buckets_in,
    size_t new_capacity,
    ht_hash_fn hash_fn,
    uint64_t hash_seed
) {
    inline_array_bucket *old_buckets = old_buckets_in;
    inline_array_bucket *new_buckets = new_buckets_in;
    size_t i;

    if (ctx == NULL || old_buckets == NULL || new_buckets == NULL ||
        hash_fn == NULL) {
        return HT_ERR_INVALID;
    }

    for (i = 0; i < old_capacity; i++) {
        inline_array_bucket *bucket = &old_buckets[i];

        /* Reinsert into the new array so packed root buckets and overflow
         * chains are rebuilt according to the new hash indexes. */
        while (bucket != NULL) {
            uint8_t j;

            for (j = 0; j < bucket->used; j++) {
                size_t bucket_index = HT_INDEX_FOR_U64(
                    hash_fn(bucket->keys[j], hash_seed),
                    new_capacity
                );
                ht_result rc = inline_array_bucket_insert_known_absent(
                    ctx,
                    new_buckets,
                    bucket_index,
                    bucket->keys[j],
                    bucket->values[j]
                );

                if (rc != HT_OK) {
                    return rc;
                }
            }

            bucket = bucket->next;
        }
    }

    for (i = 0; i < old_capacity; i++) {
        inline_array_bucket_destroy_overflow_chain(ctx, old_buckets[i].next);
        old_buckets[i].next = NULL;
        old_buckets[i].used = 0;
    }

    return HT_OK;
}

static size_t inline_array_bucket_extra_bytes(
    const void *ctx_in,
    const void *buckets,
    size_t capacity,
    size_t size
) {
    const inline_array_bucket_ctx *ctx = ctx_in;

    (void)buckets;
    (void)capacity;
    (void)size;

    if (ctx == NULL) {
        return SIZE_MAX;
    }

    return sizeof(*ctx) + slab_pool_bytes_owned(&ctx->pool);
}

const mod_separate_chaining_bucket_ops inline_array_bucket_ops = {
    .bucket_size = sizeof(inline_array_bucket),
    .ctx_create = inline_array_bucket_ctx_create,
    .ctx_destroy = inline_array_bucket_ctx_destroy,
    .array_alloc = inline_array_bucket_array_alloc,
    .array_release = inline_array_bucket_array_release,
    .array_destroy = inline_array_bucket_array_destroy,
    .insert = inline_array_bucket_insert,
    .get = inline_array_bucket_get,
    .remove = inline_array_bucket_remove,
    .rehash_all = inline_array_bucket_rehash_all,
    .extra_bytes = inline_array_bucket_extra_bytes
};

static inline_array_bucket *inline_array_bucket_overflow_alloc(
    inline_array_bucket_ctx *ctx
) {
    inline_array_bucket *bucket;

    if (ctx == NULL) {
        return NULL;
    }

    bucket = slab_pool_alloc(&ctx->pool);
    if (bucket == NULL) {
        return NULL;
    }

    bucket->next = NULL;
    bucket->used = 0;
    return bucket;
}

static void inline_array_bucket_overflow_free(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *bucket
) {
    if (ctx == NULL || bucket == NULL) {
        return;
    }

    slab_pool_free(&ctx->pool, bucket);
}

static void inline_array_bucket_destroy_overflow_chain(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *bucket
) {
    while (bucket != NULL) {
        inline_array_bucket *next = bucket->next;
        inline_array_bucket_overflow_free(ctx, bucket);
        bucket = next;
    }
}
