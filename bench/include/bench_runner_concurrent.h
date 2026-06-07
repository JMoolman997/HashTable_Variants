/**
 * @file    bench_runner_concurrent.h
 * @brief   Concurrent benchmark runners.
 *
 * Declares concurrent-family benchmark entry points that measure wall-clock
 * scaling over deterministic per-thread traces.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#ifndef BENCH_RUNNER_CONCURRENT_H
#define BENCH_RUNNER_CONCURRENT_H

#include "bench_runner_common.h"

/**
 * @brief Run the concurrent read-only lookup benchmark once.
 */
int bench_run_concurrent_lookup(
    const bench_plan *plan,
    bench_run_result *result
);

/**
 * @brief Run the concurrent workload benchmark once.
 */
int bench_run_concurrent_workload(
    const bench_plan *plan,
    bench_run_result *result
);

#endif /* BENCH_RUNNER_CONCURRENT_H */
