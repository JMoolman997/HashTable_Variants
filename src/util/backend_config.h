/**
 * @file backend_config.h
 * @brief Backend configuration normalization helpers.
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#ifndef HT_UTIL_BACKEND_CONFIG_H
#define HT_UTIL_BACKEND_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

#define DEFAULT_INITIAL_CAPACITY 16u
#define DEFAULT_MIN_CAPACITY     16u
#define DEFAULT_MAX_LOAD         0.85
#define DEFAULT_MIN_LOAD         0.10

typedef struct {
    size_t capacity;
    size_t min_capacity;
    double max_load_factor;
    double min_load_factor;
    ht_rsz_mode resize_mode;
    ht_hash_fn hash_fn;
    uint64_t hash_seed;
    size_t thread_count;
    int collect_stats;
} ht_backend_config;

ht_result ht_backend_config_resolve(
    const ht_config *cfg,
    size_t default_initial_capacity,
    size_t default_min_capacity,
    double default_max_load_factor,
    double default_min_load_factor,
    ht_backend_config *out
);

#endif /* HT_UTIL_BACKEND_CONFIG_H */
