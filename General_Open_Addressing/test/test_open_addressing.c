/**
 * @file    test_open_addressing.c
 * @brief   Test program for generic open addressing hash table implementation,
 * @author  J.W Moolman
 * @date    2024-11-27
 */
#include <stdint.h>     
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>     
#include "unity.h"
#include "open_addressing.h"

/* --------------------------------------------------------------------------
   Example Probing Method Enum
 * -------------------------------------------------------------------------- */
typedef enum {
    LINEAR,
    QUADRATIC
} ProbingMethod;

static ProbingMethod probing_method;

/* Example linear and quadratic probe functions */
static uint32_t linear_probe_func(uint32_t k, uint32_t i, uint32_t m) {
    //return (k + i) % m;
    return (k + i) & (m - 1);
}
static uint32_t quadratic_probe_func(uint32_t k, uint32_t i, uint32_t m) {
    // Basic example: (k + i^2) mod m
    return (k + i * i) % m;
}

/* Global pointer to a hash table used by all tests */
static HashTab *ht = NULL;

/* --------------------------------------------------------------------------
   A real comparison function for integers stored in memory
 * -------------------------------------------------------------------------- */
static int compare_int_keys(const void *a, const void *b) {

    const int *int_a = (const int *)a;
    const int *int_b = (const int *)b;

    return (*int_a == *int_b) ? 0 : -1 ; 
}

/**
 * @brief Unity setup function. Initializes the hash table.
 */
void setUp(void)
{
    uint32_t (*probe_ptr)(uint32_t, uint32_t, uint32_t) = NULL;
    switch (probing_method) {
        case LINEAR:
            probe_ptr = linear_probe_func;
            break;
        case QUADRATIC:
            probe_ptr = quadratic_probe_func;
            break;
        default:
            probe_ptr = NULL; // fallback to default_probe_func
    }

    /* Create a new hash table, specifying our integer-compare function. */
    ht = init_ht(
        0.0f,                /* load_factor -> default */
        0.0f,                /* min_load_factor -> default */
        0.0f,                /* inactive_factor -> default */
        NULL,                /* hash_func -> use default_hash_func */
        compare_int_keys,    /* cmp_func -> compare_int_keys */
        probe_ptr,           /* probe function -> linear/quadratic */
        free,                /* freekey -> none (we don't free stack vars here) */
        free                 /* freeval -> same reason */
    );

    TEST_ASSERT_NOT_NULL(ht);
}

/**
 * @brief Unity teardown function. Frees the allocated hash table.
 */
void tearDown(void)
{
    int result = free_ht(ht);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    ht = NULL;
}

/* --------------------------------------------------------------------------
   BasicTests
 * -------------------------------------------------------------------------- */

/**
 * @brief Inserting a new key should succeed (HT_SUCCESS).
 */
void test_insert_should_succeed(void)
{
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 1;
    *value = 100;

    int result = insert_ht(ht, key, sizeof(*key), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    /* Verify by searching */
    int index = search_ht(ht, key, sizeof(*key));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index);

    /* Fetch the stored value and compare */
    int *fetched_value = (int *)fetch_ht(ht, (uint32_t)index);
    TEST_ASSERT_NOT_NULL(fetched_value);
    TEST_ASSERT_EQUAL_INT(100, *fetched_value);

}

/**
 * @brief Inserting a duplicate key should fail (HT_KEY_EXISTS).
 */
void test_insert_duplicate_should_fail(void)
{
    int *key = malloc(sizeof(int));
    int *value1 = malloc(sizeof(int));
    int *value2 = malloc(sizeof(int));
    *key = 2;
    *value1 = 200;
    *value2 = 300;

    int result1 = insert_ht(ht, key, sizeof(*key), value1);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result1);

    /* Insert the same key again -> expect HT_KEY_EXISTS */
    int result2 = insert_ht(ht, key, sizeof(*key), value2);
    TEST_ASSERT_EQUAL_INT(HT_KEY_EXISTS, result2);

    free(value2);
}

/**
 * @brief Searching for an existing key should return a valid index >= 0.
 */
void test_search_existing_key(void)
{
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 3;
    *value = 300;

    /* Insert key before searching */
    int insert_result = insert_ht(ht, key, sizeof(*key), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, insert_result);

    int index = search_ht(ht, key, sizeof(*key));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index);

    int *fetched_value = (int *)fetch_ht(ht, (uint32_t)index);
    TEST_ASSERT_NOT_NULL(fetched_value);
    TEST_ASSERT_EQUAL_INT(300, *fetched_value);

}

