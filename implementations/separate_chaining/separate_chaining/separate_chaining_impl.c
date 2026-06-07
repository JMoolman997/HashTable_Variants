/**
 * @file    separate_chaining_impl.c
 * @brief   Linked-list separate-chaining hashtable backend.
 *
 * Implements the separate-chaining backend used by the generic hashtable
 * wrapper. The table stores 64-bit integer keys and values, uses power-of-two
 * bucket counts, linked bucket chains, optional resizing, and optional
 * statistics collection for benchmarking.
 *
 * @author  J.W. Moolman
 * @date    2026-03-30
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend_util.h"
#include "separate_chaining_impl.h"
#include "ht_internal.h"

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Release all memory owned by a separate-chaining backend instance.
 *
 * @param impl Separate-chaining backend instance to destroy.
 */
static void separate_chaining_destroy_impl(
    void *impl
);

/**
 * @brief Insert a key/value pair through the separate-chaining backend.
 *
 * @param impl Separate-chaining backend instance to modify.
 * @param key Key to insert.
 * @param value Value to associate with `key`.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`,
 *         `HT_ERR_EXISTS`, or `HT_ERR_OOM`.
 */
static ht_result separate_chaining_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
);

/**
 * @brief Look up a key through the separate-chaining backend.
 *
 * @param impl Separate-chaining backend instance to query.
 * @param key Key to search for.
 * @param value_out Output location that receives the value on success.
 *
 * @return `HT_OK` when found, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result separate_chaining_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
);

/**
 * @brief Remove a key through the separate-chaining backend.
 *
 * @param impl Separate-chaining backend instance to modify.
 * @param key Key to remove.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result separate_chaining_remove_impl(
    void *impl,
    ht_key_t key
);

/**
 * @brief Return the live entry count from a separate-chaining backend.
 *
 * @param impl Separate-chaining backend instance to query.
 *
 * @return The number of live entries, or `0` if `impl` is invalid.
 */
static size_t separate_chaining_size_impl(
    const void *impl
);

/**
 * @brief Return the current bucket count from a separate-chaining backend.
 *
 * @param impl Separate-chaining backend instance to query.
 *
 * @return The current bucket count, or `0` if `impl` is invalid.
 */
static size_t separate_chaining_capacity_impl(
    const void *impl
);

/**
 * @brief Return the current load factor from a separate-chaining backend.
 *
 * @param impl Separate-chaining backend instance to query.
 *
 * @return The load factor, or `0.0` if `impl` is invalid.
 */
static double separate_chaining_load_factor_impl(
    const void *impl
);

/**
 * @brief Ensure the backend can hold at least the requested capacity.
 *
 * @param impl Separate-chaining backend instance to resize if needed.
 * @param capacity Minimum target capacity.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result separate_chaining_reserve_impl(
    void *impl,
    size_t capacity
);

/**
 * @brief Rehash the backend around a requested capacity.
 *
 * @param impl Separate-chaining backend instance to rebuild.
 * @param capacity Requested target capacity.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result separate_chaining_rehash_impl(
    void *impl,
    size_t capacity
);

/**
 * @brief Copy the statistics snapshot out of a separate-chaining backend.
 *
 * @param impl Separate-chaining backend instance to query.
 * @param out Output structure that receives the copied statistics.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
static ht_result separate_chaining_get_stats_impl(
    const void *impl,
    ht_stats *out
);

/**
 * @brief Reset the statistics counters stored by a separate-chaining backend.
 *
 * @param impl Separate-chaining backend instance whose counters should be cleared.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if `impl` is invalid.
 */
static ht_result separate_chaining_reset_stats_impl(
    void *impl
);

/**
 * @brief Recompute the approximate memory footprint stored in stats.
 *
 * @param t Table whose byte-count statistic should be refreshed.
 */
static void separate_chaining_update_bytes_used(
    separate_chaining_table *t
);

/**
 * @brief Free every node reachable from the table's bucket array.
 *
 * @param t Table whose chains should be released.
 */
static void separate_chaining_free_nodes(
    separate_chaining_table *t
);

