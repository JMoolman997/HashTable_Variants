/**
 * @file    bench_random.h
 * @brief   Deterministic pseudo-random helpers for benchmarks.
 *
 * Provides the small counter-mixing primitive shared by dataset and trace
 * generation.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#ifndef BENCH_RANDOM_H
#define BENCH_RANDOM_H

#include <stdint.h>

/**
 * @brief Mix a 64-bit counter into a deterministic pseudo-random value.
 */
static inline uint64_t bench_splitmix64(
    uint64_t x
) {
    x += 0x9e3779b97f4a7c15ULL;
    /* The avalanche steps make adjacent counters look unrelated. */
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= x >> 31;

    return x;
}

#endif /* BENCH_RANDOM_H */
