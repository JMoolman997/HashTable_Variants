#include <string.h>

#include "test_support.h"

int test_create_destroy(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(ht_size(map) == 0, "expected empty map");
    TEST_CHECK(ht_capacity(map) >= 16, "unexpected initial capacity");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_insert_one_get_one(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_val_t    value;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(
        ht_insert(map, 42, test_value_for_key(42)) == HT_OK,
        "insert failed"
    );
    TEST_CHECK(ht_size(map) == 1, "expected size 1");
    TEST_CHECK(ht_get(map, 42, &value) == HT_OK, "lookup failed");
    TEST_CHECK(value == test_value_for_key(42), "wrong value returned");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_duplicate_insert_rejected(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_val_t    value;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(ht_insert(map, 42, 100) == HT_OK, "first insert failed");
    TEST_CHECK(
        ht_insert(map, 42, 200) == HT_ERR_EXISTS,
        "duplicate insert did not return HT_ERR_EXISTS"
    );
    TEST_CHECK(ht_size(map) == 1, "duplicate insert changed size");
    TEST_CHECK(ht_get(map, 42, &value) == HT_OK, "lookup failed");
    TEST_CHECK(value == 100, "duplicate insert overwrote existing value");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_insert_multiple_get_all(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 256);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(test_insert_range(map, 1000, 64) == 0, "bulk insert failed");
    TEST_CHECK(ht_size(map) == 64, "expected size 64");
    TEST_CHECK(test_verify_range(map, 1000, 64) == 0, "bulk lookup failed");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_lookup_missing(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_val_t    value;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 32);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(
        ht_get(map, 9999, &value) == HT_ERR_NOT_FOUND,
        "missing lookup did not return HT_ERR_NOT_FOUND"
    );

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_remove_existing(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_val_t    value;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 64);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(ht_insert(map, 11, test_value_for_key(11)) == HT_OK, "insert failed");
    TEST_CHECK(ht_remove(map, 11) == HT_OK, "remove failed");
    TEST_CHECK(ht_size(map) == 0, "expected empty map after remove");
    TEST_CHECK(
        ht_get(map, 11, &value) == HT_ERR_NOT_FOUND,
        "removed key still present"
    );

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_remove_missing(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 32);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(
        ht_remove(map, 12345) == HT_ERR_NOT_FOUND,
        "missing remove did not return HT_ERR_NOT_FOUND"
    );

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_size_capacity_load_factor_sanity(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    size_t      capacity;
    double      expected;
    double      actual;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 32);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(test_insert_range(map, 200, 8) == 0, "insert failed");

    capacity = ht_capacity(map);
    expected = (double)ht_size(map) / (double)capacity;
    actual   = ht_load_factor(map);

    TEST_CHECK(ht_size(map) == 8, "expected size 8");
    TEST_CHECK(capacity >= 32, "unexpected capacity");
    TEST_CHECK(
        test_abs_double(actual - expected) < 1e-12,
        "unexpected load factor"
    );

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_many_inserts_then_gets(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;

    cfg = test_make_config(
        impl,
        impl == HT_IMPL_LF_HOPSCOTCH ? HT_RESIZE_NONE : HT_RESIZE_GROW,
        impl == HT_IMPL_LF_HOPSCOTCH ? 1024 : 8
    );
    cfg.max_load_factor = impl == HT_IMPL_LF_HOPSCOTCH ? 0.95 : 0.50;
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(test_insert_range(map, 10000, 512) == 0, "bulk insert failed");
    TEST_CHECK(ht_size(map) == 512, "expected size 512");
    TEST_CHECK(test_verify_range(map, 10000, 512) == 0, "bulk verify failed");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_interleaved_ops(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_val_t    value;
    size_t      i;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 128);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");

    for (i = 0; i < 40; i++) {
        TEST_CHECK(
            ht_insert(map, (ht_key_t)i, test_value_for_key((ht_key_t)i)) == HT_OK,
            "insert failed at index %zu",
            i
        );
    }

    for (i = 0; i < 20; i++) {
        TEST_CHECK(ht_get(map, (ht_key_t)i, &value) == HT_OK, "lookup failed");
        TEST_CHECK(value == test_value_for_key((ht_key_t)i), "wrong value");
        TEST_CHECK(ht_remove(map, (ht_key_t)i) == HT_OK, "remove failed");
        TEST_CHECK(
            ht_get(map, (ht_key_t)i, &value) == HT_ERR_NOT_FOUND,
            "removed key still found"
        );
    }

    for (i = 40; i < 60; i++) {
        TEST_CHECK(
            ht_insert(map, (ht_key_t)i, test_value_for_key((ht_key_t)i)) == HT_OK,
            "reinsertion phase failed at index %zu",
            i
        );
    }

    TEST_CHECK(ht_size(map) == 40, "unexpected final size");
    TEST_CHECK(test_verify_range(map, 20, 40) == 0, "surviving keys incorrect");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_delete_then_reinsert(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_val_t    value;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 32);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(ht_insert(map, 77, 1111) == HT_OK, "first insert failed");
    TEST_CHECK(ht_remove(map, 77) == HT_OK, "remove failed");
    TEST_CHECK(ht_insert(map, 77, 2222) == HT_OK, "second insert failed");
    TEST_CHECK(ht_get(map, 77, &value) == HT_OK, "lookup failed");
    TEST_CHECK(value == 2222, "reinserted value not returned");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_stats_and_reset(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    ht_stats    stats;
    ht_val_t    value;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 64);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(ht_insert(map, 1, 10) == HT_OK, "insert 1 failed");
    TEST_CHECK(ht_insert(map, 2, 20) == HT_OK, "insert 2 failed");
    TEST_CHECK(ht_get(map, 1, &value) == HT_OK, "lookup hit failed");
    TEST_CHECK(ht_get(map, 99, &value) == HT_ERR_NOT_FOUND, "lookup miss failed");
    TEST_CHECK(ht_remove(map, 2) == HT_OK, "remove hit failed");
    TEST_CHECK(ht_remove(map, 2) == HT_ERR_NOT_FOUND, "remove miss failed");
    TEST_CHECK(ht_get_stats(map, &stats) == HT_OK, "get_stats failed");

    TEST_CHECK(stats.inserts == 2, "unexpected insert count: %" PRIu64, stats.inserts);
    TEST_CHECK(stats.lookups == 2, "unexpected lookup count: %" PRIu64, stats.lookups);
    TEST_CHECK(
        stats.lookup_misses == 1,
        "unexpected lookup miss count: %" PRIu64,
        stats.lookup_misses
    );
    TEST_CHECK(stats.removes == 2, "unexpected remove count: %" PRIu64, stats.removes);
    TEST_CHECK(
        stats.remove_misses == 1,
        "unexpected remove miss count: %" PRIu64,
        stats.remove_misses
    );
    TEST_CHECK(stats.bytes_used > 0, "bytes_used should be non-zero");

    TEST_CHECK(ht_reset_stats(map) == HT_OK, "reset_stats failed");
    TEST_CHECK(ht_get_stats(map, &stats) == HT_OK, "get_stats after reset failed");

    TEST_CHECK(stats.inserts == 0, "insert count not reset");
    TEST_CHECK(stats.lookups == 0, "lookup count not reset");
    TEST_CHECK(stats.lookup_misses == 0, "lookup miss count not reset");
    TEST_CHECK(stats.removes == 0, "remove count not reset");
    TEST_CHECK(stats.remove_misses == 0, "remove miss count not reset");
    TEST_CHECK(stats.bytes_used > 0, "bytes_used should remain tracked after reset");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_reserve_and_rehash(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config   cfg;
    ht_map     *map;
    size_t      reserved_capacity;
    size_t      rehashed_capacity;

    if (impl == HT_IMPL_LF_HOPSCOTCH) {
        return 0;
    }

    cfg = test_make_config(impl, HT_RESIZE_GROW, 16);
    map = ht_create(&cfg);

    TEST_CHECK(map != NULL, "ht_create returned NULL");
    TEST_CHECK(test_insert_range(map, 5000, 16) == 0, "insert failed");
    TEST_CHECK(ht_reserve(map, 128) == HT_OK, "reserve failed");

    reserved_capacity = ht_capacity(map);
    TEST_CHECK(reserved_capacity >= 128, "reserve did not grow capacity enough");
    TEST_CHECK(test_verify_range(map, 5000, 16) == 0, "data lost after reserve");

    TEST_CHECK(ht_rehash(map, 32) == HT_OK, "rehash failed");
    rehashed_capacity = ht_capacity(map);
    TEST_CHECK(rehashed_capacity >= 32, "rehash produced invalid capacity");
    TEST_CHECK(test_verify_range(map, 5000, 16) == 0, "data lost after rehash");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_config_helpers(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config cfg;

    cfg = ht_config_default(impl);
    TEST_CHECK(cfg.impl_kind == impl, "default helper set wrong impl");
    TEST_CHECK(cfg.init_capacity == 0, "default helper should leave init capacity to backend");
    TEST_CHECK(cfg.min_capacity == 0, "default helper should leave min capacity to backend");
    TEST_CHECK(cfg.max_load_factor == 0.0, "default helper should leave max load to backend");
    TEST_CHECK(cfg.min_load_factor == 0.0, "default helper should leave min load to backend");
    TEST_CHECK(cfg.rsz_mode == HT_RESIZE_GROW, "default helper should enable grow-only resize");
    TEST_CHECK(cfg.hash_fn == NULL, "default helper should use default hash");
    TEST_CHECK(cfg.hash_seed == 0, "default helper should use zero hash seed");
    TEST_CHECK(cfg.thread_count == 1, "default helper should use one thread");
    TEST_CHECK(cfg.collect_stats == 0, "default helper should disable stats");

    cfg = ht_config_fixed(impl, 32);
    TEST_CHECK(cfg.impl_kind == impl, "fixed helper set wrong impl");
    TEST_CHECK(cfg.init_capacity == 32, "fixed helper set wrong init capacity");
    TEST_CHECK(cfg.min_capacity == 32, "fixed helper set wrong min capacity");
    TEST_CHECK(cfg.rsz_mode == HT_RESIZE_NONE, "fixed helper should disable resize");

    cfg = ht_config_resizing(impl, 64);
    TEST_CHECK(cfg.impl_kind == impl, "resizing helper set wrong impl");
    TEST_CHECK(cfg.init_capacity == 64, "resizing helper set wrong init capacity");
    TEST_CHECK(cfg.min_capacity == 64, "resizing helper set wrong min capacity");
    TEST_CHECK(cfg.rsz_mode == HT_RESIZE_GROW_SHRINK, "resizing helper should grow and shrink");

    return 0;

fail:
    return -1;
}

int test_create_ex_diagnostics(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config cfg;
    ht_map   *map = NULL;
    ht_result rc;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    rc = ht_create_ex(&cfg, &map);
    TEST_CHECK(rc == HT_OK, "ht_create_ex failed: %s", ht_result_name(rc));
    TEST_CHECK(map != NULL, "ht_create_ex returned HT_OK with NULL map");
    ht_destroy(map);
    map = NULL;

    map = (ht_map *)1;
    rc = ht_create_ex(NULL, &map);
    TEST_CHECK(rc == HT_ERR_INVALID, "NULL config should be invalid");
    TEST_CHECK(map == NULL, "ht_create_ex should clear out on NULL config");

    cfg = test_make_config((ht_impl)9999, HT_RESIZE_NONE, 16);
    map = (ht_map *)1;
    rc = ht_create_ex(&cfg, &map);
    TEST_CHECK(rc == HT_ERR_UNSUPPORTED, "unknown impl should be unsupported");
    TEST_CHECK(map == NULL, "ht_create_ex should clear out on unsupported impl");

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    cfg.rsz_mode = (ht_rsz_mode)9999;
    map = (ht_map *)1;
    rc = ht_create_ex(&cfg, &map);
    TEST_CHECK(rc == HT_ERR_INVALID, "bad resize mode should be invalid");
    TEST_CHECK(map == NULL, "ht_create_ex should clear out on bad resize mode");

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    cfg.max_load_factor = -1.0;
    map = (ht_map *)1;
    rc = ht_create_ex(&cfg, &map);
    TEST_CHECK(rc == HT_ERR_INVALID, "bad max load factor should be invalid");
    TEST_CHECK(map == NULL, "ht_create_ex should clear out on bad max load factor");

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    cfg.max_load_factor = 0.50;
    cfg.min_load_factor = 0.50;
    map = (ht_map *)1;
    rc = ht_create_ex(&cfg, &map);
    TEST_CHECK(rc == HT_ERR_INVALID, "min load factor must be below max");
    TEST_CHECK(map == NULL, "ht_create_ex should clear out on bad load factors");

    cfg = test_make_config(impl, HT_RESIZE_NONE, 16);
    cfg.min_capacity = 32;
    map = (ht_map *)1;
    rc = ht_create_ex(&cfg, &map);
    TEST_CHECK(rc == HT_ERR_INVALID, "min capacity above init capacity should be invalid");
    TEST_CHECK(map == NULL, "ht_create_ex should clear out on bad capacity config");

    rc = ht_create_ex(&cfg, NULL);
    TEST_CHECK(rc == HT_ERR_INVALID, "NULL output pointer should be invalid");

    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_contains_helper(
    ht_impl     impl,
    const char *impl_name
) {
    ht_config cfg;
    ht_map   *map = NULL;
    ht_result rc;

    cfg = test_make_config(impl, HT_RESIZE_NONE, 64);
    rc = ht_create_ex(&cfg, &map);
    TEST_CHECK(rc == HT_OK, "ht_create_ex failed: %s", ht_result_name(rc));

    TEST_CHECK(ht_contains(NULL, 1) == 0, "NULL map should not contain keys");
    TEST_CHECK(ht_contains(map, 1) == 0, "empty map should not contain key");

    TEST_CHECK(ht_insert(map, 1, 100) == HT_OK, "insert failed");
    TEST_CHECK(ht_contains(map, 1) == 1, "inserted key not found");
    TEST_CHECK(ht_contains(map, 2) == 0, "missing key should not be found");
    TEST_CHECK(ht_remove(map, 1) == HT_OK, "remove failed");
    TEST_CHECK(ht_contains(map, 1) == 0, "removed key still found");

    ht_destroy(map);
    return 0;

fail:
    ht_destroy(map);
    return -1;
}

int test_name_helpers(
    ht_impl     impl,
    const char *impl_name
) {
    TEST_CHECK(strcmp(ht_impl_name(impl), impl_name) == 0, "wrong impl name");
    TEST_CHECK(strcmp(ht_impl_name((ht_impl)9999), "unknown") == 0, "bad unknown impl name");

    TEST_CHECK(strcmp(ht_result_name(HT_OK), "ok") == 0, "wrong HT_OK name");
    TEST_CHECK(strcmp(ht_result_name(HT_ERR_INVALID), "invalid") == 0, "wrong invalid name");
    TEST_CHECK(strcmp(ht_result_name(HT_ERR_UNSUPPORTED), "unsupported") == 0, "wrong unsupported name");
    TEST_CHECK(strcmp(ht_result_name((ht_result)9999), "unknown") == 0, "bad unknown result name");

    return 0;

fail:
    return -1;
}
