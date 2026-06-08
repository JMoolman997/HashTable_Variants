/**
 * @file    mod_separate_chaining_impl.c
 * @brief   Core modified separate-chaining hashtable backend.
 *
 * Implements the backend-facing logic for the modified separate-chaining
 * hashtable used by the generic wrapper. Bucket storage is selected from
 * `ht_impl` at creation time, while this file owns lifecycle, hashing policy,
 * resize decisions, statistics, and dispatch wiring.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend_config.h"
#include "capacity_util.h"
#include "hash_util.h"
#include "memory_util.h"
#include "resize_stats.h"
#include "stats_util.h"
#include "inline_array_bucket.h"
#include "linkedlist_bucket.h"
#include "mod_separate_chaining_impl.h"
#include "segmented_bucket.h"
#include "ht_internal.h"

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Return the bucket operation table for a modified chaining variant.
 *
 * @param impl_kind Public implementation identifier requested by `ht_create`.
 *
 * @return Static bucket dispatch table, or `NULL` for non-modified variants.
 */
static const mod_separate_chaining_bucket_ops *
mod_separate_chaining_bucket_ops_for_impl(
    ht_impl impl_kind
);

/**
 * @brief Release all memory owned by a modified separate-chaining backend.
 *
 * @param impl Backend instance to destroy. `NULL` is ignored.
 */
static void mod_separate_chaining_destroy_impl(
    void *impl
);

/**
 * @brief Insert a key/value pair through the selected bucket implementation.
 *
 * @param impl Backend instance to modify.
 * @param key Key to insert.
 * @param value Value to associate with `key`.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_EXISTS`,
 *         `HT_ERR_FULL`, `HT_ERR_INVALID`, or `HT_ERR_OOM`.
 */
static ht_result mod_separate_chaining_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
);

/**
 * @brief Look up a key through the selected bucket implementation.
 *
 * @param impl Backend instance to query.
 * @param key Key to search for.
 * @param value_out Output location for the value on success.
 *
 * @return `HT_OK` when found, or `HT_ERR_NOT_FOUND`/`HT_ERR_INVALID`.
 */
static ht_result mod_separate_chaining_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
);

/**
 * @brief Remove a key through the selected bucket implementation.
 *
 * @param impl Backend instance to modify.
 * @param key Key to remove.
 *
 * @return `HT_OK` when removed, or `HT_ERR_NOT_FOUND`/`HT_ERR_INVALID`.
 */
static ht_result mod_separate_chaining_remove_impl(
    void *impl,
    ht_key_t key
);

/**
 * @brief Return the number of live entries in the backend.
 *
 * @param impl Backend instance to query.
 *
 * @return Live entry count, or `0` when `impl` is invalid.
 */
static size_t mod_separate_chaining_size_impl(
    const void *impl
);

/**
 * @brief Return the current bucket-array capacity.
 *
 * @param impl Backend instance to query.
 *
 * @return Bucket count, or `0` when `impl` is invalid.
 */
static size_t mod_separate_chaining_capacity_impl(
    const void *impl
);

/**
 * @brief Return the current live load factor.
 *
 * @param impl Backend instance to query.
 *
 * @return `size / capacity`, or `0.0` when `impl` is invalid.
 */
static double mod_separate_chaining_load_factor_impl(
    const void *impl
);

/**
 * @brief Ensure the backend has at least the requested bucket capacity.
 *
 * @param impl Backend instance to resize if needed.
 * @param capacity Minimum requested bucket count.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result mod_separate_chaining_reserve_impl(
    void *impl,
    size_t capacity
);

/**
 * @brief Rebuild the backend around a requested capacity.
 *
 * @param impl Backend instance to rebuild.
 * @param capacity Requested target capacity before normalization.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result mod_separate_chaining_rehash_impl(
    void *impl,
    size_t capacity
);

/**
 * @brief Copy the backend statistics snapshot.
 *
 * @param impl Backend instance to query.
 * @param out Output structure that receives the stats.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID`.
 */
static ht_result mod_separate_chaining_get_stats_impl(
    const void *impl,
    ht_stats *out
);

/**
 * @brief Reset operation counters while preserving current memory statistics.
 *
 * @param impl Backend instance whose stats should be cleared.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID`.
 */
static ht_result mod_separate_chaining_reset_stats_impl(
    void *impl
);

/**
 * @brief Refresh the approximate bytes-used counter for stats collection.
 *
 * @param t Backend table whose stats should be updated.
 */
static void mod_separate_chaining_update_bytes_used(
    mod_separate_chaining_table *t
);

/**
 * @brief Allocate a new bucket array and move all entries into it.
 *
 * @param t Backend table to resize.
 * @param new_capacity Requested capacity before min-capacity and power-of-two
 *                     normalization.
 *
 * @return `HT_OK` on success, or an error from allocation or bucket rehashing.
 */
