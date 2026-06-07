/**
 * @file    linkedlist_bucket.c
 * @brief   Linked-list bucket operations for `mod_separate_chaining`.
 *
 * Implements the linked-list bucket storage used by the modified
 * separate-chaining backend. List nodes are allocated exclusively from the
 * bucket-local slab pool and returned to that pool on removal or teardown.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "backend_util.h"
#include "linkedlist_bucket.h"
#include "slab_pool.h"

/**
 * @brief One linked-list node stored in a modified chaining bucket.
 */
typedef struct linkedlist_bucket_node {
    ht_key_t key; /**< Stored key. */
    ht_val_t value; /**< Stored value. */
    struct linkedlist_bucket_node *next; /**< Next node in the bucket chain. */
} linkedlist_bucket_node;

/**
 * @brief Allocation context for linked-list bucket nodes.
 */
struct linkedlist_bucket_ctx {
    slab_pool pool; /**< Slab pool that owns list nodes. */
};

/**
 * @brief Find a key in a linked bucket chain.
 *
 * @param head First node in the bucket chain.
 * @param key Key to find.
 * @param node_out Output for the matching node, or `NULL` when absent.
 * @param prev_out Optional output for the previous node.
 * @param probe_len_out Optional output for nodes examined.
 *
 * @return `HT_OK` when found, `HT_ERR_NOT_FOUND`, or `HT_ERR_INVALID`.
 */
static ht_result linkedlist_bucket_find_node(
    linkedlist_bucket head,
    ht_key_t key,
    linkedlist_bucket_node **node_out,
    linkedlist_bucket_node **prev_out,
    uint64_t *probe_len_out
);

/**
 * @brief Allocate one linked-list node from the context slab.
 *
 * @param ctx Context whose slab owns node storage.
 *
 * @return Node storage on success, or `NULL` on allocation failure.
 */
static linkedlist_bucket_node *linkedlist_bucket_node_alloc(
    linkedlist_bucket_ctx *ctx
);

/**
 * @brief Return one linked-list node to the context slab.
 *
 * @param ctx Context whose slab owns `node`.
 * @param node Node to free. `NULL` is ignored.
 */
static void linkedlist_bucket_node_free(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket_node *node
);

