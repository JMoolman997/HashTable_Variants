/**
 * @file    open_table_V1_1.c
 * @brief   A open addressing hashtable which uses uses a SoA instead of AoS 
 *          to try and improve cache friendliness  
 * @author  J.W Moolman
 * @date    2025-3-31
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "open_table.h"
#include "debug_hashtab.h"

#define PRINT_BUFFER_SIZE 1024

#define SAFETY_CHECKS_ENABLED 1

#if SAFETY_CHECKS_ENABLED
#define LOG_ERROR(fmt, ...) \ 
    fprintf(stderr, "%s:%d " fmt "\n", __FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...) ((void)0)
#endif

#define GET_ARG_COUNT(...) GET_ARG_COUNT_HELPER(__VA_ARGS__, 3, 2, 1)
#define GET_ARG_COUNT_HELPER(_1, _2, _3, count, ...) count

#define CHECK_CONDITION_2(cond, return_val) \
    do { if (!(cond)) return (return_val); } while (0)

#define CHECK_CONDITION_3(cond, msg, return_val) \
    do { \
        if (!(cond)) { \
            LOG_ERROR("%s", msg); \
            return (return_val); \
        } \
    } while (0)

#define CHECK_CONDITION(...) \
    _CHECK_CONDITION(GET_ARG_COUNT(__VA_ARGS__), __VA_ARGS__)

#define _CHECK_CONDITION(N, ...) \
    _CHECK_CONDITION_IMPL(N, __VA_ARGS__)

#define _CHECK_CONDITION_IMPL(N, ...) CHECK_CONDITION_##N(__VA_ARGS__)

#define CHECK_NULL(...) CHECK_CONDITION(__VA_ARGS__)
#define CHECK_RANGE(val, min, max, ...) \
    CHECK_CONDITION((val) >= (min) && (val) <= (max), __VA_ARGS__)
#define CHECK_NONZERO(val, ...) CHECK_CONDITION((val) != 0, __VA_ARGS__)

/* a hash table container */
struct hashtab {

    uint32_t *hash_keys;  /* Array of hash codes for quick comparison      */
    uint32_t *psls;       /* Array of probe sequence lengths               */
    void **keys;          /* Array of pointers to key data                 */
    void **values;        /* Array of pointers to value data               */

    uint32_t size;       /* Current size (capacity) of the table           */
    uint32_t active;     /* Number of non-empty entries (active)           */

    float load_factor;       /* Max load factor before resizing            */
    float min_load_factor;   /* Min load factor to consider downsizing     */

    uint32_t (*hash_func)(const void *key, size_t len);
	int (*cmp_func)(const void *a, const void *b);

    void (*free_key)(void *k);
    void (*free_val)(void *v);
};

/* --- function prototypes -------------------------------------------------- */

static uint32_t default_hash_func(
        const void *key, size_t len
);
static int default_cmp_func(
        const void *a, const void *b
);
static HTResult insert_entry(
        HashTab *ht, uint32_t hash_key, void *key, void *value
);
static void rehash_entries(
    HashTab *ht, uint32_t *old_hash_keys, uint32_t *old_psls, 
    void **old_keys, void **old_values, uint32_t old_size
);
static HTResult remove_entry(
        HashTab *ht, uint32_t hash_key, const void *key
);
static void shift_entries_backward(
        HashTab *ht, uint32_t current_index, uint32_t hash_key,
        uint32_t *probe_count
);
static void remove_table_update(
        HashTab *ht
);
static HTResult resize(
        HashTab *ht, uint32_t new_size
);
static void free_entry(
        HashTab *ht, uint32_t index
);
static inline void get_entry(
    const HashTab *ht, uint32_t index, uint32_t *hash_key,
    uint32_t *psl, void **key, void **value
);
static inline void set_entry(
    HashTab *ht, uint32_t index, uint32_t hash_key,
    uint32_t psl, void *key, void *value
);
static inline void clear_entry(
    HashTab *ht, uint32_t index
);
static inline uint32_t probe_func(
        uint32_t k, uint32_t i, uint32_t m
);

static inline HTResult validate_load_factors(
        float load_factor, float min_load_factor
);
static inline HTResult validate_size(
        uint32_t size, uint32_t new_size
);
/* --- hash table interface ------------------------------------------------- */

