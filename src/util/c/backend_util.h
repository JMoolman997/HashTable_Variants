/**
 * @file    backend_util.h
 * @brief   Utilities used by hashtable implementations.
 *
 * Provides shared defaults, hashing helpers, power-of-two helpers, resize
 * instrumentation hooks, and snapshot-based load/memory helpers that can be
 * used safely by concurrent backends after atomic fields have been loaded into
 * local variables.
 *
 * @author  J.W Moolman
 * @date    2026-04-16
 */

#ifndef BACKEND_UTIL_H
#define BACKEND_UTIL_H

#include <stdint.h>
#include <stddef.h>

#include "ht_types.h"

#define DEFAULT_INITIAL_CAPACITY 16u
#define DEFAULT_MIN_CAPACITY     16u
#define DEFAULT_MAX_LOAD         0.85
#define DEFAULT_MIN_LOAD         0.10

#define UPDATE_BYTES_USED(t)                                                  \
    do {                                                                      \
        if ((t) != NULL && (t)->collect_stats) {                              \
            (t)->stats.bytes_used =                                           \
                ht_bytes_used_snapshot(                                       \
                    sizeof(*(t)),                                             \
                    (t)->capacity,                                            \
                    sizeof(*(t)->slots)                                       \
                );                                                            \
        }                                                                     \
    } while (0)

static inline void ht_stats_inc(
    uint64_t *counter,
    int       collect_stats
) {
    if (counter != NULL && collect_stats) {
        (*counter)++;
    }
}

static inline void ht_stats_record_probe(
    ht_stats *stats,
    int       collect_stats,
    uint64_t  probe_len
) {
    if (stats == NULL || !collect_stats) {
        return;
    }

    stats->probes += probe_len;
    if (probe_len > stats->max_probe_len) {
        stats->max_probe_len = probe_len;
    }
}

#define HT_STATS_INC(t, field)                                                \
    ht_stats_inc(&(t)->stats.field, (t)->collect_stats)

#define HT_RECORD_INSERT_FAILURE(t)                                           \
    HT_STATS_INC((t), insert_failures)

#define HT_UPDATE_PROBE_STATS(t, probe_len)                                   \
    ht_stats_record_probe(&(t)->stats, (t)->collect_stats, (probe_len))

#define HT_SHOULD_GROW_COUNT(t, count_field)                                  \
    ht_should_grow_snapshot(                                                  \
        (t)->count_field,                                                     \
        (t)->capacity,                                                        \
        (t)->max_load_factor                                                  \
    )

#define HT_SHOULD_SHRINK_COUNT(t, count_field)                                \
    ht_should_shrink_snapshot(                                                \
        (t)->count_field,                                                     \
        (t)->capacity,                                                        \
        (t)->min_capacity,                                                    \
        (t)->min_load_factor                                                  \
    )

/*
 * Return the table index for a hash in a power-of-two table.
 *
 * capacity - 1 acts as a bit mask, so this keeps only the low bits of hash.
 * The result is in the range [0, capacity).
 *
 * Example:
 *   capacity = 16  -> mask = 15  -> 0b1111
 *   hash & mask gives an index from 0 to 15
 *
 * Precondition:
 *   capacity must be a non-zero power of two.
 */
#define HT_INDEX_FOR_U64(hash, capacity) \
    ((size_t)(hash) & ((capacity) - 1))

/*
 * Return the smallest power of two greater than or equal to x.
 *
 * Cases:
 *   x == 0 or x == 1  -> 1
 *   exact power of 2  -> x
 *   otherwise         -> next larger power of 2
 *
 * The bit trick works by:
 *   1. decrementing x,
 *   2. propagating the highest set bit to all lower bits,
 *   3. adding 1.
 *
 * Example:
 *   13  = 01101
 *   12  = 01100   after decrement
 *   15  = 01111   after bit spreading
 *   16  = 10000   after adding 1
 *
 * Overflow:
 *   If the true result does not fit in size_t, the spread step produces
 *   all 1 bits, and the final +1 wraps to 0. In that case, return 0.
 */
