/**
 * @file    open_addressing_impl.c
 * @brief   Tombstone-based open-addressing hashtable backend.
 *
 * Implements the open-addressing backend used by the generic hashtable
 * wrapper. The table stores 64-bit integer keys and values, uses power-of-two
 * capacities, cached slot hashes, tombstone deletion, optional resizing, and
 * optional statistics collection for benchmarking.
 *
 * @author  J.W. Moolman
 * @date    2026-03-23
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend_util.h"
#include "open_addressing_impl.h"
#include "ht_internal.h"

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Release all memory owned by a open-addressing table backend.
 *
 * @param impl Open-addressing backend instance to destroy.
 */
static void open_addressing_destroy_impl(
    void *impl);

/**
 * @brief Insert a key/value pair through the open-addressing backend implementation.
 *
 * @param impl Open-addressing backend instance to modify.
 * @param key Key to insert.
 * @param value Value to associate with `key`.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`,
 *         `HT_ERR_EXISTS`, `HT_ERR_FULL`, or `HT_ERR_OOM`.
 */
static ht_result open_addressing_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value);

/**
 * @brief Look up a key through the open-addressing backend implementation.
 *
 * @param impl Open-addressing backend instance to query.
 * @param key Key to search for.
 * @param value_out Output location that receives the value on success.
 *
 * @return `HT_OK` when found, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result open_addressing_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out);

/**
 * @brief Remove a key through the open-addressing backend implementation.
 *
 * @param impl Open-addressing backend instance to modify.
 * @param key Key to remove.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result open_addressing_remove_impl(
    void *impl,
    ht_key_t key);

/**
 * @brief Return the live entry count from a open-addressing backend instance.
 *
 * @param impl Open-addressing backend instance to query.
 *
 * @return The number of live entries, or `0` if `impl` is invalid.
 */
static size_t open_addressing_size_impl(
    const void *impl);

/**
 * @brief Return the current slot capacity from a open-addressing backend instance.
 *
 * @param impl Open-addressing backend instance to query.
 *
 * @return The current slot capacity, or `0` if `impl` is invalid.
 */
static size_t open_addressing_capacity_impl(
    const void *impl);

/**
 * @brief Return the current load factor from a open-addressing backend instance.
 *
 * @param impl Open-addressing backend instance to query.
 *
 * @return The load factor, or `0.0` if `impl` is invalid.
 */
static double open_addressing_load_factor_impl(
    const void *impl);

/**
 * @brief Ensure the open-addressing backend can hold at least the requested capacity.
 *
 * @param impl Open-addressing backend instance to resize if needed.
 * @param capacity Minimum target capacity.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result open_addressing_reserve_impl(
    void *impl,
    size_t capacity);

/**
 * @brief Rehash the open-addressing backend around a requested capacity.
 *
 * @param impl Open-addressing backend instance to rebuild.
 * @param capacity Requested target capacity.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result open_addressing_rehash_impl(
    void *impl,
    size_t capacity);

/**
 * @brief Copy the statistics snapshot out of a open-addressing backend instance.
 *
 * @param impl Open-addressing backend instance to query.
 * @param out Output structure that receives the copied statistics.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
static ht_result open_addressing_get_stats_impl(
    const void *impl,
    ht_stats *out);

/**
 * @brief Reset the statistics counters stored by a open-addressing backend instance.
 *
 * @param impl Open-addressing backend instance whose counters should be cleared.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if `impl` is invalid.
 */
static ht_result open_addressing_reset_stats_impl(
    void *impl);

/**
 * @brief Rebuild the table storage at a new capacity.
 *
 * @param t Table to resize.
 * @param new_capacity Requested target capacity before normalization.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`,
 *         `HT_ERR_OOM`, or `HT_ERR_FULL`.
 */
static ht_result open_addressing_resize(
    open_addressing_table *t,
    size_t new_capacity);

/**
 * @brief Insert an existing entry into a freshly allocated table during rehash.
 *
 * @param t Destination table being rebuilt.
 * @param hash Cached hash for the key.
 * @param key Key to insert.
 * @param value Value to insert.
 *
 * @return `HT_OK` on success, or `HT_ERR_FULL` if no slot is available.
 */
static ht_result open_addressing_insert_rehash(
    open_addressing_table *t,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value);