static ht_result mod_separate_chaining_resize(
    mod_separate_chaining_table *t,
    size_t new_capacity
);

/* ------------------------------------------------------------------------- */
/* bucket variant dispatch                                                   */
/* ------------------------------------------------------------------------- */

static const mod_separate_chaining_bucket_ops *
mod_separate_chaining_bucket_ops_for_impl(
    ht_impl impl_kind
) {
    switch (impl_kind) {
    case HT_IMPL_BUCKET_MOD_SEPARATE_CHAINING:
        return &inline_array_bucket_ops;

    case HT_IMPL_LINKED_MOD_SEPARATE_CHAINING:
        return &linkedlist_bucket_ops;

    case HT_IMPL_SEGMENTED_MOD_SEPARATE_CHAINING:
        return &segmented_bucket_ops;

    default:
        return NULL;
    }
}

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable MOD_SEPARATE_CHAINING_VTABLE = {
    .destroy = mod_separate_chaining_destroy_impl,
    .insert = mod_separate_chaining_insert_impl,
    .get = mod_separate_chaining_get_impl,
    .remove = mod_separate_chaining_remove_impl,
    .size = mod_separate_chaining_size_impl,
    .capacity = mod_separate_chaining_capacity_impl,
    .load_factor = mod_separate_chaining_load_factor_impl,
    .reserve = mod_separate_chaining_reserve_impl,
    .rehash = mod_separate_chaining_rehash_impl,
    .get_stats = mod_separate_chaining_get_stats_impl,
    .reset_stats = mod_separate_chaining_reset_stats_impl,
    .bind_bench_iface = mod_separate_chaining_bind_bench_iface
};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

ht_result mod_separate_chaining_create_impl_ex(
    const ht_config *cfg,
    void           **out
) {
    mod_separate_chaining_table *t;
    const mod_separate_chaining_bucket_ops *bucket_ops;
    ht_backend_config resolved;
    ht_result rc;

    if (out == NULL) {
        return HT_ERR_INVALID;
    }
    *out = NULL;

    if (cfg == NULL) {
        return HT_ERR_INVALID;
    }

    bucket_ops = mod_separate_chaining_bucket_ops_for_impl(cfg->impl_kind);
    if (bucket_ops == NULL) {
        return HT_ERR_UNSUPPORTED;
    }

    rc = ht_backend_config_resolve(
        cfg,
        DEFAULT_INITIAL_CAPACITY,
        DEFAULT_MIN_CAPACITY,
        DEFAULT_MAX_LOAD,
        DEFAULT_MIN_LOAD,
        &resolved
    );
    if (rc != HT_OK) {
        return rc;
    }

    t = calloc(1, sizeof(*t));
    if (t == NULL) {
        return HT_ERR_OOM;
    }

    t->bucket_ops = bucket_ops;
    t->bucket_ctx = t->bucket_ops->ctx_create();
    if (t->bucket_ctx == NULL) {
        free(t);
        return HT_ERR_OOM;
    }

    t->buckets = t->bucket_ops->array_alloc(resolved.capacity);
    if (t->buckets == NULL) {
        t->bucket_ops->ctx_destroy(t->bucket_ctx);
        free(t);
        return HT_ERR_OOM;
    }

    t->capacity     = resolved.capacity;
    t->min_capacity = resolved.min_capacity;
    t->size         = 0;
    t->max_load_factor = resolved.max_load_factor;
    t->min_load_factor = resolved.min_load_factor;
    t->resize_mode   = resolved.resize_mode;
    t->hash_fn       = resolved.hash_fn;
    t->hash_seed     = resolved.hash_seed;
    t->collect_stats = resolved.collect_stats;

    mod_separate_chaining_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    *out = t;
    return HT_OK;
}

const struct ht_vtable *mod_separate_chaining_vtable(
    void
) {
    return &MOD_SEPARATE_CHAINING_VTABLE;
}

