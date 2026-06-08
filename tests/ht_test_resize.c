#include "test_support.h"

int test_full_table_no_resize(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    size_t      limit;

    if (impl == HT_IMPL_LF_HOPSCOTCH) {
        return 0;
    }

    cfg = test_make_config(impl, HT_RESIZE_NONE, 8);
    cfg.max_load_factor = 0.50;
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    limit = ht_capacity(map) / 2u;
    TEST_CHECK(limit > 0u, "unexpected zero fixed-capacity limit");
    TEST_CHECK(test_insert_range(map, 800, limit) == 0, "expected initial inserts to succeed");
    TEST_CHECK(
        ht_insert(map, 800 + (ht_key_t)limit, test_value_for_key(800 + (ht_key_t)limit)) == HT_ERR_FULL,
        "expected HT_ERR_FULL on insert past fixed-capacity limit"
    );
    TEST_CHECK(ht_size(map) == limit, "size changed after full-table insert");
    TEST_CHECK(test_verify_range(map, 800, limit) == 0, "existing entries corrupted");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_backshift_full_table_remove_integrity(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config cfg;
    ht_map   *map = NULL;
    ht_val_t  value;
    size_t    i;

    if (impl != HT_IMPL_BACKSHIFT) {
        return 0;
    }

    cfg = test_make_config(impl, HT_RESIZE_NONE, 8);
    cfg.max_load_factor = 1.0;
    cfg.hash_fn = test_constant_hash;
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    for (i = 0; i < ht_capacity(map); i++) {
        TEST_CHECK(
            ht_insert(map, 1000 + (ht_key_t)i, test_value_for_key(1000 + (ht_key_t)i)) == HT_OK,
            "fill insert failed at index %zu",
            i
        );
    }

    TEST_CHECK(ht_remove(map, 1003) == HT_OK, "full-table remove failed");
    TEST_CHECK(ht_size(map) == ht_capacity(map) - 1u, "unexpected size after remove");
    TEST_CHECK(ht_get(map, 1003, &value) == HT_ERR_NOT_FOUND, "removed key still present");

    for (i = 0; i < ht_capacity(map); i++) {
        ht_key_t key = 1000 + (ht_key_t)i;

        if (key == 1003) {
            continue;
        }

        TEST_CHECK(ht_get(map, key, &value) == HT_OK, "survivor missing at index %zu", i);
        TEST_CHECK(value == test_value_for_key(key), "wrong survivor value");
    }

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_adv_full_table_insert_failure_integrity(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config cfg;
    ht_map   *map = NULL;
    ht_val_t  value;
    size_t    capacity;
    size_t    i;

    if (impl != HT_IMPL_ADV_OPEN_ADDRESSING) {
        return 0;
    }

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    cfg.max_load_factor = 1.0;
    cfg.hash_fn = test_constant_hash;
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    capacity = ht_capacity(map);

    for (i = 0; i < capacity; i++) {
        TEST_CHECK(
            ht_insert(map, 2000 + (ht_key_t)i, test_value_for_key(2000 + (ht_key_t)i)) == HT_OK,
            "fill insert failed at index %zu",
            i
        );
    }

    TEST_CHECK(
        ht_insert(map, 9999, test_value_for_key(9999)) == HT_ERR_FULL,
        "full-table insert did not fail"
    );
    TEST_CHECK(ht_size(map) == capacity, "failed insert changed size");
    TEST_CHECK(ht_get(map, 9999, &value) == HT_ERR_NOT_FOUND, "failed insert key became visible");

    for (i = 0; i < capacity; i++) {
        ht_key_t key = 2000 + (ht_key_t)i;

        TEST_CHECK(ht_get(map, key, &value) == HT_OK, "existing key missing at index %zu", i);
        TEST_CHECK(value == test_value_for_key(key), "existing key value changed");
    }

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_reuse_deleted_slot_no_resize(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_val_t    value;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 8);
    cfg.max_load_factor = 0.50;
    cfg.hash_fn         = test_constant_hash;
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(test_insert_range(map, 900, 4) == 0, "expected first 4 inserts to succeed");
    TEST_CHECK(ht_remove(map, 901) == HT_OK, "remove failed");
    TEST_CHECK(
        ht_insert(map, 904, test_value_for_key(904)) == HT_OK,
        "expected insert to reuse deleted slot"
    );
    TEST_CHECK(ht_size(map) == 4, "unexpected size after reuse insert");
    TEST_CHECK(ht_get(map, 901, &value) == HT_ERR_NOT_FOUND, "removed key reappeared");
    TEST_CHECK(ht_get(map, 904, &value) == HT_OK, "reused-slot key missing");
    TEST_CHECK(value == test_value_for_key(904), "wrong value after reuse insert");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_resize_grow_integrity(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    size_t      initial_capacity;
    size_t      grown_capacity;

    if (impl == HT_IMPL_LF_HOPSCOTCH) {
        return 0;
    }

    cfg = test_make_config(impl, HT_RESIZE_GROW, 8);
    cfg.max_load_factor = 0.50;
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    initial_capacity = ht_capacity(map);
    TEST_CHECK(test_insert_range(map, 100000, 200) == 0, "insert failed during grow test");

    grown_capacity = ht_capacity(map);
    TEST_CHECK(grown_capacity > initial_capacity, "capacity did not grow");
    TEST_CHECK(ht_size(map) == 200, "unexpected size after growth");
    TEST_CHECK(test_verify_range(map, 100000, 200) == 0, "data lost after growth");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_resize_shrink_integrity(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    size_t      grown_capacity;
    size_t      shrunken_capacity;
    size_t      i;
    ht_val_t    value;

    if (impl == HT_IMPL_LF_HOPSCOTCH) {
        return 0;
    }

    cfg = test_make_config(impl, HT_RESIZE_GROW_SHRINK, 8);
    cfg.max_load_factor = 0.75;
    cfg.min_load_factor = 0.25;
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(test_insert_range(map, 200000, 64) == 0, "insert failed");

    grown_capacity = ht_capacity(map);
    TEST_CHECK(grown_capacity > 8, "capacity did not grow before shrink test");

    for (i = 0; i < 60; i++) {
        TEST_CHECK(ht_remove(map, 200000 + (ht_key_t)i) == HT_OK, "remove failed");
    }

    shrunken_capacity = ht_capacity(map);
    if (impl == HT_IMPL_P_OPEN_ADDRESSING) {
        TEST_CHECK(
            shrunken_capacity == grown_capacity,
            "concurrent backend should not shrink capacity"
        );
    } else {
        TEST_CHECK(shrunken_capacity < grown_capacity, "capacity did not shrink");
    }
    TEST_CHECK(ht_size(map) == 4, "unexpected final size after shrink");

    for (i = 60; i < 64; i++) {
        TEST_CHECK(ht_get(map, 200000 + (ht_key_t)i, &value) == HT_OK, "survivor missing");
        TEST_CHECK(
            value == test_value_for_key(200000 + (ht_key_t)i),
            "wrong survivor value"
        );
    }

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_collision_heavy_case(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_val_t    value;
    size_t      i;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 128);
    cfg.hash_fn = test_constant_hash;
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");

    for (i = 0; i < 40; i++) {
        TEST_CHECK(
            ht_insert(map, 300000 + (ht_key_t)i, test_value_for_key(300000 + (ht_key_t)i)) == HT_OK,
            "collision insert failed at index %zu",
            i
        );
    }

    for (i = 0; i < 40; i++) {
        TEST_CHECK(
            ht_get(map, 300000 + (ht_key_t)i, &value) == HT_OK,
            "collision lookup failed at index %zu",
            i
        );
        TEST_CHECK(
            value == test_value_for_key(300000 + (ht_key_t)i),
            "collision lookup returned wrong value"
        );
    }

    for (i = 0; i < 40; i += 3) {
        TEST_CHECK(
            ht_remove(map, 300000 + (ht_key_t)i) == HT_OK,
            "collision remove failed at index %zu",
            i
        );
    }

    for (i = 0; i < 40; i++) {
        if (i % 3 == 0) {
            TEST_CHECK(
                ht_get(map, 300000 + (ht_key_t)i, &value) == HT_ERR_NOT_FOUND,
                "removed collision key still present"
            );
        } else {
            TEST_CHECK(
                ht_get(map, 300000 + (ht_key_t)i, &value) == HT_OK,
                "surviving collision key missing"
            );
        }
    }

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}
