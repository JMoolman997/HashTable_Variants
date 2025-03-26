/**
 * @file    test_open_table.c
 * @brief   Test program for the open addressing hash table implementation.
 * @author  J.W Moolman
 * @date    2025-03-20
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include "unity.h"
#include "open_table.h"

/* Global pointer to a hash table used by all tests */
static HashTab *ht = NULL;

/* Comparison function for integers */
static int compare_int_keys(const void *a, const void *b) {
    int int_a = *(const int *)a;
    int int_b = *(const int *)b;
    return (int_a > int_b) - (int_a < int_b);
}

/* Comparison function for strings */
static int compare_string_keys(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

/* Custom hash function that causes all keys to collide */
static uint32_t constant_hash_func(const void *key, size_t len) {
    return 42;  // All keys hash to the same value
}

/* Custom free functions to track calls */
static int key_free_count = 0;
static int val_free_count = 0;

static void custom_free_key(void *k) {
    key_free_count++;
    free(k);
}

static void custom_free_val(void *v) {
    val_free_count++;
    free(v);
}

static void reset_free_counters(void) {
    key_free_count = 0;
    val_free_count = 0;
}

/**
 * @brief Unity setup function. Initializes the hash table.
 */
void setUp(void) {
    HTConfig config = {
        .load_factor = 0.75f,       /* Max load factor */
        .min_load_factor = 0.25f,   /* Min load factor */
        .hash_func = NULL,          /* Use default_hash_func */
        .cmp_func = compare_int_keys,
        .free_key = free,           /* Free allocated keys */
        .free_val = free            /* Free allocated values */
    };

    ht = ht_create(&config);
    TEST_ASSERT_NOT_NULL(ht);
}

/**
 * @brief Unity teardown function. Frees the allocated hash table.
 */
void tearDown(void) {
    ht_destroy(ht);
    ht = NULL;
}

/* --------------------------------------------------------------------------
   Basic Tests
 * -------------------------------------------------------------------------- */

/**
 * @brief Inserting a new key should succeed (HT_SUCCESS).
 */
void test_insert_should_succeed(void) {
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 1;
    *value = 100;

    HTResult result = ht_insert(ht, key, sizeof(int), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    /* Verify by searching */
    void *fetched_value = ht_search(ht, key, sizeof(int));
    TEST_ASSERT_NOT_NULL(fetched_value);
    TEST_ASSERT_EQUAL_INT(100, *(int *)fetched_value);
}

/**
 * @brief Inserting a duplicate key should fail (HT_KEY_EXISTS).
 */
void test_insert_duplicate_should_fail(void) {
    int *key = malloc(sizeof(int));
    int *value1 = malloc(sizeof(int));
    int *value2 = malloc(sizeof(int));
    *key = 2;
    *value1 = 200;
    *value2 = 300;

    HTResult result1 = ht_insert(ht, key, sizeof(int), value1);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result1);

    HTResult result2 = ht_insert(ht, key, sizeof(int), value2);
    TEST_ASSERT_EQUAL_INT(HT_KEY_EXISTS, result2);

    free(value2);  /* value2 wasn’t inserted */
}

/**
 * @brief Searching for an existing key should return its value.
 */
void test_search_existing_key(void) {
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 3;
    *value = 300;

    HTResult insert_result = ht_insert(ht, key, sizeof(int), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, insert_result);

    void *fetched_value = ht_search(ht, key, sizeof(int));
    TEST_ASSERT_NOT_NULL(fetched_value);
    TEST_ASSERT_EQUAL_INT(300, *(int *)fetched_value);
}

/**
 * @brief Searching for a non-existent key should return NULL.
 */
void test_search_nonexistent_key(void) {
    int *key = malloc(sizeof(int));
    *key = 4;

    void *result = ht_search(ht, key, sizeof(int));
    TEST_ASSERT_NULL(result);

    free(key);
}

/**
 * @brief Removing an existing key should succeed, and subsequent searches should fail.
 */
void test_remove_existing_key(void) {
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 5;
    *value = 500;

    HTResult insert_result = ht_insert(ht, key, sizeof(int), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, insert_result);

    HTResult remove_result = ht_remove(ht, key, sizeof(int));
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, remove_result);

    void *result = ht_search(ht, key, sizeof(int));
    TEST_ASSERT_NULL(result);
}

/**
 * @brief Removing a non-existent key should yield HT_KEY_NOT_FOUND.
 */
void test_remove_nonexistent_key(void) {
    int *key = malloc(sizeof(int));
    *key = 6;

    HTResult remove_result = ht_remove(ht, key, sizeof(int));
    TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, remove_result);

    free(key);
}

