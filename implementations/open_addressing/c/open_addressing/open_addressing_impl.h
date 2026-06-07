/**
 * @file    open_addressing_impl.h
 * @brief   Internal open-addressing backend interface and representation.
 *
 * Declares the internal constants, slot/table layouts, backend entry points,
 * and benchmark binding hook for the open-addressing implementation.
 *
 * This header is intended for the core wrapper and open-addressing backend support
 * code. It is not part of the public user-facing API.
 *
 * @author  J.W. Moolman
 * @date    2026-03-23
 */

#ifndef OPEN_ADDRESSING_IMPL_H
#define OPEN_ADDRESSING_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"
#include "ht_bench.h"

struct ht_vtable;

/** Slot state values used by the open-addressing backend. */
enum {
    OPEN_ADDRESSING_SLOT_EMPTY = 0,
    OPEN_ADDRESSING_SLOT_FULL = 1,
    OPEN_ADDRESSING_SLOT_TOMBSTONE = 2
};

/**
 * @brief One slot in the open-addressing table.
 */
typedef struct {
    uint8_t state;   /**< Slot state: empty, full, or tombstone. */
    uint64_t hash;   /**< Cached mixed 64-bit hash for the key. */
    ht_key_t key;    /**< Stored key when the slot is full. */
    ht_val_t value;  /**< Stored value when the slot is full. */
} open_addressing_slot;

/**
 * @brief Private state for the open-addressing backend.
 */
typedef struct {
    open_addressing_slot *slots; /**< Contiguous slot array for the table. */

    size_t capacity;      /**< Total number of slots, always a power of two. */
    size_t min_capacity;  /**< Smallest capacity the table may shrink to. */
    size_t size;          /**< Number of live entries currently stored. */
    size_t used;          /**< Live entries plus tombstone slots. */

    double max_load_factor; /**< Upper load factor threshold for growth. */
    double min_load_factor; /**< Lower load factor threshold for shrink. */
    ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

    ht_hash_fn hash_fn;   /**< Hash function used for all key lookups. */
    uint64_t hash_seed;   /**< Seed passed into `hash_fn`. */

    int collect_stats;  /**< Non-zero when benchmark statistics are tracked. */
    ht_stats stats;     /**< Accumulated operation and memory statistics. */
} open_addressing_table;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Allocate and initialize a open-addressing backend instance.
 *
 * @param cfg Backend configuration used to size and initialize the table.
 *
 * @return An initialized backend object on success, or `NULL` if `cfg` is
 *         invalid or allocation fails.
 */
void *open_addressing_create_impl(
    const ht_config *cfg
);

/**
 * @brief Return the open-addressing backend vtable used by the core wrapper.
 *
 * @return A pointer to the static open-addressing backend dispatch table.
 */
const struct ht_vtable *open_addressing_vtable(
    void
);

/**
 * @brief Populate a benchmark interface with open-addressing-table direct operations.
 *
 * @param ctx Open-addressing backend instance to expose through the benchmark hooks.
 * @param out Output interface to populate.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
int open_addressing_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

#endif /* OPEN_ADDRESSING_IMPL_H */
