/**
 * @file stats_util.h
 * @brief Operation and memory statistics helpers for backends.
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#ifndef HT_UTIL_STATS_UTIL_H
#define HT_UTIL_STATS_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"
#include "memory_util.h"

static inline void ht_stats_inc(uint64_t *counter, int collect_stats) {
    if (counter != NULL && collect_stats) {
        (*counter)++;
    }
}

static inline void ht_stats_record_probe(
    ht_stats *stats,
    int collect_stats,
    uint64_t probe_len
) {
    if (stats == NULL || !collect_stats) {
        return;
    }

    stats->probes += probe_len;
    if (probe_len > stats->max_probe_len) {
        stats->max_probe_len = probe_len;
    }
}

static inline void ht_stats_set_bytes(
    ht_stats *stats,
    int collect_stats,
    size_t bytes
) {
    if (stats != NULL && collect_stats) {
        stats->bytes_used = bytes;
    }
}

static inline void ht_stats_set_slot_array_bytes(
    ht_stats *stats,
    int collect_stats,
    size_t table_size,
    size_t capacity,
    size_t slot_size
) {
    ht_stats_set_bytes(
        stats,
        collect_stats,
        ht_bytes_used_snapshot(table_size, capacity, slot_size)
    );
}

#define HT_STATS_INC(t, field)                                                \
    ht_stats_inc(&(t)->stats.field, (t)->collect_stats)

#define HT_RECORD_INSERT_FAILURE(t)                                           \
    HT_STATS_INC((t), insert_failures)

#define HT_UPDATE_PROBE_STATS(t, probe_len)                                   \
    ht_stats_record_probe(&(t)->stats, (t)->collect_stats, (probe_len))

#endif /* HT_UTIL_STATS_UTIL_H */