HashTab *ht_create(
    const HTConfig *config 
) {
    HashTab *ht;

    DBG_start("init_ht_");
    CHECK_NULL(config, "HTConfig NULL", NULL);

    if (
        validate_load_factors(config->load_factor, config->min_load_factor) != HT_SUCCESS
    ) {return NULL;}
    
    ht = (HashTab *)malloc(sizeof(HashTab));
    CHECK_NULL(ht, "Hashtable allocation failed", NULL);

    /* Initialize load tracking variables */
    ht->size = 2;
    ht->active = 0;
    
    /* Initialize load factors with defaults if zero */
    ht->load_factor = config->load_factor;
    ht->min_load_factor = config->min_load_factor;

    /* Initialize function ptrs withe defaults if NULL */
    ht->hash_func = config->hash_func ? config->hash_func : default_hash_func;
    ht->cmp_func = config->cmp_func ? config->cmp_func : default_cmp_func;
    ht->free_key = config->free_key ? config->free_key : NULL;
    ht->free_val = config->free_val ? config->free_val : NULL;

    /* Initialize structure of arrays for table entries */
    ht->hash_keys = (uint32_t *)calloc(ht->size, sizeof(uint32_t));
    ht->psls = (uint32_t *)calloc(ht->size, sizeof(uint32_t));
    ht->keys = (void **)calloc(ht->size, sizeof(void *));
    ht->values = (void **)calloc(ht->size, sizeof(void *));
    CHECK_NULL(ht->hash_keys, "Hash keys allocation failed", NULL);
    CHECK_NULL(ht->psls, "PSLs allocation failed", NULL);
    CHECK_NULL(ht->keys, "Keys allocation failed", NULL);
    CHECK_NULL(ht->values, "Values allocation failed", NULL);

    DBG_end("_init_ht");

	return ht;
}

void *ht_search(
        const HashTab *ht,
        const void *key,
        size_t key_len
) {
    uint32_t i, hash_key, index;

    DBG_info("ht_search");
    CHECK_NULL(ht,"ht_search: HashTab NULL", NULL);
    CHECK_NULL(key, "HT_search: Key NULL", NULL);
    CHECK_NONZERO(key_len, "ht_search: Zero key length", NULL);

    hash_key = ht->hash_func(key, key_len);

    for (i = 0; i < ht->size; i++) {
        /* calculate index to probe */
        index = probe_func(hash_key, i, ht->size);

        /* empty bucket key not in table */

        if (ht->keys[index] == NULL) {return NULL;}
        if (
            ht->hash_keys[index] == hash_key &&
            ht->cmp_func(ht->keys[index], key) == 0
        ) {
            /* key found return */
            return ht->values[index];
        }
        /* if the current entries psl is less the i(probe length) ,the entry
         * would have been swapped earlier if if was present */
        if (ht->psls[index] < i) {return NULL;}
    }

    DBG_info("ht_search: Key not found");
    return NULL;
    
}

HTResult ht_insert(
        HashTab *ht,
        const void *key,
        size_t key_len,
        void *value
) {
    uint32_t hash_key;
    HTResult result;

    CHECK_NULL(ht, "ht_insert: HashTab NULL", HT_INVALID_ARG);
    CHECK_NULL(key, "ht_insert: Key NULL", HT_INVALID_ARG);
    CHECK_NONZERO(key_len, "ht_insert: Zero key length", HT_INVALID_ARG);

    if (ht_search(ht, key, key_len)) {
        return HT_KEY_EXISTS;// replace with CHECK_NULL if possible
    }

    if (ht->active + 1 > ht->size * ht->load_factor) {
        result = validate_size(ht->size, ht->size << 1);
        if (result != HT_SUCCESS) {return result;}
        result = resize(ht, ht->size << 1);
        if (result != HT_SUCCESS) {return result;}
    }

    hash_key = ht->hash_func(key, key_len);
    return insert_entry(
        ht,
        hash_key,
        (void *)key,
        value
    );
}

/**
 * @brief Removes a key and its associated value from the hash table.
 * @param ht Pointer to the hash table.
 * @param key Pointer to the key to remove.
 * @param key_len Length of the key in bytes.
 * @return HT_SUCCESS on success,
 *         HT_INVALID_ARG if inputs are invalid,
 *         HT_KEY_NOT_FOUND if key isn’t found.
 */
