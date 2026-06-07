/**
 * @file memory_util.h
 * @brief Checked size arithmetic and memory accounting helpers.
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#ifndef HT_UTIL_MEMORY_UTIL_H
#define HT_UTIL_MEMORY_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

static inline ht_result ht_checked_add_size(
    size_t a,
    size_t b,
    size_t *out
) {
    if (out == NULL) {
        return HT_ERR_INVALID;
    }
    if (a > SIZE_MAX - b) {
        return HT_ERR_OOM;
    }

    *out = a + b;
    return HT_OK;
}

static inline ht_result ht_checked_mul_size(
    size_t count,
    size_t size,
    size_t *out
) {
    if (out == NULL) {
        return HT_ERR_INVALID;
    }
    if (size != 0u && count > SIZE_MAX / size) {
        return HT_ERR_OOM;
    }

    *out = count * size;
    return HT_OK;
}

static inline size_t ht_bytes_add_or_max(size_t total, size_t extra) {
    if (total == SIZE_MAX || extra > SIZE_MAX - total) {
        return SIZE_MAX;
    }

    return total + extra;
}

static inline size_t ht_bytes_mul_or_max(size_t count, size_t size) {
    if (size != 0u && count > SIZE_MAX / size) {
        return SIZE_MAX;
    }

    return count * size;
}

static inline size_t ht_bytes_add_array_or_max(
    size_t total,
    size_t count,
    size_t size
) {
    return ht_bytes_add_or_max(total, ht_bytes_mul_or_max(count, size));
}

#endif /* HT_UTIL_MEMORY_UTIL_H */
