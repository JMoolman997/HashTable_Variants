#define _POSIX_C_SOURCE 200809L

/**
 * @file    p_separate_chaining.c
 * @brief   Concurrent segmented separate-chaining backend.
 *
 * Implements the concurrent separate-chaining backend used by the generic
 * hashtable wrapper. Buckets are chains of fixed-capacity segments allocated
 * from per-stripe slab pools, with stripe read/write locks protecting normal
 * operations and a table-wide resize lock coordinating rehashes.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#include <pthread.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend_util.h"
#include "ht_internal.h"
#include "p_separate_chaining.h"
#include "slab_pool.h"

/* Version 1 uses stripe rwlocks. Readers on the same stripe can run together,
 * while writers and resize still exclude readers before segments can be freed. */
#define P_SEP_SEGMENT_CAPACITY 8u
#define P_SEP_MIN_STRIPE_COUNT 64u
#define P_SEP_MAX_STRIPE_COUNT 1024u
#define P_SEP_STRIPES_PER_THREAD 32u
#define P_SEP_MIN_BUCKETS_PER_STRIPE 16u
#define P_SEP_CACHELINE_SIZE 64u
#define P_SEP_SLAB_OBJECTS_PER_BLOCK 64u
#define P_SEP_EMERGENCY_BUCKET_ENTRIES 32u
#define P_SEP_EMERGENCY_SEGMENT_DEPTH 4u
#define P_SEP_DEFAULT_MAX_LOAD 1.50
#define P_SEP_DEFAULT_MIN_LOAD 0.25

#ifndef P_SEP_LOCK_KIND
#define P_SEP_LOCK_KIND 1
#endif

#ifndef P_SEP_LOG_CONFIG
#define P_SEP_LOG_CONFIG 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define P_SEP_CACHELINE_ALIGNED __attribute__((aligned(P_SEP_CACHELINE_SIZE)))
#else
#define P_SEP_CACHELINE_ALIGNED
#endif

#if P_SEP_LOCK_KIND == 1
typedef pthread_rwlock_t p_sep_lock;

static inline int p_sep_lock_init(p_sep_lock *lock) {
    return pthread_rwlock_init(lock, NULL);
}

static inline void p_sep_lock_destroy(p_sep_lock *lock) {
    pthread_rwlock_destroy(lock);
}

static inline void p_sep_lock_read(p_sep_lock *lock) {
    pthread_rwlock_rdlock(lock);
}

static inline void p_sep_lock_write(p_sep_lock *lock) {
    pthread_rwlock_wrlock(lock);
}

static inline void p_sep_lock_unlock(p_sep_lock *lock) {
    pthread_rwlock_unlock(lock);
}
#elif P_SEP_LOCK_KIND == 2
typedef pthread_mutex_t p_sep_lock;

static inline int p_sep_lock_init(p_sep_lock *lock) {
    return pthread_mutex_init(lock, NULL);
}

static inline void p_sep_lock_destroy(p_sep_lock *lock) {
    pthread_mutex_destroy(lock);
}

static inline void p_sep_lock_read(p_sep_lock *lock) {
    pthread_mutex_lock(lock);
}

static inline void p_sep_lock_write(p_sep_lock *lock) {
    pthread_mutex_lock(lock);
}

static inline void p_sep_lock_unlock(p_sep_lock *lock) {
    pthread_mutex_unlock(lock);
}
#else
#error "P_SEP_LOCK_KIND must be 1 (pthread_rwlock_t) or 2 (pthread_mutex_t)"
#endif

/**
 * @brief Fixed-capacity bucket-chain segment.
 */
typedef struct p_sep_segment {
    struct p_sep_segment *next; /**< Next overflow segment in the bucket. */
    uint8_t used; /**< Number of occupied entries in this segment. */
    ht_key_t keys[P_SEP_SEGMENT_CAPACITY]; /**< Stored keys. */
    ht_val_t values[P_SEP_SEGMENT_CAPACITY]; /**< Values paired with `keys`. */
} p_sep_segment;

/**
 * @brief Bucket head for a chain of p_sep_segment nodes.
 */
typedef struct {
    p_sep_segment *head; /**< First segment in the bucket chain. */
} p_sep_bucket;

/**
 * @brief Lock, allocator, and counters owned by one concurrency stripe.
 */
typedef struct {
    p_sep_lock lock; /**< Guards buckets and slab segments owned by stripe. */
    slab_pool pool;  /**< Segment allocator local to this stripe. */
    size_t local_size; /**< Number of live entries owned by this stripe. */
    size_t deletes_since_resize; /**< Remove count since last table rebuild. */
    size_t max_bucket_entries_seen; /**< Largest chain length seen by insert. */
    size_t max_segment_depth_seen;  /**< Largest segment depth seen by insert. */

    uint64_t inserts; /**< Successful and attempted insert counter. */
    uint64_t insert_failures; /**< Failed insert counter. */
    _Atomic uint64_t lookups; /**< Lookup counter, shared by readers. */
    _Atomic uint64_t lookup_misses; /**< Lookup miss counter. */
    uint64_t removes; /**< Remove counter under the stripe write lock. */
    uint64_t remove_misses; /**< Remove miss counter. */
    _Atomic uint64_t probes; /**< Total probe count accumulated for stats. */
    _Atomic uint64_t max_probe_len; /**< Largest probe length seen. */

    unsigned char pad[P_SEP_CACHELINE_SIZE]; /**< Avoid adjacent hot stripes. */
} P_SEP_CACHELINE_ALIGNED p_sep_stripe;

/**
 * @brief Cache-line-isolated atomic size counter.
 */
typedef struct {
    _Atomic size_t value; /**< Counter value updated by concurrent stripes. */
} P_SEP_CACHELINE_ALIGNED p_sep_atomic_size;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(
    sizeof(p_sep_stripe) % P_SEP_CACHELINE_SIZE == 0,
    "p_sep_stripe size must be a cache-line multiple"
);
#endif

/**
 * @brief Private state for the concurrent separate-chaining backend.
 */
typedef struct {
    p_sep_bucket *buckets; /**< Current bucket array. */
    size_t capacity;      /**< Bucket count, always a power of two. */
    size_t mask;          /**< Cached capacity - 1 index mask. */
    size_t min_capacity;  /**< Smallest bucket count allowed. */
    size_t max_capacity;  /**< Largest bucket count allowed. */

    p_sep_stripe *stripes; /**< Stripe locks, pools, and local counters. */
    size_t stripe_count;   /**< Number of concurrency stripes. */
    size_t stripe_mask;    /**< Cached stripe_count - 1 mask. */
    size_t buckets_per_stripe; /**< Bucket span owned by each stripe. */
    size_t buckets_per_stripe_shift; /**< Shift from bucket index to stripe. */

    pthread_mutex_t resize_lock; /**< Serializes resize attempts. */
    p_sep_atomic_size live_size; /**< Global live-entry count. */
    p_sep_atomic_size delete_debt; /**< Delete count used to trigger shrink. */

    double max_load_factor; /**< Upper load factor threshold for growth. */
    double min_load_factor; /**< Lower load factor threshold for shrink. */
    ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

    ht_hash_fn hash_fn; /**< Hash function used for key lookup. */
    uint64_t hash_seed; /**< Seed passed into `hash_fn`. */

    int collect_stats; /**< Non-zero when resize stats are tracked. */
    int collect_op_stats; /**< Non-zero when operation stats are tracked. */
    _Atomic size_t grow_entry_limit; /**< Cached entry count growth limit. */
    _Atomic size_t shrink_entry_limit; /**< Cached entry count shrink limit. */
    ht_stats resize_stats; /**< Resize and memory statistics. */
} p_sep_table;