/**
 * @brief Find the slot containing a key.
 *
 * @param t Table to search.
 * @param key Key to look up.
 * @param hash Cached hash for the key.
 * @param slot_out Output location for the matching slot index.
 * @param probe_len_out Optional output for the number of probes performed.
 *
 * @return `HT_OK` when found, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result open_addressing_find_slot(
    const open_addressing_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *slot_out,
    uint64_t *probe_len_out);

/**
 * @brief Find the slot to use for an insert, reusing tombstones when possible.
 *
 * @param t Table to search.
 * @param key Key to insert.
 * @param hash Cached hash for the key.
 * @param slot_out Output location for the candidate slot index.
 * @param found_existing Output flag set when the key is already present.
 * @param probe_len_out Optional output for the number of probes performed.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_FULL`.
 */
static ht_result open_addressing_find_insert_slot(
    const open_addressing_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *slot_out,
    int *found_existing,
    uint64_t *probe_len_out);

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable OPEN_ADDRESSING_VTABLE = {
    .destroy = open_addressing_destroy_impl,
    .insert = open_addressing_insert_impl,
    .get = open_addressing_get_impl,
    .remove = open_addressing_remove_impl,
    .size = open_addressing_size_impl,
    .capacity = open_addressing_capacity_impl,
    .load_factor = open_addressing_load_factor_impl,
    .reserve = open_addressing_reserve_impl,
    .rehash = open_addressing_rehash_impl,
    .get_stats = open_addressing_get_stats_impl,
    .reset_stats = open_addressing_reset_stats_impl,
    .bind_bench_iface = open_addressing_bind_bench_iface
};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

