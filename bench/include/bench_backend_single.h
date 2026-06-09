/**
 * @file    bench_backend_single.h
 * @brief   Single-thread benchmark backend.
 */

#ifndef BENCH_BACKEND_SINGLE_H
#define BENCH_BACKEND_SINGLE_H

#include "bench_fixture.h"
#include "bench_metric.h"
#include "bench_plan.h"

int bench_backend_single_run(
    const bench_plan *plan,
    const bench_fixture *fixture,
    bench_sample *sample
);

#endif /* BENCH_BACKEND_SINGLE_H */