static inline void p_sep_stripe_read_lock(
    p_sep_stripe *stripe
) {
    if (stripe != NULL) { p_sep_lock_read(&stripe->lock); }
}

static inline void p_sep_stripe_write_lock(
    p_sep_stripe *stripe
) {
    if (stripe != NULL) { p_sep_lock_write(&stripe->lock); }
}

static inline void p_sep_stripe_unlock(
    p_sep_stripe *stripe
) {
    if (stripe != NULL) { p_sep_lock_unlock(&stripe->lock); }
}

static inline size_t p_sep_stripe_for_hash(
    const p_sep_table *table,
    uint64_t hash
) {
    return ((size_t)hash & table->mask) >> table->buckets_per_stripe_shift;
}

static inline size_t p_sep_stripe_for_bucket_shift(
    const p_sep_table *table,
    size_t bucket_index,
    size_t shift
) {
    size_t stripe_index;

    if (table == NULL || table->stripe_count == 0) { return 0; }

    stripe_index = bucket_index >> shift;
    return (stripe_index < table->stripe_count)
        ? stripe_index
        : table->stripe_count - 1u;
}

static inline size_t p_sep_stripe_for_bucket(
    const p_sep_table *table,
    size_t bucket_index
) {
    return p_sep_stripe_for_bucket_shift(
        table,
        bucket_index,
        table->buckets_per_stripe_shift
    );
}

/* --- vtable operations --------------------------------------------------- */

static void p_sep_destroy_impl(void *impl);
static ht_result p_sep_insert_impl(void *impl, ht_key_t key, ht_val_t value);
static ht_result p_sep_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
);
static ht_result p_sep_remove_impl(void *impl, ht_key_t key);
static size_t p_sep_size_impl(const void *impl);
static size_t p_sep_capacity_impl(const void *impl);
static double p_sep_load_factor_impl(const void *impl);
static ht_result p_sep_reserve_impl(void *impl, size_t capacity);
static ht_result p_sep_rehash_impl(void *impl, size_t capacity);
static ht_result p_sep_get_stats_impl(const void *impl, ht_stats *out);
static ht_result p_sep_reset_stats_impl(void *impl);
static int p_sep_bind_bench_iface_flags_impl(
    void *impl,
    bench_iface *out,
    unsigned flags
);

/* --- helpers ------------------------------------------------------------- */

static int p_sep_valid_load_factors(double min_load, double max_load);
static void p_sep_update_stripe_mapping(p_sep_table *table);
static void p_sep_update_resize_limits(p_sep_table *table);
static void p_sep_log_config(const p_sep_table *table);
static int p_sep_try_reserve_insert(p_sep_table *table);
static void p_sep_cancel_reserved_insert(p_sep_table *table);
static void p_sep_account_insert_success(p_sep_table *table);
static void p_sep_account_remove_success(p_sep_table *table);
static ht_result p_sep_reserve_segments(
    p_sep_table *table,
    size_t expected_entries
);
static size_t p_sep_normalize_stripe_count(
    size_t capacity,
    size_t thread_count
);
static p_sep_stripe *p_sep_stripe_array_alloc(size_t count);
static int p_sep_stripes_init(p_sep_stripe *stripes, size_t count);
static void p_sep_stripes_destroy(p_sep_stripe *stripes, size_t count);
static p_sep_bucket *p_sep_bucket_array_alloc(size_t capacity);
static void p_sep_bucket_array_free_segments_with_shift(
    p_sep_table *t,
    p_sep_bucket *buckets,
    size_t capacity,
    size_t stripe_shift
);

static void p_sep_read_lock_all_stripes(p_sep_table *table);
static void p_sep_write_lock_all_stripes(p_sep_table *table);
static void p_sep_unlock_all_stripes(p_sep_table *table);
static size_t p_sep_size_locked(const p_sep_table *table);
static size_t p_sep_bytes_used_locked(const p_sep_table *table);
static p_sep_segment *p_sep_segment_alloc(p_sep_stripe *stripe);
static void p_sep_segment_free(p_sep_stripe *stripe, p_sep_segment *segment);

/**
 * @brief Result of scanning one locked segmented bucket chain.
 */
typedef struct {
    int found; /**< Non-zero when the requested key was found. */
    p_sep_segment *found_segment; /**< Segment that contains the key. */
    uint8_t found_index; /**< Entry index inside `found_segment`. */
    p_sep_segment *first_free_segment; /**< First segment with free capacity. */
    size_t bucket_entries; /**< Number of live entries in the chain. */
    size_t segment_depth;  /**< Number of segments traversed. */
    uint64_t probe_len;    /**< Probe count recorded for stats. */
} p_sep_bucket_scan;

static p_sep_bucket_scan p_sep_bucket_scan_locked(
    const p_sep_bucket *bucket,
    ht_key_t key
);
static ht_result p_sep_bucket_insert_absent_no_resize(
    p_sep_table *table,
    p_sep_bucket *bucket,
    size_t bucket_index,
    size_t stripe_shift,
    ht_key_t key,
    ht_val_t value,
    const p_sep_bucket_scan *scan,
    size_t *bucket_entries_out,
    size_t *segment_depth_out
);
static ht_result p_sep_bucket_insert_migrated_locked(
    p_sep_table *t,
    p_sep_bucket *buckets,
    size_t mask,
    size_t stripe_shift,
    ht_key_t key,
    ht_val_t value
);
static ht_result p_sep_bucket_get_locked(
    const p_sep_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
);
static ht_result p_sep_get_frozen_read_only(
    const void *ctx,
    ht_key_t key,
    ht_val_t *value_out
);
static ht_result p_sep_bucket_remove_locked(
    p_sep_table *t,
    p_sep_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
);

static void p_sep_note_probe(
    p_sep_table *table,
    p_sep_stripe *stripe,
    uint64_t probe_len
);
static void p_sep_record_insert_failure(
    p_sep_table *table,
    p_sep_stripe *stripe
);
static size_t p_sep_deletes_since_resize_locked(const p_sep_table *table);
static void p_sep_reset_deletes_since_resize_locked(p_sep_table *table);
static ht_result p_sep_maybe_resize(p_sep_table *table, int force_grow);
static ht_result p_sep_resize_locked(
    p_sep_table *table,
    size_t requested_capacity
);

static const struct ht_vtable P_SEP_VTABLE = {
    .destroy = p_sep_destroy_impl,
    .insert = p_sep_insert_impl,
    .get = p_sep_get_impl,
    .remove = p_sep_remove_impl,
    .size = p_sep_size_impl,
    .capacity = p_sep_capacity_impl,
    .load_factor = p_sep_load_factor_impl,
    .reserve = p_sep_reserve_impl,
    .rehash = p_sep_rehash_impl,
    .get_stats = p_sep_get_stats_impl,
    .reset_stats = p_sep_reset_stats_impl,
    .bind_bench_iface = p_separate_chaining_bind_bench_iface,
    .bind_bench_iface_flags = p_sep_bind_bench_iface_flags_impl
};

static p_sep_table *p_sep_table_alloc(
    void
) {
    void *ptr = NULL;

    if (posix_memalign(&ptr, P_SEP_CACHELINE_SIZE, sizeof(p_sep_table)) != 0) {
        return NULL;
    }
    memset(ptr, 0, sizeof(p_sep_table));
    return ptr;
}