/* --------------------------------------------------------------------------
   Edge Case Tests
 * -------------------------------------------------------------------------- */

/**
 * @brief Passing NULL as the HashTab pointer should yield HT_INVALID_ARG.
 */
void test_null_input(void) {
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 1;
    *value = 100;

    HTResult result = ht_insert(NULL, key, sizeof(int), value);
    TEST_ASSERT_EQUAL_INT(HT_INVALID_ARG, result);

    void *search_result = ht_search(NULL, key, sizeof(int));
    TEST_ASSERT_NULL(search_result);

    result = ht_remove(NULL, key, sizeof(int));
    TEST_ASSERT_EQUAL_INT(HT_INVALID_ARG, result);

    free(key);
    free(value);
}

/**
 * @brief Insert and search boundary keys like INT_MIN and INT_MAX.
 */
void test_boundary_keys(void) {
    int *min_key = malloc(sizeof(int));
    int *max_key = malloc(sizeof(int));
    int *val_for_min = malloc(sizeof(int));
    int *val_for_max = malloc(sizeof(int));
    *min_key = INT_MIN;
    *max_key = INT_MAX;
    *val_for_min = -1;
    *val_for_max = 1;

    HTResult result_min = ht_insert(ht, min_key, sizeof(int), val_for_min);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result_min);

    HTResult result_max = ht_insert(ht, max_key, sizeof(int), val_for_max);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result_max);

    void *fetched_min = ht_search(ht, min_key, sizeof(int));
    TEST_ASSERT_NOT_NULL(fetched_min);
    TEST_ASSERT_EQUAL_INT(-1, *(int *)fetched_min);

    void *fetched_max = ht_search(ht, max_key, sizeof(int));
    TEST_ASSERT_NOT_NULL(fetched_max);
    TEST_ASSERT_EQUAL_INT(1, *(int *)fetched_max);
}

/**
 * @brief Insert a key "0" as a special boundary test.
 */
void test_zero_key_insertion(void) {
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 0;
    *value = 999;

    HTResult result = ht_insert(ht, key, sizeof(int), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    void *fetched_val = ht_search(ht, key, sizeof(int));
    TEST_ASSERT_NOT_NULL(fetched_val);
    TEST_ASSERT_EQUAL_INT(999, *(int *)fetched_val);
}

/**
 * @brief Test that double removal doesn’t cause issues (key is freed only once).
 */
void test_double_free_trigger(void) {
    int *key = malloc(sizeof(int));
    int *value = malloc(sizeof(int));
    *key = 42;
    *value = 4242;

    HTResult result = ht_insert(ht, key, sizeof(int), value);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    result = ht_remove(ht, key, sizeof(int));
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    result = ht_remove(ht, key, sizeof(int));
    TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, result);
}

/* --------------------------------------------------------------------------
   Advanced Tests
 * -------------------------------------------------------------------------- */

/**
 * @brief Test rehashing by inserting enough keys to trigger a resize.
 */