HTResult ht_remove(HashTab *ht, const void *key, size_t key_len) {
    CHECK_NULL(ht, "ht_remove: HashTab NULL", HT_INVALID_ARG);
    CHECK_NULL(key, "ht_remove: Key NULL", HT_INVALID_ARG);
    CHECK_NONZERO(key_len, "ht_remove: Zero key length", HT_INVALID_ARG);

    uint32_t hash_key = ht->hash_func(key, key_len);
    return remove_entry(ht, hash_key, key);
}

void ht_destroy(
		HashTab *ht
) {
    uint32_t i;

    if (ht == NULL) {
        return;
    }

    for (i = 0; i < ht->size; i++) {
        if (ht->keys[i] != NULL) {
            free_entry(ht,i);
        }
    }

    free(ht->hash_keys);
    free(ht->psls);
    free(ht->keys);
    free(ht->values);
    ht->hash_keys = NULL;
    ht->psls = NULL;
    ht->keys = NULL;
    ht->values = NULL;
    ht->hash_func = NULL;
    ht->cmp_func = NULL;
    free(ht);
}

void ht_print(
    const HashTab *ht,
    void (*format_key)(void *key, char *buf, size_t buf_size),
    void (*format_value)(void *value, char *buf, size_t buf_size)
) {

    char key_buffer[PRINT_BUFFER_SIZE];
    char value_buffer[PRINT_BUFFER_SIZE];
    if (!ht || !format_key || !format_value) return;

    printf("--- HashTab - size[%u] - entries[%u] - loadfct[%.2f] ---\n",
           ht->size, ht->active, ht->load_factor);

    for (uint32_t i = 0; i < ht->size; i++) {
        if (ht->keys[i] != NULL) {  // Check if the slot is occupied
            format_key(ht->keys[i], key_buffer, PRINT_BUFFER_SIZE);
            format_value(ht->values[i], value_buffer, PRINT_BUFFER_SIZE);
            printf(
                    "Index %u: hash=%u, psl=%u, key=%s, value=%s\n", 
                   i, ht->hash_keys[i], ht->psls[i], key_buffer, value_buffer
            );
        }
    }
}

uint32_t ht_capacity(
        const HashTab *ht
) {
    CHECK_NULL(ht, "ht_capacity: HashTab NULL", 0);
    return ht->size;
}

/* --- utility functions ---------------------------------------------------- */

/**
 * @brief Inserts a key-value pair into the hash table using Robin Hood hashing.
 * @param ht Pointer to the hash table.
 * @param hash_key Precomputed hash value of the key.
 * @param key Pointer to the key data.
 * @param value Pointer to the value data.
 * @return HT_SUCCESS on success, HT_INVALID_STATE if table is full.
 */
static HTResult insert_entry(
        HashTab *ht,
        uint32_t hash_key,
        void *key,
        void *value
) {
    uint32_t i, index, psl, temp_psl, temp_hash_key;
    void *temp_key, *temp_value;

    i = 0;

    while (i < ht->size) {
        index = probe_func(hash_key, i, ht->size);
        /* empty buckect found */
        if (ht->keys[index] == NULL) {
            set_entry(
                ht,
                index,
                hash_key,
                i,
                key,
                value
            );
            ht->active++;
            return HT_SUCCESS;
        }
        /* compare probe length */
        if (i > ht->psls[index]) {
            /* swap "poorer" (further element steals the spot.) */
            temp_hash_key = ht->hash_keys[index];
            temp_psl = ht->psls[index];
            temp_key = ht->keys[index];
            temp_value = ht->values[index];
            set_entry(
                ht,
                index,
                hash_key,
                i,
                key,
                value
            );
            hash_key = temp_hash_key;
            key = temp_key;
            value = temp_value;
            //i = temp_psl + 1;
            //break;
        }
        i++;
    }

    /* should never occur */
    return HT_FAILURE;
}

/**
 * @brief Rehashes entries from an old table into a new table during resizing.
 * @param ht Pointer to the hash table with the new table allocated.
 * @param old_table Pointer to the old table’s entries.
 * @param old_size Size of the old table.
 */
static void rehash_entries(
    HashTab *ht,
    uint32_t *old_hash_keys,
    uint32_t *old_psls, 
    void **old_keys,
    void **old_values,
    uint32_t old_size
) {
    uint32_t i;
    for (i = 0; i < old_size; i++) {
        if (old_keys[i] != NULL) {
            insert_entry(
                ht,
                old_hash_keys[i],
                old_keys[i],
                old_values[i]
            );
        }
    }
}

