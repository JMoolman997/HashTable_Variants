/**
 * @file    fingerprint_impl.c
 * @brief   Fingerprint-accelerated separate-chaining hashtable backend.
 *
 * Implements a variant of separate chaining where chains are stored in
 * segments containing an array of small hash fingerprints and a separate 
 * array of key/value pairs. Lookups scan the fingerprints first to avoid
 * expensive full-key comparisons.
 *
 * @author  J.W. Moolman
 * @date    2026-05-08
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
#include "fingerprint_impl.h"
#include "ht_internal.h"
#include "slab_pool.h"

/* --- function prototypes -------------------------------------------------- */

static void fingerprint_destroy_impl(void *impl);
static ht_result fingerprint_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
);
static ht_result fingerprint_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
);
static ht_result fingerprint_remove_impl(void *impl, ht_key_t key);
static size_t fingerprint_size_impl(const void *impl);
static size_t fingerprint_capacity_impl(const void *impl);
static double fingerprint_load_factor_impl(const void *impl);
static ht_result fingerprint_reserve_impl(void *impl, size_t capacity);
static ht_result fingerprint_rehash_impl(void *impl, size_t capacity);
static ht_result fingerprint_get_stats_impl(const void *impl, ht_stats *out);
static ht_result fingerprint_reset_stats_impl(void *impl);
static void fingerprint_update_bytes_used(fingerprint_table *t);
static void fingerprint_free_all_segments(fingerprint_table *t);
static ht_result fingerprint_resize(fingerprint_table *t, size_t new_capacity);
static ht_result fingerprint_insert_rehash(
    fingerprint_table *t,
    ht_key_t key,
    ht_val_t value
);

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable FINGERPRINT_VTABLE = {
    .destroy = fingerprint_destroy_impl,
    .insert = fingerprint_insert_impl,
    .get = fingerprint_get_impl,
    .remove = fingerprint_remove_impl,
    .size = fingerprint_size_impl,
    .capacity = fingerprint_capacity_impl,
    .load_factor = fingerprint_load_factor_impl,
    .reserve = fingerprint_reserve_impl,
    .rehash = fingerprint_rehash_impl,
    .get_stats = fingerprint_get_stats_impl,
    .reset_stats = fingerprint_reset_stats_impl,
    .bind_bench_iface = fingerprint_bind_bench_iface
};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

ht_result fingerprint_create_impl_ex(
    const ht_config *cfg,
    void           **out
) {
    fingerprint_table *t;
    ht_backend_config resolved;
    ht_result rc;

    if (out == NULL) { return HT_ERR_INVALID; }
    *out = NULL;

    if (cfg == NULL) { return HT_ERR_INVALID; }

    rc = ht_backend_config_resolve(
        cfg,
        FINGERPRINT_DEFAULT_INITIAL_CAPACITY,
        FINGERPRINT_DEFAULT_MIN_CAPACITY,
        FINGERPRINT_DEFAULT_MAX_LOAD,
        FINGERPRINT_DEFAULT_MIN_LOAD,
        &resolved
    );
    if (rc != HT_OK) {
        return rc;
    }

    t = calloc(1, sizeof(*t));
    if (t == NULL) { return HT_ERR_OOM; }

    t->buckets = calloc(resolved.capacity, sizeof(*t->buckets));
    if (t->buckets == NULL) {
        free(t);
        return HT_ERR_OOM;
    }

    if (slab_pool_init(
            &t->pool,
            sizeof(fingerprint_segment),
            SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK
        ) != 0) {
        free(t->buckets);
        free(t);
        return HT_ERR_OOM;
    }

    t->capacity        = resolved.capacity;
    t->min_capacity    = resolved.min_capacity;
    t->size            = 0;
    t->max_load_factor = resolved.max_load_factor;
    t->min_load_factor = resolved.min_load_factor;
    t->resize_mode   = resolved.resize_mode;
    t->hash_fn       = resolved.hash_fn;
    t->hash_seed     = resolved.hash_seed;
    t->collect_stats = resolved.collect_stats;

    fingerprint_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    *out = t;
    return HT_OK;
}

const struct ht_vtable *fingerprint_vtable(
    void
) {
    return &FINGERPRINT_VTABLE;
}

