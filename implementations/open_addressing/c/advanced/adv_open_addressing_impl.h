/**
 * @file    adv_open_addressing_impl.h
 * @brief   Internal SIMD-optimized open-addressing backend interface.
 *
 * Declares the internal constants, layouts, and backend entry points for the
 * SIMD-optimized open-addressing hashtable implementation. Separate arrays
 * keep control tags, cached hashes, and entries cache-friendly during probing.
 *
 * This header is intended for the core wrapper and backend support
 * code. It is not part of the public user-facing API.
 *
 * @author  Christiaan Swanepoel
 * @date    2026-04-16
 */

#ifndef ADV_OPEN_ADDRESSING_IMPL_H
#define ADV_OPEN_ADDRESSING_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_bench.h"
#include "ht_types.h"

struct ht_vtable;

/** Control tag values used by the SIMD-optimized open-addressing backend. */
enum {
  ADV_OPEN_ADDRESSING_CTRL_EMPTY = 0x80u, /**< Slot is empty (bit 7 set). */
  ADV_OPEN_ADDRESSING_CTRL_FULL_MASK =
      0x7Fu /**< Mask for entry fingerprints (bit 7 clear). */
};

/**
 * @brief Actual key/value storage for one slot.
 */
typedef struct {
  ht_key_t key;   /**< Stored key when the slot is full. */
  ht_val_t value; /**< Stored value when the slot is full. */
} adv_open_addressing_entry;

/**
 * @brief Private state for the SIMD-optimized open-addressing backend.
 */
typedef struct {
  /**
   * @brief Control tags for probing. 1 byte per slot.
   * Use 0x80 for empty, and (hash & 0x7F) for full.
   * This array is padded with 15 extra bytes to allow for unaligned 16-byte
   * SIMD loads at the end of the table.
   */
  uint8_t *ctrl;

  /**
   * @brief Cached 64-bit hashes for secondary filtering.
   */
  uint64_t *hashes;

  /**
   * @brief Contiguous key/value storage array.
   */
  adv_open_addressing_entry *entries;

  size_t capacity;     /**< Total number of slots, always a power of two. */
  size_t min_capacity; /**< Smallest capacity the table may shrink to. */
  size_t size;         /**< Number of live entries currently stored. */
  size_t used; /**< Occupied slots; equal to size after backshift deletion. */

  double max_load_factor;  /**< Upper load factor threshold for growth. */
  double min_load_factor;  /**< Lower load factor threshold for shrink. */
  ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

  ht_hash_fn hash_fn; /**< Hash function used for all key lookups. */
  uint64_t hash_seed; /**< Seed passed into `hash_fn`. */

  int collect_stats; /**< Non-zero when benchmark statistics are tracked. */
  ht_stats stats;    /**< Accumulated operation and memory statistics. */
} adv_open_addressing_table;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Allocate and initialize a SIMD-optimized open-addressing backend
 * instance.
 *
 * @param cfg Backend configuration used to size and initialize the table.
 *
 * @return An initialized backend object on success, or `NULL` if `cfg` is
 *         invalid or allocation fails.
 */
void *adv_open_addressing_create_impl(const ht_config *cfg);

/**
 * @brief Return the SIMD-optimized open-addressing backend vtable used by the
 * core wrapper.
 *
 * @return A pointer to the static open-addressing backend dispatch table.
 */
const struct ht_vtable *adv_open_addressing_vtable(void);

/**
 * @brief Populate a benchmark interface with open-addressing direct operations.
 *
 * @param ctx Backend instance to expose through the benchmark hooks.
 * @param out Output interface to populate.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
int adv_open_addressing_bind_bench_iface(void *ctx, bench_iface *out);

#endif /* ADV_OPEN_ADDRESSING_IMPL_H */