static inline size_t next_pow2(size_t x)
{
    if (x <= 1) {
        return 1;
    }

    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    if (sizeof(size_t) >= 8) {
        x |= x >> 32;
    }

    return x + 1;
}

static inline ht_result ht_checked_next_pow2(
    size_t  value,
    size_t *out
) {
    size_t rounded;

    if (value == 0 || out == NULL) {
        return HT_ERR_INVALID;
    }

    rounded = next_pow2(value);
    if (rounded == 0) {
        return HT_ERR_OOM;
    }

    *out = rounded;
    return HT_OK;
}

/*
 * Return the largest power of two less than or equal to x.
 *
 * Cases:
 *   x == 0  -> 0
 *   exact power of 2 -> x
 *   otherwise -> next smaller power of 2
 */
static inline size_t floor_pow2(size_t x)
{
    size_t p = 1;

    if (x == 0) {
        return 0;
    }

    while (p <= x / 2) {
        p <<= 1;
    }

    return p;
}

/* Return the log2 shift for a non-zero power-of-two value. */
static inline size_t ht_pow2_shift(
    size_t value
) {
    size_t shift = 0;

    while (value > 1u) {
        value >>= 1u;
        shift++;
    }

    return shift;
}

/* Use high hash bits as an 8-bit tag, separate from low index bits. */
static inline uint8_t ht_hash_tag_u8(
    uint64_t hash
) {
    return (uint8_t)(hash >> 56);
}

/* Clamp capacity * load to a size_t floor for resize thresholds. */
static inline size_t ht_load_floor_limit(
    size_t capacity,
    double load
) {
    long double limit = (long double)capacity * (long double)load;

    if (limit <= 0.0L) {
        return 0;
    }
    if (limit >= (long double)SIZE_MAX) {
        return SIZE_MAX;
    }
    return (size_t)limit;
}

/* Clamp capacity * load to a size_t ceiling for resize thresholds. */
static inline size_t ht_load_ceil_limit(
    size_t capacity,
    double load
) {
    long double limit = (long double)capacity * (long double)load;
    size_t floored;

    if (limit <= 0.0L) {
        return 0;
    }
    if (limit >= (long double)SIZE_MAX) {
        return SIZE_MAX;
    }

    floored = (size_t)limit;
    return ((long double)floored < limit)
        ? floored + 1u
        : floored
    ;
}

/* Add byte counts, saturating at SIZE_MAX on overflow. */
static inline size_t ht_bytes_add_or_max(
    size_t total,
    size_t extra
) {
    if (total == SIZE_MAX || extra > SIZE_MAX - total) {
        return SIZE_MAX;
    }

    return total + extra;
}

/* Multiply byte counts, saturating at SIZE_MAX on overflow. */
static inline size_t ht_bytes_mul_or_max(
    size_t count,
    size_t size
) {
    if (size != 0 && count > SIZE_MAX / size) {
        return SIZE_MAX;
    }

    return count * size;
}

/* Add count * size to a running byte total, saturating on overflow. */
static inline size_t ht_bytes_add_array_or_max(
    size_t total,
    size_t count,
    size_t size
) {
    return ht_bytes_add_or_max(total, ht_bytes_mul_or_max(count, size));
}

/*
 * Snapshot helpers are safe to use with concurrent tables after the caller has
 * loaded any atomic fields into local variables.
 */
static inline size_t ht_bytes_used_snapshot(
    size_t table_size,
    size_t capacity,
    size_t slot_size
) {
    if (slot_size != 0 && capacity > (SIZE_MAX - table_size) / slot_size) {
        return SIZE_MAX;
    }

    return table_size + capacity * slot_size;
}

static inline double ht_load_factor_snapshot(
    size_t size,
    size_t capacity
) {
    return (capacity == 0)
        ? 0.0
        : (double)size / (double)capacity
    ;
}

