/**
 * @file    metadata_impl.h
 * @brief   Internal metadata-separated backend interface and representation.
 *
 * Declares the internal constants, slot/table layouts, backend entry points,
 * and benchmark binding hook for the metadata-separated implementation.
 *
 * This header is intended for the core wrapper and open-addressing backend
 * support code. It is not part of the public user-facing API.
 *
 * @author  Christiaan Swanepoel
 * @date    2026-03-23
 */

#ifndef METADATA_IMPL_H
#define METADATA_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_bench.h"
#include "ht_types.h"

struct ht_vtable;

/** Control-byte values for the metadata backend.
 *  0xFF marks empty slots, 0xFE marks tombstones, and 0x00-0x7F values are
 *  occupied-slot hash tags.
 */
enum {
  METADATA_EMPTY = 0xFF,
  METADATA_TOMBSTONE = 0xFE,
  METADATA_TAG_MASK = 0x7F
};

/**
 * @brief Storage for the key/value data.
 */
typedef struct {
  uint64_t hash;  /**< Full mixed 64-bit hash for the key. */
  ht_key_t key;   /**< Stored key. */
  ht_val_t value; /**< Stored value. */
} metadata_data_slot;

/**
 * @brief Private state for the metadata-separated backend.
 */
typedef struct {
  uint8_t *ctrl;            /**< Metadata/control array (1 byte per slot). */
  metadata_data_slot *data; /**< Data array (hash, key, value). */

  size_t capacity;     /**< Total number of slots, always a power of two. */
  size_t min_capacity; /**< Smallest capacity the table may shrink to. */
  size_t size;         /**< Number of live entries currently stored. */
  size_t used;         /**< Live entries plus tombstone slots. */

  double max_load_factor;  /**< Upper load factor threshold for growth. */
  double min_load_factor;  /**< Lower load factor threshold for shrink. */
  ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

  ht_hash_fn hash_fn; /**< Hash function used for all key lookups. */
  uint64_t hash_seed; /**< Seed passed into `hash_fn`. */

  int collect_stats; /**< Non-zero when benchmark statistics are tracked. */
  ht_stats stats;    /**< Accumulated operation and memory statistics. */
} metadata_table;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Allocate and initialize a metadata-separated backend instance.
 *
 * @param cfg Backend configuration used to size and initialize the table.
 *
 * @return An initialized backend object on success, or `NULL` if `cfg` is
 *         invalid or allocation fails.
 */
ht_result metadata_create_impl_ex(const ht_config *cfg, void **out);

/**
 * @brief Return the metadata-separated backend vtable used by the core wrapper.
 *
 * @return A pointer to the static open-addressing backend dispatch table.
 */
const struct ht_vtable *metadata_vtable(void);

/**
 * @brief Populate a benchmark interface with open-addressing-table direct
 * operations.
 *
 * @param ctx Open-addressing backend instance to expose through the benchmark
 * hooks.
 * @param out Output interface to populate.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
int metadata_bind_bench_iface(void *ctx, bench_iface *out);

#endif /* METADATA_IMPL_H */
