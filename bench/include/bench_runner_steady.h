/**
 * @file    bench_runner_steady.h
 * @brief   Steady-state benchmark runners.
 *
 * Declares the legacy single-thread benchmark entry points that preserve the
 * current CSV schema and execution semantics.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#ifndef BENCH_RUNNER_STEADY_H
#define BENCH_RUNNER_STEADY_H

#include "bench_runner_common.h"

/**
 * @brief Run the steady-state insert-build benchmark once.
 */
int bench_run_insert_build(
    const bench_plan *plan,
    bench_result     *result
);

/**
 * @brief Run the steady-state successful lookup benchmark once.
 */
int bench_run_lookup_hit(
    const bench_plan *plan,
    bench_result     *result
);

/**
 * @brief Run the steady-state missing lookup benchmark once.
 */
int bench_run_lookup_miss(
    const bench_plan *plan,
    bench_result     *result
);

/**
 * @brief Run the steady-state erase-existing benchmark once.
 */
int bench_run_erase_existing(
    const bench_plan *plan,
    bench_result     *result
);

/**
 * @brief Run the steady-state mixed workload benchmark once.
 */
int bench_run_workload(
    const bench_plan *plan,
    bench_result     *result
);

#endif /* BENCH_RUNNER_STEADY_H */