ht_result open_addressing_create_impl_ex(
    const ht_config *cfg,
    void           **out)
{
    open_addressing_table *t;
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
    if (t == NULL)
    {
        return HT_ERR_OOM;
    }

    capacity = (cfg->init_capacity > 0)
                   ? cfg->init_capacity
                   : DEFAULT_INITIAL_CAPACITY;
    capacity = next_pow2(capacity);

    min_capacity = (cfg->min_capacity > 0)
                       ? cfg->min_capacity
                       : DEFAULT_MIN_CAPACITY;
    min_capacity = next_pow2(min_capacity);

    if (capacity == 0 || min_capacity == 0) {
        free(t);
        return HT_ERR_INVALID;
    }

    if (capacity < min_capacity)
    {
        capacity = min_capacity;
    }

    t->slots = calloc(capacity, sizeof(*t->slots));
    if (t->slots == NULL)
    {
        free(t);
        return HT_ERR_OOM;
    }

    t->capacity = capacity;
    t->min_capacity = min_capacity;
    t->size = 0;
    t->used = 0;

    t->max_load_factor = (cfg->max_load_factor > 0.0)
                             ? cfg->max_load_factor
                             : DEFAULT_MAX_LOAD;
    t->min_load_factor = (cfg->min_load_factor > 0.0)
                             ? cfg->min_load_factor
                             : DEFAULT_MIN_LOAD;

    t->resize_mode = cfg->rsz_mode;
    t->hash_fn =
        (cfg->hash_fn != NULL) ? cfg->hash_fn : default_hash;
    t->hash_seed = cfg->hash_seed;
    t->collect_stats = cfg->collect_stats;

    UPDATE_BYTES_USED(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    *out = t;
    return HT_OK;
}

const struct ht_vtable *open_addressing_vtable(
    void)
{
    return &OPEN_ADDRESSING_VTABLE;
}

int open_addressing_bind_bench_iface(
    void *ctx,
    bench_iface *out)
{
    if (ctx == NULL || out == NULL)
    {
        return HT_ERR_INVALID;
    }

    out->ctx    = ctx;
    out->insert = open_addressing_insert_impl;
    out->get    = open_addressing_get_impl;
    out->remove = open_addressing_remove_impl;

    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void open_addressing_destroy_impl(
    void *impl)
{
    open_addressing_table *t = impl;

    if (t == NULL)
    {
        return;
    }

    free(t->slots);
    free(t);
}

static ht_result open_addressing_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value)
{
    open_addressing_table *t = impl;
    uint64_t hash;
    size_t slot;
    int found_existing;
    int needs_empty_slot;
    uint64_t probe_len = 0;
    ht_result rc;

    if (t == NULL)
    {
        return HT_ERR_INVALID;
    }

    HT_STATS_INC(t, inserts);

    hash = t->hash_fn(key, t->hash_seed);

retry:
    rc = open_addressing_find_insert_slot(
        t,
        key,
        hash,
        &slot,
        &found_existing,
        &probe_len
    );
    if (rc != HT_OK)
    {
        if (rc == HT_ERR_FULL && t->resize_mode != HT_RESIZE_NONE) {
            rc = open_addressing_resize(t, t->capacity * 2);
            if (rc == HT_OK) {
                goto retry;
            }
        }
        HT_UPDATE_PROBE_STATS(t, probe_len);
        HT_RECORD_INSERT_FAILURE(t);
        return rc;
    }

    if (found_existing)
    {
        HT_UPDATE_PROBE_STATS(t, probe_len);
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_EXISTS;
    }

    needs_empty_slot = (t->slots[slot].state == OPEN_ADDRESSING_SLOT_EMPTY);
    if (t->resize_mode != HT_RESIZE_NONE &&
        needs_empty_slot &&
        HT_SHOULD_GROW_COUNT(t, used))
    {
        rc = open_addressing_resize(t, t->capacity * 2);
        if (rc != HT_OK)
        {
            HT_RECORD_INSERT_FAILURE(t);
            return rc;
        }
        goto retry;
    }

    HT_UPDATE_PROBE_STATS(t, probe_len);

    /* `used` counts live slots plus tombstones, so only a truly empty slot
     * consumes more of the no-resize occupancy budget. */
    if (t->resize_mode == HT_RESIZE_NONE &&
        needs_empty_slot &&
        (double)(t->used + 1) >
            (double)t->capacity * t->max_load_factor)
    {
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_FULL;
    }

    if (needs_empty_slot)
    {
        t->used++;
    }

    t->slots[slot].state = OPEN_ADDRESSING_SLOT_FULL;
    t->slots[slot].hash = hash;
    t->slots[slot].key = key;
    t->slots[slot].value = value;
    t->size++;

    return HT_OK;
}

static ht_result open_addressing_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out)
{
    const open_addressing_table *t = impl;
    size_t slot;
    uint64_t probe_len = 0;
    uint64_t hash;
    ht_result rc;

    if (t == NULL || value_out == NULL)
    {
        return HT_ERR_INVALID;
    }

    if (t->collect_stats)
    {
        ((open_addressing_table *)t)->stats.lookups++;
    }

    hash = t->hash_fn(key, t->hash_seed);
    rc = open_addressing_find_slot(t, key, hash, &slot, &probe_len);

    if (rc != HT_OK)
    {
        if (t->collect_stats)
        {
            ((open_addressing_table *)t)->stats.lookup_misses++;
            HT_UPDATE_PROBE_STATS((open_addressing_table *)t, probe_len);
        }
        return HT_ERR_NOT_FOUND;
    }

    HT_UPDATE_PROBE_STATS((open_addressing_table *)t, probe_len);
    *value_out = t->slots[slot].value;
    return HT_OK;
}

static ht_result open_addressing_remove_impl(
    void *impl,
    ht_key_t key)
{
    open_addressing_table *t = impl;
    size_t slot;
    uint64_t probe_len = 0;
    uint64_t hash;
    ht_result rc;

    if (t == NULL)
    {
        return HT_ERR_INVALID;
    }

    if (t->collect_stats)
    {
        t->stats.removes++;
    }

    hash = t->hash_fn(key, t->hash_seed);
    rc = open_addressing_find_slot(t, key, hash, &slot, &probe_len);

    if (rc != HT_OK)
    {
        if (t->collect_stats)
        {
            t->stats.remove_misses++;
            HT_UPDATE_PROBE_STATS(t, probe_len);
        }
        return HT_ERR_NOT_FOUND;
    }

    HT_UPDATE_PROBE_STATS(t, probe_len);

    /* Leave a tombstone behind so later lookups keep probing through this run. */
    t->slots[slot].state = OPEN_ADDRESSING_SLOT_TOMBSTONE;
    t->size--;

    if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
        HT_SHOULD_SHRINK_COUNT(t, size))
    {
        size_t new_capacity = t->capacity / 2;

        if (new_capacity < t->min_capacity)
        {
            new_capacity = t->min_capacity;
        }

        return open_addressing_resize(t, new_capacity);
    }

    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* metadata/stat ops                                                         */