void test_rehashing(void) {
    size_t initial_size = ht_capacity(ht);
    float default_load_factor = 0.75f;
    unsigned int max_entries = (unsigned int)(initial_size * default_load_factor);

    for (int i = 0; i < max_entries + 1; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = i;
        *value = i;
        HTResult result = ht_insert(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    for (int i = 0; i < max_entries + 1; i++) {
        int temp_key = i;
        void *fetched_val = ht_search(ht, &temp_key, sizeof(int));
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(i, *(int *)fetched_val);
    }
}

/**
 * @brief Test table resizing downward by removing most entries.
 */
void test_table_resize_downward(void) {
    for (int i = 0; i < 10; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = i;
        *value = i;
        HTResult result = ht_insert(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    for (int i = 0; i < 8; i++) {
        int temp_key = i;
        HTResult result = ht_remove(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    for (int i = 8; i < 10; i++) {
        int temp_key = i;
        void *fetched_val = ht_search(ht, &temp_key, sizeof(int));
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(i, *(int *)fetched_val);
    }
}

/**
 * @brief Mixed operations: insert, remove, and lookup keys.
 */
void test_mixed_insertions_deletions_lookup(void) {
    int keys_to_insert[] = {10, 20, 30, 40, 50, 60, 70};
    size_t num_keys = sizeof(keys_to_insert) / sizeof(keys_to_insert[0]);

    for (size_t i = 0; i < num_keys; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = keys_to_insert[i];
        *value = keys_to_insert[i] * 10;
        HTResult result = ht_insert(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    int keys_to_remove[] = {20, 40, 70};
    size_t num_remove = sizeof(keys_to_remove) / sizeof(keys_to_remove[0]);
    for (size_t i = 0; i < num_remove; i++) {
        int temp_key = keys_to_remove[i];
        HTResult result = ht_remove(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    int non_existent_keys[] = {80, 90};
    size_t num_nonexistent = sizeof(non_existent_keys) / sizeof(non_existent_keys[0]);
    for (size_t i = 0; i < num_nonexistent; i++) {
        int temp_key = non_existent_keys[i];
        HTResult result = ht_remove(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, result);
    }

    int keys_should_exist[] = {10, 30, 50, 60};
    size_t num_exist = sizeof(keys_should_exist) / sizeof(keys_should_exist[0]);
    for (size_t i = 0; i < num_exist; i++) {
        int temp_key = keys_should_exist[i];
        void *fetched_val = ht_search(ht, &temp_key, sizeof(int));
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(temp_key * 10, *(int *)fetched_val);
    }

    int keys_should_not_exist[] = {20, 40, 70, 80, 90};
    size_t num_not_exist = sizeof(keys_should_not_exist) / sizeof(keys_should_not_exist[0]);
    for (size_t i = 0; i < num_not_exist; i++) {
        int temp_key = keys_should_not_exist[i];
        void *result = ht_search(ht, &temp_key, sizeof(int));
        TEST_ASSERT_NULL(result);
    }
}

/**
 * @brief Stress test with large insertions.
 */
void test_large_insertions(void) {
    size_t large_size = 1000;
    for (size_t i = 0; i < large_size; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = (int)i;
        *value = (int)i;
        HTResult result = ht_insert(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    for (size_t i = 0; i < large_size; i++) {
        int temp_key = (int)i;
        void *fetched_val = ht_search(ht, &temp_key, sizeof(int));
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT((int)i, *(int *)fetched_val);
    }
}

/**
 * @brief Large-scale mixed operations test.
 */
void test_large_mixed_insertions_deletions_lookup(void) {
    const size_t TOTAL_KEYS = 10000;

    for (size_t i = 0; i < TOTAL_KEYS; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = (int)i;
        *value = (int)(i * 2);
        HTResult result = ht_insert(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    for (size_t i = 0; i < TOTAL_KEYS; i++) {
        if (i % 3 == 0) {
            int temp_key = (int)i;
            HTResult result = ht_remove(ht, &temp_key, sizeof(int));
            TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
        }
    }

    for (size_t i = TOTAL_KEYS; i < TOTAL_KEYS + 100; i++) {
        int temp_key = (int)i;
        HTResult result = ht_remove(ht, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_KEY_NOT_FOUND, result);
    }

    for (size_t i = 0; i < TOTAL_KEYS; i++) {
        int temp_key = (int)i;
        void *fetched_val = ht_search(ht, &temp_key, sizeof(int));
        if (i % 3 != 0) {
            TEST_ASSERT_NOT_NULL(fetched_val);
            TEST_ASSERT_EQUAL_INT((int)(i * 2), *(int *)fetched_val);
        } else {
            TEST_ASSERT_NULL(fetched_val);
        }
    }
}

/* --------------------------------------------------------------------------
   Additional Extensive Tests
 * -------------------------------------------------------------------------- */

/**
 * @brief Test creation with invalid load factors.
 */
void test_create_invalid_load_factors(void) {
    HTConfig config = {
        .load_factor = 0.0f,  // Invalid: <= 0
        .min_load_factor = 0.25f,
        .hash_func = NULL,
        .cmp_func = compare_int_keys,
        .free_key = free,
        .free_val = free
    };

    HashTab *ht_invalid = ht_create(&config);
    TEST_ASSERT_NULL(ht_invalid);

    config.load_factor = 1.5f;  // Invalid: > 1
    ht_invalid = ht_create(&config);
    TEST_ASSERT_NULL(ht_invalid);

    config.load_factor = 0.75f;
    config.min_load_factor = 0.8f;  // Invalid: min_load_factor >= load_factor
    ht_invalid = ht_create(&config);
    TEST_ASSERT_NULL(ht_invalid);
}

/**
 * @brief Test insertion into a full table without resizing.
 */
void test_insert_into_full_table(void) {
    HTConfig config = {
        .load_factor = 1.0f,  // Allow filling completely
        .min_load_factor = 0.0f,
        .hash_func = NULL,
        .cmp_func = compare_int_keys,
        .free_key = free,
        .free_val = free
    };
    HashTab *ht_full = ht_create(&config);
    TEST_ASSERT_NOT_NULL(ht_full);

    // Fill the table (initial size is 2)
    for (int i = 0; i < 2; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = i;
        *value = i * 10;
        HTResult result = ht_insert(ht_full, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    // Insert one more key
    int *extra_key = malloc(sizeof(int));
    int *extra_value = malloc(sizeof(int));
    *extra_key = 3;
    *extra_value = 30;
    HTResult result = ht_insert(ht_full, extra_key, sizeof(int), extra_value);
    TEST_ASSERT_EQUAL_INT(HT_FAILURE, result);  // Table is full

    ht_destroy(ht_full);
    free(extra_key);
    free(extra_value);
}

/**
 * @brief Test insertion and search with a constant hash function (all keys collide).
 */
void test_insert_with_constant_hash(void) {
    HTConfig config = {
        .load_factor = 0.75f,
        .min_load_factor = 0.25f,
        .hash_func = constant_hash_func,
        .cmp_func = compare_int_keys,
        .free_key = free,
        .free_val = free
    };
    HashTab *ht_collide = ht_create(&config);
    TEST_ASSERT_NOT_NULL(ht_collide);

    for (int i = 0; i < 5; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = i;
        *value = i * 10;
        HTResult result = ht_insert(ht_collide, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    for (int i = 0; i < 5; i++) {
        int temp_key = i;
        void *fetched_val = ht_search(ht_collide, &temp_key, sizeof(int));
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(i * 10, *(int *)fetched_val);
    }

    ht_destroy(ht_collide);
}

/**
 * @brief Test insertion and search with string keys.
 */
void test_insert_and_search_string_keys(void) {
    HTConfig config = {
        .load_factor = 0.75f,
        .min_load_factor = 0.25f,
        .hash_func = NULL,
        .cmp_func = compare_string_keys,
        .free_key = free,
        .free_val = free
    };
    HashTab *ht_str = ht_create(&config);
    TEST_ASSERT_NOT_NULL(ht_str);

    char *key1 = malloc(strlen("hello") + 1);
    char *value1 = malloc(strlen("world") + 1);
    HTResult result = ht_insert(ht_str, key1, strlen(key1) + 1, value1);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    char *key2 = malloc(strlen("foo") + 1);;
    char *value2 = malloc(strlen("bar") + 1);
    result = ht_insert(ht_str, key2, strlen(key2) + 1, value2);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    char *search_key1 = "hello";
    void *fetched_val1 = ht_search(ht_str, search_key1, strlen(search_key1) + 1);
    TEST_ASSERT_NOT_NULL(fetched_val1);
    TEST_ASSERT_EQUAL_STRING("world", (char *)fetched_val1);

    char *search_key2 = "foo";
    void *fetched_val2 = ht_search(ht_str, search_key2, strlen(search_key2) + 1);
    TEST_ASSERT_NOT_NULL(fetched_val2);
    TEST_ASSERT_EQUAL_STRING("bar", (char *)fetched_val2);

    ht_destroy(ht_str);
}

/**
 * @brief Test multiple resizes (up and down).
 */
void test_multiple_resizes(void) {
    HTConfig config = {
        .load_factor = 0.5f,  // Low load factor to trigger resizes early
        .min_load_factor = 0.1f,
        .hash_func = NULL,
        .cmp_func = compare_int_keys,
        .free_key = free,
        .free_val = free
    };
    HashTab *ht_resize = ht_create(&config);
    TEST_ASSERT_NOT_NULL(ht_resize);
    uint32_t initial_size = ht_capacity(ht_resize);

    // Insert keys to trigger multiple resizes
    for (int i = 0; i < 10; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = i;
        *value = i * 10;
        HTResult result = ht_insert(ht_resize, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }
    TEST_ASSERT_TRUE(ht_capacity(ht_resize) > initial_size);

    // Remove keys to trigger downsizing
    for (int i = 0; i < 8; i++) {
        int temp_key = i;
        HTResult result = ht_remove(ht_resize, &temp_key, sizeof(int));
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    // Verify remaining keys
    for (int i = 8; i < 10; i++) {
        int temp_key = i;
        void *fetched_val = ht_search(ht_resize, &temp_key, sizeof(int));
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT(i * 10, *(int *)fetched_val);
    }

    ht_destroy(ht_resize);
}

/**
 * @brief Test that custom free functions are called correctly.
 */
void test_free_functions_called(void) {
    HTConfig config = {
        .load_factor = 0.75f,
        .min_load_factor = 0.25f,
        .hash_func = NULL,
        .cmp_func = compare_int_keys,
        .free_key = custom_free_key,
        .free_val = custom_free_val
    };
    HashTab *ht_free = ht_create(&config);
    TEST_ASSERT_NOT_NULL(ht_free);
    reset_free_counters();

    // Insert two keys
    int *key1 = malloc(sizeof(int));
    int *value1 = malloc(sizeof(int));
    *key1 = 1;
    *value1 = 10;
    HTResult result = ht_insert(ht_free, key1, sizeof(int), value1);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    int *key2 = malloc(sizeof(int));
    int *value2 = malloc(sizeof(int));
    *key2 = 2;
    *value2 = 20;
    result = ht_insert(ht_free, key2, sizeof(int), value2);
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);

    // Remove one key
    int temp_key = 1;
    result = ht_remove(ht_free, &temp_key, sizeof(int));
    TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(1, key_free_count);
    TEST_ASSERT_EQUAL_INT(1, val_free_count);

    // Destroy table
    ht_destroy(ht_free);
    TEST_ASSERT_EQUAL_INT(2, key_free_count);  // One from remove, one from destroy
    TEST_ASSERT_EQUAL_INT(2, val_free_count);
}

/**
 * @brief Test with extreme load factors.
 */
void test_extreme_load_factors(void) {
    HTConfig config = {
        .load_factor = 0.1f,  // Very low, frequent resizes
        .min_load_factor = 0.05f,
        .hash_func = NULL,
        .cmp_func = compare_int_keys,
        .free_key = free,
        .free_val = free
    };
    HashTab *ht_extreme = ht_create(&config);
    TEST_ASSERT_NOT_NULL(ht_extreme);

    uint32_t prev_size = ht_capacity(ht_extreme);
    for (int i = 0; i < 5; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = i;
        *value = i * 10;
        HTResult result = ht_insert(ht_extreme, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
        if (i > 0) {  // After first insertion, expect resize
            TEST_ASSERT_TRUE(ht_capacity(ht_extreme) > prev_size);
            prev_size = ht_capacity(ht_extreme);
        }
    }

    ht_destroy(ht_extreme);
}

/**
 * @brief Stress test with a very large number of insertions.
 */
void test_very_large_insertions(void) {
    const size_t LARGE_SIZE = 100000;
    for (size_t i = 0; i < LARGE_SIZE; i++) {
        int *key = malloc(sizeof(int));
        int *value = malloc(sizeof(int));
        *key = (int)i;
        *value = (int)i * 10;
        HTResult result = ht_insert(ht, key, sizeof(int), value);
        TEST_ASSERT_EQUAL_INT(HT_SUCCESS, result);
    }

    for (size_t i = 0; i < LARGE_SIZE; i++) {
        int temp_key = (int)i;
        void *fetched_val = ht_search(ht, &temp_key, sizeof(int));
        TEST_ASSERT_NOT_NULL(fetched_val);
        TEST_ASSERT_EQUAL_INT((int)(i * 10), *(int *)fetched_val);
    }
}

/* --------------------------------------------------------------------------
   Test Runner
 * -------------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();

    printf("\n --- Open Table Tests --- \n");
    RUN_TEST(test_insert_should_succeed);
    RUN_TEST(test_insert_duplicate_should_fail);
    RUN_TEST(test_search_existing_key);
    RUN_TEST(test_search_nonexistent_key);
    RUN_TEST(test_remove_existing_key);
    RUN_TEST(test_remove_nonexistent_key);

    RUN_TEST(test_null_input);
    RUN_TEST(test_boundary_keys);
    RUN_TEST(test_zero_key_insertion);
    RUN_TEST(test_double_free_trigger);

    RUN_TEST(test_rehashing);
    RUN_TEST(test_mixed_insertions_deletions_lookup);
    RUN_TEST(test_table_resize_downward);
    RUN_TEST(test_large_insertions);
    RUN_TEST(test_large_mixed_insertions_deletions_lookup);

    // New tests
    RUN_TEST(test_create_invalid_load_factors);
    RUN_TEST(test_insert_into_full_table);
    RUN_TEST(test_insert_with_constant_hash);
    RUN_TEST(test_insert_and_search_string_keys);
    RUN_TEST(test_multiple_resizes);
    RUN_TEST(test_free_functions_called);
    RUN_TEST(test_extreme_load_factors);
    RUN_TEST(test_very_large_insertions);

    return UNITY_END();
}
