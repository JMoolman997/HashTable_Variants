/**
 * @file    bench_time.h
 * @brief   Monotonic timing helpers for the benchmark harness.
 *
 * Exposes a nanosecond-resolution monotonic clock helper used to measure
 * benchmark elapsed time.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#ifndef BENCH_TIME_H
#define BENCH_TIME_H

#include <stdint.h>

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Read the current monotonic time in nanoseconds.
 *
 * @return The current monotonic timestamp in nanoseconds, or `0` if the
 *         platform clock query fails.
 */
uint64_t bench_now_ns(
    void
);

#endif /* BENCH_TIME_H */
