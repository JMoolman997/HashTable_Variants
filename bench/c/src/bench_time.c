/**
 * @file    bench_time.c
 * @brief   Monotonic timing helpers for the benchmark harness.
 *
 * Implements nanosecond-resolution monotonic time retrieval.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <time.h>

#include "bench_time.h"

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif /* Some platforms expose only the portable monotonic clock. */

/**
 * @brief Read the current monotonic time in nanoseconds.
 *
 * @return The current monotonic timestamp in nanoseconds, or `0` if the
 *         clock query fails.
 */
uint64_t bench_now_ns(
    void
) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        return 0;
    } /* Zero is reserved as the caller-visible clock failure sentinel. */

    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