/* ------------------------------------------------------------------------- */

static size_t open_addressing_size_impl(
    const void *impl)
{
    const open_addressing_table *t = impl;
    return (t != NULL) ? t->size : 0;
}

static size_t open_addressing_capacity_impl(
    const void *impl)
{
    const open_addressing_table *t = impl;
    return (t != NULL) ? t->capacity : 0;
}

static double open_addressing_load_factor_impl(
    const void *impl)
{
    const open_addressing_table *t = impl;

    return (t != NULL)
               ? ht_load_factor_snapshot(t->size, t->capacity)
               : 0.0;
}

static ht_result open_addressing_reserve_impl(
    void *impl,
    size_t capacity)
{
    open_addressing_table *t = impl;

    if (t == NULL)
    {
        return HT_ERR_INVALID;
    }

    if (capacity <= t->capacity)
    {
        return HT_OK;
    }

    return open_addressing_resize(t, next_pow2(capacity));
}

static ht_result open_addressing_rehash_impl(
    void *impl,
    size_t capacity)
{
    open_addressing_table *t = impl;
    size_t target;

    if (t == NULL)
    {
        return HT_ERR_INVALID;
    }

    target = (capacity > t->size) ? capacity : t->size;

    if (target < t->min_capacity)
    {
        target = t->min_capacity;
    }

    return open_addressing_resize(t, next_pow2(target));
}

static ht_result open_addressing_get_stats_impl(
    const void *impl,
    ht_stats *out)
{
    const open_addressing_table *t = impl;

    if (t == NULL || out == NULL)
    {
        return HT_ERR_INVALID;
    }

    *out = t->stats;
    return HT_OK;
}