/**
 * @brief Rebuild the bucket array at a new capacity.
 *
 * @param t Table to resize.
 * @param new_capacity Requested target capacity before normalization.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result separate_chaining_resize(
    separate_chaining_table *t,
    size_t new_capacity
);

/**
 * @brief Find a node for a key inside its hashed bucket chain.
 *
 * @param t Table to search.
 * @param key Key to search for.
 * @param hash Mixed hash for the key.
 * @param bucket_out Optional output for the bucket index.
 * @param node_out Output location for the matching node.
 * @param prev_out Optional output for the previous node in the chain.
 * @param probe_len_out Optional output for the number of inspected nodes.
 *
 * @return `HT_OK` when found, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result separate_chaining_find_node(
    const separate_chaining_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *bucket_out,
    separate_chaining_node **node_out,
    separate_chaining_node **prev_out,
    uint64_t *probe_len_out
);

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable SEPARATE_CHAINING_VTABLE = {
    .destroy = separate_chaining_destroy_impl,
    .insert = separate_chaining_insert_impl,
    .get = separate_chaining_get_impl,
    .remove = separate_chaining_remove_impl,
    .size = separate_chaining_size_impl,
    .capacity = separate_chaining_capacity_impl,
    .load_factor = separate_chaining_load_factor_impl,
    .reserve = separate_chaining_reserve_impl,
    .rehash = separate_chaining_rehash_impl,
    .get_stats = separate_chaining_get_stats_impl,
    .reset_stats = separate_chaining_reset_stats_impl,
    .bind_bench_iface = separate_chaining_bind_bench_iface
};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

ht_result separate_chaining_create_impl_ex(
    const ht_config *cfg,
    void           **out
) {
    separate_chaining_table *t;
    size_t capacity;
    size_t min_capacity;

    if (out == NULL) {
        return HT_ERR_INVALID;
    }
    *out = NULL;

    if (cfg == NULL) {
        return HT_ERR_INVALID;
    }

    t = calloc(1, sizeof(*t));
    if (t == NULL) {
        return HT_ERR_OOM;
    }

    capacity = (cfg->init_capacity > 0)
        ? cfg->init_capacity
        : SEPARATE_CHAINING_DEFAULT_INITIAL_CAPACITY;
    capacity = next_pow2(capacity);

    min_capacity = (cfg->min_capacity > 0)
        ? cfg->min_capacity
        : SEPARATE_CHAINING_DEFAULT_MIN_CAPACITY;
    min_capacity = next_pow2(min_capacity);

    if (capacity == 0 || min_capacity == 0) {
        free(t);
        return HT_ERR_INVALID;
    }

    if (capacity < min_capacity) {
        capacity = min_capacity;
    }

    t->buckets = calloc(capacity, sizeof(*t->buckets));
    if (t->buckets == NULL) {
        free(t);
        return HT_ERR_OOM;
    }

    t->capacity     = capacity;
    t->min_capacity = min_capacity;
    t->size         = 0;

    t->max_load_factor = (cfg->max_load_factor > 0.0)
        ? cfg->max_load_factor
        : SEPARATE_CHAINING_DEFAULT_MAX_LOAD;
    t->min_load_factor = (cfg->min_load_factor > 0.0)
        ? cfg->min_load_factor
        : SEPARATE_CHAINING_DEFAULT_MIN_LOAD;

    t->resize_mode   = cfg->rsz_mode;
    t->hash_fn       = (cfg->hash_fn != NULL)
        ? cfg->hash_fn
        : default_hash;
    t->hash_seed     = cfg->hash_seed;
    t->collect_stats = cfg->collect_stats;

    separate_chaining_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    *out = t;
    return HT_OK;
}

const struct ht_vtable *separate_chaining_vtable(
    void
) {
    return &SEPARATE_CHAINING_VTABLE;
}

int separate_chaining_bind_bench_iface(
    void *ctx,
    bench_iface *out
) {
    if (ctx == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    out->ctx    = ctx;
    out->insert = separate_chaining_insert_impl;
    out->get    = separate_chaining_get_impl;
    out->remove = separate_chaining_remove_impl;

    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void separate_chaining_destroy_impl(
    void *impl
) {
    separate_chaining_table *t = impl;

    if (t == NULL) {
        return;
    }

    separate_chaining_free_nodes(t);
    free(t->buckets);
    free(t);
}

static ht_result separate_chaining_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
) {
    separate_chaining_table *t = impl;
    separate_chaining_node *node;
    uint64_t hash;
    size_t bucket;
    uint64_t probe_len;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    HT_STATS_INC(t, inserts);

    hash      = t->hash_fn(key, t->hash_seed);
    bucket    = HT_INDEX_FOR_U64(hash, t->capacity);
    probe_len = 1;

    for (node = t->buckets[bucket]; node != NULL; node = node->next) {
        if (node->key == key) {
            HT_UPDATE_PROBE_STATS(t, probe_len);
            HT_RECORD_INSERT_FAILURE(t);
            return HT_ERR_EXISTS;
        }
        probe_len++;
    }

    if (t->resize_mode != HT_RESIZE_NONE &&
        HT_SHOULD_GROW_COUNT(t, size)) {
        rc = separate_chaining_resize(
            t,
            t->capacity * 2
        );
        if (rc != HT_OK) {
            HT_RECORD_INSERT_FAILURE(t);
            return rc;
        }
        bucket = HT_INDEX_FOR_U64(hash, t->capacity);
        probe_len = 1;
        for (node = t->buckets[bucket]; node != NULL; node = node->next) {
            probe_len++;
        }
    } else if (t->resize_mode == HT_RESIZE_NONE &&
               (double)(t->size + 1) >
               (double)t->capacity * t->max_load_factor) {
        HT_UPDATE_PROBE_STATS(t, probe_len);
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_FULL;
    }

    node = malloc(sizeof(*node));
    if (node == NULL) {
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_OOM;
    }

    node->key   = key;
    node->value = value;
    /* Prepend into the target bucket so insertion stays O(1). */
    node->next  = t->buckets[bucket];
    t->buckets[bucket] = node;
    t->size++;

    HT_UPDATE_PROBE_STATS(t, probe_len);
    separate_chaining_update_bytes_used(t);

    return HT_OK;
}