/**
 * @brief Searching for a non-existent key should yield HT_KEY_NOT_FOUND.
 */
void test_search_nonexistent_key(void)
{
    int *key = malloc(sizeof(int));
    *key = 4;
    int index = search_ht(ht, key, sizeof(*key));
    TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, index);
    free(key);
}

/**
 * @brief Removing an existing key should succeed, and subsequent searches should fail.
 */
void test_remove_existing_key(void)
{
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 5;
    *value = 500;

    int insert_result = insert_ht(ht, key, sizeof(*key), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, insert_result);

    int remove_result = remove_ht(ht, key, sizeof(*key));
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, remove_result);

    int index = search_ht(ht, key, sizeof(*key));
    TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, index);

}

/**
 * @brief Removing a non-existent key should yield HT_KEY_NOT_FOUND.
 */
void test_remove_nonexistent_key(void)
{
    int *key = malloc(sizeof(int));
    *key = 6;
    int remove_result = remove_ht(ht, key, sizeof(*key));
    TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, remove_result);
    free(key);
}

/* --------------------------------------------------------------------------
   EdgeCaseTests
 * -------------------------------------------------------------------------- */

/**
 * @brief Passing NULL as the HashTab pointer should yield HT_INVALID_ARG.
 */
void test_null_input(void)
{
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 1;
    *value = 100;

    int result = insert_ht(NULL, key, sizeof(*key), value);
    TEST_ASSERT_EQUAL_INT(HT_INVALID_ARG, result);

    int index = search_ht(NULL, key, sizeof(*key));
    TEST_ASSERT_EQUAL_INT(HT_INVALID_ARG, index);

    result = remove_ht(NULL, key, sizeof(*key));
    TEST_ASSERT_EQUAL_INT(HT_INVALID_ARG, result);

}

/**
 * @brief Insert and search boundary keys like INT_MIN and INT_MAX.
 */
void test_boundary_keys(void)
{
    int *min_key = malloc(sizeof(int));
    int *max_key = malloc(sizeof(int));
    int *val_for_min = malloc(sizeof(int));
    int *val_for_max = malloc(sizeof(int));
    *min_key = INT_MIN;
    *max_key = INT_MAX;
    *val_for_min = -1;
    *val_for_max = 1;

    /* Insert INT_MIN */
    int result_min = insert_ht(ht, min_key, sizeof(*min_key), val_for_min);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result_min);

    /* Insert INT_MAX */
    int result_max = insert_ht(ht, max_key, sizeof(*max_key), val_for_max);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result_max);

    /* Search for INT_MIN */
    int index_min = search_ht(ht, min_key, sizeof(*min_key));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index_min);
    int *fetched_min = (int *)fetch_ht(ht, (uint32_t)index_min);
    TEST_ASSERT_NOT_NULL(fetched_min);
    TEST_ASSERT_EQUAL_INT(-1, *fetched_min);

    /* Search for INT_MAX */
    int index_max = search_ht(ht, max_key, sizeof(*max_key));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index_max);
    int *fetched_max = (int *)fetch_ht(ht, (uint32_t)index_max);
    TEST_ASSERT_NOT_NULL(fetched_max);
    TEST_ASSERT_EQUAL_INT(1, *fetched_max);

}

/**
 * @brief Insert a key "0" as a special boundary test.
 */
void test_zero_key_insertion(void)
{
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 0;
    *value = 999;

    int result = insert_ht(ht, key, sizeof(*key), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    int index = search_ht(ht, key, sizeof(*key));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index);

    int *fetched_val = (int *)fetch_ht(ht, (uint32_t)index);
    TEST_ASSERT_NOT_NULL(fetched_val);
    TEST_ASSERT_EQUAL_INT(999, *fetched_val);

}

void test_double_free_trigger(void)
{
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 42;
    *value = 4242;

    int result = insert_ht(ht, key, sizeof(*key), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    /* First removal should free the key/value. */
    result = remove_ht(ht, key, sizeof(*key));
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    /* Second removal with the same key should return HT_KEY_NOT_FOUND.
       It must not attempt to free the key again. */
    result = remove_ht(ht, key, sizeof(*key));
    TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, result);
}
/* --------------------------------------------------------------------------
   AdvancedTests
 * -------------------------------------------------------------------------- */
