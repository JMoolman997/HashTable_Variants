/**
 * @file    ht.h
 * @brief   Public generic API for interchangeable hashtable implementations.
 *
 * Declares the opaque hashtable handle and the common operations exposed by
 * every backend.
 *
 * Client code, tests, and benchmark setup should interact with hashtables 
 * through this interface rather than through implementation-specific types.
 *
 * @author  J.W Moolman
 * @date    2026-03-23
 */

#ifndef HT_H
#define HT_H

#include "ht_types.h"

typedef struct ht_map ht_map;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Return a default configuration for a backend implementation.
 *
 * Backend sizing and load-factor defaults are left at zero so each concrete
 * implementation can apply its own defaults. The returned configuration uses
 * grow-only resizing, the default hash function, one thread, and disabled
 * statistics collection.
 */
ht_config ht_config_default(
    ht_impl impl
);

/**
 * @brief Return a fixed-capacity configuration for a backend implementation.
 */
ht_config ht_config_fixed(
    ht_impl impl,
    size_t  capacity
);

/**
 * @brief Return a grow/shrink resizing configuration for a backend.
 */
ht_config ht_config_resizing(
    ht_impl impl,
    size_t  capacity
);

/**
 * @brief Create a hashtable and return a diagnostic result code.
 *
 * @param cfg Configuration describing the backend, sizing policy, hashing,
 *            and statistics settings.
 * @param out Output pointer that receives the new table on success. This is
 *            set to `NULL` before any work is attempted.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID`, `HT_ERR_UNSUPPORTED`, or
 *         `HT_ERR_OOM` on failure.
 */
ht_result ht_create_ex(
    const ht_config *cfg,
    ht_map         **out
);

/**
 * @brief Create a hashtable instance for the selected backend.
 *
 * @param cfg Configuration describing the backend, sizing policy, hashing,
 *            and statistics settings.
 *
 * @return A newly allocated hashtable on success, or `NULL` if `cfg` is
 *         invalid or allocation/backend initialization fails.
 */
ht_map *ht_create(
    const ht_config *cfg
);

/**
 * @brief Destroy a hashtable instance created by ht_create().
 *
 * @param map Hashtable instance to destroy. `NULL` is ignored.
 */
void ht_destroy(
    ht_map *map
);

/**
 * @brief Insert a key/value pair into the hashtable.
 *
 * @param map Hashtable instance to modify.
 * @param key Key to insert.
 * @param value Value to associate with `key`.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`,
 *         `HT_ERR_EXISTS`, `HT_ERR_FULL`, or `HT_ERR_OOM`. If `key` is
 *         already present, implementations must report `HT_ERR_EXISTS` before
 *         capacity/fullness failures such as `HT_ERR_FULL`.
 */
ht_result ht_insert(
    ht_map *map,
    ht_key_t key,
    ht_val_t value
);

/**
 * @brief Look up a key in the hashtable.
 *
 * @param map Hashtable instance to query.
 * @param key Key to search for.
 * @param value_out Output location that receives the value on success.
 *
 * @return `HT_OK` when the key is found, or an error such as
 *         `HT_ERR_INVALID` or `HT_ERR_NOT_FOUND`.
 */
ht_result ht_get(
    const ht_map *map,
    ht_key_t key,
    ht_val_t *value_out
);

/**
 * @brief Remove a key from the hashtable.
 *
 * @param map Hashtable instance to modify.
 * @param key Key to remove.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
ht_result ht_remove(
    ht_map *map,
    ht_key_t key
);

/**
 * @brief Test whether a key is present.
 *
 * @return `1` when found, otherwise `0`. Invalid maps count as not found.
 */
int ht_contains(
    const ht_map *map,
    ht_key_t     key
);

/**
 * @brief Insert a new key or update an existing key.
 */
ht_result ht_upsert(
    ht_map  *map,
    ht_key_t key,
    ht_val_t value
);

/**
 * @brief Return the number of live entries stored in the table.
 *
 * @param map Hashtable instance to query.
 *
 * @return The number of live entries, or `0` if `map` is invalid.
 */
size_t ht_size(
    const ht_map *map
);

/**
 * @brief Return the current slot or bucket capacity of the table.
 *
 * @param map Hashtable instance to query.
 *
 * @return The current capacity, or `0` if `map` is invalid.
 */
size_t ht_capacity(
    const ht_map *map
);

/**
 * @brief Return the current load factor of the table.
 *
 * @param map Hashtable instance to query.
 *
 * @return The current load factor, or `0.0` if `map` is invalid.
 */
double ht_load_factor(
    const ht_map *map
);

/**
 * @brief Ensure the table can hold at least the requested capacity.
 *
 * @param map Hashtable instance to resize if needed.
 * @param capacity Minimum capacity to guarantee.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`,
 *         `HT_ERR_OOM`, or a backend-specific resize failure.
 */
ht_result ht_reserve(
    ht_map *map,
    size_t capacity
);

/**
 * @brief Rebuild the table using a capacity suitable for the requested size.
 *
 * @param map Hashtable instance to rehash.
 * @param capacity Requested target capacity.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
ht_result ht_rehash(
    ht_map *map,
    size_t capacity
);

/**
 * @brief Copy the current statistics snapshot out of the table.
 *
 * @param map Hashtable instance to query.
 * @param out Output structure that receives the copied statistics.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`.
 */
ht_result ht_get_stats(
    const ht_map *map,
    ht_stats *out
);

/**
 * @brief Reset the table's accumulated statistics counters.
 *
 * @param map Hashtable instance whose statistics should be cleared.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`.
 */
ht_result ht_reset_stats(
    ht_map *map
);

/**
 * @brief Return the stable public name for an implementation kind.
 */
const char *ht_impl_name(
    ht_impl impl
);

/**
 * @brief Return the stable public name for a result code.
 */
const char *ht_result_name(
    ht_result result
);

#endif /* HT_H */
