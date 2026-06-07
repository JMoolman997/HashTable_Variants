/**
 * @file    ht_test.c
 * @brief   Public-API conformance test runner.
 */

#include <stdio.h>

#include "hash_func.h"
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

static int test_hash_helpers(void);

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
    { "collision_heavy_case", test_collision_heavy_case }
};

int main(
    void
) {
    const test_impl_case *impls;
    size_t                impl_count;

    if (test_hash_helpers() != 0) {
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
