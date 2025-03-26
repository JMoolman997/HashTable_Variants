/**
 * @file    open_addressing.h
 * @brief   A modular open addressing hash table implementation for
 *          testing and benchmarking.
 * @author  J.W Moolman
 * @date    2024-10-23
 */

#ifndef OPEN_ADDRESSING_H
#define OPEN_ADDRESSING_H

#include <stdint.h>
/* --- Macros -------------------------------------------------------------- */

/** Default maximum load factor before resizing the hash table */
#define DEFAULT_LOAD_FACTOR 0.5
/** Default minimum load factor before attempting downsizing */
#define DEFAULT_MIN_LOAD_FACTOR 0.25
/** Default inactive entry threshold for downsizing */
#define DEFAULT_INACTIVE_FACTOR 0.1
/** Default maximum size of the hash table */
#define DEFAULT_SIZE_MAX 1048576
/** Default minimum size of the hash table */
#define DEFAULT_SIZE_MIN 13

/* --- Error Return Codes --------------------------------------------------- */

#define HT_FAILURE 1
#define HT_SUCCESS 0
#define HT_KEY_EXISTS -1
#define HT_NO_SPACE   -2
#define HT_KEY_NOT_FOUND -3
#define HT_MEM_ERROR -4
#define HT_INVALID_ARG -5
#define HT_INVALID_STATE -6

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

/* --- Function Prototypes ------------------------------------------------- */

/**
 * @brief Initialize a hash table with configurable parameters.
 * 
 * @param load_factor        Maximum load factor before resizing.
 * @param min_load_factor    Minimum load factor before downsizing.
 * @param inactive_factor    Threshold for inactive entries to trigger downsizing.
 * @param hash_func          Function pointer to the hash function.
 * @param cmp_func           Function pointer to the key comparison function.
 * @param p                  Function pointer to the probing method.
 * @return A pointer to the initialized hash table, or NULL on failure.
 */
HashTab *init_ht(
        float load_factor,
        float min_load_factor,
        float inactive_factor,
        uint32_t (*hash_func)(void *key, size_t len),
        int (*cmp_func)(const void *key1, const void *key2),
        uint32_t (*p)(uint32_t k, uint32_t i, uint32_t m),
        void (*freekey)(void *k),
        void (*freeval)(void *v)
);

/**
 * @brief Free the memory allocated for a hash table.
 * 
 * @param self      Pointer to the hash table.
 * @return HT_SUCCESS on success, or an error code on failure.
 */
int free_ht(
        HashTab *self
);

/**
 * @brief Search for a key in the hash table.
 * 
 * @param self  Pointer to the hash table.
 * @param key   Key to search for.
 * @return Index of the key if found, or an error code if not found.
 */
int search_ht(
        HashTab *self,
        void *key,
        size_t key_len
);

/**
 * @brief Fetch a pointer to the value at a specific index in the hash table.
 * 
 * @param self  Pointer to the hash table.
 * @param index Index of the entry to fetch.
 * @return Pointer to the value stored at the specified index, or NULL if invalid.
 */
void *fetch_ht(
        HashTab *self,
        uint32_t index
);

/**
 * @brief Insert a key-value pair into the hash table.
 * 
 * @param self   Pointer to the hash table.
 * @param key    Key to insert.
 * @param value  Value associated with the key.
 * @return HT_SUCCESS on success, or an error code on failure.
 */
int insert_ht(
        HashTab *self,
        void *key,
        size_t key_len,
        void *value
);

/**
 * @brief Remove a key from the hash table.
 * 
 * @param self  Pointer to the hash table.
 * @param key   Key to remove.
 * @return HT_SUCCESS on success, or an error code on failure.
 */
int remove_ht(
        HashTab *self,
        void *key,
        size_t key_len
);

/**
 * @brief Print the contents of the hash table.
 * 
 * @param self        Pointer to the hash table.
 * @param keyval2str  Function pointer to format key-value pairs as strings.
 */
void print_ht(
        HashTab *self,
        void (*keyval2str)(int flag, void *k, void *v, char *b)
);

/**
 * @brief Get the size of the hash table.
 * 
 * @param self  Pointer to the hash table.
 * @return The size of the hash table.
 */
size_t size_ht(
        HashTab *self
);

#endif /* OPEN_ADDRESSING_H */
