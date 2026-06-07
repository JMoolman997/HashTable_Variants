/**
 * @file    bench_output.h
 * @brief   CSV output helpers for benchmark results.
 *
 * Declares the CSV header and per-row emitters used by the benchmark
 * harness.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#ifndef BENCH_OUTPUT_H
#define BENCH_OUTPUT_H

#include <stdio.h>

#include "bench_config.h"

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Write the raw benchmark CSV header line.
 *
 * @param stream Output stream that receives the header. `NULL` is ignored.
 */
void bench_output_write_csv_header(
    FILE *stream
);

/**
 * @brief Write one raw benchmark result row in CSV format.
 *
 * @param stream Output stream that receives the row. `NULL` is ignored.
 * @param result Per-repetition benchmark result to serialize.
 * @param repetition Zero-based repetition number to emit in the row.
 */
void bench_output_write_csv_row(
    FILE *stream,
    const bench_result *result,
    size_t repetition
);

/**
 * @brief Write the resize benchmark CSV header line.
 *
 * @param stream Output stream that receives the header. `NULL` is ignored.
 */
void bench_output_write_resize_csv_header(
    FILE *stream
);

/**
 * @brief Write one resize benchmark result row in CSV format.
 *
 * @param stream Output stream that receives the row. `NULL` is ignored.
 * @param result Per-repetition resize benchmark result to serialize.
 * @param repetition Zero-based repetition number to emit in the row.
 */
void bench_output_write_resize_csv_row(
    FILE *stream,
    const bench_run_result *result,
    size_t repetition
);

/**
 * @brief Write the concurrent benchmark CSV header line.
 *
 * @param stream Output stream that receives the header. `NULL` is ignored.
 */
void bench_output_write_concurrent_csv_header(
    FILE *stream
);

/**
 * @brief Write one concurrent benchmark result row in CSV format.
 *
 * @param stream Output stream that receives the row. `NULL` is ignored.
 * @param result Per-repetition concurrent benchmark result to serialize.
 * @param repetition Zero-based repetition number to emit in the row.
 */
void bench_output_write_concurrent_csv_row(
    FILE *stream,
    const bench_run_result *result,
    size_t repetition
);

#endif /* BENCH_OUTPUT_H */
