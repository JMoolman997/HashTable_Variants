/**
 * @file    open_table.h
 * @brief   A modular open addressing hash table implementation for
 *          testing and benchmarking.
 * @author  J.W Moolman
 * @date    2024-10-23
 */

#ifndef OPEN_TABLE_H
#define OPEN_TABLE_H

#include <stdint.h>
#include <stddef.h>

/* --- Macros -------------------------------------------------------------- */

/** Default maximum load factor before resizing the hash table */
#define DEFAULT_LOAD_FACTOR 0.5
/** Default minimum load factor before attempting downsizing */
#define DEFAULT_MIN_LOAD_FACTOR 0.25

/**
 * @brief Default configuration macro for convenience.
 */
#define HT_DEFAULT_CONFIG { \
    .load_factor = 0.75f, \
    .min_load_factor = 0.25f, \
    .hash_func = NULL, \
    .cmp_func = NULL, \
    .free_key = NULL, \
    .free_val = NULL \
}

/* --- Error Return Codes --------------------------------------------------- */

/**
 * @brief Result codes for hash table operations.
 */
typedef enum {
    HT_FAILURE = 1,
    HT_SUCCESS = 0,           /**< Operation completed successfully. */
    HT_KEY_EXISTS = -1,       /**< Key already exists in the table. */
    HT_NO_SPACE = -2,
    HT_KEY_NOT_FOUND = -3,    /**< Key was not found in the table. */
    HT_MEM_ERROR = -4,        /**< Memory allocation failed. */
    HT_INVALID_ARG = -5,      /**< Invalid argument provided. */
    HT_INVALID_STATE = -6     /**< Internal table state is inconsistent. */
} HTResult;

/* --- Data Structures ----------------------------------------------------- */

/** 
 * @struct hashtab
 * @brief  The main container structure for the hash table.
 */
typedef struct hashtab HashTab;

/** 
 * @struct htentry
 * @brief  Represents an entry in the hash table.
 */
typedef struct htentry HTentry;

typedef struct {
    float load_factor;
    float min_load_factor;
    uint32_t (*hash_func)(const void *key, size_t len);
    int (*cmp_func)(const void *a, const void *b);
    void (*free_key)(void *k);
    void (*free_val)(void *v);
} HTConfig;

/* --- Function Prototypes ------------------------------------------------- */

/**
 * @brief Creates a new hash table with the specified configuration.
 *
 * @param config Pointer to configuration (use HT_DEFAULT_CONFIG for defaults).
 *
 * @return Pointer to the new hash table, or NULL on failure.
 */
HashTab *ht_create(
        const HTConfig *config
);

/**
 * @brief Destroys a hash table and frees all associated memory.
 *
 * @param ht Pointer to the hash table to destroy.
 */
void ht_destroy(
        HashTab *ht
);

/**
 * @brief Searches for a key in the hash table and returns its associated value.
 *
 * @param ht Pointer to the hash table.
 * @param key Pointer to the key to search for.
 * @param key_len Length of the key in bytes.
 *
 * @return Pointer to the value if found, NULL if not found.
 */
void *ht_search(
        const HashTab *ht,
        const void *key,
        size_t key_len
);

/**
 * @brief Inserts a key-value pair into the hash table.
 *
 * @param ht Pointer to the hash table.
 * @param key Pointer to the key to insert.
 * @param key_len Length of the key in bytes.
 * @param value Pointer to the value to associate with the key.
 *
 * @return HT_OK on success, or an error code on failure.
 */
HTResult ht_insert(
        HashTab *ht,
        const void *key,
        size_t key_len,
        void *value
);

/**
 * @brief Removes a key and its associated value from the hash table.
 *
 * @param ht Pointer to the hash table.
 * @param key Pointer to the key to remove.
 * @param key_len Length of the key in bytes.
 *
 * @return HT_OK on success, or an error code on failure.
 */
HTResult ht_remove(
        HashTab *ht,
        const void *key,
        size_t key_len
);

/**
 * @brief Prints the contents of the hash table using a custom formatter.
 *
 * @param ht Pointer to the hash table.
 * @param format_entry Function to format an entry into a string.
 */
void ht_print(
        const HashTab *ht,
        void (*format_key)(void *key, char *buf, size_t buf_size),
        void (*format_value)(void *value, char *buf, size_t buf_size)
);

/**
 * @brief Returns the current capacity of the hash table.
 *
 * @param ht Pointer to the hash table.
 *
 * @return Number of slots in the table (capacity).
 */
uint32_t ht_capacity(
        const HashTab *ht
);

#endif /* OPEN_TABLE_H */