ht_result p_separate_chaining_create_impl_ex(
    const ht_config *cfg,
    void           **out
) {
    p_sep_table *table;
    size_t capacity;
    size_t min_capacity;
    size_t stripe_count;
    size_t thread_count;

    if (out == NULL) { return HT_ERR_INVALID; }
    *out = NULL;

    if (cfg == NULL) { return HT_ERR_INVALID; }

    capacity = (cfg->init_capacity > 0)
        ? cfg->init_capacity
        : DEFAULT_INITIAL_CAPACITY;
    if (ht_checked_next_pow2(capacity, &capacity) != HT_OK) {
        return HT_ERR_INVALID;
    }

    min_capacity = (cfg->min_capacity > 0)
        ? cfg->min_capacity
        : DEFAULT_MIN_CAPACITY;
    if (ht_checked_next_pow2(min_capacity, &min_capacity) != HT_OK) {
        return HT_ERR_INVALID;
    }

    if (capacity < min_capacity) { capacity = min_capacity; }

    thread_count = (cfg->thread_count > 0) ? cfg->thread_count : 1u;
    stripe_count = p_sep_normalize_stripe_count(capacity, thread_count);
    if (stripe_count == 0) { return HT_ERR_INVALID; }
    if (min_capacity < stripe_count) { min_capacity = stripe_count; }
    if (capacity < min_capacity) { capacity = min_capacity; }

    table = p_sep_table_alloc();
    if (table == NULL) { return HT_ERR_OOM; }

    table->buckets = p_sep_bucket_array_alloc(capacity);
    if (table->buckets == NULL) {
        goto fail_table_oom;
    }

    table->stripes = p_sep_stripe_array_alloc(stripe_count);
    if (table->stripes == NULL) {
        goto fail_buckets_oom;
    }

    if (p_sep_stripes_init(table->stripes, stripe_count) != 0) {
        goto fail_stripes_error;
    }

    if (pthread_mutex_init(&table->resize_lock, NULL) != 0) {
        goto fail_resize_lock_error;
    }

    table->capacity = capacity;
    table->mask = capacity - 1u;
    table->min_capacity = min_capacity;
    table->max_capacity = SIZE_MAX / 2u;
    table->stripe_count = stripe_count;
    table->stripe_mask = stripe_count - 1u;
    p_sep_update_stripe_mapping(table);
    table->max_load_factor = (cfg->max_load_factor == 0.0)
        ? P_SEP_DEFAULT_MAX_LOAD
        : cfg->max_load_factor;
    table->min_load_factor = (cfg->min_load_factor == 0.0)
        ? P_SEP_DEFAULT_MIN_LOAD
        : cfg->min_load_factor;
    if (!p_sep_valid_load_factors(
            table->min_load_factor,
            table->max_load_factor
        )) {
        goto fail_stripes_initialized_invalid;
    }
    table->resize_mode = cfg->rsz_mode;
    table->hash_fn = (cfg->hash_fn != NULL) ? cfg->hash_fn : default_hash;
    table->hash_seed = cfg->hash_seed;
    table->collect_stats = cfg->collect_stats;
    table->collect_op_stats = cfg->collect_stats;
    atomic_init(&table->live_size.value, 0);
    atomic_init(&table->delete_debt.value, 0);
    atomic_init(&table->grow_entry_limit, 0);
    atomic_init(&table->shrink_entry_limit, 0);
    p_sep_update_resize_limits(table);
    ht_resize_stats_init(
        &table->resize_stats,
        table->collect_stats,
        table->capacity
    );
    p_sep_log_config(table);

    *out = table;
    return HT_OK;

fail_stripes_initialized_invalid:
    pthread_mutex_destroy(&table->resize_lock);
    p_sep_stripes_destroy(table->stripes, stripe_count);
    free(table->stripes);
    free(table->buckets);
    free(table);
    return HT_ERR_INVALID;

fail_resize_lock_error:
    p_sep_stripes_destroy(table->stripes, stripe_count);
fail_stripes_error:
    free(table->stripes);
    free(table->buckets);
    free(table);
    return HT_ERR;

fail_buckets_oom:
    free(table->buckets);
fail_table_oom:
    free(table);
    return HT_ERR_OOM;
}

const struct ht_vtable *p_separate_chaining_vtable(
    void
) {
    return &P_SEP_VTABLE;
}

int p_separate_chaining_bind_bench_iface(
    void *ctx,
    bench_iface *out
) {
    return p_sep_bind_bench_iface_flags_impl(ctx, out, 0);
}

static void p_sep_destroy_impl(
    void *impl
) {
    p_sep_table *table = impl;

    if (table == NULL) { return; }

    free(table->buckets);
    pthread_mutex_destroy(&table->resize_lock);
    p_sep_stripes_destroy(table->stripes, table->stripe_count);
    free(table->stripes);
    free(table);
}

static ht_result p_sep_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
) {
    p_sep_table *table = impl;
    p_sep_stripe *stripe;
    p_sep_bucket_scan scan;
    uint64_t hash;
    size_t stripe_index;
    size_t bucket_index;
    size_t entry_count = 0;
    size_t depth = 0;
    ht_result rc;
    int inserted = 0;
    int should_resize = 0;
    int reserved = 0;

    if (table == NULL) { return HT_ERR_INVALID; }

    hash = table->hash_fn(key, table->hash_seed);
    stripe_index = p_sep_stripe_for_hash(table, hash);

    if (table->resize_mode == HT_RESIZE_NONE) {
        stripe = &table->stripes[stripe_index];

        /* Fixed-size mode mutates only one stripe and reserves global capacity
         * while the owning stripe is still locked. */
        p_sep_stripe_write_lock(stripe);
        if (table->collect_op_stats) { stripe->inserts++; }
        bucket_index = (size_t)hash & table->mask;
        scan = p_sep_bucket_scan_locked(&table->buckets[bucket_index], key);
        if (scan.found) {
            p_sep_record_insert_failure(table, stripe);
            p_sep_note_probe(table, stripe, scan.probe_len);
            p_sep_stripe_unlock(stripe);
            rc = HT_ERR_EXISTS;
            return rc;
        }

        reserved = p_sep_try_reserve_insert(table);
        if (!reserved) {
            p_sep_record_insert_failure(table, stripe);
            p_sep_note_probe(table, stripe, scan.probe_len);
            p_sep_stripe_unlock(stripe);
            rc = HT_ERR_FULL;
            return rc;
        }

        rc = p_sep_bucket_insert_absent_no_resize(
            table,
            &table->buckets[bucket_index],
            bucket_index,
            table->buckets_per_stripe_shift,
            key,
            value,
            &scan,
            &entry_count,
            &depth
        );
        if (rc == HT_OK) {
            inserted = 1;
            stripe->local_size++;
            if (entry_count > stripe->max_bucket_entries_seen) {
                stripe->max_bucket_entries_seen = entry_count;
            }
            if (depth > stripe->max_segment_depth_seen) {
                stripe->max_segment_depth_seen = depth;
            }
        } else {
            p_sep_cancel_reserved_insert(table);
            p_sep_record_insert_failure(table, stripe);
        }
        p_sep_note_probe(table, stripe, scan.probe_len);
        p_sep_stripe_unlock(stripe);
        return inserted ? HT_OK : rc;
    }

    stripe = &table->stripes[stripe_index];

    /* Normal inserts optimistically touch one stripe; resize is considered
     * after releasing it so table-wide locking never nests under a stripe. */
    p_sep_stripe_write_lock(stripe);
    if (table->collect_op_stats) { stripe->inserts++; }

    bucket_index = (size_t)hash & table->mask;
    scan = p_sep_bucket_scan_locked(&table->buckets[bucket_index], key);
    if (scan.found) {
        rc = HT_ERR_EXISTS;
    } else {
        rc = p_sep_bucket_insert_absent_no_resize(
            table,
            &table->buckets[bucket_index],
            bucket_index,
            table->buckets_per_stripe_shift,
            key,
            value,
            &scan,
            &entry_count,
            &depth
        );
        inserted = (rc == HT_OK);
    }
    if (inserted) {
        stripe->local_size++;
        p_sep_account_insert_success(table);
        if (entry_count > stripe->max_bucket_entries_seen) {
            stripe->max_bucket_entries_seen = entry_count;
        }
        if (depth > stripe->max_segment_depth_seen) {
            stripe->max_segment_depth_seen = depth;
        }
        /* A very long bucket means this stripe is seeing pathological chains
         * even if average load has not crossed the normal growth threshold. */
        should_resize =
            (entry_count > P_SEP_EMERGENCY_BUCKET_ENTRIES ||
             depth > P_SEP_EMERGENCY_SEGMENT_DEPTH);
    } else {
        p_sep_record_insert_failure(table, stripe);
    }
    p_sep_note_probe(table, stripe, scan.probe_len);
    p_sep_stripe_unlock(stripe);

    if (!inserted) { return rc; }

    if (table->resize_mode != HT_RESIZE_NONE) {
        (void)p_sep_maybe_resize(table, should_resize);
    }

    return HT_OK;
}

