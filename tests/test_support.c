#include "test_support.h"

ht_config test_make_config(
    ht_impl     impl,
    ht_rsz_mode resize_mode,
    size_t      init_capacity
) {
    ht_config cfg;

    if (resize_mode == HT_RESIZE_NONE) {
        cfg = ht_config_fixed(impl, init_capacity);
    } else if (resize_mode == HT_RESIZE_GROW_SHRINK) {
        cfg = ht_config_resizing(impl, init_capacity);
    } else {
        cfg = ht_config_default(impl);
        cfg.init_capacity = init_capacity;
        cfg.min_capacity  = init_capacity;
        cfg.rsz_mode      = resize_mode;
    }

    cfg.max_load_factor = 0.70;
    cfg.min_load_factor = 0.20;
    cfg.collect_stats   = 1;

    return cfg;
}

ht_val_t test_value_for_key(
    ht_key_t key
) {
    return (key * 3ULL) + 7ULL;
}

double test_abs_double(
    double x
) {
    return (x < 0.0) ? -x : x;
}

uint64_t test_constant_hash(
    ht_key_t key,
    uint64_t seed
) {
    (void)key;
    (void)seed;

    return 0;
}

int test_insert_range(
    ht_map   *map,
    ht_key_t  start_key,
    size_t    count
) {
    size_t i;

    if (map == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (ht_insert(
                map,
                start_key + (ht_key_t)i,
                test_value_for_key(start_key + (ht_key_t)i)
            ) != HT_OK) {
            return -1;
        }
    }

    return 0;
}

int test_verify_range(
    const ht_map *map,
    ht_key_t     start_key,
    size_t       count
) {
    ht_val_t value;
    size_t   i;

    if (map == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (ht_get(map, start_key + (ht_key_t)i, &value) != HT_OK) {
            return -1;
        }

        if (value != test_value_for_key(start_key + (ht_key_t)i)) {
            return -1;
        }
    }

    return 0;
}