static ht_result separate_chaining_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
) {
    const separate_chaining_table *t = impl;
    separate_chaining_node *node;
    uint64_t hash;
    uint64_t probe_len;
    ht_result rc;

    if (t == NULL || value_out == NULL) {
        return HT_ERR_INVALID;
    }

    if (t->collect_stats) {
        ((separate_chaining_table *)t)->stats.lookups++;
    }

    hash = t->hash_fn(key, t->hash_seed);
    rc = separate_chaining_find_node(
        t,
        key,
        hash,
        NULL,
        &node,
        NULL,
        &probe_len
    );

    if (rc != HT_OK) {
        if (t->collect_stats) {
            ((separate_chaining_table *)t)->stats.lookup_misses++;
            HT_UPDATE_PROBE_STATS((separate_chaining_table *)t, probe_len);
        }
        return HT_ERR_NOT_FOUND;
    }

    HT_UPDATE_PROBE_STATS((separate_chaining_table *)t, probe_len);
    *value_out = node->value;

    return HT_OK;
}

static ht_result separate_chaining_remove_impl(
    void *impl,
    ht_key_t key
) {
    separate_chaining_table *t = impl;
    separate_chaining_node *node;
    separate_chaining_node *prev;
    size_t bucket;
    uint64_t hash;
    uint64_t probe_len;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (t->collect_stats) {
        t->stats.removes++;
    }

    hash = t->hash_fn(key, t->hash_seed);
    rc = separate_chaining_find_node(
        t,
        key,
        hash,
        &bucket,
        &node,
        &prev,
        &probe_len
    );

    if (rc != HT_OK) {
        if (t->collect_stats) {
            t->stats.remove_misses++;
            HT_UPDATE_PROBE_STATS(t, probe_len);
        }
        return HT_ERR_NOT_FOUND;
    }

    HT_UPDATE_PROBE_STATS(t, probe_len);

    /* Unlink the node from the singly linked bucket chain in place. */
    if (prev == NULL) {
        t->buckets[bucket] = node->next;
    } else {
        prev->next = node->next;
    }

    free(node);
    t->size--;
    separate_chaining_update_bytes_used(t);

    if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
        HT_SHOULD_SHRINK_COUNT(t, size)) {
        size_t new_capacity = t->capacity / 2;

        if (new_capacity < t->min_capacity) {
            new_capacity = t->min_capacity;
        }

        return separate_chaining_resize(t, new_capacity);
    }

    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* metadata/stat ops                                                         */
/* ------------------------------------------------------------------------- */

static size_t separate_chaining_size_impl(
    const void *impl
) {
    const separate_chaining_table *t = impl;
    return (t != NULL) ? t->size : 0;
}

static size_t separate_chaining_capacity_impl(
    const void *impl
) {
    const separate_chaining_table *t = impl;
    return (t != NULL) ? t->capacity : 0;
}

