/**
 * @file    bench_runner_resize.h
 * @brief   Resize-aware benchmark runners.
 *
 * Declares the resize-family benchmark entry points that measure table growth
 * and workload behavior without disturbing the legacy steady-state runners.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#ifndef BENCH_RUNNER_RESIZE_H
#define BENCH_RUNNER_RESIZE_H

#include "bench_runner_common.h"

/**
 * @brief Run the resize-aware insert-build benchmark once.
 */
int bench_run_resize_build(
    const bench_plan *plan,
    bench_run_result *result
);

/**
 * @brief Run the resize-aware successful lookup benchmark once.
 */
int bench_run_resize_lookup_hit(
    const bench_plan *plan,
    bench_run_result *result
);

/**
 * @brief Run the resize-aware missing lookup benchmark once.
 */
int bench_run_resize_lookup_miss(
    const bench_plan *plan,
    bench_run_result *result
);

/**
 * @brief Run the resize-aware erase-existing benchmark once.
 */
int bench_run_resize_erase_existing(
    const bench_plan *plan,
    bench_run_result *result
);

/**
 * @brief Run the resize-aware mixed workload benchmark once.
 */
int bench_run_resize_workload(
    const bench_plan *plan,
    bench_run_result *result
);

#endif /* BENCH_RUNNER_RESIZE_H */
