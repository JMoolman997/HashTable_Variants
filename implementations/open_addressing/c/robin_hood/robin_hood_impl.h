/**
 * @file    robin_hood_impl.h
 * @brief   Internal Robin Hood backend interface and representation.
 *
 * Declares the internal constants, slot/table layouts, backend entry points,
 * and benchmark binding hook for the Robin Hood implementation.
 *
 * This header is intended for the core wrapper and Robin Hood backend support
 * code. It is not part of the public user-facing API.
 *
 * @author  Christiaan Swanepoel
 * @date    2026-03-23
 */

#ifndef ROBIN_HOOD_IMPL_H
#define ROBIN_HOOD_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_bench.h"
#include "ht_types.h"

struct ht_vtable;

/** Slot state values used by the Robin Hood backend. */
enum { ROBIN_HOOD_SLOT_EMPTY = 0, ROBIN_HOOD_SLOT_FULL = 1 };

/**
 * @brief One slot in the Robin Hood table.
 */
typedef struct {
  uint8_t state;  /**< Slot state: empty or full. */
  uint64_t hash;  /**< Cached mixed 64-bit hash for the key. */
  ht_key_t key;   /**< Stored key when the slot is full. */
  ht_val_t value; /**< Stored value when the slot is full. */
} robin_hood_slot;

/**
 * @brief Private state for the Robin Hood backend.
 */
typedef struct {
  robin_hood_slot *slots; /**< Contiguous slot array for the table. */

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
} robin_hood_table;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Allocate and initialize a Robin Hood backend instance.
 *
 * @param cfg Backend configuration used to size and initialize the table.
 *
 * @return An initialized backend object on success, or `NULL` if `cfg` is
 *         invalid or allocation fails.
 */
ht_result robin_hood_create_impl_ex(const ht_config *cfg, void **out);

/**
 * @brief Return the Robin Hood backend vtable used by the core wrapper.
 *
 * @return A pointer to the static Robin Hood backend dispatch table.
 */
const struct ht_vtable *robin_hood_vtable(void);

/**
 * @brief Populate a benchmark interface with Robin Hood direct operations.
 *
 * @param ctx Robin Hood backend instance to expose through benchmark hooks.
 * @param out Output interface to populate.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
int robin_hood_bind_bench_iface(void *ctx, bench_iface *out);

#endif /* ROBIN_HOOD_IMPL_H */
