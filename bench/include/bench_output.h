/**
 * @file    bench_output.h
 * @brief   Benchmark result output helpers.
 */

#ifndef BENCH_OUTPUT_H
#define BENCH_OUTPUT_H

#include <stdio.h>

#include "bench_metric.h"
#include "bench_plan.h"

void bench_output_write_canonical_csv_header(
    FILE *stream
);

void bench_output_write_canonical_csv_sample(
    FILE *stream,
    const bench_plan *plan,
    const bench_sample *sample
);

void bench_output_write_text_sample(
    FILE *stream,
    const bench_plan *plan,
    const bench_sample *sample
);

#endif /* BENCH_OUTPUT_H */
