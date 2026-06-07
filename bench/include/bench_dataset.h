/**
 * @file    bench_dataset.h
 * @brief   Deterministic benchmark dataset generation helpers.
 *
 * Declares helpers for generating deterministic unique key datasets used by
 * benchmark runners.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#ifndef BENCH_DATASET_H
#define BENCH_DATASET_H

#include <stddef.h>

#include "ht_types.h"

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Generate a deterministic array of unique keys.
 *
 * @param count Number of keys to generate.
 * @param seed Seed used by the deterministic generator.
 * @param keys_out Output pointer that receives the allocated key array.
 *
 * @return `0` on success, or `-1` if validation or allocation fails.
 */
int bench_dataset_generate_unique_keys(
    size_t   count,
    uint64_t seed,
    ht_key_t **keys_out
);

/**
 * @brief Generate the live-key set used to prepopulate a table.
 *
 * @param capacity Target table capacity used with `target_alpha`.
 * @param target_alpha Load factor used to derive the live key count.
 * @param seed Seed used by the deterministic generator.
 * @param live_count_out Output location that receives the derived live count.
 * @param keys_out Output pointer that receives the allocated key array.
 *
 * @return `0` on success, or `-1` if validation or allocation fails.
 */
int bench_dataset_generate_populated_keys(
    size_t   capacity,
    double   target_alpha,
    uint64_t seed,
    size_t   *live_count_out,
    ht_key_t **keys_out
);

/**
 * @brief Generate keys guaranteed to be absent and unique.
 *
 * @param existing_keys Existing keys that must be avoided.
 * @param existing_count Number of keys in `existing_keys`.
 * @param seed Seed used by the deterministic generator.
 * @param missing_count Number of absent keys to generate.
 * @param keys_out Output pointer that receives the allocated key array.
 *
 * @return `0` on success, or `-1` if validation or allocation fails.
 */
int bench_dataset_generate_missing_keys(
    const ht_key_t *existing_keys,
    size_t          existing_count,
    uint64_t        seed,
    size_t          missing_count,
    ht_key_t        **keys_out
);

/**
 * @brief Return a shuffled copy of an existing key array.
 *
 * @param input_keys Input keys to copy and shuffle.
 * @param key_count Number of keys in `input_keys`.
 * @param seed Seed used by the deterministic shuffler.
 * @param keys_out Output pointer that receives the allocated shuffled array.
 *
 * @return `0` on success, or `-1` if validation or allocation fails.
 */
int bench_dataset_shuffle_keys(
    const ht_key_t *input_keys,
    size_t          key_count,
    uint64_t        seed,
    ht_key_t        **keys_out
);

#endif /* BENCH_DATASET_H */