static inline int ht_should_grow_snapshot(
    size_t count,
    size_t capacity,
    double max_load_factor
) {
    if (capacity == 0 || count == SIZE_MAX) {
        return 1;
    }

    return (
        (double)(count + 1) >
        (double)capacity * max_load_factor
    );
}

static inline int ht_should_shrink_snapshot(
    size_t count,
    size_t capacity,
    size_t min_capacity,
    double min_load_factor
) {
    return (
        capacity > min_capacity &&
        (double)count < (double)capacity * min_load_factor
    );
}

static inline uint64_t default_hash(ht_key_t key, uint64_t seed)
{
    uint64_t x = key ^ seed;

    /* Apply the MurmurHash3 finalizer to avalanche nearby keys apart. */
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;

    return x;
}

#if HT_ENABLE_RESIZE_INSTRUMENTATION
uint64_t ht_resize_instrumentation_now_ns(void);

static inline uint64_t ht_resize_instrumentation_start(
    int collect_stats
) {
    return collect_stats
        ? ht_resize_instrumentation_now_ns()
        : 0ULL
    ;
}

static inline void ht_resize_stats_init(
    ht_stats *stats,
    int       collect_stats,
    size_t    capacity
) {
    if (stats == NULL || !collect_stats) {
        return;
    }

    stats->resize_min_capacity = capacity;
    stats->resize_max_capacity = capacity;
}

static inline void ht_resize_stats_record(
    ht_stats *stats,
    int       collect_stats,
    size_t    old_capacity,
    size_t    new_capacity,
    size_t    entries_moved,
    uint64_t  start_ns
) {
    uint64_t end_ns;
    uint64_t elapsed_ns;

    if (stats == NULL || !collect_stats || old_capacity == new_capacity) {
        return;
    }

    end_ns = ht_resize_instrumentation_now_ns();
    elapsed_ns = (start_ns != 0ULL && end_ns > start_ns)
        ? (end_ns - start_ns)
        : 0ULL
    ;

    stats->resize_count++;
    stats->rehash_count++;
    if (new_capacity > old_capacity) {
        stats->grow_count++;
    } else if (new_capacity < old_capacity) {
        stats->shrink_count++;
    }

    stats->resize_entries_moved += (uint64_t)entries_moved;
    stats->resize_total_ns += elapsed_ns;
    if (elapsed_ns > stats->resize_max_ns) {
        stats->resize_max_ns = elapsed_ns;
    }

    if (stats->resize_min_capacity == 0 ||
        old_capacity < stats->resize_min_capacity) {
        stats->resize_min_capacity = old_capacity;
    }
    if (new_capacity < stats->resize_min_capacity) {
        stats->resize_min_capacity = new_capacity;
    }
    if (old_capacity > stats->resize_max_capacity) {
        stats->resize_max_capacity = old_capacity;
    }
    if (new_capacity > stats->resize_max_capacity) {
        stats->resize_max_capacity = new_capacity;
    }
}
#else
#define ht_resize_instrumentation_start(collect_stats) (0ULL)
#define ht_resize_stats_init(stats, collect_stats, capacity)              \
    do {                                                                  \
        (void)(stats);                                                    \
        (void)(collect_stats);                                            \
        (void)(capacity);                                                 \
    } while (0)
#define ht_resize_stats_record(                                           \
    stats, collect_stats, old_capacity, new_capacity, entries_moved,     \
    start_ns                                                             \
)                                                                         \
    do {                                                                  \
        (void)(stats);                                                    \
        (void)(collect_stats);                                            \
        (void)(old_capacity);                                             \
        (void)(new_capacity);                                             \
        (void)(entries_moved);                                            \
        (void)(start_ns);                                                 \
    } while (0)
#endif /* HT_ENABLE_RESIZE_INSTRUMENTATION */

#endif /* BACKEND_UTIL_H */
