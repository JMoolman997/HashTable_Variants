/**
 * @file resize_stats.h
 * @brief Resize instrumentation helpers for backend statistics.
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#ifndef HT_UTIL_RESIZE_STATS_H
#define HT_UTIL_RESIZE_STATS_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

#if HT_ENABLE_RESIZE_INSTRUMENTATION
uint64_t ht_resize_instrumentation_now_ns(void);

static inline uint64_t ht_resize_instrumentation_start(int collect_stats) {
    return collect_stats ? ht_resize_instrumentation_now_ns() : 0ULL;
}

static inline void ht_resize_stats_init(
    ht_stats *stats,
    int collect_stats,
    size_t capacity
) {
    if (stats == NULL || !collect_stats) {
        return;
    }

    stats->resize_min_capacity = capacity;
    stats->resize_max_capacity = capacity;
}

static inline void ht_resize_stats_record(
    ht_stats *stats,
    int collect_stats,
    size_t old_capacity,
    size_t new_capacity,
    size_t entries_moved,
    uint64_t start_ns
) {
    uint64_t end_ns;
    uint64_t elapsed_ns;

    if (stats == NULL || !collect_stats || old_capacity == new_capacity) {
        return;
    }

    end_ns = ht_resize_instrumentation_now_ns();
    elapsed_ns = (start_ns != 0ULL && end_ns > start_ns)
        ? end_ns - start_ns
        : 0ULL;

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

    if (stats->resize_min_capacity == 0u ||
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
#define ht_resize_stats_init(stats, collect_stats, capacity)                  \
    do {                                                                      \
        (void)(stats);                                                        \
        (void)(collect_stats);                                                \
        (void)(capacity);                                                     \
    } while (0)
#define ht_resize_stats_record(                                               \
    stats, collect_stats, old_capacity, new_capacity, entries_moved, start_ns \
)                                                                             \
    do {                                                                      \
        (void)(stats);                                                        \
        (void)(collect_stats);                                                \
        (void)(old_capacity);                                                 \
        (void)(new_capacity);                                                 \
        (void)(entries_moved);                                                \
        (void)(start_ns);                                                     \
    } while (0)
#endif /* HT_ENABLE_RESIZE_INSTRUMENTATION */

#endif /* HT_UTIL_RESIZE_STATS_H */