static ht_result p_sep_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
) {
    p_sep_table *table = (p_sep_table *)impl;
    p_sep_stripe *stripe;
    uint64_t hash;
    uint64_t probe_len = 1;
    size_t bucket_index;
    ht_result rc;

    if (value_out == NULL) { return HT_ERR_INVALID; }
    if (table == NULL) { return HT_ERR_INVALID; }

    hash = table->hash_fn(key, table->hash_seed);
    stripe = &table->stripes[p_sep_stripe_for_hash(table, hash)];

    /* The read lock keeps segment memory stable while this lookup walks the
     * bucket chain. */
    p_sep_stripe_read_lock(stripe);
    if (table->collect_op_stats) {
        atomic_fetch_add_explicit(&stripe->lookups, 1, memory_order_relaxed);
    }
    bucket_index = (size_t)hash & table->mask;
    rc = p_sep_bucket_get_locked(
        table->buckets,
        bucket_index,
        key,
        value_out,
        &probe_len
    );
    p_sep_note_probe(table, stripe, probe_len);
    if (rc != HT_OK && table->collect_op_stats) {
        atomic_fetch_add_explicit(
            &stripe->lookup_misses,
            1,
            memory_order_relaxed
        );
    }
    p_sep_stripe_unlock(stripe);

    rc = (rc == HT_OK) ? HT_OK : HT_ERR_NOT_FOUND;
    return rc;
}

static ht_result p_sep_remove_impl(
    void *impl,
    ht_key_t key
) {
    p_sep_table *table = impl;
    p_sep_stripe *stripe;
    uint64_t hash;
    uint64_t probe_len = 1;
    size_t bucket_index;
    ht_result rc;

    if (table == NULL) { return HT_ERR_INVALID; }

    hash = table->hash_fn(key, table->hash_seed);
    stripe = &table->stripes[p_sep_stripe_for_hash(table, hash)];

    /* Removal can free an empty segment, so it takes the stripe write lock. */
    p_sep_stripe_write_lock(stripe);
    if (table->collect_op_stats) { stripe->removes++; }

    bucket_index = (size_t)hash & table->mask;
    rc = p_sep_bucket_remove_locked(
        table,
        table->buckets,
        bucket_index,
        key,
        &probe_len
    );
    if (rc == HT_OK) {
        stripe->local_size--;
        stripe->deletes_since_resize++;
        p_sep_account_remove_success(table);
    } else if (table->collect_op_stats) {
        stripe->remove_misses++;
    }
    p_sep_note_probe(table, stripe, probe_len);
    p_sep_stripe_unlock(stripe);

    if (rc != HT_OK) {
        return HT_ERR_NOT_FOUND;
    }

    if (table->resize_mode == HT_RESIZE_GROW_SHRINK) {
        (void)p_sep_maybe_resize(table, 0);
    }

    return HT_OK;
}

static size_t p_sep_size_impl(
    const void *impl
) {
    p_sep_table *table = (p_sep_table *)impl;

    if (table == NULL) { return 0; }

    return atomic_load_explicit(&table->live_size.value, memory_order_relaxed);
}

static size_t p_sep_capacity_impl(
    const void *impl
) {
    p_sep_table *table = (p_sep_table *)impl;
    size_t capacity;

    if (table == NULL) { return 0; }

    p_sep_read_lock_all_stripes(table);
    capacity = table->capacity;
    p_sep_unlock_all_stripes(table);
    return capacity;
}

static double p_sep_load_factor_impl(
    const void *impl
) {
    p_sep_table *table = (p_sep_table *)impl;
    size_t entry_count;
    size_t capacity;
    double load;

    if (table == NULL) { return 0.0; }

    p_sep_read_lock_all_stripes(table);
    entry_count = p_sep_size_locked(table);
    capacity = table->capacity;
    load = ht_load_factor_snapshot(entry_count, capacity);
    p_sep_unlock_all_stripes(table);
    return load;
}

static ht_result p_sep_reserve_impl(
    void *impl,
    size_t capacity
) {
    p_sep_table *table = impl;
    size_t rounded;
    size_t max_entries;
    size_t live_entries;
    ht_result rc;

    if (table == NULL) { return HT_ERR_INVALID; }
    if (capacity == 0) { return HT_ERR_INVALID; }

    rc = ht_checked_next_pow2(capacity, &rounded);
    if (rc != HT_OK) { return rc; }
    if (rounded > table->max_capacity) {
        return HT_ERR_OOM;
    }

    pthread_mutex_lock(&table->resize_lock);
    /* Reserve may allocate/free segments through resize, so every stripe is
     * locked before the bucket mapping can change. */
    p_sep_write_lock_all_stripes(table);
    if (rounded > table->capacity) {
        rc = p_sep_resize_locked(table, rounded);
    } else {
        rc = HT_OK;
    }
    if (rc == HT_OK) {
        max_entries = ht_load_floor_limit(
            table->capacity,
            table->max_load_factor
        );
        live_entries = p_sep_size_locked(table);
        rc = p_sep_reserve_segments(
            table,
            (max_entries > live_entries) ? max_entries - live_entries : 0
        );
    }
    p_sep_unlock_all_stripes(table);
    pthread_mutex_unlock(&table->resize_lock);
    return rc;
}