int fingerprint_bind_bench_iface(
    void *ctx,
    bench_iface *out
) {
    if (ctx == NULL || out == NULL) { return HT_ERR_INVALID; }

    out->ctx    = ctx;
    out->insert = fingerprint_insert_impl;
    out->get    = fingerprint_get_impl;
    out->remove = fingerprint_remove_impl;
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void fingerprint_destroy_impl(
    void *impl
) {
    fingerprint_table *t = impl;

    if (t == NULL) { return; }

    fingerprint_free_all_segments(t);
    slab_pool_destroy(&t->pool);
    free(t->buckets);
    free(t);
}

static ht_result fingerprint_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
) {
    fingerprint_table *t = impl;
    fingerprint_segment *seg;
    uint64_t hash, probe_len = 1;
    size_t bucket;
    size_t new_capacity;
    uint8_t tag;
    ht_result rc;

    if (t == NULL) { return HT_ERR_INVALID; }
    HT_STATS_INC(t, inserts);

    hash = t->hash_fn(key, t->hash_seed);
    bucket = HT_INDEX_FOR_U64(hash, t->capacity);
    tag = ht_hash_tag_u8(hash);

    seg = t->buckets[bucket];
    while (seg != NULL) {
        for (uint8_t i = 0; i < seg->used; i++) {
            if (seg->tags[i] == tag && seg->keys[i] == key) {
                HT_UPDATE_PROBE_STATS(t, probe_len);
                HT_RECORD_INSERT_FAILURE(t);
                return HT_ERR_EXISTS;
            }
            probe_len++;
        }
        seg = seg->next;
    }

    if (t->resize_mode != HT_RESIZE_NONE && HT_SHOULD_GROW_COUNT(t, size)) {
        rc = ht_grow_capacity_pow2(t->capacity, &new_capacity);
        if (rc != HT_OK) {
            HT_RECORD_INSERT_FAILURE(t);
            HT_UPDATE_PROBE_STATS(t, probe_len);
            return rc;
        }

        rc = fingerprint_resize(t, new_capacity);
        if (rc != HT_OK) {
            HT_RECORD_INSERT_FAILURE(t);
            HT_UPDATE_PROBE_STATS(t, probe_len);
            return rc;
        }
        bucket = HT_INDEX_FOR_U64(hash, t->capacity);
        probe_len = 1;
        for (seg = t->buckets[bucket]; seg != NULL; seg = seg->next) {
            probe_len += seg->used;
        }
    } else if (t->resize_mode == HT_RESIZE_NONE &&
               (double)(t->size + 1) >
               (double)t->capacity * t->max_load_factor) {
        HT_UPDATE_PROBE_STATS(t, probe_len);
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_FULL;
    }

    /* Key not found, find a segment with room or allocate a new one. */
    seg = t->buckets[bucket];
    while (seg != NULL) {
        if (seg->used < FINGERPRINT_SEGMENT_CAPACITY) { break; }
        seg = seg->next;
    }

    if (seg == NULL) {
        seg = slab_pool_alloc(&t->pool);
        if (seg == NULL) {
            HT_RECORD_INSERT_FAILURE(t);
            return HT_ERR_OOM;
        }
        seg->next = t->buckets[bucket];
        seg->used = 0;
        t->buckets[bucket] = seg;
    }

    seg->tags[seg->used] = tag;
    seg->keys[seg->used] = key;
    seg->values[seg->used] = value;
    seg->used++;
    t->size++;

    HT_UPDATE_PROBE_STATS(t, probe_len);
    fingerprint_update_bytes_used(t);
    return HT_OK;
}