static ht_result open_addressing_reset_stats_impl(
    void *impl)
{
    open_addressing_table *t = impl;

    if (t == NULL)
    {
        return HT_ERR_INVALID;
    }

    memset(&t->stats, 0, sizeof(t->stats));
    UPDATE_BYTES_USED(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static ht_result open_addressing_resize(
    open_addressing_table *t,
    size_t new_capacity)
{
    open_addressing_slot *old_slots;
    size_t old_capacity;
    size_t old_size;
    open_addressing_slot *new_slots;
    uint64_t resize_start_ns;
    size_t i;
    ht_result rc;

    if (t == NULL)
    {
        return HT_ERR_INVALID;
    }

    if (new_capacity < t->min_capacity)
    {
        new_capacity = t->min_capacity;
    }

    new_capacity = next_pow2(new_capacity);

    if (new_capacity == t->capacity)
    {
        return HT_OK;
    }

    old_slots = t->slots;
    old_capacity = t->capacity;
    old_size = t->size;
    resize_start_ns = ht_resize_instrumentation_start(t->collect_stats);

    new_slots = calloc(new_capacity, sizeof(*new_slots));
    if (new_slots == NULL)
    {
        return HT_ERR_OOM;
    }

    t->slots = new_slots;
    t->capacity = new_capacity;
    t->size = 0;
    t->used = 0;

    for (i = 0; i < old_capacity; i++)
    {
        if (old_slots[i].state == OPEN_ADDRESSING_SLOT_FULL)
        {
            /* Reinsert only live entries; tombstones are discarded on rehash. */
            rc = open_addressing_insert_rehash(
                t,
                old_slots[i].hash,
                old_slots[i].key,
                old_slots[i].value
            );
            if (rc != HT_OK)
            {
                free(t->slots);
                t->slots = old_slots;
                t->capacity = old_capacity;
                t->size = old_size;

                {
                    size_t j;
                    size_t used = 0;

                    /* Reconstruct the old non-empty slot count before
                     * restoring the previous table state. */
                    for (j = 0; j < old_capacity; j++)
                    {
                        if (old_slots[j].state != OPEN_ADDRESSING_SLOT_EMPTY)
                        {
                            used++;
                        }
                    }
                    t->used = used;
                }

                return rc;
            }
        }
    }

    free(old_slots);

    ht_resize_stats_record(
        &t->stats,
        t->collect_stats,
        old_capacity,
        t->capacity,
        old_size,
        resize_start_ns);
    UPDATE_BYTES_USED(t);

    return HT_OK;
}

static ht_result open_addressing_insert_rehash(
    open_addressing_table *t,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value)
{
    size_t base;
    size_t i;

    base = HT_INDEX_FOR_U64(hash, t->capacity);

    for (i = 0; i < t->capacity; i++)
    {
        /* Wrap probing with a mask instead of an explicit modulo. */
        size_t idx = (base + i) & (t->capacity - 1);

        if (t->slots[idx].state != OPEN_ADDRESSING_SLOT_FULL)
        {
            if (t->slots[idx].state == OPEN_ADDRESSING_SLOT_EMPTY)
            {
                t->used++;
            }

            t->slots[idx].state = OPEN_ADDRESSING_SLOT_FULL;
            t->slots[idx].hash = hash;
            t->slots[idx].key = key;
            t->slots[idx].value = value;
            t->size++;
            return HT_OK;
        }
    }

    return HT_ERR_FULL;
}

static ht_result open_addressing_find_slot(
    const open_addressing_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *slot_out,
    uint64_t *probe_len_out)
{
    size_t base;
    size_t i;

    if (t == NULL || slot_out == NULL)
    {
        return HT_ERR_INVALID;
    }

    if (probe_len_out != NULL)
    {
        *probe_len_out = 0;
    }

    base = HT_INDEX_FOR_U64(hash, t->capacity);

    for (i = 0; i < t->capacity; i++)
    {
        /* Probe in open-addressing order and wrap with the power-of-two mask. */
        size_t idx = (base + i) & (t->capacity - 1);

        if (probe_len_out != NULL)
        {
            *probe_len_out = i + 1;
        }

        if (t->slots[idx].state == OPEN_ADDRESSING_SLOT_EMPTY)
        {
            return HT_ERR_NOT_FOUND;
        }

        if (t->slots[idx].state == OPEN_ADDRESSING_SLOT_FULL &&
            t->slots[idx].hash == hash &&
            t->slots[idx].key == key)
        {
            *slot_out = idx;
            return HT_OK;
        }
    }

    return HT_ERR_NOT_FOUND;
}

static ht_result open_addressing_find_insert_slot(
    const open_addressing_table *t,
    ht_key_t key,
    uint64_t hash,
    size_t *slot_out,
    int *found_existing,
    uint64_t *probe_len_out)
{
    size_t base;
    size_t i;
    size_t first_tombstone;
    int have_tombstone;

    if (t == NULL || slot_out == NULL)
    {
        return HT_ERR_INVALID;
    }

    if (found_existing != NULL)
    {
        *found_existing = 0;
    }

    if (probe_len_out != NULL)
    {
        *probe_len_out = 0;
    }

    base = HT_INDEX_FOR_U64(hash, t->capacity);
    first_tombstone = 0;
    have_tombstone = 0;

    for (i = 0; i < t->capacity; i++)
    {
        /* Remember the first tombstone so inserts can reuse it later. */
        size_t idx = (base + i) & (t->capacity - 1);

        if (probe_len_out != NULL)
        {
            *probe_len_out = i + 1;
        }

        if (t->slots[idx].state == OPEN_ADDRESSING_SLOT_FULL)
        {
            if (t->slots[idx].hash == hash &&
                t->slots[idx].key == key)
            {
                *slot_out = idx;
                if (found_existing != NULL)
                {
                    *found_existing = 1;
                }
                return HT_OK;
            }
            continue;
        }

        if (t->slots[idx].state == OPEN_ADDRESSING_SLOT_TOMBSTONE &&
            !have_tombstone)
        {
            first_tombstone = idx;
            have_tombstone = 1;
            continue;
        }

        if (t->slots[idx].state == OPEN_ADDRESSING_SLOT_EMPTY)
        {
            /* Reuse the earliest tombstone so the cluster does not grow
             * farther than necessary. */
            *slot_out = have_tombstone ? first_tombstone : idx;
            return HT_OK;
        }
    }

    if (have_tombstone)
    {
        *slot_out = first_tombstone;
        return HT_OK;
    }

    return HT_ERR_FULL;
}
