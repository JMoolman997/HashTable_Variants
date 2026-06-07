#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ht.h"

#define TEST_CHECK(cond, ...)                                                 \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "[%s][%s] ", impl_name, __func__);                \
            fprintf(stderr, __VA_ARGS__);                                     \
            fprintf(stderr, "\n");                                            \
            goto fail;                                                        \
        }                                                                     \
    } while (0)

ht_config test_make_config(
    ht_impl     impl,
    ht_rsz_mode resize_mode,
    size_t      init_capacity
);

ht_val_t test_value_for_key(
    ht_key_t key
);

double test_abs_double(
    double x
);

uint64_t test_constant_hash(
    ht_key_t key,
    uint64_t seed
);

int test_insert_range(
    ht_map   *map,
    ht_key_t  start_key,
    size_t    count
);

int test_verify_range(
    const ht_map *map,
    ht_key_t     start_key,
    size_t       count
);

#endif