int mod_separate_chaining_bind_bench_iface(
    void *ctx,
    bench_iface *out
) {
    if (ctx == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    out->ctx    = ctx;
    out->insert = mod_separate_chaining_insert_impl;
    out->get    = mod_separate_chaining_get_impl;
    out->remove = mod_separate_chaining_remove_impl;

    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void mod_separate_chaining_destroy_impl(
    void *impl
) {
    mod_separate_chaining_table *t = impl;

    if (t == NULL) {
        return;
    }

    t->bucket_ops->array_destroy(
        t->bucket_ctx,
        t->buckets,
        t->capacity
    );
    t->bucket_ops->ctx_destroy(t->bucket_ctx);
    free(t);
}

static ht_result mod_separate_chaining_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
) {
    mod_separate_chaining_table *t = impl;
    uint64_t hash;
    size_t bucket;
    size_t new_capacity;
    uint64_t probe_len = 0;
    uint64_t insert_probe_len;
    ht_val_t existing_value;
    int needs_grow;
    int would_exceed_fixed;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    HT_STATS_INC(t, inserts);

    hash = t->hash_fn(key, t->hash_seed);
    bucket = HT_INDEX_FOR_U64(hash, t->capacity);
    needs_grow =
        t->resize_mode != HT_RESIZE_NONE &&
        HT_SHOULD_GROW_COUNT(t, size);
    would_exceed_fixed =
        t->resize_mode == HT_RESIZE_NONE &&
        (double)(t->size + 1) >
        (double)t->capacity * t->max_load_factor;

    if (needs_grow || would_exceed_fixed) {
        rc = t->bucket_ops->get(
            t->bucket_ctx,
            t->buckets,
            bucket,
            key,
            &existing_value,
            &probe_len
        );
        if (rc == HT_OK) {
            HT_UPDATE_PROBE_STATS(t, probe_len);
            HT_RECORD_INSERT_FAILURE(t);
            return HT_ERR_EXISTS;
        }
        if (rc != HT_ERR_NOT_FOUND) {
            HT_UPDATE_PROBE_STATS(t, probe_len);
            HT_RECORD_INSERT_FAILURE(t);
            return rc;
        }
    }

    if (needs_grow) {
        rc = ht_grow_capacity_pow2(t->capacity, &new_capacity);
        if (rc != HT_OK) {
            HT_RECORD_INSERT_FAILURE(t);
            HT_UPDATE_PROBE_STATS(t, probe_len);
            return rc;
        }

        rc = mod_separate_chaining_resize(t, new_capacity);
        if (rc != HT_OK) {
            HT_RECORD_INSERT_FAILURE(t);
            HT_UPDATE_PROBE_STATS(t, probe_len);
            return rc;
        }
        bucket = HT_INDEX_FOR_U64(hash, t->capacity);
    } else if (would_exceed_fixed) {
        HT_UPDATE_PROBE_STATS(t, probe_len);
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_FULL;
    }

    insert_probe_len = 1;
    rc = t->bucket_ops->insert(
        t->bucket_ctx,
        t->buckets,
        bucket,
        key,
        value,
        &insert_probe_len
    );
    if (rc != HT_OK) {
        HT_RECORD_INSERT_FAILURE(t);
        HT_UPDATE_PROBE_STATS(t, probe_len + insert_probe_len);
        return rc;
    }

    t->size++;
    HT_UPDATE_PROBE_STATS(t, probe_len + insert_probe_len);
    mod_separate_chaining_update_bytes_used(t);

    return HT_OK;
}

static ht_result mod_separate_chaining_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
) {
    const mod_separate_chaining_table *t = impl;
    uint64_t hash;
    size_t bucket;
    uint64_t probe_len = 0;
    ht_result rc;

    if (t == NULL || value_out == NULL) {
        return HT_ERR_INVALID;
    }

    if (t->collect_stats) {
        ((mod_separate_chaining_table *)t)->stats.lookups++;
    }

    hash = t->hash_fn(key, t->hash_seed);
    bucket = HT_INDEX_FOR_U64(hash, t->capacity);

    rc = t->bucket_ops->get(
        t->bucket_ctx,
        t->buckets,
        bucket,
        key,
        value_out,
        &probe_len
    );
    if (rc != HT_OK) {
        if (t->collect_stats) {
            ((mod_separate_chaining_table *)t)->stats.lookup_misses++;
            HT_UPDATE_PROBE_STATS((mod_separate_chaining_table *)t, probe_len);
        }
        return HT_ERR_NOT_FOUND;
    }

    HT_UPDATE_PROBE_STATS((mod_separate_chaining_table *)t, probe_len);
    return HT_OK;
}

static ht_result mod_separate_chaining_remove_impl(
    void *impl,
    ht_key_t key
) {
    mod_separate_chaining_table *t = impl;
    uint64_t hash;
    size_t bucket;
    uint64_t probe_len = 0;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (t->collect_stats) {
        t->stats.removes++;
    }

    hash = t->hash_fn(key, t->hash_seed);
    bucket = HT_INDEX_FOR_U64(hash, t->capacity);

    rc = t->bucket_ops->remove(
        t->bucket_ctx,
        t->buckets,
        bucket,
        key,
        &probe_len
    );
    if (rc != HT_OK) {
        if (t->collect_stats) {
            t->stats.remove_misses++;
            HT_UPDATE_PROBE_STATS(t, probe_len);
        }
        return HT_ERR_NOT_FOUND;
    }

    t->size--;
    HT_UPDATE_PROBE_STATS(t, probe_len);
    mod_separate_chaining_update_bytes_used(t);

    if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
        HT_SHOULD_SHRINK_COUNT(t, size)) {
        size_t new_capacity = t->capacity / 2;

        if (new_capacity < t->min_capacity) {
            new_capacity = t->min_capacity;
        }

        return mod_separate_chaining_resize(t, new_capacity);
    }

    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* metadata/stat ops                                                         */
