/**
 * @file    bench_plan.h
 * @brief   Normalized execution plan for benchmark runs.
 */

#ifndef BENCH_PLAN_H
#define BENCH_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "bench_config.h"

typedef struct {
    const bench_case *bench_case;
    const char       *benchmark_name;
    bench_scenario        scenario;
    bench_backend_kind    backend;
    bench_capacity_policy capacity_policy;
    ht_impl          impl_kind;
    uint64_t         hash_seed;
    uint64_t         key_seed;
    uint64_t         trace_seed;
    workload_kind    workload;
    bench_resize_mode resize_mode;
    bench_resize_mode concurrent_resize_mode;
    bench_stats_mode  stats_mode;
    bench_keyspace_mode keyspace_mode;
    bench_output_format output_format;
    size_t           dataset_size;
    size_t           timed_ops;
    size_t           warmup_ops;
    size_t           repetitions;
    size_t           planned_capacity;
    size_t           initial_capacity;
    double           target_alpha;
    double           prefill_alpha;
    double           grow_alpha;
    double           shrink_alpha;
    size_t           thread_count;
    uint64_t         set_options;
} bench_plan;

int bench_plan_validate(
    bench_plan *plan
);

#endif /* BENCH_PLAN_H */