static ht_result fingerprint_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
) {
    const fingerprint_table *t = impl;
    fingerprint_segment *seg;
    uint64_t hash, probe_len = 1;
    size_t bucket;
    uint8_t tag;

    if (t == NULL || value_out == NULL) { return HT_ERR_INVALID; }
    if (t->collect_stats) { ((fingerprint_table *)t)->stats.lookups++; }

    hash = t->hash_fn(key, t->hash_seed);
    bucket = HT_INDEX_FOR_U64(hash, t->capacity);
    tag = ht_hash_tag_u8(hash);

    seg = t->buckets[bucket];
    while (seg != NULL) {
        for (uint8_t i = 0; i < seg->used; i++) {
            if (seg->tags[i] == tag && seg->keys[i] == key) {
                HT_UPDATE_PROBE_STATS((fingerprint_table *)t, probe_len);
                *value_out = seg->values[i];
                return HT_OK;
            }
            probe_len++;
        }
        seg = seg->next;
    }

    if (t->collect_stats) {
        ((fingerprint_table *)t)->stats.lookup_misses++;
        HT_UPDATE_PROBE_STATS((fingerprint_table *)t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
}

static ht_result fingerprint_remove_impl(
    void *impl,
    ht_key_t key
) {
    fingerprint_table *t = impl;
    fingerprint_segment *seg, *prev = NULL;
    uint64_t hash, probe_len = 1;
    size_t bucket;
    uint8_t tag;

    if (t == NULL) { return HT_ERR_INVALID; }
    if (t->collect_stats) { t->stats.removes++; }

    hash = t->hash_fn(key, t->hash_seed);
    bucket = HT_INDEX_FOR_U64(hash, t->capacity);
    tag = ht_hash_tag_u8(hash);

    seg = t->buckets[bucket];
    while (seg != NULL) {
        for (uint8_t i = 0; i < seg->used; i++) {
            if (seg->tags[i] == tag && seg->keys[i] == key) {
                HT_UPDATE_PROBE_STATS(t, probe_len);
                
                /* Replace with last entry in segment. */
                uint8_t last_idx = seg->used - 1;
                seg->tags[i] = seg->tags[last_idx];
                seg->keys[i] = seg->keys[last_idx];
                seg->values[i] = seg->values[last_idx];
                seg->used--;

                if (seg->used == 0) {
                    if (prev == NULL) {
                        t->buckets[bucket] = seg->next;
                    } else {
                        prev->next = seg->next;
                    }
                    slab_pool_free(&t->pool, seg);
                }
                
                t->size--;
                fingerprint_update_bytes_used(t);

                if (t->resize_mode == HT_RESIZE_GROW_SHRINK && HT_SHOULD_SHRINK_COUNT(t, size)) {
                    size_t new_cap = t->capacity / 2;
                    if (new_cap < t->min_capacity) { new_cap = t->min_capacity; }
                    return fingerprint_resize(t, new_cap);
                }
                return HT_OK;
            }
            probe_len++;
        }
        prev = seg;
        seg = seg->next;
    }

    if (t->collect_stats) {
        t->stats.remove_misses++;
        HT_UPDATE_PROBE_STATS(t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
}

static size_t fingerprint_size_impl(
    const void *impl
) {
    const fingerprint_table *t = impl;

    return (t != NULL)
        ? t->size
        : 0
    ;
}

static size_t fingerprint_capacity_impl(
    const void *impl
) {
    const fingerprint_table *t = impl;

    return (t != NULL)
        ? t->capacity
        : 0
    ;
}

static double fingerprint_load_factor_impl(
    const void *impl
) {
    const fingerprint_table *t = impl;

    return (t != NULL)
        ? ht_load_factor_snapshot(t->size, t->capacity)
        : 0.0
    ;
}

static ht_result fingerprint_reserve_impl(
    void *impl,
    size_t capacity
) {
    fingerprint_table *t = impl;
    size_t target;
    ht_result rc;

    if (t == NULL) { return HT_ERR_INVALID; }
    rc = ht_reserve_target(t->capacity, capacity, &target);
    if (rc != HT_OK) { return rc; }
    if (target == t->capacity) { return HT_OK; }
    return fingerprint_resize(t, target);
}

static ht_result fingerprint_rehash_impl(
    void *impl,
    size_t capacity
) {
    fingerprint_table *t = impl;
    size_t target;
    ht_result rc;

    if (t == NULL) { return HT_ERR_INVALID; }

    rc = ht_rehash_target(t->size, t->min_capacity, capacity, &target);
    if (rc != HT_OK) { return rc; }
    return fingerprint_resize(t, target);
}

static ht_result fingerprint_get_stats_impl(
    const void *impl,
    ht_stats *out
) {
    if (impl == NULL || out == NULL) { return HT_ERR_INVALID; }

    *out = ((const fingerprint_table *)impl)->stats;
    return HT_OK;
}

static ht_result fingerprint_reset_stats_impl(
    void *impl
) {
    fingerprint_table *t = impl;

    if (t == NULL) { return HT_ERR_INVALID; }

    memset(&t->stats, 0, sizeof(t->stats));
    fingerprint_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void fingerprint_update_bytes_used(fingerprint_table *t) {
    size_t bytes;

    if (t != NULL && t->collect_stats) {
        bytes = sizeof(*t);
        bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->buckets));
        bytes = ht_bytes_add_or_max(bytes, slab_pool_bytes_owned(&t->pool));
        t->stats.bytes_used = bytes;
    }
}

static void fingerprint_free_all_segments(fingerprint_table *t) {
    if (t == NULL || t->buckets == NULL) { return; }
    for (size_t i = 0; i < t->capacity; i++) {
        fingerprint_segment *seg = t->buckets[i];
        while (seg != NULL) {
            fingerprint_segment *next = seg->next;
            slab_pool_free(&t->pool, seg);
            seg = next;
        }
        t->buckets[i] = NULL;
    }
}

static ht_result fingerprint_insert_rehash(
    fingerprint_table *t,
    ht_key_t key,
    ht_val_t value
) {
    fingerprint_segment *seg;
    uint64_t hash;
    size_t bucket;
    uint8_t tag;

    if (t == NULL) { return HT_ERR_INVALID; }

    hash = t->hash_fn(key, t->hash_seed);
    bucket = HT_INDEX_FOR_U64(hash, t->capacity);
    tag = ht_hash_tag_u8(hash);

    seg = t->buckets[bucket];
    while (seg != NULL && seg->used == FINGERPRINT_SEGMENT_CAPACITY) {
        seg = seg->next;
    }

    if (seg == NULL) {
        seg = slab_pool_alloc(&t->pool);
        if (seg == NULL) { return HT_ERR_OOM; }
        seg->next = t->buckets[bucket];
        seg->used = 0;
        t->buckets[bucket] = seg;
    }

    seg->tags[seg->used] = tag;
    seg->keys[seg->used] = key;
    seg->values[seg->used] = value;
    seg->used++;
    t->size++;
    return HT_OK;
}

static ht_result fingerprint_resize(fingerprint_table *t, size_t new_capacity) {
    fingerprint_segment **old_buckets, **new_buckets;
    slab_pool old_pool, new_pool;
    size_t old_capacity, old_size;
    uint64_t start_ns;
    ht_result rc;

    if (new_capacity < t->min_capacity) { new_capacity = t->min_capacity; }
    rc = ht_checked_next_pow2(new_capacity, &new_capacity);
    if (rc != HT_OK) { return rc; }
    if (new_capacity == t->capacity) { return HT_OK; }

    new_buckets = calloc(new_capacity, sizeof(*new_buckets));
    if (new_buckets == NULL) { return HT_ERR_OOM; }
    if (slab_pool_init(
            &new_pool,
            sizeof(fingerprint_segment),
            SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK
        ) != 0) {
        free(new_buckets);
        return HT_ERR_OOM;
    }

    old_buckets = t->buckets;
    old_pool = t->pool;
    old_capacity = t->capacity;
    old_size = t->size;
    start_ns = ht_resize_instrumentation_start(t->collect_stats);

    /* Build the new chains in a fresh pool so OOM can roll back cleanly. */
    t->buckets = new_buckets;
    t->pool = new_pool;
    t->capacity = new_capacity;
    t->size = 0;

    for (size_t i = 0; i < old_capacity; i++) {
        fingerprint_segment *seg = old_buckets[i];
        while (seg != NULL) {
            for (uint8_t j = 0; j < seg->used; j++) {
                rc = fingerprint_insert_rehash(
                    t,
                    seg->keys[j],
                    seg->values[j]
                );
                if (rc != HT_OK) {
                    slab_pool_destroy(&t->pool);
                    free(new_buckets);
                    t->buckets = old_buckets;
                    t->pool = old_pool;
                    t->capacity = old_capacity;
                    t->size = old_size;
                    return rc;
                }
            }
            seg = seg->next;
        }
    }

    slab_pool_destroy(&old_pool);
    free(old_buckets);

    fingerprint_update_bytes_used(t);
    ht_resize_stats_record(
        &t->stats,
        t->collect_stats,
        old_capacity,
        t->capacity,
        old_size,
        start_ns
    );
    return HT_OK;
}
