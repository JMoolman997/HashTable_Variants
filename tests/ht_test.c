/**
 * @file    ht_test.c
 * @brief   Public-API conformance test runner.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "backend_config.h"
#include "capacity_util.h"
#include "hash_func.h"
#include "ht_registry.h"
#include "memory_util.h"
#include "test_registry.h"
#include "test_runner.h"

int test_create_destroy(ht_impl impl, const char *impl_name);
int test_insert_one_get_one(ht_impl impl, const char *impl_name);
int test_duplicate_insert_rejected(ht_impl impl, const char *impl_name);
int test_insert_multiple_get_all(ht_impl impl, const char *impl_name);
int test_lookup_missing(ht_impl impl, const char *impl_name);
int test_remove_existing(ht_impl impl, const char *impl_name);
int test_remove_missing(ht_impl impl, const char *impl_name);
int test_size_capacity_load_factor_sanity(ht_impl impl, const char *impl_name);
int test_many_inserts_then_gets(ht_impl impl, const char *impl_name);
int test_interleaved_ops(ht_impl impl, const char *impl_name);
int test_delete_then_reinsert(ht_impl impl, const char *impl_name);
int test_stats_and_reset(ht_impl impl, const char *impl_name);
int test_reserve_and_rehash(ht_impl impl, const char *impl_name);
int test_config_helpers(ht_impl impl, const char *impl_name);
int test_create_ex_diagnostics(ht_impl impl, const char *impl_name);
int test_contains_helper(ht_impl impl, const char *impl_name);
int test_name_helpers(ht_impl impl, const char *impl_name);
int test_full_table_no_resize(ht_impl impl, const char *impl_name);
int test_reuse_deleted_slot_no_resize(ht_impl impl, const char *impl_name);
int test_resize_grow_integrity(ht_impl impl, const char *impl_name);
int test_resize_shrink_integrity(ht_impl impl, const char *impl_name);
int test_collision_heavy_case(ht_impl impl, const char *impl_name);
int test_backshift_full_table_remove_integrity(ht_impl impl, const char *impl_name);
int test_adv_full_table_insert_failure_integrity(ht_impl impl, const char *impl_name);

static int test_hash_helpers(void);
static int test_internal_helpers(void);

static const test_case TEST_CASES[] = {
    { "create_destroy", test_create_destroy },
    { "insert_one_get_one", test_insert_one_get_one },
    { "duplicate_insert_rejected", test_duplicate_insert_rejected },
    { "insert_multiple_get_all", test_insert_multiple_get_all },
    { "lookup_missing", test_lookup_missing },
    { "remove_existing", test_remove_existing },
    { "remove_missing", test_remove_missing },
    { "size_capacity_load_factor_sanity", test_size_capacity_load_factor_sanity },
    { "many_inserts_then_gets", test_many_inserts_then_gets },
    { "interleaved_ops", test_interleaved_ops },
    { "delete_then_reinsert", test_delete_then_reinsert },
    { "stats_and_reset", test_stats_and_reset },
    { "reserve_and_rehash", test_reserve_and_rehash },
    { "config_helpers", test_config_helpers },
    { "create_ex_diagnostics", test_create_ex_diagnostics },
    { "contains_helper", test_contains_helper },
    { "name_helpers", test_name_helpers },
    { "full_table_no_resize", test_full_table_no_resize },
    { "reuse_deleted_slot_no_resize", test_reuse_deleted_slot_no_resize },
    { "resize_grow_integrity", test_resize_grow_integrity },
    { "resize_shrink_integrity", test_resize_shrink_integrity },
    { "collision_heavy_case", test_collision_heavy_case },
    { "backshift_full_table_remove_integrity", test_backshift_full_table_remove_integrity },
    { "adv_full_table_insert_failure_integrity", test_adv_full_table_insert_failure_integrity }
};

int main(
    void
) {
    const test_impl_case *impls;
    size_t                impl_count;

    if (test_hash_helpers() != 0) {
        return 1;
    }
    if (test_internal_helpers() != 0) {
        return 1;
    }

    impls = test_all_impls(&impl_count);
    if (test_run_impl_matrix(
            impls,
            impl_count,
            TEST_CASES,
            TEST_ARRAY_LEN(TEST_CASES)
        ) != 0) {
        return 1;
    }

    fprintf(stdout, "All tests passed.\n");
    return 0;
}

static int test_hash_helpers(
    void
) {
    const char *crc_input = "123456789";

    if (crc32_hash("", 0) != 0x00000000U) {
        fprintf(stderr, "[FAIL] crc32 empty input vector\n");
        return -1;
    }

    if (crc32_hash(crc_input, 9) != 0xCBF43926U) {
        fprintf(stderr, "[FAIL] crc32 123456789 vector\n");
        return -1;
    }

    return 0;
}

static int test_internal_helpers(
    void
) {
    ht_backend_config resolved;
    ht_config cfg;
    ht_result rc;
    ht_map *map;
    const ht_registry_entry *entries;
    size_t count;
    size_t out;
    size_t i;

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    rc = ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved);
    if (rc != HT_OK || resolved.capacity != 16 || resolved.min_capacity != 8 ||
        resolved.max_load_factor != 0.75 || resolved.min_load_factor != 0.20 ||
        resolved.resize_mode != HT_RESIZE_GROW || resolved.hash_fn == NULL ||
        resolved.thread_count != 1 || resolved.collect_stats != 0) {
        fprintf(stderr, "[FAIL] backend config default resolution\n");
        return -1;
    }

    cfg = ht_config_fixed(HT_IMPL_OPEN_ADDRESSING, 17);
    rc = ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved);
    if (rc != HT_OK || resolved.capacity != 32 ||
        resolved.min_capacity != 32 || resolved.resize_mode != HT_RESIZE_NONE) {
        fprintf(stderr, "[FAIL] backend config fixed resolution\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    cfg.thread_count = 0;
    rc = ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved);
    if (rc != HT_OK || resolved.thread_count != 1) {
        fprintf(stderr, "[FAIL] backend config thread normalization\n");
        return -1;
    }

    cfg.init_capacity = SIZE_MAX;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) == HT_OK ||
        ht_backend_config_resolve(NULL, 16, 8, 0.75, 0.20, &resolved) !=
            HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config invalid handling\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    cfg.rsz_mode = (ht_rsz_mode)99;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config invalid resize mode\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.0, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config invalid zero max load\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    cfg.max_load_factor = INFINITY;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config infinite max load\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    cfg.max_load_factor = NAN;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config NaN max load\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    cfg.min_load_factor = INFINITY;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config infinite min load\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    cfg.min_load_factor = NAN;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config NaN min load\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    cfg.min_load_factor = -0.10;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config negative min load\n");
        return -1;
    }

    cfg = ht_config_default(HT_IMPL_OPEN_ADDRESSING);
    cfg.max_load_factor = 0.50;
    cfg.min_load_factor = 0.75;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config min load above max load\n");
        return -1;
    }

    cfg.min_load_factor = cfg.max_load_factor;
    if (ht_backend_config_resolve(&cfg, 16, 8, 0.75, 0.20, &resolved) !=
        HT_ERR_INVALID) {
        fprintf(stderr, "[FAIL] backend config equal load factors\n");
        return -1;
    }

    cfg = ht_config_fixed(HT_IMPL_OPEN_ADDRESSING, 16);
    cfg.max_load_factor = 2.0;
    map = (ht_map *)1;
    rc = ht_create_ex(&cfg, &map);
    if (rc != HT_ERR_INVALID || map != NULL) {
        fprintf(stderr, "[FAIL] open addressing accepted oversized max load\n");
        if (rc == HT_OK) {
            ht_destroy(map);
        }
        return -1;
    }

    cfg = ht_config_fixed(HT_IMPL_SEPARATE_CHAINING, 16);
    cfg.max_load_factor = 2.0;
    map = NULL;
    rc = ht_create_ex(&cfg, &map);
    if (rc != HT_OK || map == NULL) {
        fprintf(stderr, "[FAIL] separate chaining rejected high max load\n");
        ht_destroy(map);
        return -1;
    }
    ht_destroy(map);

    cfg = ht_config_fixed(HT_IMPL_ADV_OPEN_ADDRESSING, 16);
    cfg.max_load_factor = 1.0;
    map = NULL;
    rc = ht_create_ex(&cfg, &map);
    if (rc != HT_OK || map == NULL) {
        fprintf(stderr, "[FAIL] full-load open addressing rejected max load 1\n");
        ht_destroy(map);
        return -1;
    }
    ht_destroy(map);

    if (ht_checked_add_size(SIZE_MAX, 1, &out) != HT_ERR_OOM ||
        ht_checked_mul_size(SIZE_MAX, 2, &out) != HT_ERR_OOM ||
        ht_grow_capacity_pow2(SIZE_MAX / 2u + 1u, &out) != HT_ERR_OOM ||
        ht_checked_add_size(16, 15, &out) != HT_OK || out != 31) {
        fprintf(stderr, "[FAIL] memory helper overflow handling\n");
        return -1;
    }

    entries = ht_registry_entries(&count);
    if (entries == NULL || count == 0 ||
        ht_registry_find((ht_impl)9999) != NULL) {
        fprintf(stderr, "[FAIL] registry basic lookup\n");
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (strcmp(ht_impl_name(entries[i].impl), entries[i].name) != 0) {
            fprintf(stderr, "[FAIL] registry name mismatch for %s\n", entries[i].name);
            return -1;
        }

        cfg = ht_config_fixed(entries[i].impl, 32);
        map = NULL;
        rc = ht_create_ex(&cfg, &map);
        if (rc != HT_OK || map == NULL) {
            fprintf(stderr, "[FAIL] registry create path for %s\n", entries[i].name);
            ht_destroy(map);
            return -1;
        }
        ht_destroy(map);
    }

    return 0;
}
