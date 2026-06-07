/**
 * @file    linear_hashing_impl.c
 * @brief   Linear hashing backend with fingerprint-accelerated segments.
 *
 * Implements linear hashing to distribute the cost of resizing by growing
 * the table one bucket at a time. Each bucket chain uses segmented
 * storage with hash fingerprints for fast lookups.
 *
 * @author  J.W. Moolman
 * @date    2026-05-08
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend_util.h"
#include "linear_hashing_impl.h"
#include "ht_internal.h"
#include "slab_pool.h"

/* --- function prototypes -------------------------------------------------- */

static void linear_hashing_destroy_impl(void *impl);
static ht_result linear_hashing_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
);
static ht_result linear_hashing_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
);
static ht_result linear_hashing_remove_impl(void *impl, ht_key_t key);
static size_t linear_hashing_size_impl(const void *impl);
static size_t linear_hashing_capacity_impl(const void *impl);
static double linear_hashing_load_factor_impl(const void *impl);
static ht_result linear_hashing_reserve_impl(void *impl, size_t capacity);
static ht_result linear_hashing_rehash_impl(void *impl, size_t capacity);
static ht_result linear_hashing_get_stats_impl(const void *impl, ht_stats *out);
static ht_result linear_hashing_reset_stats_impl(void *impl);
static void linear_hashing_update_bytes_used(linear_hashing_table *t);
static void linear_hashing_free_all_segments(linear_hashing_table *t);
static ht_result linear_hashing_bucket_push(
    linear_hashing_table *t,
    size_t bucket,
    uint8_t tag,
    ht_key_t key,
    ht_val_t value
);
static size_t linear_hashing_bucket_entry_count(
    const linear_hashing_segment *segment
);
static size_t linear_hashing_segments_for_entries(size_t entries);
static ht_result linear_hashing_split(linear_hashing_table *t);
static ht_result linear_hashing_merge(linear_hashing_table *t);

static inline size_t linear_hashing_get_bucket(
    const linear_hashing_table *t,
    uint64_t hash
) {
    size_t m = t->initial_buckets << t->level;
    size_t bucket = hash & (m - 1); /* Assumes initial_buckets is pow2 */

    if (bucket < t->split_ptr) {
        bucket = hash & ((m << 1) - 1);
    }

    return bucket;
}

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable LINEAR_HASHING_VTABLE = {
    .destroy = linear_hashing_destroy_impl,
    .insert = linear_hashing_insert_impl,
    .get = linear_hashing_get_impl,
    .remove = linear_hashing_remove_impl,
    .size = linear_hashing_size_impl,
    .capacity = linear_hashing_capacity_impl,
    .load_factor = linear_hashing_load_factor_impl,
    .reserve = linear_hashing_reserve_impl,
    .rehash = linear_hashing_rehash_impl,
    .get_stats = linear_hashing_get_stats_impl,
    .reset_stats = linear_hashing_reset_stats_impl,
    .bind_bench_iface = linear_hashing_bind_bench_iface
};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

ht_result linear_hashing_create_impl_ex(
    const ht_config *cfg,
    void           **out
) {
    linear_hashing_table *t;
    size_t init_buckets;

    if (out == NULL) { return HT_ERR_INVALID; }
    *out = NULL;

    if (cfg == NULL) { return HT_ERR_INVALID; }

    t = calloc(1, sizeof(*t));
    if (t == NULL) { return HT_ERR_OOM; }

    init_buckets = (cfg->init_capacity > 0)
        ? cfg->init_capacity
        : LINEAR_HASHING_DEFAULT_INITIAL_CAPACITY
    ;
    init_buckets = next_pow2(init_buckets);

    if (init_buckets == 0) {
        free(t);
        return HT_ERR_INVALID;
    }

    t->buckets = calloc(init_buckets, sizeof(*t->buckets));
    if (t->buckets == NULL) {
        free(t);
        return HT_ERR_OOM;
    }

    if (slab_pool_init(
            &t->pool,
            sizeof(linear_hashing_segment),
            SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK
        ) != 0) {
        free(t->buckets);
        free(t);
        return HT_ERR_OOM;
    }

    t->initial_buckets = init_buckets;
    t->total_buckets   = init_buckets;
    t->capacity        = init_buckets;
    t->split_ptr       = 0;
    t->level           = 0;
    t->size            = 0;
    
    t->max_load_factor = (cfg->max_load_factor > 0.0)
        ? cfg->max_load_factor
        : LINEAR_HASHING_DEFAULT_MAX_LOAD
    ;
    t->min_load_factor = (cfg->min_load_factor > 0.0)
        ? cfg->min_load_factor
        : LINEAR_HASHING_DEFAULT_MIN_LOAD
    ;
    t->resize_mode     = cfg->rsz_mode;
    t->hash_fn         = (cfg->hash_fn != NULL)
        ? cfg->hash_fn
        : default_hash
    ;
    t->hash_seed       = cfg->hash_seed;
    t->collect_stats   = cfg->collect_stats;

    linear_hashing_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->total_buckets);
    *out = t;
    return HT_OK;
}