/* ------------------------------------------------------------------------- */

static size_t mod_separate_chaining_size_impl(
    const void *impl
) {
    const mod_separate_chaining_table *t = impl;
    return (t != NULL) ? t->size : 0;
}

static size_t mod_separate_chaining_capacity_impl(
    const void *impl
) {
    const mod_separate_chaining_table *t = impl;
    return (t != NULL) ? t->capacity : 0;
}

static double mod_separate_chaining_load_factor_impl(
    const void *impl
) {
    const mod_separate_chaining_table *t = impl;

    return (t != NULL)
        ? ht_load_factor_snapshot(t->size, t->capacity)
        : 0.0;
}

static ht_result mod_separate_chaining_reserve_impl(
    void *impl,
    size_t capacity
) {
    mod_separate_chaining_table *t = impl;
    size_t target;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    rc = ht_reserve_target(t->capacity, capacity, &target);
    if (rc != HT_OK) {
        return rc;
    }

    if (target == t->capacity) {
        return HT_OK;
    }

    return mod_separate_chaining_resize(t, target);
}

static ht_result mod_separate_chaining_rehash_impl(
    void *impl,
    size_t capacity
) {
    mod_separate_chaining_table *t = impl;
    size_t target;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    rc = ht_rehash_target(t->size, t->min_capacity, capacity, &target);
    if (rc != HT_OK) {
        return rc;
    }

    return mod_separate_chaining_resize(t, target);
}

static ht_result mod_separate_chaining_get_stats_impl(
    const void *impl,
    ht_stats *out
) {
    const mod_separate_chaining_table *t = impl;

    if (t == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    *out = t->stats;
    return HT_OK;
}

static ht_result mod_separate_chaining_reset_stats_impl(
    void *impl
) {
    mod_separate_chaining_table *t = impl;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    memset(&t->stats, 0, sizeof(t->stats));
    mod_separate_chaining_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void mod_separate_chaining_update_bytes_used(
    mod_separate_chaining_table *t
) {
    size_t bytes;
    size_t extra_bytes;

    if (t == NULL || !t->collect_stats) {
        return;
    }

    bytes = ht_bytes_add_array_or_max(
        sizeof(*t),
        t->capacity,
        t->bucket_ops->bucket_size
    );
    extra_bytes = t->bucket_ops->extra_bytes(
        t->bucket_ctx,
        t->buckets,
        t->capacity,
        t->size
    );

    if (bytes == SIZE_MAX || extra_bytes == SIZE_MAX ||
        extra_bytes > SIZE_MAX - bytes) {
        t->stats.bytes_used = SIZE_MAX;
        return;
    }

    t->stats.bytes_used = bytes + extra_bytes;
}

static ht_result mod_separate_chaining_resize(
    mod_separate_chaining_table *t,
    size_t new_capacity
) {
    void *old_buckets;
    void *new_buckets;
    size_t old_capacity;
    size_t old_size;
    uint64_t resize_start_ns;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (new_capacity < t->min_capacity) {
        new_capacity = t->min_capacity;
    }

    rc = ht_checked_next_pow2(new_capacity, &new_capacity);
    if (rc != HT_OK) {
        return rc;
    }

    if (new_capacity == t->capacity) {
        return HT_OK;
    }

    new_buckets = t->bucket_ops->array_alloc(new_capacity);
    if (new_buckets == NULL) {
        return HT_ERR_OOM;
    }

    old_buckets = t->buckets;
    old_capacity = t->capacity;
    old_size = t->size;
    resize_start_ns = ht_resize_instrumentation_start(t->collect_stats);

    rc = t->bucket_ops->rehash_all(
        t->bucket_ctx,
        old_buckets,
        old_capacity,
        new_buckets,
        new_capacity,
        t->hash_fn,
        t->hash_seed
    );
    if (rc != HT_OK) {
        t->bucket_ops->array_destroy(
            t->bucket_ctx,
            new_buckets,
            new_capacity
        );
        return rc;
    }

    t->buckets = new_buckets;
    t->capacity = new_capacity;

    t->bucket_ops->array_release(old_buckets);

    ht_resize_stats_record(
        &t->stats,
        t->collect_stats,
        old_capacity,
        t->capacity,
        old_size,
        resize_start_ns
    );
    mod_separate_chaining_update_bytes_used(t);

    return HT_OK;
}
