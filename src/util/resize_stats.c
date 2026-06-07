/**
 * @file resize_stats.c
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#define _POSIX_C_SOURCE 200809L

#include "resize_stats.h"

#if HT_ENABLE_RESIZE_INSTRUMENTATION
#include <time.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

uint64_t ht_resize_instrumentation_now_ns(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        return 0ULL;
    }

    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
#endif /* HT_ENABLE_RESIZE_INSTRUMENTATION */
