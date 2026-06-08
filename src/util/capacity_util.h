/**
 * @file capacity_util.h
 * @brief Capacity, power-of-two, and load-threshold helpers for backends.
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#ifndef HT_UTIL_CAPACITY_UTIL_H
#define HT_UTIL_CAPACITY_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

static inline size_t next_pow2(size_t x) {
    if (x <= 1u) {
        return 1u;
    }

    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    if (sizeof(size_t) >= 8u) {
        x |= x >> 32;
    }

    return x + 1u;
}

static inline ht_result ht_checked_next_pow2(size_t value, size_t *out) {
    size_t rounded;

    if (value == 0u || out == NULL) {
        return HT_ERR_INVALID;
    }

    rounded = next_pow2(value);
    if (rounded == 0u) {
        return HT_ERR_OOM;
    }

    *out = rounded;
    return HT_OK;
}

static inline ht_result ht_grow_capacity_pow2(size_t capacity, size_t *out) {
    if (out == NULL) {
        return HT_ERR_INVALID;
    }
    if (capacity == 0u || capacity > SIZE_MAX / 2u) {
        return HT_ERR_OOM;
    }

    return ht_checked_next_pow2(capacity * 2u, out);
}

static inline size_t floor_pow2(size_t x) {
    size_t p = 1u;

    if (x == 0u) {
        return 0u;
    }

    while (p <= x / 2u) {
        p <<= 1u;
    }

    return p;
}

static inline size_t ht_pow2_shift(size_t value) {
    size_t shift = 0u;

    while (value > 1u) {
        value >>= 1u;
        shift++;
    }

    return shift;
}

static inline size_t ht_load_floor_limit(size_t capacity, double load) {
    long double limit = (long double)capacity * (long double)load;

    if (limit <= 0.0L) {
        return 0u;
    }
    if (limit >= (long double)SIZE_MAX) {
        return SIZE_MAX;
    }
    return (size_t)limit;
}

static inline size_t ht_load_ceil_limit(size_t capacity, double load) {
    long double limit = (long double)capacity * (long double)load;
    size_t floored;

    if (limit <= 0.0L) {
        return 0u;
    }
    if (limit >= (long double)SIZE_MAX) {
        return SIZE_MAX;
    }

    floored = (size_t)limit;
    return ((long double)floored < limit) ? floored + 1u : floored;
}

static inline int ht_should_grow_snapshot(
    size_t count,
    size_t capacity,
    double max_load_factor
) {
    if (capacity == 0u || count == SIZE_MAX) {
        return 1;
    }

    return (double)(count + 1u) > (double)capacity * max_load_factor;
}

static inline int ht_should_shrink_snapshot(
    size_t count,
    size_t capacity,
    size_t min_capacity,
    double min_load_factor
) {
    return capacity > min_capacity &&
           (double)count < (double)capacity * min_load_factor;
}

static inline double ht_load_factor_snapshot(size_t size, size_t capacity) {
    return (capacity != 0u) ? (double)size / (double)capacity : 0.0;
}

static inline ht_result ht_reserve_target(
    size_t current_capacity,
    size_t requested_capacity,
    size_t *out
) {
    if (out == NULL) {
        return HT_ERR_INVALID;
    }
    if (requested_capacity <= current_capacity) {
        *out = current_capacity;
        return HT_OK;
    }

    return ht_checked_next_pow2(requested_capacity, out);
}

static inline ht_result ht_rehash_target(
    size_t live_count,
    size_t min_capacity,
    size_t requested_capacity,
    size_t *out
) {
    size_t target;

    if (out == NULL) {
        return HT_ERR_INVALID;
    }

    target = (requested_capacity > live_count) ? requested_capacity : live_count;
    if (target < min_capacity) {
        target = min_capacity;
    }

    return ht_checked_next_pow2(target, out);
}

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

#endif /* HT_UTIL_CAPACITY_UTIL_H */
