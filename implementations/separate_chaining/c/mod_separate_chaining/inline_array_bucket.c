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

#include "backend_util.h"
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

inline_array_bucket_ctx *inline_array_bucket_ctx_create(
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

void inline_array_bucket_ctx_destroy(
    inline_array_bucket_ctx *ctx
) {
    if (ctx == NULL) {
        return;
    }

    slab_pool_destroy(&ctx->pool);
    free(ctx);
}

inline_array_bucket *inline_array_bucket_array_alloc(
    size_t capacity
) {
    return calloc(capacity, sizeof(inline_array_bucket));
}

void inline_array_bucket_array_release(
    inline_array_bucket *buckets
) {
    free(buckets);
}

void inline_array_bucket_array_destroy(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *buckets,
    size_t capacity
) {
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

ht_result inline_array_bucket_insert(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
) {
    inline_array_bucket *bucket;
    inline_array_bucket *tail;
    uint64_t probe_len = 1;

    if (ctx == NULL || buckets == NULL) {
        return HT_ERR_INVALID;
    }

    bucket = &buckets[bucket_index];
    tail = bucket;

    while (bucket != NULL) {
        uint8_t i;

        for (i = 0; i < bucket->used; i++) {
            if (bucket->keys[i] == key) {
                if (probe_len_out != NULL) {
                    *probe_len_out = probe_len;
                }
                return HT_ERR_EXISTS;
            }
            probe_len++;
        }

        if (bucket->used < INLINE_BUCKET_CAPACITY) {
            bucket->keys[bucket->used] = key;
            bucket->values[bucket->used] = value;
            bucket->used++;
            if (probe_len_out != NULL) {
                *probe_len_out = probe_len;
            }
            return HT_OK;
        }

        tail = bucket;
        bucket = bucket->next;
    }

    bucket = inline_array_bucket_overflow_alloc(ctx);
    if (bucket == NULL) {
        if (probe_len_out != NULL) {
            *probe_len_out = probe_len;
        }
        return HT_ERR_OOM;
    }

    bucket->keys[0] = key;
    bucket->values[0] = value;
    bucket->used = 1;
    tail->next = bucket;

    if (probe_len_out != NULL) {
        *probe_len_out = probe_len;
    }

    return HT_OK;
}

ht_result inline_array_bucket_get(
    inline_array_bucket_ctx *ctx,
    const inline_array_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
) {
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

ht_result inline_array_bucket_remove(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
) {
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

ht_result inline_array_bucket_rehash_all(
    inline_array_bucket_ctx *ctx,
    inline_array_bucket *old_buckets,
    size_t old_capacity,
    inline_array_bucket *new_buckets,
    size_t new_capacity,
    ht_hash_fn hash_fn,
    uint64_t hash_seed
) {
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
                ht_result rc = inline_array_bucket_insert(
                    ctx,
                    new_buckets,
                    bucket_index,
                    bucket->keys[j],
                    bucket->values[j],
                    NULL
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

size_t inline_array_bucket_extra_bytes(
    const inline_array_bucket_ctx *ctx,
    const inline_array_bucket *buckets,
    size_t capacity,
    size_t size
) {
    (void)buckets;
    (void)capacity;
    (void)size;

    if (ctx == NULL) {
        return SIZE_MAX;
    }

    return sizeof(*ctx) + slab_pool_bytes_owned(&ctx->pool);
}

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
