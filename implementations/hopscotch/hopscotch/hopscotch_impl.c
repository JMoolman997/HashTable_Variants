/**
 * @file    hopscotch_impl.c
 * @brief   Hopscotch hashtable backend.
 *
 * Implements a single-threaded hopscotch hashtable backend for the generic
 * hashtable wrapper.
 *
 * This source file is for the core wrapper and hopscotch support code. It is
 * not part of the public user-facing API.
 *
 * @author  J.W. Moolman
 * @date    2026-04-16
 */

#include <stdlib.h>
#include <string.h>

#include "backend_config.h"
#include "capacity_util.h"
#include "hash_util.h"
#include "memory_util.h"
#include "resize_stats.h"
#include "stats_util.h"
#include "hopscotch_impl.h"
#include "ht_internal.h"

#ifndef HOPSCOTCH_ENABLE_INVARIANT_CHECKS
#define HOPSCOTCH_ENABLE_INVARIANT_CHECKS 0
#endif

#if HOPSCOTCH_ENABLE_INVARIANT_CHECKS
#define HOPSCOTCH_CHECK_VALID(t)                                              \
    do {                                                                      \
        if (!hopscotch_validate((t))) {                                       \
            abort();                                                          \
        }                                                                     \
    } while (0)
#else
#define HOPSCOTCH_CHECK_VALID(t) ((void)0)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define HOPSCOTCH_LIKELY(x)   __builtin_expect(!!(x), 1)
#define HOPSCOTCH_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define HOPSCOTCH_INLINE      static inline __attribute__((always_inline))
#else
#define HOPSCOTCH_LIKELY(x)   (x)
#define HOPSCOTCH_UNLIKELY(x) (x)
#define HOPSCOTCH_INLINE      static inline
#endif

#define HOPSCOTCH_TAG(hash) ht_hash_tag_u8(hash)

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Release all memory owned by a hopscotch table backend.
 *
 * @param impl Hopscotch backend instance to destroy.
 */
static void hopscotch_destroy_impl(
    void *impl
);

static ht_result hopscotch_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
);

static ht_result hopscotch_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
);

static ht_result hopscotch_remove_impl(
    void *impl,
    ht_key_t key
);

/**
 * @brief Return the live entry count from a hopscotch backend instance.
 *
 * @param impl Hopscotch backend instance to query.
 *
 * @return The number of live entries, or `0` if `impl` is invalid.
 */
static size_t hopscotch_size_impl(
    const void *impl
);

/**
 * @brief Return the current slot capacity from a hopscotch backend instance.
 *
 * @param impl Hopscotch backend instance to query.
 *
 * @return The current slot capacity, or `0` if `impl` is invalid.
 */
static size_t hopscotch_capacity_impl(
    const void *impl
);

/**
 * @brief Return the current load factor from a hopscotch backend instance.
 *
 * @param impl Hopscotch backend instance to query.
 *
 * @return The load factor, or `0.0` if `impl` is invalid.
 */
static double hopscotch_load_factor_impl(
    const void *impl
);

static ht_result hopscotch_reserve_impl(
    void *impl,
    size_t capacity
);

static ht_result hopscotch_rehash_impl(
    void *impl,
    size_t capacity
);

/**
 * @brief Copy the statistics snapshot out of a hopscotch backend instance.
 *
 * @param impl Hopscotch backend instance to query.
 * @param out Output structure that receives the copied statistics.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
static ht_result hopscotch_get_stats_impl(
    const void *impl,
    ht_stats *out
);

/**
 * @brief Reset the statistics counters stored by a hopscotch backend instance.
 *
 * @param impl Hopscotch backend instance whose counters should be cleared.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if `impl` is invalid.
 */
static ht_result hopscotch_reset_stats_impl(
    void *impl
);

static ht_result hopscotch_resize(
    hopscotch_table *t,
    size_t new_capacity
);

static ht_result hopscotch_alloc_arrays(
    hopscotch_table *t,
    size_t capacity
);

static void hopscotch_free_arrays(
    hopscotch_table *t
);

static void hopscotch_set_capacity_fields(
    hopscotch_table *t,
    size_t capacity
);

static void hopscotch_update_bytes_used(
    hopscotch_table *t
);

HOPSCOTCH_INLINE ht_result hopscotch_find_slot(
    const hopscotch_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *slot_out,
    uint64_t *probe_len_out
);

static ht_result hopscotch_find_overflow(
    const hopscotch_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *index_out,
    uint64_t *probe_len_out
);

static ht_result hopscotch_find_empty(
    const hopscotch_table *t,
    size_t home,
    size_t *empty_out,
    uint64_t *probe_len_out
);

static ht_result hopscotch_place_new(
    hopscotch_table *t,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
);

static ht_result hopscotch_overflow_insert(
    hopscotch_table *t,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value
);