static double separate_chaining_load_factor_impl(
    const void *impl
) {
    const separate_chaining_table *t = impl;

    return (t != NULL)
        ? ht_load_factor_snapshot(t->size, t->capacity)
        : 0.0;
}

static ht_result separate_chaining_reserve_impl(
    void *impl,
    size_t capacity
) {
    separate_chaining_table *t = impl;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (capacity <= t->capacity) {
        return HT_OK;
    }

    return separate_chaining_resize(t, next_pow2(capacity));
}

static ht_result separate_chaining_rehash_impl(
    void *impl,
    size_t capacity
) {
    separate_chaining_table *t = impl;
    size_t target;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    target = (capacity > t->size) ? capacity : t->size;

    if (target < t->min_capacity) {
        target = t->min_capacity;
    }

    return separate_chaining_resize(t, next_pow2(target));
}

static ht_result separate_chaining_get_stats_impl(
    const void *impl,
    ht_stats *out
) {
    const separate_chaining_table *t = impl;

    if (t == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    *out = t->stats;
    return HT_OK;
}

static ht_result separate_chaining_reset_stats_impl(
    void *impl
) {
    separate_chaining_table *t = impl;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    memset(&t->stats, 0, sizeof(t->stats));
    separate_chaining_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void separate_chaining_update_bytes_used(
    separate_chaining_table *t
) {
    if (!t->collect_stats) {
        return;
    }

    t->stats.bytes_used =
        sizeof(*t) +
        t->capacity * sizeof(*t->buckets) +
        t->size * sizeof(separate_chaining_node)
    ;
}

static void separate_chaining_free_nodes(
    separate_chaining_table *t
) {
    size_t i;

    if (t == NULL || t->buckets == NULL) {
        return;
    }

    for (i = 0; i < t->capacity; i++) {
        separate_chaining_node *node = t->buckets[i];

        while (node != NULL) {
            separate_chaining_node *next = node->next;
            free(node);
            node = next;
        }
    }
}

static ht_result separate_chaining_resize(
    separate_chaining_table *t,
    size_t new_capacity
) {
    separate_chaining_node **old_buckets;
    separate_chaining_node **new_buckets;
    size_t old_capacity;
    size_t old_size;
    uint64_t resize_start_ns;
    size_t i;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (new_capacity < t->min_capacity) {
        new_capacity = t->min_capacity;
    }

    new_capacity = next_pow2(new_capacity);

    if (new_capacity == t->capacity) {
        return HT_OK;
    }

    new_buckets = calloc(new_capacity, sizeof(*new_buckets));
    if (new_buckets == NULL) {
        return HT_ERR_OOM;
    }

    old_buckets  = t->buckets;
    old_capacity = t->capacity;
    old_size     = t->size;
    resize_start_ns = ht_resize_instrumentation_start(t->collect_stats);

    for (i = 0; i < old_capacity; i++) {
        separate_chaining_node *node = old_buckets[i];

        while (node != NULL) {
            separate_chaining_node *next = node->next;
            size_t bucket = HT_INDEX_FOR_U64(
                t->hash_fn(node->key, t->hash_seed),
                new_capacity
            );

            /* Re-thread each existing node into its new bucket without
             * allocating replacement nodes. */
            node->next          = new_buckets[bucket];
            new_buckets[bucket] = node;
            node                = next;
        }
    }

    t->buckets  = new_buckets;
    t->capacity = new_capacity;

    free(old_buckets);

    ht_resize_stats_record(
        &t->stats,
        t->collect_stats,
        old_capacity,
        t->capacity,
        old_size,
        resize_start_ns
    );
    separate_chaining_update_bytes_used(t);

    return HT_OK;
}

static ht_result separate_chaining_find_node(
    const separate_chaining_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *bucket_out,
    separate_chaining_node **node_out,
    separate_chaining_node **prev_out,
    uint64_t *probe_len_out
) {
    separate_chaining_node *node;
    separate_chaining_node *prev;
    size_t bucket;
    uint64_t probe_len;

    if (t == NULL || node_out == NULL) {
        return HT_ERR_INVALID;
    }

    bucket    = HT_INDEX_FOR_U64(hash, t->capacity);
    prev      = NULL;
    node      = t->buckets[bucket];
    probe_len = 1;

    while (node != NULL) {
        if (node->key == key) {
            if (bucket_out != NULL) {
                *bucket_out = bucket;
            }
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

    if (bucket_out != NULL) {
        *bucket_out = bucket;
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
