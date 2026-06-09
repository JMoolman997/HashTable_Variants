/**
 * @file    bench_fixture.h
 * @brief   Generated benchmark data owned by one repetition.
 */

#ifndef BENCH_FIXTURE_H
#define BENCH_FIXTURE_H

#include <stddef.h>

#include "bench_plan.h"
#include "bench_trace.h"
#include "ht_types.h"

typedef struct {
    ht_key_t *initial_keys;
    size_t    initial_count;

    bench_op *warmup_ops;
    size_t    warmup_op_count;
    bench_op *timed_ops;
    size_t    timed_op_count;

    bench_op **thread_ops;
    size_t    *thread_op_counts;
    bench_op **warmup_thread_ops;
    size_t    *warmup_thread_op_counts;
    size_t    thread_count;
} bench_fixture;

int bench_fixture_build(
    const bench_plan *plan,
    bench_fixture    *fixture
);

void bench_fixture_destroy(
    bench_fixture *fixture,
    const bench_plan *plan
);

#endif /* BENCH_FIXTURE_H */