static ht_result hopscotch_insert_rebuild(
    hopscotch_table *t,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value,
    int allow_overflow
);

static int hopscotch_move_empty_toward_home(
    hopscotch_table *t,
    size_t home,
    size_t *empty_inout
);

HOPSCOTCH_INLINE void hopscotch_move_payload(
    hopscotch_table *t,
    size_t dst,
    size_t src
);

HOPSCOTCH_INLINE void hopscotch_clear_payload(
    hopscotch_table *t,
    size_t idx
);

#if HOPSCOTCH_ENABLE_INVARIANT_CHECKS
static int hopscotch_validate(
    const hopscotch_table *t
);
#endif

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable HOPSCOTCH_VTABLE = {
    .destroy         = hopscotch_destroy_impl,
    .insert          = hopscotch_insert_impl,
    .get             = hopscotch_get_impl,
    .remove          = hopscotch_remove_impl,
    .size            = hopscotch_size_impl,
    .capacity        = hopscotch_capacity_impl,
    .load_factor     = hopscotch_load_factor_impl,
    .reserve         = hopscotch_reserve_impl,
    .rehash          = hopscotch_rehash_impl,
    .get_stats       = hopscotch_get_stats_impl,
    .reset_stats     = hopscotch_reset_stats_impl,
    .bind_bench_iface = hopscotch_bind_bench_iface
};

/* ------------------------------------------------------------------------- */
/* public entry points                                                       */
/* ------------------------------------------------------------------------- */

