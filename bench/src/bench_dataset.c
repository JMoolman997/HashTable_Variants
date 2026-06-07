/**
 * @file    bench_dataset.c
 * @brief   Deterministic benchmark dataset generation helpers.
 *
 * Implements unique key generation from a fixed seed for repeatable
 * benchmarks.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bench_dataset.h"
#include "bench_random.h"

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Compare two keys for sorting or binary search.
 *
 * @param lhs Pointer to the left-hand key.
 * @param rhs Pointer to the right-hand key.
 *
 * @return A negative value if `lhs < rhs`, zero if equal, or a positive
 *         value if `lhs > rhs`.
 */
static int bench_compare_keys(
    const void *lhs,
    const void *rhs
);

/**
 * @brief Return non-zero when a key appears in a sorted key array.
 *
 * @param key Key to search for.
 * @param keys Sorted key array to inspect.
 * @param count Number of keys in `keys`.
 *
 * @return Non-zero if `key` is present, otherwise zero.
 */
static int bench_key_in_sorted_keys(
    ht_key_t        key,
    const ht_key_t *keys,
    size_t          count
);

int bench_dataset_generate_unique_keys(
    size_t   count,
    uint64_t seed,
    ht_key_t **keys_out
) {
    ht_key_t *keys;
    size_t    i;

    if (keys_out == NULL) {
        return -1;
    }

    *keys_out = NULL;

    if (count == 0) {
        return 0;
    }

    if (count > SIZE_MAX / sizeof(*keys)) {
        return -1;
    }

    keys = malloc(count * sizeof(*keys));
    if (keys == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        keys[i] = bench_splitmix64(seed + (uint64_t)i);
    } /* SplitMix64 turns the counter into a deterministic key stream. */

    *keys_out = keys;
    return 0;
}

int bench_dataset_generate_populated_keys(
    size_t   capacity,
    double   target_alpha,
    uint64_t seed,
    size_t   *live_count_out,
    ht_key_t **keys_out
) {
    size_t live_count;

    if (live_count_out == NULL || keys_out == NULL) {
        return -1;
    }

    *live_count_out = 0;
    *keys_out       = NULL;

    live_count = (size_t)(target_alpha * (double)capacity);
    if (live_count == 0) {
        return 0;
    } /* Tiny capacities can legitimately round down to no live keys. */

    if (bench_dataset_generate_unique_keys(live_count, seed, keys_out) != 0) {
        return -1;
    }

    *live_count_out = live_count;
    return 0;
}

int bench_dataset_generate_missing_keys(
    const ht_key_t *existing_keys,
    size_t          existing_count,
    uint64_t        seed,
    size_t          missing_count,
    ht_key_t        **keys_out
) {
    ht_key_t *sorted_existing;
    ht_key_t *missing_keys;
    ht_key_t  candidate;
    size_t    generated;
    size_t    i;

    if (keys_out == NULL) {
        return -1;
    }

    *keys_out = NULL;

    if (missing_count == 0) {
        return 0;
    }

    if (existing_keys == NULL && existing_count > 0) {
        return -1;
    }

    sorted_existing = NULL;
    if (existing_count > 0) {
        if (existing_count > SIZE_MAX / sizeof(*sorted_existing)) {
            return -1;
        }

        sorted_existing = malloc(existing_count * sizeof(*sorted_existing));
        if (sorted_existing == NULL) {
            return -1;
        }

        memcpy(
            sorted_existing,
            existing_keys,
            existing_count * sizeof(*sorted_existing)
        );
        qsort(
            sorted_existing,
            existing_count,
            sizeof(*sorted_existing),
            bench_compare_keys
        );
    } /* Sorting lets missing-key generation reject collisions cheaply. */

    if (missing_count > SIZE_MAX / sizeof(*missing_keys)) {
        free(sorted_existing);
        return -1;
    }

    missing_keys = malloc(missing_count * sizeof(*missing_keys));
    if (missing_keys == NULL) {
        free(sorted_existing);
        return -1;
    }

    generated = 0;
    i         = 0;

    /* SplitMix64 is a permutation, so this counter stream has no duplicates. */
    while (generated < missing_count) {
        candidate = bench_splitmix64(
            seed + (uint64_t)existing_count + (uint64_t)i
        );
        i++;

        if (bench_key_in_sorted_keys(candidate, sorted_existing,
                existing_count) != 0) {
            continue;
        } /* Skip keys that would accidentally become lookup hits. */

        missing_keys[generated] = candidate;
        generated++;
    }

    free(sorted_existing);
    *keys_out = missing_keys;
    return 0;
}

int bench_dataset_shuffle_keys(
    const ht_key_t *input_keys,
    size_t          key_count,
    uint64_t        seed,
    ht_key_t        **keys_out
) {
    ht_key_t *shuffled_keys;
    ht_key_t  tmp;
    size_t    i;
    size_t    j;

    if (keys_out == NULL) {
        return -1;
    }

    *keys_out = NULL;

    if (key_count == 0) {
        return 0;
    }

    if (input_keys == NULL) {
        return -1;
    }

    if (key_count > SIZE_MAX / sizeof(*shuffled_keys)) {
        return -1;
    }

    shuffled_keys = malloc(key_count * sizeof(*shuffled_keys));
    if (shuffled_keys == NULL) {
        return -1;
    }

    memcpy(
        shuffled_keys,
        input_keys,
        key_count * sizeof(*shuffled_keys)
    );

    for (i = key_count - 1; i > 0; i--) {
        j = (size_t)(bench_splitmix64(seed + (uint64_t)i) % (uint64_t)(i + 1));

        tmp              = shuffled_keys[i];
        shuffled_keys[i] = shuffled_keys[j];
        shuffled_keys[j] = tmp;
    } /* Fisher-Yates gives a repeatable deletion order. */

    *keys_out = shuffled_keys;
    return 0;
}

static int bench_compare_keys(
    const void *lhs,
    const void *rhs
) {
    ht_key_t left;
    ht_key_t right;

    left  = *(const ht_key_t *)lhs;
    right = *(const ht_key_t *)rhs;

    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }

    return 0;
}

static int bench_key_in_sorted_keys(
    ht_key_t        key,
    const ht_key_t *keys,
    size_t          count
) {
    if (keys == NULL || count == 0) {
        return 0;
    }

    return (bsearch(
                &key,
                keys,
                count,
                sizeof(*keys),
                bench_compare_keys
            ) != NULL);
}