/**
 * @brief Attempts to find and remove an entry with the given hash key and key.
 * @param ht Pointer to the hash table.
 * @param hash_key Precomputed hash value of the key.
 * @param key Pointer to the key to remove.
 * @return HT_SUCCESS if removed, HT_KEY_NOT_FOUND if not found.
 */
static HTResult remove_entry(
        HashTab *ht,
        uint32_t hash_key,
        const void *key
) {
    uint32_t probe_count, current_index;
    for (probe_count = 0; probe_count < ht->size; probe_count++) {
        current_index = probe_func(hash_key, probe_count, ht->size);

        if (ht->keys[current_index] == NULL) {
            return HT_KEY_NOT_FOUND;
        }

        if (
            ht->hash_keys[current_index] == hash_key &&
            ht->cmp_func(ht->keys[current_index], key) == 0
        ) {
            free_entry(ht, current_index);
            shift_entries_backward(ht, current_index, hash_key, &probe_count);
            remove_table_update(ht);
            return HT_SUCCESS;
        }

        if (ht->psls[current_index] < probe_count) {
            return HT_KEY_NOT_FOUND;
        }
    }
    return HT_KEY_NOT_FOUND;
}

/**
 * @brief Shifts subsequent entries backward to fill the gap after removal.
 * @param ht Pointer to the hash table.
 * @param current_index Index of the removed entry.
 * @param hash_key Hash key for probing.
 * @param probe_count Pointer to the current probe iteration (updated in-place).
 */
static void shift_entries_backward(
        HashTab *ht,
        uint32_t current_index,
        uint32_t hash_key,
        uint32_t *probe_count
) {
    uint32_t next_index = probe_func(hash_key, ++(*probe_count), ht->size);

    while (ht->keys[next_index] != NULL && ht->psls[next_index] > 0) {
        /* Move back and adjust probe sequence length */
        set_entry(
            ht,
            current_index,
            ht->hash_keys[next_index],
            ht->psls[next_index] - 1,
            ht->keys[next_index],
            ht->values[next_index]
        );
        current_index = next_index;
        next_index = probe_func(hash_key, ++(*probe_count), ht->size);
    }

    clear_entry(ht, current_index);  /* Mark last shifted slot as empty */
}

/**
 * @brief Updates the table state after removal, including resizing if needed.
 * @param ht Pointer to the hash table.
 */
static void remove_table_update(
        HashTab *ht
) {
    ht->active--;
    if (ht->active < (float)ht->size * ht->min_load_factor && ht->size > 2) {
        resize(ht, ht->size / 2);  /* Downsize if below min load factor */
    }
}

/**
 * @brief Resizes the hash table to a new capacity.
 * @param ht Pointer to the hash table.
 * @param new_size New capacity of the table.
 * @return HT_SUCCESS on success, HT_OUT_OF_MEMORY or HT_INVALID_ARG on failure.
 */
static HTResult resize(
        HashTab *ht,
        uint32_t new_size
) {

    uint32_t *old_hash_keys = ht->hash_keys;
    uint32_t *old_psls = ht->psls;
    void **old_keys = ht->keys;
    void **old_values = ht->values;
    uint32_t old_size = ht->size;
    HTResult result;

    result = validate_size(ht->size, new_size);
    if (result != HT_SUCCESS) return result;

    ht->hash_keys = (uint32_t *)calloc(new_size, sizeof(uint32_t));
    ht->psls = (uint32_t *)calloc(new_size, sizeof(uint32_t));
    ht->keys = (void **)calloc(new_size, sizeof(void *));
    ht->values = (void **)calloc(new_size, sizeof(void *));
    if (!ht->hash_keys || !ht->psls || !ht->keys || !ht->values) {
        free(ht->hash_keys);
        free(ht->psls);
        free(ht->keys);
        free(ht->values);
        ht->hash_keys = old_hash_keys;
        ht->psls = old_psls;
        ht->keys = old_keys;
        ht->values = old_values;
        return HT_MEM_ERROR;
    }

    ht->size = new_size;
    ht->active = 0;

    rehash_entries(ht, old_hash_keys, old_psls, old_keys, old_values, old_size);
    free(old_hash_keys);
    free(old_psls);
    free(old_keys);
    free(old_values);
    return HT_SUCCESS;    
}