ht_result hopscotch_create_impl_ex(
    const ht_config *cfg,
    void           **out
) {

    hopscotch_table *t;
    ht_backend_config resolved;
    ht_result rc;

    if (out == NULL) {
        return HT_ERR_INVALID;
    }
    *out = NULL;

    if (cfg == NULL) {
        return HT_ERR_INVALID;
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

    if (hopscotch_alloc_arrays(t, resolved.capacity) != HT_OK) {
        free(t);
        return HT_ERR_OOM;
    }

    t->min_capacity = resolved.min_capacity;
    t->size         = 0;
    t->used         = 0;
    t->overflow     = NULL;
    t->overflow_size = 0;
    t->overflow_capacity = 0;
    t->max_load_factor = resolved.max_load_factor;
    t->min_load_factor = resolved.min_load_factor;
    t->resize_mode   = resolved.resize_mode;
    t->hash_fn       = resolved.hash_fn;
    t->hash_seed     = resolved.hash_seed;
    t->collect_stats = resolved.collect_stats;

    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    hopscotch_update_bytes_used(t);
    HOPSCOTCH_CHECK_VALID(t);
    *out = t;
    return HT_OK;
}

const struct ht_vtable *hopscotch_vtable(
    void
) {
    return &HOPSCOTCH_VTABLE;
}

int hopscotch_bind_bench_iface(
    void *ctx,
    bench_iface *out
) {
    if (ctx == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    out->ctx    = ctx;
    out->insert = hopscotch_insert_impl;
    out->get    = hopscotch_get_impl;
    out->remove = hopscotch_remove_impl;

    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* backend implementation                                                     */
/* ------------------------------------------------------------------------- */

static void hopscotch_destroy_impl(
    void *impl
) {
    hopscotch_table *t = impl;

    if (t == NULL) {
        return;
    }

    hopscotch_free_arrays(t);
    free(t->overflow);
    free(t);
}

static ht_result hopscotch_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
) {
    hopscotch_table *t = impl;
    uint64_t hash;
    uint64_t probe_len;
    size_t entry_index;
    size_t home;
    ht_result result;
    size_t next_capacity;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    HT_STATS_INC(t, inserts);

    hash = t->hash_fn(key, t->hash_seed);
    home = hash & t->mask;
    result = HT_ERR_NOT_FOUND;
    probe_len = 0;

    if (t->buckets[home].hop_info != 0) {
        result = hopscotch_find_slot(t, key, hash, &entry_index, &probe_len);
        HT_UPDATE_PROBE_STATS(t, probe_len);
    }

    if (HOPSCOTCH_UNLIKELY(result == HT_OK)) {
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_EXISTS;
    }

    if (HOPSCOTCH_UNLIKELY(result != HT_ERR_NOT_FOUND)) {
        HT_RECORD_INSERT_FAILURE(t);
        return result;
    }

    if (HOPSCOTCH_UNLIKELY(t->overflow_size != 0)) {
        result = hopscotch_find_overflow(
            t,
            key,
            hash,
            &entry_index,
            &probe_len
        );
        HT_UPDATE_PROBE_STATS(t, probe_len);

        if (HOPSCOTCH_UNLIKELY(result == HT_OK)) {
            HT_RECORD_INSERT_FAILURE(t);
            return HT_ERR_EXISTS;
        }

        if (HOPSCOTCH_UNLIKELY(result != HT_ERR_NOT_FOUND)) {
            HT_RECORD_INSERT_FAILURE(t);
            return result;
        }
    }

    if (HOPSCOTCH_UNLIKELY(
            t->resize_mode == HT_RESIZE_NONE &&
            HT_SHOULD_GROW_COUNT(t, size))) {
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_FULL;
    }

    if (HOPSCOTCH_UNLIKELY(
            t->resize_mode != HT_RESIZE_NONE &&
            HT_SHOULD_GROW_COUNT(t, size))) {
        if (HOPSCOTCH_UNLIKELY(t->capacity > SIZE_MAX / 2)) {
            HT_RECORD_INSERT_FAILURE(t);
            return HT_ERR_FULL;
        }

        result = hopscotch_resize(t, t->capacity * 2);
        if (HOPSCOTCH_UNLIKELY(result != HT_OK)) {
            HT_RECORD_INSERT_FAILURE(t);
            return result;
        }
    }

    for (;;) {
        result = hopscotch_place_new(t, hash, key, value, &probe_len);
        HT_UPDATE_PROBE_STATS(t, probe_len);

        if (HOPSCOTCH_LIKELY(result == HT_OK)) {
            HOPSCOTCH_CHECK_VALID(t);
            return HT_OK;
        }

        if (HOPSCOTCH_UNLIKELY(result != HT_ERR_NOT_FOUND)) {
            HT_RECORD_INSERT_FAILURE(t);
            return result;
        }

        if (HOPSCOTCH_UNLIKELY(
                t->resize_mode == HT_RESIZE_NONE ||
                t->capacity > SIZE_MAX / 2)) {
            if (t->resize_mode == HT_RESIZE_NONE) {
                result = hopscotch_overflow_insert(t, hash, key, value);
                if (result == HT_OK) {
                    HOPSCOTCH_CHECK_VALID(t);
                    return HT_OK;
                }
            }

            HT_RECORD_INSERT_FAILURE(t);
            return (result == HT_ERR_NOT_FOUND) ? HT_ERR_FULL : result;
        }

        next_capacity = t->capacity * 2;
        result = hopscotch_resize(t, next_capacity);
        if (HOPSCOTCH_UNLIKELY(result != HT_OK)) {
            HT_RECORD_INSERT_FAILURE(t);
            return result;
        }
    }
}

static ht_result hopscotch_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
) {
    const hopscotch_table *t = impl;
    size_t index;
    uint64_t hash;
    uint64_t probe_len;
    uint64_t overflow_probes;
    ht_result result;

    if (t == NULL || value_out == NULL) {
        return HT_ERR_INVALID;
    }

    if (t->collect_stats) {
        ((hopscotch_table *)t)->stats.lookups++;
    }

    hash = t->hash_fn(key, t->hash_seed);

    result = hopscotch_find_slot(t, key, hash, &index, &probe_len);

    if (HOPSCOTCH_LIKELY(result == HT_OK)) {
        HT_UPDATE_PROBE_STATS((hopscotch_table *)t, probe_len);
        *value_out = t->buckets[index].payload.value;
        return HT_OK;
    }

    if (HOPSCOTCH_UNLIKELY(result != HT_ERR_NOT_FOUND)) {
        HT_UPDATE_PROBE_STATS((hopscotch_table *)t, probe_len);
        return result;
    }

    if (HOPSCOTCH_UNLIKELY(t->overflow_size != 0)) {
        result = hopscotch_find_overflow(
            t,
            key,
            hash,
            &index,
            &overflow_probes
        );
        probe_len += overflow_probes;

        if (HOPSCOTCH_UNLIKELY(result == HT_OK)) {
            HT_UPDATE_PROBE_STATS((hopscotch_table *)t, probe_len);
            *value_out = t->overflow[index].value;
            return HT_OK;
        }

        if (HOPSCOTCH_UNLIKELY(result != HT_ERR_NOT_FOUND)) {
            HT_UPDATE_PROBE_STATS((hopscotch_table *)t, probe_len);
            return result;
        }
    }

    if (t->collect_stats) {
        ((hopscotch_table *)t)->stats.lookup_misses++;
        HT_UPDATE_PROBE_STATS((hopscotch_table *)t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
}

static ht_result hopscotch_remove_impl(
    void *impl,
    ht_key_t key
) {
    hopscotch_table *t = impl;
    uint64_t hash;
    uint64_t probe_len;
    uint64_t overflow_probes;
    size_t index;
    size_t home;
    size_t dist;
    ht_result result;
    int in_overflow = 0;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (t->collect_stats) {
        t->stats.removes++;
    }

    hash = t->hash_fn(key, t->hash_seed);

    result = hopscotch_find_slot(t, key, hash, &index, &probe_len);
    HT_UPDATE_PROBE_STATS(t, probe_len);

    if (HOPSCOTCH_UNLIKELY(
            result != HT_OK && result != HT_ERR_NOT_FOUND)) {
        return result;
    }

    if (HOPSCOTCH_UNLIKELY(
            result == HT_ERR_NOT_FOUND && t->overflow_size != 0)) {
        result = hopscotch_find_overflow(
            t,
            key,
            hash,
            &index,
            &overflow_probes
        );
        HT_UPDATE_PROBE_STATS(t, overflow_probes);
        in_overflow = (result == HT_OK);
    }

    if (HOPSCOTCH_UNLIKELY(result != HT_OK)) {
        if (t->collect_stats) {
            t->stats.remove_misses++;
        }
        return HT_ERR_NOT_FOUND;
    }

    if (in_overflow) {
        t->overflow[index] = t->overflow[t->overflow_size - 1u];
        t->overflow_size--;
    } else {
        home = hash & t->mask;
        dist = (index - home) & t->mask;

#if HOPSCOTCH_ENABLE_INVARIANT_CHECKS
        if ((t->buckets[home].hop_info & (1ULL << dist)) == 0) {
            if (t->collect_stats) {
                t->stats.remove_misses++;
            }
            return HT_ERR_INVALID;
        }
#endif

        t->buckets[home].hop_info &= ~(1ULL << dist);
        hopscotch_clear_payload(t, index);
    }

    t->size--;
    t->used = t->size;

    if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
        HT_SHOULD_SHRINK_COUNT(t, size)) {
        result = hopscotch_resize(t, t->capacity / 2);
        if (result != HT_OK) {
            return result;
        }
    }

    HOPSCOTCH_CHECK_VALID(t);
    return HT_OK;
}

static size_t hopscotch_size_impl(
    const void *impl
) {
    const hopscotch_table *t = impl;

    return (t != NULL) ? t->size : 0;
}

static size_t hopscotch_capacity_impl(
    const void *impl
) {
    const hopscotch_table *t = impl;

    return (t != NULL) ? t->capacity : 0;
}

static double hopscotch_load_factor_impl(
    const void *impl
) {
    const hopscotch_table *t = impl;

    return (t != NULL)
        ? ht_load_factor_snapshot(t->size, t->capacity)
        : 0.0;
}

static ht_result hopscotch_reserve_impl(
    void *impl,
    size_t capacity
) {
    hopscotch_table *t = impl;
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

    return hopscotch_resize(t, target);
}

static ht_result hopscotch_rehash_impl(
    void *impl,
    size_t capacity
) {
    hopscotch_table *t = impl;
    size_t target;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    rc = ht_rehash_target(t->size, t->min_capacity, capacity, &target);
    if (rc != HT_OK) {
        return rc;
    }

    return hopscotch_resize(t, target);
}

static ht_result hopscotch_get_stats_impl(
    const void *impl,
    ht_stats *out
) {
    const hopscotch_table *t = impl;

    if (t == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    *out = t->stats;
    return HT_OK;
}

static ht_result hopscotch_reset_stats_impl(
    void *impl
) {
    hopscotch_table *t = impl;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    memset(&t->stats, 0, sizeof(t->stats));
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    hopscotch_update_bytes_used(t);
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void hopscotch_update_bytes_used(
    hopscotch_table *t
) {
    size_t bytes;

    if (t == NULL || !t->collect_stats) {
        return;
    }

    bytes = sizeof(*t);
    bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->buckets));
    bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->state));
    bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->tag));
    bytes = ht_bytes_add_array_or_max(
        bytes,
        t->overflow_capacity,
        sizeof(*t->overflow)
    );
    t->stats.bytes_used = bytes;
}