static ht_result p_sep_rehash_impl(
    void *impl,
    size_t capacity
) {
    p_sep_table *table = impl;
    size_t target;
    size_t rounded;
    ht_result rc;

    if (table == NULL) { return HT_ERR_INVALID; }
    if (capacity == 0) { return HT_ERR_INVALID; }

    pthread_mutex_lock(&table->resize_lock);
    /* Rehash rebuilds the full bucket array under the same lock order used by
     * automatic resize: resize mutex first, then stripes in index order. */
    p_sep_write_lock_all_stripes(table);
    target = capacity;
    if (target < p_sep_size_locked(table)) {
        target = p_sep_size_locked(table);
    }
    if (target < table->min_capacity) {
        target = table->min_capacity;
    }
    rc = ht_checked_next_pow2(target, &rounded);
    if (rc == HT_OK) {
        rc = p_sep_resize_locked(table, rounded);
    }
    p_sep_unlock_all_stripes(table);
    pthread_mutex_unlock(&table->resize_lock);
    return rc;
}

static ht_result p_sep_get_stats_impl(
    const void *impl,
    ht_stats *out
) {
    p_sep_table *table = (p_sep_table *)impl;
    size_t stripe_index;

    if (out == NULL) { return HT_ERR_INVALID; }
    if (table == NULL) { return HT_ERR_INVALID; }

    p_sep_read_lock_all_stripes(table);
    *out = table->resize_stats;
    for (stripe_index = 0; stripe_index < table->stripe_count; stripe_index++) {
        const p_sep_stripe *stripe = &table->stripes[stripe_index];

        out->inserts += stripe->inserts;
        out->insert_failures += stripe->insert_failures;
        out->lookups += atomic_load_explicit(
            &stripe->lookups,
            memory_order_relaxed
        );
        out->lookup_misses += atomic_load_explicit(
            &stripe->lookup_misses,
            memory_order_relaxed
        );
        out->removes += stripe->removes;
        out->remove_misses += stripe->remove_misses;
        {
            uint64_t probes = atomic_load_explicit(
                &stripe->probes,
                memory_order_relaxed
            );
            uint64_t max_probe_len = atomic_load_explicit(
                &stripe->max_probe_len,
                memory_order_relaxed
            );

            out->probes += probes;
            if (max_probe_len > out->max_probe_len) {
                out->max_probe_len = max_probe_len;
            }
        }
    }
    out->bytes_used = p_sep_bytes_used_locked(table);
    p_sep_unlock_all_stripes(table);

    return HT_OK;
}

static ht_result p_sep_reset_stats_impl(
    void *impl
) {
    p_sep_table *table = impl;
    size_t stripe_index;

    if (table == NULL) { return HT_ERR_INVALID; }

    p_sep_write_lock_all_stripes(table);
    memset(&table->resize_stats, 0, sizeof(table->resize_stats));
    ht_resize_stats_init(
        &table->resize_stats,
        table->collect_stats,
        table->capacity
    );
    for (stripe_index = 0; stripe_index < table->stripe_count; stripe_index++) {
        table->stripes[stripe_index].inserts = 0;
        table->stripes[stripe_index].insert_failures = 0;
        atomic_store_explicit(
            &table->stripes[stripe_index].lookups,
            0,
            memory_order_relaxed
        );
        atomic_store_explicit(
            &table->stripes[stripe_index].lookup_misses,
            0,
            memory_order_relaxed
        );
        table->stripes[stripe_index].removes = 0;
        table->stripes[stripe_index].remove_misses = 0;
        atomic_store_explicit(
            &table->stripes[stripe_index].probes,
            0,
            memory_order_relaxed
        );
        atomic_store_explicit(
            &table->stripes[stripe_index].max_probe_len,
            0,
            memory_order_relaxed
        );
        table->stripes[stripe_index].max_bucket_entries_seen = 0;
        table->stripes[stripe_index].max_segment_depth_seen = 0;
    }
    p_sep_unlock_all_stripes(table);
    return HT_OK;
}

