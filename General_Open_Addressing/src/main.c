/**
 * @file    main.c
 * @brief   Interactive program to demonstrate the open addressing hash table
 *          using the new API.
 * @date    2025-01-25
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>      // For intptr_t, uint32_t
#include "open_addressing.h"

//static uint32_t int_hash_func(void *key, size_t len);
static int int_cmp_func(const void *a, const void *b);
//static void int_free(void *int_ptr);

/**static uint32_t int_hash_func(
        void *key,
        size_t len
} {
    
} **/

static int  int_cmp_func(
        const void *a,
        const void *b
) {
    const int *int_a = (const int *)a;
    const int *int_b = (const int *)b;

    return (*int_a == *int_b) ? 0 : -1 ; 
}

/**static void int_free(
        void *int_ptr
) {

}**/

/**
 * @brief Converts a key-value pair to a string for display.
 *
 * @param flag    Status flag of the entry (1: occupied, 2: deleted, 0: empty)
 * @param k       Pointer to the key (void *)
 * @param v       Pointer to the value (void *)
 * @param buffer  Buffer to store the formatted string
 */
void keyval2str(int flag, void *k, void *v, char *buffer) {
    if (flag == 1) { // Occupied
        int key   = *(int *)k;
        int value = *(int *)v;
        sprintf(buffer, "Key: %d, Value: %d", key, value);
    } 
    else if (flag == 2) { // Deleted
        sprintf(buffer, "Deleted");
    } 
    else { // Empty
        sprintf(buffer, "Empty");
    }
}


int main(void) 
{
    /*
     * Initialize the hash table with the new API:
     *   float load_factor        -> pass 0.0 to use default
     *   float min_load_factor    -> pass 0.0 to use default
     *   float inactive_factor    -> pass 0.0 to use default
     *   uint32_t (*hash_func)(void*, size_t) -> NULL for default
     *   int (*cmp_func)(const void*, const void*) -> NULL for default
     *   uint32_t (*p)(uint32_t, uint32_t, uint32_t) -> NULL for default (linear)
     *   void (*freekey)(void*) -> NULL (no automatic free of keys)
     *   void (*freeval)(void*) -> NULL (no automatic free of values)
     */
    HashTab *ht = init_ht(
        0.0f,   /* load_factor      */
        0.0f,   /* min_load_factor  */
        0.0f,   /* inactive_factor  */
        NULL,   /* hash_func        */
        int_cmp_func,   /* cmp_func         */
        NULL,   /* probe_func       */
        free,   /* freekey          */
        free    /* freeval          */
    );

    if (!ht) {
        fprintf(stderr, "Failed to initialize hash table.\n");
        return EXIT_FAILURE;
    }

    int choice;
    while (1) {
        printf("\nHash Table Menu:\n");
        printf("1. Insert Key-Value Pair\n");
        printf("2. Search for Key\n");
        printf("3. Remove Key\n");
        printf("4. Print Hash Table\n");
        printf("5. Exit\n");
        printf("Enter your choice: ");
        
        /* Input validation for choice */
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number between 1 and 5.\n");
            int c;
            while ((c = getchar()) != '\n' && c != EOF); // Clear input buffer
            continue;
        }

        switch (choice) {
            case 1: { // Insert
                int *key = malloc(sizeof(int));
                int *value = malloc(sizeof(int));

                if (!key || !value) {
                    fprintf(stderr, "Memory allocation failed.\n");
                    free(key);
                    free(value);
                    break;
                }

                printf("Enter key: ");
                if (scanf("%d", key) != 1) {
                    printf("Invalid key input. Please enter an integer.\n");
                    free(key);
                    free(value);
                    break;
                }

                printf("Enter value: ");
                if (scanf("%d", value) != 1) {
                    printf("Invalid value input. Please enter an integer.\n");
                    free(key);
                    free(value);
                    break;
                }

                /* Insert into hash table */
                int status = insert_ht(ht, key, sizeof(int), value);

                if (status == HT_SUCCESS) {
                    printf("Key-Value pair inserted successfully.\n");
                } else if (status == HT_KEY_EXISTS) {
                    printf("Error: Key already exists.\n");
                    free(key);   // Free unused memory
                    free(value); // Free unused memory
                } else {
                    printf("Error inserting Key-Value pair (status=%d).\n", status);
                    free(key);
                    free(value);
                }
                break;
            }

            case 2: { // Search
                int key;
                printf("Enter key to search: ");
                if (scanf("%d", &key) != 1) {
                    printf("Invalid key input. Please enter an integer.\n");
                    break;
                }

                int index = search_ht(ht, &key, sizeof(int));
                if (index >= 0) {
                    void *val_ptr = fetch_ht(ht, (uint32_t)index);
                    if (val_ptr) {
                        int found_value = *(int *)val_ptr;
                        printf("Key %d found with value: %d\n", key, found_value);
                    } else {
                        printf("Error fetching value for key %d.\n", key);
                    }
                } else if (index == HT_KEY_NOT_FOUND) {
                    printf("Key %d not found.\n", key);
                } else {
                    printf("Search failed with error code: %d\n", index);
                }
                break;
            }

            case 3: { // Remove
                int key;
                printf("Enter key to remove: ");
                if (scanf("%d", &key) != 1) {
                    printf("Invalid key input. Please enter an integer.\n");
                    break;
                }

                int status = remove_ht(ht, &key, sizeof(int));
                if (status == HT_SUCCESS) {
                    printf("Key %d removed successfully.\n", key);
                } else if (status == HT_KEY_NOT_FOUND) {
                    printf("Key %d not found.\n", key);
                } else {
                    printf("Remove failed with error code: %d\n", status);
                }
                break;
            }

            case 4: { // Print
                print_ht(ht, keyval2str);
                break;
            }

            case 5: { // Exit
                free_ht(ht);
                printf("Hash table freed successfully.\n");
                printf("Exiting...\n");
                return EXIT_SUCCESS;
            }

            default:
                printf("Invalid choice. Please select a number between 1 and 5.\n");
                break;
        }
    }

    free_ht(ht);
    return EXIT_SUCCESS;
}