static void hopscotch_set_capacity_fields(
    hopscotch_table *t,
    size_t capacity
) {
    t->capacity = capacity;
    t->mask = capacity - 1u;
    t->hop_range = (capacity < HOPSCOTCH_HOP_RANGE)
        ? capacity
        : HOPSCOTCH_HOP_RANGE;
    t->resize_search_limit = (capacity < HOPSCOTCH_ADD_RANGE)
        ? capacity
        : HOPSCOTCH_ADD_RANGE;
}

static ht_result hopscotch_alloc_arrays(
    hopscotch_table *t,
    size_t capacity
) {
    if (t == NULL || capacity == 0) {
        return HT_ERR_INVALID;
    }

    if (capacity > SIZE_MAX / sizeof(*t->buckets) ||
        capacity > SIZE_MAX / sizeof(*t->state) ||
        capacity > SIZE_MAX / sizeof(*t->tag)) {
        return HT_ERR_OOM;
    }

    t->buckets = calloc(capacity, sizeof(*t->buckets));
    t->state = calloc(capacity, sizeof(*t->state));
    t->tag = malloc(capacity * sizeof(*t->tag));

    if (HOPSCOTCH_UNLIKELY(
            t->buckets == NULL || t->state == NULL || t->tag == NULL)) {
        hopscotch_free_arrays(t);
        return HT_ERR_OOM;
    }

    hopscotch_set_capacity_fields(t, capacity);
    return HT_OK;
}

