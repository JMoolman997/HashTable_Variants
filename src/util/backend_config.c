/**
 * @file backend_config.c
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#include <math.h>

#include "backend_config.h"

#include "capacity_util.h"
#include "hash_util.h"

ht_result ht_backend_config_resolve(
    const ht_config *cfg,
    size_t default_initial_capacity,
    size_t default_min_capacity,
    double default_max_load_factor,
    double default_min_load_factor,
    ht_backend_config *out
) {
    size_t capacity;
    size_t min_capacity;
    ht_result rc;

    if (cfg == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    if (!ht_resize_mode_is_valid(cfg->rsz_mode)) {
        return HT_ERR_INVALID;
    }
    if (!isfinite(cfg->max_load_factor) ||
        !isfinite(cfg->min_load_factor) ||
        cfg->max_load_factor < 0.0 ||
        cfg->min_load_factor < 0.0) {
        return HT_ERR_INVALID;
    }

    capacity = (cfg->init_capacity > 0u)
        ? cfg->init_capacity
        : default_initial_capacity;
    min_capacity = (cfg->min_capacity > 0u)
        ? cfg->min_capacity
        : default_min_capacity;

    rc = ht_checked_next_pow2(capacity, &capacity);
    if (rc != HT_OK) {
        return rc;
    }
    rc = ht_checked_next_pow2(min_capacity, &min_capacity);
    if (rc != HT_OK) {
        return rc;
    }

    if (capacity < min_capacity) {
        capacity = min_capacity;
    }

    out->capacity = capacity;
    out->min_capacity = min_capacity;
    out->max_load_factor = (cfg->max_load_factor > 0.0)
        ? cfg->max_load_factor
        : default_max_load_factor;
    out->min_load_factor = (cfg->min_load_factor > 0.0)
        ? cfg->min_load_factor
        : default_min_load_factor;
    out->resize_mode = cfg->rsz_mode;
    out->hash_fn = (cfg->hash_fn != NULL) ? cfg->hash_fn : default_hash;
    out->hash_seed = cfg->hash_seed;
    out->thread_count = (cfg->thread_count > 0u) ? cfg->thread_count : 1u;
    out->collect_stats = cfg->collect_stats;

    if (!isfinite(out->max_load_factor) ||
        !isfinite(out->min_load_factor) ||
        out->max_load_factor <= 0.0 ||
        out->min_load_factor < 0.0 ||
        out->min_load_factor >= out->max_load_factor) {
        return HT_ERR_INVALID;
    }

    return HT_OK;
}