linkedlist_bucket_ctx *linkedlist_bucket_ctx_create(
    void
) {
    linkedlist_bucket_ctx *ctx;

    ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    if (slab_pool_init(
            &ctx->pool,
            sizeof(linkedlist_bucket_node),
            SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK
        ) != 0) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void linkedlist_bucket_ctx_destroy(
    linkedlist_bucket_ctx *ctx
) {
    if (ctx == NULL) {
        return;
    }

    slab_pool_destroy(&ctx->pool);
    free(ctx);
}

linkedlist_bucket *linkedlist_bucket_array_alloc(
    size_t capacity
) {
    return calloc(capacity, sizeof(linkedlist_bucket));
}

void linkedlist_bucket_array_release(
    linkedlist_bucket *buckets
) {
    free(buckets);
}

void linkedlist_bucket_array_destroy(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket *buckets,
    size_t capacity
) {
    size_t i;

    if (buckets == NULL) {
        return;
    }

    for (i = 0; i < capacity; i++) {
        linkedlist_bucket_node *node = buckets[i];

        while (node != NULL) {
            linkedlist_bucket_node *next = node->next;
            linkedlist_bucket_node_free(ctx, node);
            node = next;
        }
    }

    free(buckets);
}

ht_result linkedlist_bucket_insert(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
) {
    linkedlist_bucket_node *node;
    uint64_t probe_len = 1;

    if (ctx == NULL || buckets == NULL) {
        return HT_ERR_INVALID;
    }

    for (node = buckets[bucket_index]; node != NULL; node = node->next) {
        if (node->key == key) {
            if (probe_len_out != NULL) {
                *probe_len_out = probe_len;
            }
            return HT_ERR_EXISTS;
        }
        probe_len++;
    }

    node = linkedlist_bucket_node_alloc(ctx);
    if (node == NULL) {
        if (probe_len_out != NULL) {
            *probe_len_out = probe_len;
        }
        return HT_ERR_OOM;
    }

    node->key = key;
    node->value = value;
    node->next = buckets[bucket_index];
    buckets[bucket_index] = node;

    if (probe_len_out != NULL) {
        *probe_len_out = probe_len;
    }

    return HT_OK;
}

ht_result linkedlist_bucket_get(
    linkedlist_bucket_ctx *ctx,
    const linkedlist_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
) {
    linkedlist_bucket_node *node;
    ht_result rc;

    (void)ctx;

    if (buckets == NULL || value_out == NULL) {
        return HT_ERR_INVALID;
    }

    rc = linkedlist_bucket_find_node(
        buckets[bucket_index],
        key,
        &node,
        NULL,
        probe_len_out
    );
    if (rc != HT_OK) {
        return rc;
    }

    *value_out = node->value;
    return HT_OK;
}

ht_result linkedlist_bucket_remove(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
) {
    linkedlist_bucket_node *node;
    linkedlist_bucket_node *prev;
    ht_result rc;

    if (ctx == NULL || buckets == NULL) {
        return HT_ERR_INVALID;
    }

    rc = linkedlist_bucket_find_node(
        buckets[bucket_index],
        key,
        &node,
        &prev,
        probe_len_out
    );
    if (rc != HT_OK) {
        return rc;
    }

    /* Unlink in place without moving any other nodes. */
    if (prev == NULL) {
        buckets[bucket_index] = node->next;
    } else {
        prev->next = node->next;
    }

    linkedlist_bucket_node_free(ctx, node);
    return HT_OK;
}

ht_result linkedlist_bucket_rehash_all(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket *old_buckets,
    size_t old_capacity,
    linkedlist_bucket *new_buckets,
    size_t new_capacity,
    ht_hash_fn hash_fn,
    uint64_t hash_seed
) {
    size_t i;

    (void)ctx;

    if (old_buckets == NULL || new_buckets == NULL || hash_fn == NULL) {
        return HT_ERR_INVALID;
    }

    for (i = 0; i < old_capacity; i++) {
        linkedlist_bucket_node *node = old_buckets[i];

        /* Linked-list rehash can move existing nodes directly because bucket
         * membership is represented only by `next` pointers. */
        while (node != NULL) {
            linkedlist_bucket_node *next = node->next;
            size_t bucket = HT_INDEX_FOR_U64(
                hash_fn(node->key, hash_seed),
                new_capacity
            );

            node->next = new_buckets[bucket];
            new_buckets[bucket] = node;
            node = next;
        }

        old_buckets[i] = NULL;
    }

    return HT_OK;
}

size_t linkedlist_bucket_extra_bytes(
    const linkedlist_bucket_ctx *ctx,
    const linkedlist_bucket *buckets,
    size_t capacity,
    size_t size
) {
    (void)buckets;
    (void)capacity;

    if (ctx == NULL) {
        return SIZE_MAX;
    }

    (void)size;
    return sizeof(*ctx) + slab_pool_bytes_owned(&ctx->pool);
}

static ht_result linkedlist_bucket_find_node(
    linkedlist_bucket head,
    ht_key_t key,
    linkedlist_bucket_node **node_out,
    linkedlist_bucket_node **prev_out,
    uint64_t *probe_len_out
) {
    linkedlist_bucket_node *node = head;
    linkedlist_bucket_node *prev = NULL;
    uint64_t probe_len = 1;

    if (node_out == NULL) {
        return HT_ERR_INVALID;
    }

    while (node != NULL) {
        if (node->key == key) {
            if (prev_out != NULL) {
                *prev_out = prev;
            }
            if (probe_len_out != NULL) {
                *probe_len_out = probe_len;
            }
            *node_out = node;
            return HT_OK;
        }

        prev = node;
        node = node->next;
        probe_len++;
    }

    if (prev_out != NULL) {
        *prev_out = prev;
    }
    if (probe_len_out != NULL) {
        *probe_len_out = probe_len;
    }
    *node_out = NULL;

    return HT_ERR_NOT_FOUND;
}

static linkedlist_bucket_node *linkedlist_bucket_node_alloc(
    linkedlist_bucket_ctx *ctx
) {
    if (ctx == NULL) {
        return NULL;
    }

    return slab_pool_alloc(&ctx->pool);
}

static void linkedlist_bucket_node_free(
    linkedlist_bucket_ctx *ctx,
    linkedlist_bucket_node *node
) {
    if (ctx == NULL || node == NULL) {
        return;
    }

    slab_pool_free(&ctx->pool, node);
}