void test_rehashing(void)
{
    size_t initial_size = size_ht(ht);
    int i, *key, *value;
    /* Assuming your default load factor is 0.75f in open_addressing.c */
    float default_load_factor = 0.75f;
    unsigned int max_entries = (unsigned int)(initial_size * default_load_factor);

    /* Insert enough entries to trigger a resize. */
    for (i = 0; i < max_entries + 1; i++) {
        key = malloc(sizeof(int));
        value = malloc(sizeof(int));
        *key = i;
        *value = i;
        int result = insert_ht(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    /* Verify all inserted keys. */
    for (int i = 0; i < max_entries + 1; i++) {
        int temp_key = i;
        int index = search_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index);

        int *fetched_val = (int *)fetch_ht(ht, (uint32_t)index);
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(i, *fetched_val);
    }
}

void test_table_resize_downward(void)
{
    int i, *key, *value;
    /* Insert 10 entries. */
    for (i = 0; i < 10; i++) {
        key = malloc(sizeof(int));
        value = malloc(sizeof(int));
        *key = i;
        *value = i;
        int result = insert_ht(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    /* Remove 8 entries to (possibly) trigger downsizing. */
    for (i = 0; i < 8; i++) {
        int temp_key = i;
        int result = remove_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    /* Validate the remaining entries (keys 8 and 9) still exist. */
    for (i = 8; i < 10; i++) {
        int temp_key = i;
        int index = search_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index);

        int *fetched_val = (int *)fetch_ht(ht, (uint32_t)index);
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(i , *fetched_val);
    }
}

/**
 * @brief Mixed operations test:
 *        - Insert several keys with corresponding values.
 *        - Remove a subset of these keys (existing keys).
 *        - Attempt removals for keys that were never inserted (non-existent).
 *        - Lookup keys to ensure that:
 *             - Keys that should exist are present with their expected values.
 *             - Keys that were removed or never inserted are not found.
 */
void test_mixed_insertions_deletions_lookup(void)
{
    /* ----------------------------
       Step 1: Insert a series of keys.
       ---------------------------- */
    int keys_to_insert[] = {10, 20, 30, 40, 50, 60, 70};
    size_t num_keys = sizeof(keys_to_insert) / sizeof(keys_to_insert[0]);

    // For each key, allocate memory for the key and a value (here value = key * 10)
    for (size_t i = 0; i < num_keys; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = keys_to_insert[i];
        *value = keys_to_insert[i] * 10;  // Arbitrary value assignment
        int result = insert_ht(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    /* ----------------------------
       Step 2: Remove some keys.
       ---------------------------- */
    // Remove keys that exist: 20, 40, and 70.
    int keys_to_remove[] = {20, 40, 70};
    size_t num_remove = sizeof(keys_to_remove) / sizeof(keys_to_remove[0]);

    for (size_t i = 0; i < num_remove; i++) {
        int temp_key = keys_to_remove[i];
        int result = remove_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    // Attempt to remove keys that do not exist: 80 and 90.
    int non_existent_keys[] = {80, 90};
    size_t num_nonexistent = sizeof(non_existent_keys) / sizeof(non_existent_keys[0]);

    for (size_t i = 0; i < num_nonexistent; i++) {
        int temp_key = non_existent_keys[i];
        int result = remove_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, result);
    }

    /* ----------------------------
       Step 3: Validate lookups.
       ---------------------------- */
    // Keys that should exist: 10, 30, 50, and 60.
    int keys_should_exist[] = {10, 30, 50, 60};
    size_t num_exist = sizeof(keys_should_exist) / sizeof(keys_should_exist[0]);

    for (size_t i = 0; i < num_exist; i++) {
        int temp_key = keys_should_exist[i];
        int index = search_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index);  // Valid index found
        int *fetched_val = (int *)fetch_ht(ht, (uint32_t)index);
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(temp_key * 10, *fetched_val);
    }

    // Keys that should no longer exist: 20, 40, 70, plus the never-inserted keys 80, 90.
    int keys_should_not_exist[] = {20, 40, 70, 80, 90};
    size_t num_not_exist = sizeof(keys_should_not_exist) / sizeof(keys_should_not_exist[0]);

    for (size_t i = 0; i < num_not_exist; i++) {
        int temp_key = keys_should_not_exist[i];
        int index = search_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, index);
    }
}
/**
 * @brief Stress test: Insert many entries (e.g. 1 million).
 *        Adjust for performance or memory constraints in your environment.
 */
void test_large_insertions(void)
{
    size_t i, large_size = 1000;
    int *key, *value; 

    /* Insert 1 million entries */
    for (i = 0; i < large_size; i++) {
        key = malloc(sizeof(int));
        value = malloc(sizeof(int));
        *key = i;
        *value = i;
        int result = insert_ht(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    /* Verify them */
    for (i = 0; i < large_size; i++) {
        int temp_key = i;
        int index = search_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index);

        int *fetched_val = (int *)fetch_ht(ht, (uint32_t)index);
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(i, *fetched_val);
    }
}

/**
 * @brief Large-scale mixed operations test:
 *        - Insert a large number of keys.
 *        - Delete a subset of those keys (e.g., keys divisible by 3).
 *        - Attempt deletion on keys that were never inserted.
 *        - Verify that lookups only succeed for keys that should remain.
 */
void test_large_mixed_insertions_deletions_lookup(void)
{
    // Define the total number of keys to insert.
    const size_t TOTAL_KEYS = 10000;

    // ---------------
    // Insertion Phase:
    // Insert keys 0 to TOTAL_KEYS - 1.
    // Each key is associated with a value (for example, key * 2).
    // ---------------
    for (size_t i = 0; i < TOTAL_KEYS; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = (int)i;
        *value = (int)(i * 2);  // Arbitrary value assignment for verification.

        int result = insert_ht(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    // ---------------
    // Deletion Phase:
    // Remove a subset of keys that exist. For example, remove keys that are divisible by 3.
    // ---------------
    for (size_t i = 0; i < TOTAL_KEYS; i++) {
        if (i % 3 == 0) {
            int temp_key = (int)i;
            int result = remove_ht(ht, &temp_key, sizeof(int));
            //printf("%d\n",i);
            TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
        }
    }

    // ---------------
    // Extra Deletion Phase:
    // Attempt to remove keys that were never inserted.
    // Here we try keys from TOTAL_KEYS to TOTAL_KEYS + 99.
    // ---------------
    for (size_t i = TOTAL_KEYS; i < TOTAL_KEYS + 100; i++) {
        int temp_key = (int)i;
        int result = remove_ht(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, result);
    }

    // ---------------
    // Lookup Phase: Verify the state of the hash table.
    // ---------------

    // Keys that should still exist are those not divisible by 3.
    for (size_t i = 0; i < TOTAL_KEYS; i++) {
        if (i % 3 != 0) {
            int temp_key = (int)i;
            int index = search_ht(ht, &temp_key, sizeof(int));
            TEST_ASSERT_GREATER_OR_EQUAL_INT(0, index);
            
            int *fetched_val = (int *)fetch_ht(ht, (uint32_t)index);
            TEST_ASSERT_NOT_NULL(fetched_val);
            // Verify that the value matches the inserted value (i * 2)
            TEST_ASSERT_EQUAL_INT((int)(i * 2), *fetched_val);
        }
    }

    // Keys that should not exist are those divisible by 3.
    for (size_t i = 0; i < TOTAL_KEYS; i++) {
        if (i % 3 == 0) {
            int temp_key = (int)i;
            int index = search_ht(ht, &temp_key, sizeof(int));
            TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, index);
        }
    }
}
/* --------------------------------------------------------------------------
   Test Runner
 * -------------------------------------------------------------------------- */

void test_probing_method(ProbingMethod method)
{
    probing_method = method;

    /* BasicTests */
    RUN_TEST(test_insert_should_succeed);
    RUN_TEST(test_insert_duplicate_should_fail);
    RUN_TEST(test_search_existing_key);
    RUN_TEST(test_search_nonexistent_key);
    RUN_TEST(test_remove_existing_key);
    RUN_TEST(test_remove_nonexistent_key);

    /* EdgeCaseTests */
    RUN_TEST(test_null_input);
    RUN_TEST(test_boundary_keys);
    RUN_TEST(test_zero_key_insertion);
    RUN_TEST(test_double_free_trigger);

    /* AdvancedTests */
    RUN_TEST(test_rehashing);
    RUN_TEST(test_mixed_insertions_deletions_lookup);
    RUN_TEST(test_table_resize_downward);
    RUN_TEST(test_large_insertions);
    RUN_TEST(test_large_mixed_insertions_deletions_lookup);
}

/**
 * @brief Main test entry point.
 */
int main(void)
{
    UNITY_BEGIN();

    printf("\n --- Linear probing --- \n");
    test_probing_method(LINEAR);

    printf("\n --- Quadratic probing --- \n");
    test_probing_method(QUADRATIC);

    return UNITY_END();
}
