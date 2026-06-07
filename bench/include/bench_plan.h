/**
 * @file    bench_plan.h
 * @brief   Normalized execution plan for benchmark runs.
 *
 * Separates raw CLI input from execution-ready benchmark settings so the
 * runner layer can stay simple and mode-specific defaults can live in one
 * place.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#ifndef BENCH_PLAN_H
#define BENCH_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "bench_config.h"

typedef enum {
    BENCH_EXEC_STEADY = 0,
    BENCH_EXEC_RESIZE = 1,
    BENCH_EXEC_CONCURRENT = 2
} bench_exec_mode;

typedef struct {
    bench_exec_mode exec_mode; /* Selects steady, resize, or concurrent runner. */
    bench_kind      kind;
    workload_kind   workload;
    ht_impl         impl_kind;
    ht_hash_fn      hash_fn;
    uint64_t        hash_seed;
    uint64_t        key_seed;
    uint64_t        trace_seed;
    bench_capacity_mode capacity_mode;
    bench_resize_mode   resize_mode;
    bench_stats_mode    stats_mode;
    bench_resize_mode   concurrent_resize_mode;
    bench_keyspace_mode concurrent_keyspace_mode;
    size_t          dataset_size;
    size_t          timed_ops;
    size_t          warmup_ops;
    size_t          repetitions;
    size_t          planned_capacity; /* Derived power-of-two table capacity. */
    size_t          initial_capacity;
    size_t          min_capacity;
    size_t          min_capacity_override;
    double          target_alpha;
    double          prefill_alpha;
    double          grow_alpha;
    double          shrink_alpha;
    size_t          thread_count;
    int             is_resize_family; /* Marks resize subcommands after parsing. */
} bench_plan;

/**
 * @brief Convert a parsed benchmark spec into an execution-ready plan.
 *
 * @param spec Parsed CLI specification.
 * @param plan Output plan populated with derived defaults and capacities.
 *
 * @return `0` on success, or `-1` if required derived values are invalid.
 */
int bench_plan_build(
    const bench_spec *spec,
    bench_plan       *plan
);

/**
 * @brief Validate a normalized benchmark execution plan.
 *
 * @param plan Plan to validate before dispatching a runner.
 *
 * @return `0` when valid, or `-1` with a diagnostic on failure.
 */
int bench_plan_validate(
    const bench_plan *plan
);

#endif /* BENCH_PLAN_H */
