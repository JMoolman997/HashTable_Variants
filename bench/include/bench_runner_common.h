/**
 * @file    bench_runner_common.h
 * @brief   Shared benchmark execution helpers.
 */

#ifndef BENCH_RUNNER_COMMON_H
#define BENCH_RUNNER_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "bench_metric.h"
#include "bench_plan.h"
#include "bench_trace.h"
#include "ht.h"
#include "ht_bench.h"

typedef struct {
    ht_map      *map;
    bench_iface  bi;
    ht_stats     stats;
    ht_config    cfg;
} bench_env;

int bench_build_ht_config_from_plan(
    const bench_plan *plan,
    ht_config        *cfg,
    int               collect_stats
);

int bench_prepopulate_table(
    ht_map          *map,
    const ht_key_t *keys,
    size_t           key_count
);

int bench_run_warmup(
    const bench_plan *plan,
    const ht_key_t   *prepopulate_keys,
    size_t            prepopulate_count,
    const bench_op   *ops,
    size_t            op_count
);

int bench_env_init(
    const bench_plan *plan,
    bench_env        *env
);

void bench_env_destroy(
    bench_env *env
);

int bench_env_prepare_timed_phase(
    bench_env *env,
    unsigned iface_flags
);

int bench_env_collect_stats(
    bench_env *env
);

int bench_measure_timed_ops(
    const bench_iface *bi,
    const bench_op    *ops,
    size_t             op_count,
    uint64_t          *elapsed_ns
);

int bench_apply_op_direct(
    const bench_iface *bi,
    const bench_op    *op
);

int bench_append_core_metrics(
    const bench_plan *plan,
    const ht_map     *map,
    const ht_stats   *stats,
    size_t            initial_live,
    uint64_t          elapsed_ns,
    bench_sample     *sample
);

int bench_append_resize_metrics(
    const bench_plan *plan,
    const ht_stats   *stats,
    size_t            final_capacity,
    bench_sample     *sample
);

#endif /* BENCH_RUNNER_COMMON_H */