const struct ht_vtable *linear_hashing_vtable(
    void
) {
    return &LINEAR_HASHING_VTABLE;
}

int linear_hashing_bind_bench_iface(
    void *ctx,
    bench_iface *out
) {
    if (ctx == NULL || out == NULL) { return HT_ERR_INVALID; }

    out->ctx = ctx;
    out->insert = linear_hashing_insert_impl;
    out->get = linear_hashing_get_impl;
    out->remove = linear_hashing_remove_impl;
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void linear_hashing_destroy_impl(
    void *impl
) {
    linear_hashing_table *t = impl;

    if (t == NULL) { return; }

    linear_hashing_free_all_segments(t);
    slab_pool_destroy(&t->pool);
    free(t->buckets);
    free(t);
}

static ht_result linear_hashing_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
) {
    linear_hashing_table *t = impl;
    linear_hashing_segment *seg;
    uint64_t hash, probe_len = 1;
    size_t bucket;
    uint8_t tag;

    if (t == NULL) { return HT_ERR_INVALID; }
    HT_STATS_INC(t, inserts);

    hash = t->hash_fn(key, t->hash_seed);
    bucket = linear_hashing_get_bucket(t, hash);
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

    /* Linear hashing growth check. */
    if (
        t->resize_mode != HT_RESIZE_NONE &&
        (double)(t->size + 1) >
        (double)t->total_buckets * t->max_load_factor
    ) {
        ht_result rc = linear_hashing_split(t);
        if (rc != HT_OK) {
            HT_RECORD_INSERT_FAILURE(t);
            return rc;
        }
        bucket = linear_hashing_get_bucket(t, hash);
        probe_len = 1;
        for (seg = t->buckets[bucket]; seg != NULL; seg = seg->next) {
            probe_len += seg->used;
        }
    } else if (
        t->resize_mode == HT_RESIZE_NONE &&
        (double)(t->size + 1) >
        (double)t->total_buckets * t->max_load_factor
    ) {
        HT_UPDATE_PROBE_STATS(t, probe_len);
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_FULL;
    }

    if (linear_hashing_bucket_push(t, bucket, tag, key, value) != HT_OK) {
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_OOM;
    }

    t->size++;

    HT_UPDATE_PROBE_STATS(t, probe_len);
    linear_hashing_update_bytes_used(t);
    return HT_OK;
}

