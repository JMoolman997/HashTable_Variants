/**
 * @file    bench_config.h
 * @brief   Shared benchmark configuration types.
 */

#ifndef BENCH_CONFIG_H
#define BENCH_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

typedef enum {
    BENCH_SCENARIO_INSERT_BUILD = 0,
    BENCH_SCENARIO_LOOKUP_HIT = 1,
    BENCH_SCENARIO_LOOKUP_MISS = 2,
    BENCH_SCENARIO_ERASE_EXISTING = 3,
    BENCH_SCENARIO_WORKLOAD = 4
} bench_scenario;

typedef enum {
    BENCH_BACKEND_SINGLE = 0,
    BENCH_BACKEND_PTHREAD = 1
} bench_backend_kind;

typedef enum {
    BENCH_CAP_FIXED = 0,
    BENCH_CAP_RESIZING = 1
} bench_capacity_policy;

typedef enum {
    WORKLOAD_READ_ONLY = 0,
    WORKLOAD_READ_HEAVY = 1,
    WORKLOAD_MIXED = 2,
    WORKLOAD_BALANCED = 3
} workload_kind;

typedef enum {
    BENCH_RESIZE_DISABLED = 0,
    BENCH_RESIZE_GROW_ONLY = 1,
    BENCH_RESIZE_GROW_SHRINK = 2
} bench_resize_mode;

typedef enum {
    BENCH_STATS_OFF = 0,
    BENCH_STATS_ON = 1
} bench_stats_mode;

typedef enum {
    BENCH_KEYSPACE_DISJOINT = 0,
    BENCH_KEYSPACE_SHARED_READ = 1
} bench_keyspace_mode;

typedef enum {
    BENCH_FORMAT_TEXT = 0,
    BENCH_FORMAT_CSV = 1
} bench_output_format;

typedef enum {
    BENCH_OPTBIT_IMPL = 1ull << 0,
    BENCH_OPTBIT_DATASET_SIZE = 1ull << 1,
    BENCH_OPTBIT_ALPHA = 1ull << 2,
    BENCH_OPTBIT_TIMED_OPS = 1ull << 3,
    BENCH_OPTBIT_WARMUP_OPS = 1ull << 4,
    BENCH_OPTBIT_REPETITIONS = 1ull << 5,
    BENCH_OPTBIT_HASH_SEED = 1ull << 6,
    BENCH_OPTBIT_KEY_SEED = 1ull << 7,
    BENCH_OPTBIT_TRACE_SEED = 1ull << 8,
    BENCH_OPTBIT_WORKLOAD = 1ull << 9,
    BENCH_OPTBIT_INITIAL_CAPACITY = 1ull << 10,
    BENCH_OPTBIT_RESIZE = 1ull << 11,
    BENCH_OPTBIT_GROW_ALPHA = 1ull << 12,
    BENCH_OPTBIT_SHRINK_ALPHA = 1ull << 13,
    BENCH_OPTBIT_PREFILL_ALPHA = 1ull << 14,
    BENCH_OPTBIT_THREAD_COUNT = 1ull << 15,
    BENCH_OPTBIT_CONCURRENT_KEYSPACE = 1ull << 16,
    BENCH_OPTBIT_CONCURRENT_RESIZE = 1ull << 17,
    BENCH_OPTBIT_STATS = 1ull << 18,
    BENCH_OPTBIT_FORMAT = 1ull << 19
} bench_option_bit;

#define BENCH_OPT_COMMON \
    (BENCH_OPTBIT_IMPL | BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_ALPHA | \
     BENCH_OPTBIT_TIMED_OPS | BENCH_OPTBIT_WARMUP_OPS | \
     BENCH_OPTBIT_REPETITIONS | BENCH_OPTBIT_HASH_SEED | \
     BENCH_OPTBIT_KEY_SEED | BENCH_OPTBIT_STATS | BENCH_OPTBIT_FORMAT)

typedef enum {
    BENCH_CASE_REQUIRES_TRACE_SEED = 1u << 0
} bench_case_flag;

typedef struct {
    const char            *name;
    bench_scenario        scenario;
    bench_backend_kind    backend;
    bench_capacity_policy capacity_policy;
    bench_resize_mode     default_resize_mode;
    bench_keyspace_mode   default_keyspace_mode;
    uint64_t              allowed_options;
    uint64_t              required_options;
    unsigned              flags;
} bench_case;

const bench_case *bench_cases(
    size_t *count
);

const bench_case *bench_case_find(
    const char *name
);

const char *bench_option_set_first_name(
    uint64_t bits
);

int bench_impl_supports_concurrent_mutation(
    ht_impl impl
);

int bench_impl_supports_concurrent_resize(
    ht_impl impl
);

#endif /* BENCH_CONFIG_H */