static void hopscotch_free_arrays(
    hopscotch_table *t
) {
    if (t == NULL) {
        return;
    }

    free(t->buckets);
    free(t->state);
    free(t->tag);

    t->buckets = NULL;
    t->state = NULL;
    t->tag = NULL;
}

static ht_result hopscotch_resize(
    hopscotch_table *t,
    size_t new_capacity
) {
    hopscotch_overflow_entry *old_overflow;
    uint8_t  *old_state;
    uint8_t  *old_tag;
    hopscotch_bucket *old_buckets;
    size_t old_capacity;
    size_t old_size;
    size_t old_overflow_size;
    size_t i;
    uint64_t start_ns;
    ht_result result;
    hopscotch_table tmp;
    int allow_overflow;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (new_capacity < t->min_capacity) {
        new_capacity = t->min_capacity;
    }

    result = ht_checked_next_pow2(new_capacity, &new_capacity);
    if (result != HT_OK) {
        return result;
    }

    while ((double)t->size > (double)new_capacity * t->max_load_factor) {
        if (new_capacity > SIZE_MAX / 2) {
            return HT_ERR_INVALID;
        }
        new_capacity *= 2;
    }

    if (new_capacity == t->capacity && t->overflow_size == 0) {
        return HT_OK;
    }

    old_capacity = t->capacity;
    old_size = t->size;
    old_state = t->state;
    old_tag = t->tag;
    old_buckets = t->buckets;
    old_overflow = t->overflow;
    old_overflow_size = t->overflow_size;
    start_ns = ht_resize_instrumentation_start(t->collect_stats);
    allow_overflow = (t->resize_mode == HT_RESIZE_NONE);

    for (;;) {
        tmp = *t;
        tmp.buckets = NULL;
        tmp.state = NULL;
        tmp.tag = NULL;
        tmp.overflow = NULL;
        tmp.size = 0;
        tmp.used = 0;
        tmp.overflow_size = 0;
        tmp.overflow_capacity = 0;
        tmp.collect_stats = 0;

        result = hopscotch_alloc_arrays(&tmp, new_capacity);
        if (HOPSCOTCH_UNLIKELY(result != HT_OK)) {
            return result;
        }

        result = HT_OK;
        for (i = 0; i < old_capacity; i++) {
            if (old_state[i] != HOPSCOTCH_SLOT_FULL) {
                continue;
            }

            result = hopscotch_insert_rebuild(
                &tmp,
                old_buckets[i].payload.hash,
                old_buckets[i].payload.key,
                old_buckets[i].payload.value,
                allow_overflow
            );

            if (HOPSCOTCH_UNLIKELY(result != HT_OK)) {
                break;
            }
        }

        for (i = 0; result == HT_OK && i < old_overflow_size; i++) {
            result = hopscotch_insert_rebuild(
                &tmp,
                old_overflow[i].hash,
                old_overflow[i].key,
                old_overflow[i].value,
                allow_overflow
            );
        }

        if (result == HT_OK) {
            break;
        }

        free(tmp.overflow);
        hopscotch_free_arrays(&tmp);

        if (result != HT_ERR_NOT_FOUND ||
            allow_overflow ||
            new_capacity > SIZE_MAX / 2) {
            return result;
        }

        new_capacity *= 2;
        while ((double)old_size > (double)new_capacity * t->max_load_factor) {
            if (new_capacity > SIZE_MAX / 2) {
                return HT_ERR_INVALID;
            }
            new_capacity *= 2;
        }
    }

#if HOPSCOTCH_ENABLE_INVARIANT_CHECKS
    if (!hopscotch_validate(&tmp)) {
        free(tmp.overflow);
        hopscotch_free_arrays(&tmp);
        return HT_ERR_INVALID;
    }
#endif

    t->buckets = tmp.buckets;
    t->state = tmp.state;
    t->tag = tmp.tag;
    t->overflow = tmp.overflow;
    t->capacity = new_capacity;
    t->mask = tmp.mask;
    t->hop_range = tmp.hop_range;
    t->resize_search_limit = tmp.resize_search_limit;
    t->size = tmp.size;
    t->used = tmp.used;
    t->overflow_size = tmp.overflow_size;
    t->overflow_capacity = tmp.overflow_capacity;

    ht_resize_stats_record(
        &t->stats,
        t->collect_stats,
        old_capacity,
        new_capacity,
        old_size,
        start_ns
    );

    hopscotch_update_bytes_used(t);
    HOPSCOTCH_CHECK_VALID(t);

    free(old_buckets);
    free(old_state);
    free(old_tag);
    free(old_overflow);
    return HT_OK;
}