/**
 * @brief Frees the memory associated with a hash table entry.
 * @param ht Pointer to the hash table.
 * @param entry Pointer to the entry to free.
 */
static void free_entry(
        HashTab *ht,
        uint32_t index
) {
    if (ht->free_key) {ht->free_key(ht->keys[index]);}
    if (ht->free_val) {ht->free_val(ht->values[index]);}
    clear_entry(ht, index);
}

/* Helper function to retrieve all fields of an entry at index */
static inline void get_entry(
    const HashTab *ht,
    uint32_t index, 
    uint32_t *hash_key,
    uint32_t *psl, 
    void **key,
    void **value
) {
    *hash_key = ht->hash_keys[index];
    *psl = ht->psls[index];
    *key = ht->keys[index];
    *value = ht->values[index];
}

/* Helper function to set all fields of an entry at index */
static inline void set_entry(
    HashTab *ht,
    uint32_t index, 
    uint32_t hash_key,
    uint32_t psl, 
    void *key,
    void *value
) {
    ht->hash_keys[index] = hash_key;
    ht->psls[index] = psl;
    ht->keys[index] = key;
    ht->values[index] = value;
}

/* Helper function to clear an entry */
static inline void clear_entry(
    HashTab *ht,
    uint32_t index
) {
    ht->hash_keys[index] = 0;
    ht->psls[index] = 0;
    ht->keys[index] = NULL;
    ht->values[index] = NULL;
}

/**
 * @brief Computes the probe index using linear probing with a power-of-2 table size.
 * @param k Hash key value.
 * @param i Probe iteration number.
 * @param m Table size (must be a power of 2).
 * @return Index into the hash table.
 */
static inline uint32_t probe_func(
    uint32_t k,
    uint32_t i,
    uint32_t m
) {
    return (k + i) & (m - 1);
}

/* --- default functions ---------------------------------------------------- */

/**
 * @brief Computes a default hash value for a key using the FNV-1a algorithm.
 * @param key Pointer to the key data.
 * @param len Length of the key in bytes.
 * @return 32-bit hash value.
 */
static uint32_t default_hash_func(
    const void *key,
    size_t len
) {
    const unsigned char *bytes_ptr = (const unsigned char *)key;
    unsigned int hash = 2166136261u; // FNV offset basis
    unsigned int fnv_prime = 16777619u; // FNV prime

    for (size_t i = 0; i < len; i++) {
        hash ^= bytes_ptr[i];       // XOR with the byte
        hash *= fnv_prime;          // Multiply by FNV prime
    }

    return hash;
}

/**
 * @brief Compares two integer keys for equality or ordering.
 * @param a Pointer to the first key.
 * @param b Pointer to the second key.
 * @return Negative if a < b, 0 if a == b, positive if a > b.
 */
static int default_cmp_func(
    const void *a,
    const void *b
) {
    int int_a = *(const int *)a;
    int int_b = *(const int *)b;
    return (int_a > int_b) - (int_a < int_b);
}

/* --- validadation functions ---------------------------------------------- */

/**
 * @brief Validates load factor values for correctness.
 * @param load_factor Maximum load factor.
 * @param min_load_factor Minimum load factor.
 * @return HT_SUCCESS if valid, HT_INVALID_ARG if invalid.
 */
static inline HTResult validate_load_factors(
    float load_factor,
    float min_load_factor
) {
    if (load_factor <= 0 || load_factor > 1) {
        LOG_ERROR("Invalid load_factor: %.2f", load_factor);
        return HT_INVALID_ARG;
    }
    if (min_load_factor < 0 || min_load_factor >= load_factor) {
        LOG_ERROR("Invalid min_load_factor: %.2f", min_load_factor);
        return HT_INVALID_ARG;
    }
    return HT_SUCCESS;
}

/**
 * @brief Validates a new size against constraints.
 * @param size Current size of the table.
 * @param new_size Proposed new size.
 * @return HT_SUCCESS if valid, HT_INVALID_ARG or HT_OUT_OF_MEMORY if invalid.
 */
static inline HTResult validate_size(
    uint32_t size,
    uint32_t new_size
) {
    if (new_size == 0 || new_size > UINT32_MAX / 2) {
        LOG_ERROR("Invalid size: %u", new_size);
        return HT_FAILURE;
    }
    return HT_SUCCESS;
}
