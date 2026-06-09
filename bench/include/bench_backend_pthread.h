/**
 * @file    bench_backend_pthread.h
 * @brief   pthread benchmark backend.
 */

#ifndef BENCH_BACKEND_PTHREAD_H
#define BENCH_BACKEND_PTHREAD_H

#include "bench_fixture.h"
#include "bench_metric.h"
#include "bench_plan.h"

int bench_backend_pthread_run(
    const bench_plan *plan,
    const bench_fixture *fixture,
    bench_sample *sample
);

#endif /* BENCH_BACKEND_PTHREAD_H */