HOPSCOTCH_INLINE ht_result hopscotch_find_slot(
    const hopscotch_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *slot_out,
    uint64_t *probe_len_out
) {
    size_t home;
    size_t idx;
    size_t d;
    uint64_t hop_info;
    uint64_t probes;
    uint8_t want_tag;

    if (t == NULL || slot_out == NULL || probe_len_out == NULL) {
        return HT_ERR_INVALID;
    }

    home = hash & t->mask;
    hop_info = t->buckets[home].hop_info;

    if (hop_info == 0) {
        *probe_len_out = 0;
        return HT_ERR_NOT_FOUND;
    }

    if (HOPSCOTCH_LIKELY((hop_info & (hop_info - 1u)) == 0)) {
        d = (size_t)__builtin_ctzll(hop_info);
        idx = (home + d) & t->mask;

#if HOPSCOTCH_ENABLE_INVARIANT_CHECKS
        if (t->state[idx] != HOPSCOTCH_SLOT_FULL ||
            (t->buckets[idx].payload.hash & t->mask) != home) {
            *probe_len_out = 1u;
            return HT_ERR_INVALID;
        }
#endif

        if (t->buckets[idx].payload.key == key) {
            *slot_out = idx;
            *probe_len_out = 1u;
            return HT_OK;
        }

        *probe_len_out = 1u;
        return HT_ERR_NOT_FOUND;
    }

    want_tag = HOPSCOTCH_TAG(hash);
    probes = 0;
    while (hop_info != 0) {
        /* Each set hop bit names one candidate slot owned by this home. */
        d = (size_t)__builtin_ctzll(hop_info);
        idx = (home + d) & t->mask;
        probes++;

#if HOPSCOTCH_ENABLE_INVARIANT_CHECKS
        if (t->state[idx] != HOPSCOTCH_SLOT_FULL ||
            (t->buckets[idx].payload.hash & t->mask) != home) {
            *probe_len_out = probes;
            return HT_ERR_INVALID;
        }
#endif

        /* Tag filtering rejects non-matches before loading payload keys. */
        if (t->tag[idx] == want_tag && t->buckets[idx].payload.key == key) {
            *slot_out = idx;
            *probe_len_out = probes;
            return HT_OK;
        }

        /* Clear the least-significant (processed) set bit. */
        hop_info &= hop_info - 1;
    }

    *probe_len_out = probes;
    return HT_ERR_NOT_FOUND;
}

static ht_result hopscotch_find_overflow(
    const hopscotch_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *index_out,
    uint64_t *probe_len_out
) {
    size_t i;

    if (t == NULL || index_out == NULL || probe_len_out == NULL) {
        return HT_ERR_INVALID;
    }

    *probe_len_out = 0;
    if (t->overflow_size == 0) {
        return HT_ERR_NOT_FOUND;
    }

    for (i = 0; i < t->overflow_size; i++) {
        (*probe_len_out)++;
        if (t->overflow[i].hash == hash && t->overflow[i].key == key) {
            *index_out = i;
            return HT_OK;
        }
    }

    return HT_ERR_NOT_FOUND;
}

static ht_result hopscotch_find_empty(
    const hopscotch_table *t,
    size_t home,
    size_t *empty_out,
    uint64_t *probe_len_out
) {
    size_t limit;
    size_t first_len;
    size_t remaining;
    uint8_t *found;

    if (t == NULL || empty_out == NULL || probe_len_out == NULL) {
        return HT_ERR_INVALID;
    }

    /*
     * Fixed-capacity tables cannot recover by growing, so search the whole
     * table before reporting a true placement failure. Resize-enabled tables
     * use a bounded search so dense local clusters trigger a grow-and-retry.
     */
    limit = (t->resize_mode == HT_RESIZE_NONE)
        ? t->capacity
        : t->resize_search_limit;

    if (HOPSCOTCH_LIKELY(t->state[home] == HOPSCOTCH_SLOT_EMPTY)) {
        *empty_out = home;
        *probe_len_out = 1u;
        return HT_OK;
    }

    first_len = limit;
    if (first_len > t->capacity - home) {
        first_len = t->capacity - home;
    }

    /*
     * `state` is a byte array, so libc can scan the insertion window faster
     * than a slot-by-slot loop. A wrapped window is two contiguous scans.
     */
    found = memchr(&t->state[home], HOPSCOTCH_SLOT_EMPTY, first_len);
    if (found != NULL) {
        *empty_out = (size_t)(found - t->state);
        *probe_len_out = (uint64_t)(*empty_out - home + 1u);
        return HT_OK;
    }

    remaining = limit - first_len;
    if (remaining != 0) {
        found = memchr(t->state, HOPSCOTCH_SLOT_EMPTY, remaining);
        if (found != NULL) {
            *empty_out = (size_t)(found - t->state);
            *probe_len_out = (uint64_t)(first_len + *empty_out + 1u);
            return HT_OK;
        }
    }

    *probe_len_out = (uint64_t)limit;
    return HT_ERR_NOT_FOUND;
}