static ht_result linear_hashing_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
) {
    const linear_hashing_table *t = impl;
    linear_hashing_segment *seg;
    uint64_t hash, probe_len = 1;
    size_t bucket;
    uint8_t tag;

    if (t == NULL || value_out == NULL) { return HT_ERR_INVALID; }
    if (t->collect_stats) { ((linear_hashing_table *)t)->stats.lookups++; }

    hash = t->hash_fn(key, t->hash_seed);
    bucket = linear_hashing_get_bucket(t, hash);
    tag = ht_hash_tag_u8(hash);

    seg = t->buckets[bucket];
    while (seg != NULL) {
        for (uint8_t i = 0; i < seg->used; i++) {
            if (seg->tags[i] == tag && seg->keys[i] == key) {
                HT_UPDATE_PROBE_STATS((linear_hashing_table *)t, probe_len);
                *value_out = seg->values[i];
                return HT_OK;
            }
            probe_len++;
        }
        seg = seg->next;
    }

    if (t->collect_stats) {
        ((linear_hashing_table *)t)->stats.lookup_misses++;
        HT_UPDATE_PROBE_STATS((linear_hashing_table *)t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
}

static ht_result linear_hashing_remove_impl(
    void *impl,
    ht_key_t key
) {
    linear_hashing_table *t = impl;
    linear_hashing_segment *seg, *prev = NULL;
    uint64_t hash, probe_len = 1;
    size_t bucket;
    uint8_t tag;

    if (t == NULL) { return HT_ERR_INVALID; }
    if (t->collect_stats) { t->stats.removes++; }

    hash = t->hash_fn(key, t->hash_seed);
    bucket = linear_hashing_get_bucket(t, hash);
    tag = ht_hash_tag_u8(hash);

    seg = t->buckets[bucket];
    while (seg != NULL) {
        for (uint8_t i = 0; i < seg->used; i++) {
            if (seg->tags[i] == tag && seg->keys[i] == key) {
                HT_UPDATE_PROBE_STATS(t, probe_len);
                
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
                linear_hashing_update_bytes_used(t);
                
                if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
                    t->total_buckets > t->initial_buckets &&
                    (double)t->size < (double)t->total_buckets * t->min_load_factor) {
                    return linear_hashing_merge(t);
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

static size_t linear_hashing_size_impl(
    const void *impl
) {
    const linear_hashing_table *t = impl;

    return (t != NULL)
        ? t->size
        : 0
    ;
}

static size_t linear_hashing_capacity_impl(
    const void *impl
) {
    const linear_hashing_table *t = impl;

    return (t != NULL)
        ? t->total_buckets
        : 0
    ;
}

static double linear_hashing_load_factor_impl(
    const void *impl
) {
    const linear_hashing_table *t = impl;

    return (t != NULL)
        ? ht_load_factor_snapshot(t->size, t->total_buckets)
        : 0.0
    ;
}

static ht_result linear_hashing_reserve_impl(
    void *impl,
    size_t capacity
) {
    linear_hashing_table *t = impl;

    if (t == NULL) { return HT_ERR_INVALID; }

    while (t->total_buckets < capacity) {
        ht_result rc = linear_hashing_split(t);
        if (rc != HT_OK) { return rc; }
    }
    return HT_OK;
}

static ht_result linear_hashing_rehash_impl(
    void *impl,
    size_t capacity
) {
    (void)capacity;
    /* Rehash in linear hashing is essentially just catching up with splits. */
    return linear_hashing_reserve_impl(impl, capacity);
}

static ht_result linear_hashing_get_stats_impl(
    const void *impl,
    ht_stats *out
) {
    if (impl == NULL || out == NULL) { return HT_ERR_INVALID; }

    *out = ((const linear_hashing_table *)impl)->stats;
    return HT_OK;
}

static ht_result linear_hashing_reset_stats_impl(
    void *impl
) {
    linear_hashing_table *t = impl;

    if (t == NULL) { return HT_ERR_INVALID; }

    memset(&t->stats, 0, sizeof(t->stats));
    linear_hashing_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->total_buckets);
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void linear_hashing_update_bytes_used(
    linear_hashing_table *t
) {
    size_t bytes;

    if (t != NULL && t->collect_stats) {
        bytes = sizeof(*t);
        bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->buckets));
        bytes = ht_bytes_add_or_max(bytes, slab_pool_bytes_owned(&t->pool));
        t->stats.bytes_used = bytes;
    }
}

static void linear_hashing_free_all_segments(
    linear_hashing_table *t
) {
    if (t == NULL || t->buckets == NULL) { return; }

    for (size_t i = 0; i < t->total_buckets; i++) {
        linear_hashing_segment *seg = t->buckets[i];
        while (seg != NULL) {
            linear_hashing_segment *next = seg->next;
            slab_pool_free(&t->pool, seg);
            seg = next;
        }
    }
}

static ht_result linear_hashing_bucket_push(
    linear_hashing_table *t,
    size_t bucket,
    uint8_t tag,
    ht_key_t key,
    ht_val_t value
) {
    linear_hashing_segment *seg;

    seg = t->buckets[bucket];
    if (seg == NULL || seg->used == LINEAR_HASHING_SEGMENT_CAPACITY) {
        seg = slab_pool_alloc(&t->pool);
        if (seg == NULL) {
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
    return HT_OK;
}

static size_t linear_hashing_bucket_entry_count(
    const linear_hashing_segment *segment
) {
    size_t entries = 0;

    while (segment != NULL) {
        entries += segment->used;
        segment = segment->next;
    }

    return entries;
}

static size_t linear_hashing_segments_for_entries(
    size_t entries
) {
    return (entries == 0)
        ? 0
        : ((entries - 1u) / LINEAR_HASHING_SEGMENT_CAPACITY) + 1u;
}

static ht_result linear_hashing_split(linear_hashing_table *t) {
    size_t old_bucket = t->split_ptr;
    size_t new_bucket = t->total_buckets;
    uint64_t start_ns = ht_resize_instrumentation_start(t->collect_stats);
    size_t m_next = (t->initial_buckets << t->level) << 1;
    size_t moving_entries = 0;
    size_t moved_count = 0;

    /* Growth of the bucket directory if needed. */
    if (new_bucket >= t->capacity) {
        size_t new_cap;
        size_t new_bytes;
        linear_hashing_segment **new_buckets;

        if (t->capacity > SIZE_MAX / 2u) {
            return HT_ERR_OOM;
        }
        new_cap = t->capacity * 2u;
        new_bytes = ht_bytes_mul_or_max(new_cap, sizeof(*new_buckets));
        if (new_bytes == SIZE_MAX) {
            return HT_ERR_OOM;
        }

        new_buckets = realloc(t->buckets, new_bytes);
        if (new_buckets == NULL) {
            return HT_ERR_OOM;
        }
        memset(
            new_buckets + t->capacity,
            0,
            (new_cap - t->capacity) * sizeof(*new_buckets)
        );
        t->buckets = new_buckets;
        t->capacity = new_cap;
    }

    for (linear_hashing_segment *scan = t->buckets[old_bucket];
         scan != NULL;
         scan = scan->next) {
        for (uint8_t i = 0; i < scan->used; i++) {
            uint64_t hash = t->hash_fn(scan->keys[i], t->hash_seed);
            if ((hash & (m_next - 1)) == new_bucket) {
                moving_entries++;
            }
        }
    }
    /* Reserve before unlinking the old chain so split failure leaves state intact. */
    if (slab_pool_reserve(
            &t->pool,
            linear_hashing_segments_for_entries(moving_entries)
        ) != 0) {
        return HT_ERR_OOM;
    }

    /* Move entries from buckets[old_bucket] to buckets[new_bucket] as needed. */
    linear_hashing_segment *seg = t->buckets[old_bucket];
    t->buckets[old_bucket] = NULL;
    t->buckets[new_bucket] = NULL;

    while (seg != NULL) {
        linear_hashing_segment *next_seg = seg->next;
        uint8_t j = 0;
        while (j < seg->used) {
            uint64_t hash = t->hash_fn(seg->keys[j], t->hash_seed);
            size_t dest = hash & (m_next - 1);

            if (dest == new_bucket) {
                /* Move this entry to new_bucket. */
                ht_key_t k = seg->keys[j];
                ht_val_t v = seg->values[j];
                uint8_t tag = seg->tags[j];

                /* Remove from current segment. */
                seg->keys[j] = seg->keys[seg->used - 1];
                seg->values[j] = seg->values[seg->used - 1];
                seg->tags[j] = seg->tags[seg->used - 1];
                seg->used--;
                moved_count++;

                if (linear_hashing_bucket_push(t, new_bucket, tag, k, v) != HT_OK) {
                    return HT_ERR_OOM;
                }
            } else {
                /* Stays in the old bucket chain being rebuilt. */
                j++;
            }
        }

        if (seg->used > 0) {
            /* Put back in old_bucket. */
            seg->next = t->buckets[old_bucket];
            t->buckets[old_bucket] = seg;
        } else {
            /* Empty segment, free it. */
            slab_pool_free(&t->pool, seg);
        }
        seg = next_seg;
    }

    t->total_buckets++;
    t->split_ptr++;

    if (t->split_ptr == (t->initial_buckets << t->level)) {
        t->split_ptr = 0;
        t->level++;
    }

    ht_resize_stats_record(
        &t->stats,
        t->collect_stats,
        t->total_buckets - 1,
        t->total_buckets,
        moved_count,
        start_ns
    );
    linear_hashing_update_bytes_used(t);
    return HT_OK;
}

static ht_result linear_hashing_merge(linear_hashing_table *t) {
    uint64_t start_ns = ht_resize_instrumentation_start(t->collect_stats);
    size_t moved_count = 0;
    size_t new_level = t->level;
    size_t new_split_ptr = t->split_ptr;
    size_t old_bucket;
    size_t target_bucket;
    size_t source_entries;

    /* Compute the post-merge state first so allocation failure changes nothing. */
    if (new_split_ptr == 0) {
        new_level--;
        new_split_ptr = (t->initial_buckets << new_level) - 1;
    } else {
        new_split_ptr--;
    }

    old_bucket = t->total_buckets - 1u;
    target_bucket = new_split_ptr;
    source_entries = linear_hashing_bucket_entry_count(t->buckets[old_bucket]);
    if (slab_pool_reserve(
            &t->pool,
            linear_hashing_segments_for_entries(source_entries)
        ) != 0) {
        return HT_ERR_OOM;
    }

    t->level = new_level;
    t->split_ptr = new_split_ptr;
    t->total_buckets--;

    /* Move all entries from old_bucket to target_bucket. */
    linear_hashing_segment *seg = t->buckets[old_bucket];
    t->buckets[old_bucket] = NULL;

    while (seg != NULL) {
        linear_hashing_segment *next_seg = seg->next;
        for (uint8_t i = 0; i < seg->used; i++) {
            if (linear_hashing_bucket_push(
                    t,
                    target_bucket,
                    seg->tags[i],
                    seg->keys[i],
                    seg->values[i]
                ) != HT_OK) {
                return HT_ERR_OOM;
            }
            moved_count++;
        }
        slab_pool_free(&t->pool, seg);
        seg = next_seg;
    }

    ht_resize_stats_record(
        &t->stats,
        t->collect_stats,
        t->total_buckets + 1,
        t->total_buckets,
        moved_count,
        start_ns
    );
    linear_hashing_update_bytes_used(t);
    return HT_OK;
}