static int p_sep_bind_bench_iface_flags_impl(
    void *impl,
    bench_iface *out,
    unsigned flags
) {
    if (impl == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    out->ctx = impl;
    out->insert = p_sep_insert_impl;
    /* Frozen read-only runs can skip locks because setup has finished. */
    out->get = (flags & BENCH_IFACE_FROZEN_READ_ONLY)
        ? p_sep_get_frozen_read_only
        : p_sep_get_impl;
    out->remove = p_sep_remove_impl;
    return HT_OK;
}

static int p_sep_valid_load_factors(
    double min_load,
    double max_load
) {
    return isfinite(min_load) &&
           isfinite(max_load) &&
           min_load >= 0.0 &&
           max_load > 0.0 &&
           min_load < max_load;
}

static void p_sep_update_stripe_mapping(
    p_sep_table *table
) {
    if (table == NULL || table->stripe_count == 0) {
        return;
    }

    table->buckets_per_stripe = table->capacity / table->stripe_count;
    if (table->buckets_per_stripe == 0) {
        table->buckets_per_stripe = 1;
    }
    table->buckets_per_stripe_shift =
        ht_pow2_shift(table->buckets_per_stripe);
}

static void p_sep_update_resize_limits(
    p_sep_table *table
) {
    if (table == NULL) {
        return;
    }

    atomic_store_explicit(
        &table->grow_entry_limit,
        ht_load_floor_limit(table->capacity, table->max_load_factor),
        memory_order_release
    );
    atomic_store_explicit(
        &table->shrink_entry_limit,
        ht_load_ceil_limit(table->capacity, table->min_load_factor),
        memory_order_release
    );
}

static void p_sep_log_config(
    const p_sep_table *table
) {
#if P_SEP_LOG_CONFIG
    if (table == NULL || table->stripe_count == 0) { return; }

    fprintf(
        stderr,
        "p_sep: capacity=%zu stripes=%zu buckets_per_stripe=%.2f "
        "resize=%d stats=%d op_stats=%d lock_kind=%d\n",
        table->capacity,
        table->stripe_count,
        (double)table->capacity / (double)table->stripe_count,
        (int)table->resize_mode,
        table->collect_stats,
        table->collect_op_stats,
        P_SEP_LOCK_KIND
    );
#else
    (void)table;
#endif
}

static int p_sep_try_reserve_insert(
    p_sep_table *table
) {
    size_t current;
    size_t limit;

    if (table == NULL) { return 0; }

    limit = atomic_load_explicit(
        &table->grow_entry_limit,
        memory_order_acquire
    );
    current = atomic_load_explicit(
        &table->live_size.value,
        memory_order_relaxed
    );

    while (current < limit) {
        if (atomic_compare_exchange_weak_explicit(
                &table->live_size.value,
                &current,
                current + 1u,
                memory_order_relaxed,
                memory_order_relaxed
            )) {
            return 1;
        }
    }

    return 0;
}

static void p_sep_cancel_reserved_insert(
    p_sep_table *table
) {
    if (table == NULL) { return; }

    atomic_fetch_sub_explicit(
        &table->live_size.value,
        1u,
        memory_order_relaxed
    );
}

static void p_sep_account_insert_success(
    p_sep_table *table
) {
    if (table == NULL) { return; }

    atomic_fetch_add_explicit(
        &table->live_size.value,
        1u,
        memory_order_relaxed
    );
}

static void p_sep_account_remove_success(
    p_sep_table *table
) {
    if (table == NULL) { return; }

    atomic_fetch_sub_explicit(
        &table->live_size.value,
        1u,
        memory_order_relaxed
    );
    atomic_fetch_add_explicit(
        &table->delete_debt.value,
        1u,
        memory_order_relaxed
    );
}

static ht_result p_sep_reserve_segments(
    p_sep_table *table,
    size_t expected_entries
) {
    size_t expected_segments;
    size_t segments_per_stripe;
    size_t stripe_index;

    if (table == NULL) { return HT_ERR_INVALID; }
    if (expected_entries == 0) { return HT_OK; }

    expected_segments =
        (expected_entries + P_SEP_SEGMENT_CAPACITY - 1u) /
        P_SEP_SEGMENT_CAPACITY;
    segments_per_stripe =
        (expected_segments + table->stripe_count - 1u) /
        table->stripe_count;

    for (stripe_index = 0; stripe_index < table->stripe_count; stripe_index++) {
        if (slab_pool_reserve(
                &table->stripes[stripe_index].pool,
                segments_per_stripe
            ) != 0) {
            return HT_ERR_OOM;
        }
    }

    return HT_OK;
}

static size_t p_sep_normalize_stripe_count(
    size_t capacity,
    size_t thread_count
) {
    size_t target;
    size_t max_stripes;
    size_t stripes;

    if (capacity == 0) { return 0; }

    if (thread_count == 0) { thread_count = 1; }
    if (thread_count > SIZE_MAX / P_SEP_STRIPES_PER_THREAD) {
        target = P_SEP_MAX_STRIPE_COUNT;
    } else {
        target = thread_count * P_SEP_STRIPES_PER_THREAD;
    }
    if (target < P_SEP_MIN_STRIPE_COUNT) {
        target = P_SEP_MIN_STRIPE_COUNT;
    }
    if (target > P_SEP_MAX_STRIPE_COUNT) {
        target = P_SEP_MAX_STRIPE_COUNT;
    }

    max_stripes = capacity / P_SEP_MIN_BUCKETS_PER_STRIPE;
    if (max_stripes == 0) { max_stripes = 1; }
    max_stripes = floor_pow2(max_stripes);
    if (max_stripes == 0) { max_stripes = 1; }
    if (target > max_stripes) { target = max_stripes; }

    stripes = next_pow2(target);
    if (stripes == 0 || stripes > max_stripes) {
        stripes = floor_pow2(max_stripes);
    }
    return (stripes == 0) ? 1 : stripes;
}

static p_sep_stripe *p_sep_stripe_array_alloc(
    size_t count
) {
    void *ptr = NULL;
    size_t bytes;

    if (count == 0 || count > SIZE_MAX / sizeof(p_sep_stripe)) { return NULL; }

    bytes = count * sizeof(p_sep_stripe);
    if (posix_memalign(&ptr, P_SEP_CACHELINE_SIZE, bytes) != 0) { return NULL; }

    memset(ptr, 0, bytes);
    return ptr;
}

static int p_sep_stripes_init(
    p_sep_stripe *stripes,
    size_t count
) {
    size_t initialized = 0;
    size_t stripe_index;

    if (stripes == NULL || count == 0) { return -1; }

    for (stripe_index = 0; stripe_index < count; stripe_index++) {
        if (p_sep_lock_init(&stripes[stripe_index].lock) != 0) {
            goto fail;
        }
        atomic_init(&stripes[stripe_index].lookups, 0);
        atomic_init(&stripes[stripe_index].lookup_misses, 0);
        atomic_init(&stripes[stripe_index].probes, 0);
        atomic_init(&stripes[stripe_index].max_probe_len, 0);
        if (slab_pool_init(
                &stripes[stripe_index].pool,
                sizeof(p_sep_segment),
                P_SEP_SLAB_OBJECTS_PER_BLOCK
            ) != 0) {
            p_sep_lock_destroy(&stripes[stripe_index].lock);
            goto fail;
        }
        initialized++;
    }

    return 0;

fail:
    while (initialized > 0) {
        initialized--;
        slab_pool_destroy(&stripes[initialized].pool);
        p_sep_lock_destroy(&stripes[initialized].lock);
    }
    return -1;
}

static void p_sep_stripes_destroy(
    p_sep_stripe *stripes,
    size_t count
) {
    size_t stripe_index;

    if (stripes == NULL) { return; }

    for (stripe_index = 0; stripe_index < count; stripe_index++) {
        slab_pool_destroy(&stripes[stripe_index].pool);
        p_sep_lock_destroy(&stripes[stripe_index].lock);
    }
}

static p_sep_bucket *p_sep_bucket_array_alloc(
    size_t capacity
) {
    return calloc(capacity, sizeof(p_sep_bucket));
}

static void p_sep_bucket_array_free_segments_with_shift(
    p_sep_table *table,
    p_sep_bucket *buckets,
    size_t capacity,
    size_t stripe_shift
) {
    size_t bucket_index;

    if (table == NULL || buckets == NULL) { return; }

    for (bucket_index = 0; bucket_index < capacity; bucket_index++) {
        p_sep_segment *segment = buckets[bucket_index].head;
        p_sep_stripe *stripe = &table->stripes[p_sep_stripe_for_bucket_shift(
            table,
            bucket_index,
            stripe_shift
        )];

        while (segment != NULL) {
            p_sep_segment *next = segment->next;
            p_sep_segment_free(stripe, segment);
            segment = next;
        }
        buckets[bucket_index].head = NULL;
    }
}

static void p_sep_read_lock_all_stripes(
    p_sep_table *table
) {
    size_t stripe_index;

    if (table == NULL) { return; }

    for (stripe_index = 0; stripe_index < table->stripe_count; stripe_index++) {
        p_sep_stripe_read_lock(&table->stripes[stripe_index]);
    }
}

static void p_sep_write_lock_all_stripes(
    p_sep_table *table
) {
    size_t stripe_index;

    if (table == NULL) { return; }

    for (stripe_index = 0; stripe_index < table->stripe_count; stripe_index++) {
        p_sep_stripe_write_lock(&table->stripes[stripe_index]);
    }
}

static void p_sep_unlock_all_stripes(
    p_sep_table *table
) {
    size_t stripe_index;

    if (table == NULL) { return; }

    stripe_index = table->stripe_count;
    while (stripe_index > 0) {
        stripe_index--;
        p_sep_stripe_unlock(&table->stripes[stripe_index]);
    }
}

static size_t p_sep_size_locked(
    const p_sep_table *table
) {
    if (table == NULL) { return 0; }

    return atomic_load_explicit(&table->live_size.value, memory_order_relaxed);
}

static size_t p_sep_deletes_since_resize_locked(
    const p_sep_table *table
) {
    if (table == NULL) { return 0; }

    return atomic_load_explicit(&table->delete_debt.value, memory_order_relaxed);
}

static void p_sep_reset_deletes_since_resize_locked(
    p_sep_table *table
) {
    size_t stripe_index;

    if (table == NULL) { return; }

    for (stripe_index = 0; stripe_index < table->stripe_count; stripe_index++) {
        table->stripes[stripe_index].deletes_since_resize = 0;
    }
    atomic_store_explicit(&table->delete_debt.value, 0, memory_order_relaxed);
}

static size_t p_sep_bytes_used_locked(
    const p_sep_table *table
) {
    size_t bytes;
    size_t stripe_index;

    if (table == NULL) { return 0; }

    bytes = ht_bytes_used_snapshot(
        sizeof(*table),
        table->capacity,
        sizeof(*table->buckets)
    );
    if (bytes == SIZE_MAX) { return SIZE_MAX; }

    if (table->stripe_count > (SIZE_MAX - bytes) / sizeof(*table->stripes)) {
        return SIZE_MAX;
    }
    bytes += table->stripe_count * sizeof(*table->stripes);

    for (stripe_index = 0; stripe_index < table->stripe_count; stripe_index++) {
        size_t slab_bytes = slab_pool_bytes_owned(
            &table->stripes[stripe_index].pool
        );

        if (slab_bytes > SIZE_MAX - bytes) { return SIZE_MAX; }
        bytes += slab_bytes;
    }

    return bytes;
}

static p_sep_segment *p_sep_segment_alloc(
    p_sep_stripe *stripe
) {
    p_sep_segment *segment;

    if (stripe == NULL) { return NULL; }

    segment = slab_pool_alloc(&stripe->pool);
    if (segment == NULL) { return NULL; }

    segment->next = NULL;
    segment->used = 0;
    return segment;
}

static void p_sep_segment_free(
    p_sep_stripe *stripe,
    p_sep_segment *segment
) {
    if (stripe == NULL || segment == NULL) { return; }

    slab_pool_free(&stripe->pool, segment);
}

static p_sep_bucket_scan p_sep_bucket_scan_locked(
    const p_sep_bucket *bucket,
    ht_key_t key
) {
    p_sep_bucket_scan scan;
    p_sep_segment *segment;
    uint64_t probe_len = 1;

    memset(&scan, 0, sizeof(scan));
    scan.probe_len = 1;

    if (bucket == NULL) { return scan; }

    for (segment = bucket->head; segment != NULL; segment = segment->next) {
        uint8_t entry_index;

        scan.segment_depth++;
        scan.bucket_entries += segment->used;
        if (scan.first_free_segment == NULL &&
            segment->used < P_SEP_SEGMENT_CAPACITY) {
            scan.first_free_segment = segment;
        }
        for (entry_index = 0; entry_index < segment->used; entry_index++) {
            if (!scan.found && segment->keys[entry_index] == key) {
                scan.found = 1;
                scan.found_segment = segment;
                scan.found_index = entry_index;
                scan.probe_len = probe_len;
            }
            probe_len++;
        }
    }

    if (!scan.found) { scan.probe_len = probe_len; }
    return scan;
}

static ht_result p_sep_bucket_insert_absent_no_resize(
    p_sep_table *table,
    p_sep_bucket *bucket,
    size_t bucket_index,
    size_t stripe_shift,
    ht_key_t key,
    ht_val_t value,
    const p_sep_bucket_scan *scan,
    size_t *bucket_entries_out,
    size_t *segment_depth_out
) {
    p_sep_bucket_scan local_scan;
    p_sep_segment *target_segment;
    size_t entry_count;
    size_t depth;

    if (table == NULL || bucket == NULL) { return HT_ERR_INVALID; }

    if (scan == NULL) {
        local_scan = p_sep_bucket_scan_locked(bucket, key);
        scan = &local_scan;
    }

    if (scan->found) { return HT_ERR_EXISTS; }

    target_segment = scan->first_free_segment;
    entry_count = scan->bucket_entries;
    depth = scan->segment_depth;

    if (target_segment == NULL) {
        p_sep_stripe *stripe = &table->stripes[p_sep_stripe_for_bucket_shift(
            table,
            bucket_index,
            stripe_shift
        )];

        target_segment = p_sep_segment_alloc(stripe);
        if (target_segment == NULL) { return HT_ERR_OOM; }

        target_segment->next = bucket->head;
        bucket->head = target_segment;
        depth++;
    }

    target_segment->keys[target_segment->used] = key;
    target_segment->values[target_segment->used] = value;
    target_segment->used++;
    entry_count++;

    if (bucket_entries_out != NULL) { *bucket_entries_out = entry_count; }
    if (segment_depth_out != NULL) { *segment_depth_out = depth; }
    return HT_OK;
}

static ht_result p_sep_bucket_insert_migrated_locked(
    p_sep_table *table,
    p_sep_bucket *buckets,
    size_t mask,
    size_t stripe_shift,
    ht_key_t key,
    ht_val_t value
) {
    p_sep_bucket *bucket;
    size_t bucket_index;

    if (table == NULL || buckets == NULL) { return HT_ERR_INVALID; }

    bucket_index = (size_t)table->hash_fn(key, table->hash_seed) & mask;
    bucket = &buckets[bucket_index];
    return p_sep_bucket_insert_absent_no_resize(
        table,
        bucket,
        bucket_index,
        stripe_shift,
        key,
        value,
        NULL,
        NULL,
        NULL
    );
}

static ht_result p_sep_bucket_get_locked(
    const p_sep_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    ht_val_t *value_out,
    uint64_t *probe_len_out
) {
    const p_sep_segment *segment;
    uint64_t probe_len = 1;

    if (buckets == NULL || value_out == NULL) { return HT_ERR_INVALID; }

    for (segment = buckets[bucket_index].head;
         segment != NULL;
         segment = segment->next) {
        uint8_t entry_index;

        for (entry_index = 0; entry_index < segment->used; entry_index++) {
            if (segment->keys[entry_index] == key) {
                *value_out = segment->values[entry_index];
                if (probe_len_out != NULL) { *probe_len_out = probe_len; }
                return HT_OK;
            }
            probe_len++;
        }
    }

    if (probe_len_out != NULL) { *probe_len_out = probe_len; }
    return HT_ERR_NOT_FOUND;
}

static ht_result p_sep_get_frozen_read_only(
    const void *ctx,
    ht_key_t key,
    ht_val_t *value_out
) {
    const p_sep_table *table = ctx;
    p_sep_stripe *stripe;
    uint64_t hash;
    uint64_t probe_len = 1;
    size_t bucket_index;
    ht_result rc;

    if (table == NULL || value_out == NULL) { return HT_ERR_INVALID; }

    hash = table->hash_fn(key, table->hash_seed);
    stripe = &((p_sep_table *)table)->stripes[p_sep_stripe_for_hash(table, hash)];
    /* Benchmarks use this only after setup; no writer can free or move
     * segments while the frozen read-only phase is running. */
    if (table->collect_op_stats) {
        atomic_fetch_add_explicit(&stripe->lookups, 1, memory_order_relaxed);
    }
    bucket_index = (size_t)hash & table->mask;
    rc = p_sep_bucket_get_locked(
        table->buckets,
        bucket_index,
        key,
        value_out,
        &probe_len
    );
    p_sep_note_probe((p_sep_table *)table, stripe, probe_len);
    if (rc != HT_OK && table->collect_op_stats) {
        atomic_fetch_add_explicit(
            &stripe->lookup_misses,
            1,
            memory_order_relaxed
        );
    }

    return (rc == HT_OK) ? HT_OK : HT_ERR_NOT_FOUND;
}

static ht_result p_sep_bucket_remove_locked(
    p_sep_table *table,
    p_sep_bucket *buckets,
    size_t bucket_index,
    ht_key_t key,
    uint64_t *probe_len_out
) {
    p_sep_segment *segment;
    p_sep_segment *prev_segment = NULL;
    uint64_t probe_len = 1;

    if (table == NULL || buckets == NULL) { return HT_ERR_INVALID; }

    for (segment = buckets[bucket_index].head;
         segment != NULL;
         prev_segment = segment, segment = segment->next) {
        uint8_t entry_index;

        for (entry_index = 0; entry_index < segment->used; entry_index++) {
            if (segment->keys[entry_index] == key) {
                uint8_t last_index = (uint8_t)(segment->used - 1u);

                /* Segment order is not observable, so removal fills the gap
                 * with the last entry instead of shifting the compact array. */
                segment->keys[entry_index] = segment->keys[last_index];
                segment->values[entry_index] = segment->values[last_index];
                segment->used--;

                if (segment->used == 0) {
                    if (prev_segment == NULL) {
                        buckets[bucket_index].head = segment->next;
                    } else {
                        prev_segment->next = segment->next;
                    }
                    p_sep_segment_free(
                        &table->stripes[p_sep_stripe_for_bucket(
                            table,
                            bucket_index
                        )],
                        segment
                    );
                }

                if (probe_len_out != NULL) { *probe_len_out = probe_len; }
                return HT_OK;
            }
            probe_len++;
        }
    }

    if (probe_len_out != NULL) { *probe_len_out = probe_len; }
    return HT_ERR_NOT_FOUND;
}

static void p_sep_note_probe(
    p_sep_table *table,
    p_sep_stripe *stripe,
    uint64_t probe_len
) {
    uint64_t old;

    if (table == NULL || stripe == NULL || !table->collect_op_stats) { return; }

    atomic_fetch_add_explicit(&stripe->probes, probe_len, memory_order_relaxed);
    old = atomic_load_explicit(&stripe->max_probe_len, memory_order_relaxed);
    while (old < probe_len &&
           !atomic_compare_exchange_weak_explicit(
               &stripe->max_probe_len,
               &old,
               probe_len,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
        ;
    }
}

static void p_sep_record_insert_failure(
    p_sep_table *table,
    p_sep_stripe *stripe
) {
    if (table == NULL || stripe == NULL || !table->collect_op_stats) { return; }

    stripe->insert_failures++;
}

static ht_result p_sep_maybe_resize(
    p_sep_table *table,
    int force_grow
) {
    size_t entry_count;
    size_t delete_debt;
    size_t capacity;
    size_t grow_limit;
    size_t shrink_limit;
    size_t target = 0;
    ht_result rc = HT_OK;

    if (table == NULL || table->resize_mode == HT_RESIZE_NONE) { return HT_OK; }

    entry_count = atomic_load_explicit(
        &table->live_size.value,
        memory_order_relaxed
    );
    grow_limit = atomic_load_explicit(
        &table->grow_entry_limit,
        memory_order_acquire
    );
    /* The cheap atomic check avoids table-wide locking unless a resize is
     * plausibly needed. The locked section below rechecks exact counts. */
    if (!force_grow && entry_count <= grow_limit) {
        if (table->resize_mode != HT_RESIZE_GROW_SHRINK) {
            return HT_OK;
        }

        delete_debt = atomic_load_explicit(
            &table->delete_debt.value,
            memory_order_relaxed
        );
        shrink_limit = atomic_load_explicit(
            &table->shrink_entry_limit,
            memory_order_acquire
        );
        if (entry_count >= shrink_limit || delete_debt <= entry_count / 2u) {
            return HT_OK;
        }
    }

    pthread_mutex_lock(&table->resize_lock);
    /* Once all stripes are write-locked, no operation can hold a pointer into
     * the old bucket chains while resize migrates and frees segments. */
    p_sep_write_lock_all_stripes(table);

    entry_count = p_sep_size_locked(table);
    capacity = table->capacity;
    if ((force_grow ||
         (double)entry_count > (double)capacity * table->max_load_factor) &&
        capacity < table->max_capacity) {
        target = (capacity <= table->max_capacity / 2u)
            ? capacity * 2u
            : table->max_capacity;
    } else if (table->resize_mode == HT_RESIZE_GROW_SHRINK &&
               capacity > table->min_capacity &&
               p_sep_deletes_since_resize_locked(table) > entry_count / 2u &&
               (double)entry_count <
                   (double)capacity * table->min_load_factor) {
        target = capacity / 2u;
        while (target > table->min_capacity &&
               (double)entry_count <
                   (double)target * table->min_load_factor) {
            target /= 2u;
        }
        if (target < table->min_capacity) { target = table->min_capacity; }
    }

    if (target != 0 && target != capacity) {
        rc = p_sep_resize_locked(table, target);
    }

    p_sep_unlock_all_stripes(table);
    pthread_mutex_unlock(&table->resize_lock);
    return rc;
}

/**
 * @brief Rebuild the bucket array while resize lock and stripes are held.
 */
static ht_result p_sep_resize_locked(
    p_sep_table *table,
    size_t requested_capacity
) {
    p_sep_bucket *new_buckets;
    p_sep_bucket *old_buckets;
    size_t old_capacity;
    size_t old_entry_count;
    size_t old_stripe_shift;
    size_t new_capacity;
    size_t new_buckets_per_stripe;
    size_t new_stripe_shift;
    size_t bucket_index;
    uint64_t resize_start_ns;
    ht_result rc = HT_OK;

    if (table == NULL) { return HT_ERR_INVALID; }

    if (requested_capacity == 0 || requested_capacity > table->max_capacity) {
        return HT_ERR_OOM;
    }

    new_capacity = requested_capacity;
    if (new_capacity < table->min_capacity) {
        new_capacity = table->min_capacity;
    }
    rc = ht_checked_next_pow2(new_capacity, &new_capacity);
    if (rc != HT_OK) { return rc; }
    if (new_capacity > table->max_capacity) { return HT_ERR_OOM; }
    if (new_capacity == table->capacity) {
        p_sep_reset_deletes_since_resize_locked(table);
        p_sep_update_resize_limits(table);
        return HT_OK;
    }

    new_buckets = p_sep_bucket_array_alloc(new_capacity);
    if (new_buckets == NULL) { return HT_ERR_OOM; }

    old_buckets = table->buckets;
    old_capacity = table->capacity;
    old_stripe_shift = table->buckets_per_stripe_shift;
    old_entry_count = p_sep_size_locked(table);
    resize_start_ns = ht_resize_instrumentation_start(table->collect_stats);
    new_buckets_per_stripe = new_capacity / table->stripe_count;
    if (new_buckets_per_stripe == 0) { new_buckets_per_stripe = 1; }
    new_stripe_shift = ht_pow2_shift(new_buckets_per_stripe);

    for (bucket_index = 0; bucket_index < old_capacity; bucket_index++) {
        p_sep_segment *segment;

        for (segment = old_buckets[bucket_index].head;
             segment != NULL;
             segment = segment->next) {
            uint8_t entry_index;

            for (entry_index = 0; entry_index < segment->used; entry_index++) {
                rc = p_sep_bucket_insert_migrated_locked(
                    table,
                    new_buckets,
                    new_capacity - 1u,
                    new_stripe_shift,
                    segment->keys[entry_index],
                    segment->values[entry_index]
                );
                if (rc != HT_OK) {
                    goto fail_new_buckets;
                }
            }
        }
    }

    table->buckets = new_buckets;
    table->capacity = new_capacity;
    table->mask = new_capacity - 1u;
    p_sep_update_stripe_mapping(table);
    p_sep_reset_deletes_since_resize_locked(table);
    p_sep_update_resize_limits(table);

    p_sep_bucket_array_free_segments_with_shift(
        table,
        old_buckets,
        old_capacity,
        old_stripe_shift
    );
    free(old_buckets);

    ht_resize_stats_record(
        &table->resize_stats,
        table->collect_stats,
        old_capacity,
        table->capacity,
        old_entry_count,
        resize_start_ns
    );
    return HT_OK;

fail_new_buckets:
    p_sep_bucket_array_free_segments_with_shift(
        table,
        new_buckets,
        new_capacity,
        new_stripe_shift
    );
    free(new_buckets);
    return rc;
}