static ht_result hopscotch_place_new(
    hopscotch_table *t,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_len_out
) {
    size_t home;
    size_t empty_idx;
    size_t dist;
    uint64_t probes;
    ht_result result;

    if (t == NULL || probe_len_out == NULL) {
        return HT_ERR_INVALID;
    }

    home = hash & t->mask;

    /*
     * Find any empty physical slot in the insertion search window.
     * This slot may still be outside the legal hopscotch neighborhood.
     */
    result = hopscotch_find_empty(t, home, &empty_idx, &probes);
    if (result != HT_OK) {
        *probe_len_out = probes;
        return result;
    }

    dist = (empty_idx - home) & t->mask;

    /*
     * If the empty slot is too far away, try to move it backward by
     * relocating existing entries forward.
     */
    if (dist >= t->hop_range) {
        if (!hopscotch_move_empty_toward_home(t, home, &empty_idx)) {
            *probe_len_out = probes;
            return HT_ERR_NOT_FOUND;
        }

        dist = (empty_idx - home) & t->mask;
    }
    /* Relocation should have pulled the empty slot into the legal window. */
    if (HOPSCOTCH_UNLIKELY(dist >= t->hop_range)) {
        *probe_len_out = probes;
        return HT_ERR_NOT_FOUND;
    }

    t->buckets[empty_idx].payload.hash = hash;
    t->buckets[empty_idx].payload.key = key;
    t->buckets[empty_idx].payload.value = value;
    t->tag[empty_idx] = HOPSCOTCH_TAG(hash);
    t->state[empty_idx] = HOPSCOTCH_SLOT_FULL;

    /*
     * Record that home owns an entry at offset dist.
     * Lookup will later use this bit to find the entry.
     */
    t->buckets[home].hop_info |= 1ULL << dist;

    t->size++;
    t->used = t->size;

    *probe_len_out = probes;
    return HT_OK;
}

static ht_result hopscotch_overflow_insert(
    hopscotch_table *t,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value
) {
    hopscotch_overflow_entry *new_overflow;
    size_t new_capacity;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (t->overflow_size == t->overflow_capacity) {
        new_capacity = (t->overflow_capacity == 0)
            ? 8u : t->overflow_capacity * 2u
        ;

        if (new_capacity < t->overflow_capacity ||
            new_capacity > SIZE_MAX / sizeof(*new_overflow)) {
            return HT_ERR_FULL;
        }

        new_overflow = realloc(
            t->overflow,
            new_capacity * sizeof(*new_overflow)
        );
        if (new_overflow == NULL) {
            return HT_ERR_OOM;
        }

        t->overflow = new_overflow;
        t->overflow_capacity = new_capacity;
        hopscotch_update_bytes_used(t);
    }

    t->overflow[t->overflow_size].hash = hash;
    t->overflow[t->overflow_size].key = key;
    t->overflow[t->overflow_size].value = value;
    t->overflow_size++;
    t->size++;
    t->used = t->size;

    return HT_OK;
}

static ht_result hopscotch_insert_rebuild(
    hopscotch_table *t,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value,
    int allow_overflow
) {
    uint64_t probe_len;
    ht_result result;

    result = hopscotch_place_new(t, hash, key, value, &probe_len);
    if (result == HT_OK || result != HT_ERR_NOT_FOUND || !allow_overflow) {
        return result;
    }

    return hopscotch_overflow_insert(t, hash, key, value);
}

static int hopscotch_move_empty_toward_home(
    hopscotch_table *t,
    size_t home,
    size_t *empty_inout
) {
    size_t empty_idx;
    size_t empty_dist;
    size_t offset;
    size_t candidate_home;
    size_t d;
    size_t src;
    size_t hop_range;
    uint64_t hop_info;
    uint64_t movable_bits;
    uint64_t old_bit;
    uint64_t new_bit;

    if (t == NULL || empty_inout == NULL) {
        return 0;
    }

    empty_idx = *empty_inout;
    empty_dist = (empty_idx - home) & t->mask;
    hop_range = t->hop_range;

    if (hop_range <= 1u) {
        return 0;
    }

    while (empty_dist >= hop_range) {
        for (offset = hop_range - 1; offset > 0; offset--) {
            candidate_home = (empty_idx - offset) & t->mask;
            hop_info = t->buckets[candidate_home].hop_info;

            /*
             * Only entries with displacement d < offset can move into
             * empty_idx. Bits at offset or above are at/after empty_idx
             * relative to candidate_home, so they are not valid sources.
             */
            movable_bits = hop_info & ((1ULL << offset) - 1);

            if (movable_bits != 0) {
                /*
                 * Move the closest owned entry forward into the empty slot;
                 * this keeps that entry legal while pulling the hole homeward.
                 */
                d = (size_t)__builtin_ctzll(movable_bits);
                old_bit = 1ULL << d;

                src = (candidate_home + d) & t->mask;
                new_bit = 1ULL << offset;

#if HOPSCOTCH_ENABLE_INVARIANT_CHECKS
                if ((hop_info & new_bit) != 0 ||
                    t->state[src] != HOPSCOTCH_SLOT_FULL ||
                    (t->buckets[src].payload.hash & t->mask) !=
                        candidate_home) {
                    return 0;
                }
#endif

                hopscotch_move_payload(t, empty_idx, src);
                hopscotch_clear_payload(t, src);

                /*
                 * The moved entry still belongs to candidate_home, but its
                 * displacement changed from d to offset.
                 */
                t->buckets[candidate_home].hop_info &= ~old_bit;
                t->buckets[candidate_home].hop_info |= new_bit;

                /*
                 * The source slot is now empty. Continue moving this new
                 * empty slot toward the original insertion home.
                 */
                *empty_inout = src;
                empty_idx = src;
                empty_dist = (empty_idx - home) & t->mask;
                /*
                 * Updated empty_idx, so the current candidate search is stale.
                 * Restart from the outer loop with the new empty slot.
                 */
                goto moved_one_step;
            }
        }

        return 0;

moved_one_step:
        /* Recheck while (empty_dist >= HOPSCOTCH_HOP_RANGE) */;
    }

    return 1;
}

