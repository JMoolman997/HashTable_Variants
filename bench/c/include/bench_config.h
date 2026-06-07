/**
 * @file    bench_config.h
 * @brief   Shared benchmark configuration and result types.
 *
 * Defines the benchmark kinds, workload kinds, parsed benchmark
 * specification, and per-repetition result record used by the benchmark
 * harness.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#ifndef BENCH_CONFIG_H
#define BENCH_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

typedef enum {
    BENCH_INSERT_BUILD = 0,
    BENCH_LOOKUP_HIT = 1,
    BENCH_LOOKUP_MISS = 2,
    BENCH_ERASE_EXISTING = 3,
    BENCH_WORKLOAD = 4,
    BENCH_RESIZE_BUILD = 5, /* First resize-family dispatch value. */
    BENCH_RESIZE_LOOKUP_HIT = 6,
    BENCH_RESIZE_LOOKUP_MISS = 7,
    BENCH_RESIZE_ERASE_EXISTING = 8,
    BENCH_RESIZE_WORKLOAD = 9,
    BENCH_CONCURRENT_LOOKUP = 10, /* First concurrent dispatch value. */
    BENCH_CONCURRENT_WORKLOAD = 11
} bench_kind;

typedef enum {
    WORKLOAD_READ_ONLY = 0,
    WORKLOAD_READ_HEAVY = 1,
    WORKLOAD_MIXED = 2,
    WORKLOAD_BALANCED = 3
} workload_kind;

typedef enum {
    BENCH_CAPACITY_FIXED = 0,
    BENCH_CAPACITY_RESIZING = 1
} bench_capacity_mode;

typedef enum {
    BENCH_RESIZE_DISABLED = 0,
    BENCH_RESIZE_GROW_ONLY = 1,
    BENCH_RESIZE_GROW_SHRINK = 2,
    BENCH_RESIZE_IMPL_DEFAULT = 3
} bench_resize_mode;

typedef enum {
    BENCH_KEYSPACE_DISJOINT = 0,
    BENCH_KEYSPACE_SHARED_READ = 1, /* Internal read-only concurrent mode. */
    BENCH_KEYSPACE_SHARED_MIXED = 2
} bench_keyspace_mode;

typedef struct {
    bench_kind kind;
    workload_kind workload;
    ht_impl impl_kind;
    ht_hash_fn hash_fn;
    uint64_t hash_seed;
    size_t dataset_size;
    double target_alpha;
    size_t capacity;
    bench_capacity_mode capacity_mode;
    bench_resize_mode resize_mode;
    int resize_mode_set;
    bench_resize_mode concurrent_resize_mode;
    bench_keyspace_mode concurrent_keyspace_mode;
    size_t initial_capacity;
    size_t min_capacity_override;
    double prefill_alpha;
    int prefill_alpha_set;
    double grow_alpha;
    double shrink_alpha;
    size_t thread_count;
    size_t warmup_ops;
    size_t timed_ops;
    size_t repetitions;
    uint64_t key_seed;
    uint64_t trace_seed;
    int csv_output;
} bench_spec;

typedef struct {
    bench_spec spec; /* Snapshot of the normalized inputs written to CSV. */
    uint64_t elapsed_ns;
    double ns_per_op;
    double ops_per_sec;
    ht_stats stats;
    double avg_probe_len;
    double memory_amplification;
    double memory_amp_initial;
    double memory_amp_final;
    double final_alpha;
    size_t initial_live;
    size_t final_live;
    size_t final_capacity;
} bench_result;

typedef struct {
    uint64_t resize_events;
    uint64_t grow_events;
    uint64_t shrink_events;
    uint64_t resize_entries_moved;
    uint64_t total_resize_ns;
    uint64_t max_resize_ns;
    size_t min_capacity_seen;
    size_t max_capacity_seen;
} bench_resize_result;

typedef struct {
    size_t thread_count;
    bench_keyspace_mode keyspace_mode;
    bench_resize_mode resize_mode;
    int true_concurrent_mutation;
    int harness_op_lock;
    uint64_t wall_elapsed_ns;
    double total_ops_per_sec;
    double ops_per_sec_per_thread;
    uint64_t worker_failures;
} bench_concurrent_result;

typedef struct {
    bench_result core;
    bench_resize_result resize;
    bench_concurrent_result concurrent;
} bench_run_result;

/**
 * @brief Return non-zero if an implementation supports concurrent mutation.
 */
static inline int bench_impl_supports_concurrent_mutation(
    ht_impl impl
) {
    return (impl == HT_IMPL_P_OPEN_ADDRESSING ||
            impl == HT_IMPL_P_SEPARATE_CHAINING ||
            impl == HT_IMPL_LF_HOPSCOTCH);
}

/**
 * @brief Return non-zero if an implementation supports concurrent resizing.
 */
static inline int bench_impl_supports_concurrent_resize(
    ht_impl impl
) {
    return (impl == HT_IMPL_P_OPEN_ADDRESSING ||
            impl == HT_IMPL_P_SEPARATE_CHAINING ||
            impl == HT_IMPL_LF_HOPSCOTCH);
}

#endif /* BENCH_CONFIG_H */