HOPSCOTCH_INLINE void hopscotch_move_payload(
    hopscotch_table *t,
    size_t dst,
    size_t src
) {
    t->buckets[dst].payload = t->buckets[src].payload;
    t->tag[dst] = t->tag[src];
    t->state[dst] = t->state[src];
}

HOPSCOTCH_INLINE void hopscotch_clear_payload(
    hopscotch_table *t,
    size_t idx
) {
    t->state[idx] = HOPSCOTCH_SLOT_EMPTY;
}

#if HOPSCOTCH_ENABLE_INVARIANT_CHECKS
static int hopscotch_validate(
    const hopscotch_table *t
) {
    size_t i;
    size_t j;
    size_t idx;
    size_t home;
    size_t dist;
    size_t hop_range;
    size_t slot_count;
    size_t hop_bit_count;
    uint64_t hop_info;

    if (t == NULL || t->buckets == NULL || t->state == NULL ||
        t->tag == NULL || t->capacity == 0 ||
        (t->capacity & (t->capacity - 1u)) != 0 ||
        t->mask != t->capacity - 1u ||
        t->min_capacity == 0 || t->min_capacity > t->capacity ||
        t->overflow_size > t->overflow_capacity ||
        (t->overflow_capacity == 0 && t->overflow != NULL) ||
        (t->overflow_capacity > 0 && t->overflow == NULL)) {
        return 0;
    }

    hop_range = t->hop_range;
    if (hop_range == 0 || hop_range > 64u) {
        return 0;
    }

    slot_count = 0;
    hop_bit_count = 0;

    for (i = 0; i < t->capacity; i++) {
        if (t->state[i] != HOPSCOTCH_SLOT_EMPTY &&
            t->state[i] != HOPSCOTCH_SLOT_FULL) {
            return 0;
        }

        if (hop_range < 64u && (t->buckets[i].hop_info >> hop_range) != 0) {
            return 0;
        }

        if (t->state[i] == HOPSCOTCH_SLOT_FULL) {
            if (t->tag[i] != HOPSCOTCH_TAG(t->buckets[i].payload.hash)) {
                return 0;
            }

            home = t->buckets[i].payload.hash & t->mask;
            dist = (i - home) & t->mask;
            if (dist >= hop_range ||
                (t->buckets[home].hop_info & (1ULL << dist)) == 0) {
                return 0;
            }

            slot_count++;
        }

        hop_info = t->buckets[i].hop_info;
        while (hop_info != 0) {
            dist = (size_t)__builtin_ctzll(hop_info);
            idx = (i + dist) & t->mask;

            if (dist >= hop_range ||
                t->state[idx] != HOPSCOTCH_SLOT_FULL ||
                (t->buckets[idx].payload.hash & t->mask) != i) {
                return 0;
            }

            hop_bit_count++;
            hop_info &= hop_info - 1u;
        }
    }

    if (slot_count != hop_bit_count ||
        t->size != slot_count + t->overflow_size ||
        t->used != t->size) {
        return 0;
    }

    for (i = 0; i < t->overflow_size; i++) {
        if (hopscotch_find_slot(
                t,
                t->overflow[i].key,
                t->overflow[i].hash,
                &idx,
                &hop_info
            ) != HT_ERR_NOT_FOUND) {
            return 0;
        }

        for (j = i + 1u; j < t->overflow_size; j++) {
            if (t->overflow[i].hash == t->overflow[j].hash &&
                t->overflow[i].key == t->overflow[j].key) {
                return 0;
            }
        }
    }

    return 1;
}
#endif
