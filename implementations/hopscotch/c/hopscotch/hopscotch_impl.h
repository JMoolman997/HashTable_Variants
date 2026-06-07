/**
 * @file    hopscotch_impl.h
 * @brief   Internal hopscotch backend interface and representation.
 *
 * Declares the private state, backend entry points, direct benchmark hooks,
 * and helper declarations for the hopscotch backend implementation.
 *
 * This header is intended for the core wrapper and hopscotch support code.
 * It is not part of the public user-facing API.
 *
 * @author  J.W. Moolman
 * @date    2026-04-16
 */

#ifndef HOPSCOTCH_IMPL_H
#define HOPSCOTCH_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_bench.h"
#include "ht_types.h"

struct ht_vtable;

/*
 * 64-bit hopscotch configuration.
 *
 * HOP_RANGE:
 *   Final legal placement range.
 *   Every key must live within 64 slots of its home bucket.
 *
 * ADD_RANGE:
 *   Resize-mode empty-slot search budget before growing and retrying.
 *   Fixed-capacity tables scan the whole table because they cannot recover by
 *   growing. In both modes, the empty slot may initially be farther than
 *   64 slots away, then gets moved backward through legal hopscotch
 *   displacements.
 */
#ifndef HOPSCOTCH_HOP_RANGE
#define HOPSCOTCH_HOP_RANGE 64u
#endif

#ifndef HOPSCOTCH_ADD_RANGE
#define HOPSCOTCH_ADD_RANGE 512u
#endif

_Static_assert(
    HOPSCOTCH_HOP_RANGE <= 64u,
    "64-bit hop_info only supports HOPSCOTCH_HOP_RANGE <= 64"
);

/** Slot state values used by the hopscotch backend. */
enum {
    HOPSCOTCH_SLOT_EMPTY = 0,
    HOPSCOTCH_SLOT_FULL  = 1
};

/**
 * @brief Overflow entry used when a fixed-capacity table has an unplaceable
 *        hopscotch neighborhood before reaching its configured load limit.
 */
typedef struct hopscotch_overflow_entry {
    uint64_t hash;  /**< Cached hash for the stored key. */
    ht_key_t key;   /**< Stored key. */
    ht_val_t value; /**< Stored value. */
} hopscotch_overflow_entry;

/**
 * @brief Physical entry payload stored beside the bucket's home bitmap.
 */
typedef struct hopscotch_payload {
    uint64_t hash;  /**< Cached hash for the stored key. */
    ht_key_t key;   /**< Stored key. */
    ht_val_t value; /**< Stored value. */
} hopscotch_payload;

/**
 * @brief Cache-line friendly bucket: home bitmap plus local payload.
 */
typedef struct hopscotch_bucket {
    uint64_t hop_info; /**< Bitmap of occupied offsets from this home bucket. */
    hopscotch_payload payload; /**< Payload stored in this physical slot. */
} hopscotch_bucket;

/**
 * @brief Private state for the hopscotch backend.
 *
 * This is the private representation used by the hopscotch backend.
 */
typedef struct hopscotch_table {
    hopscotch_bucket *buckets; /**< Per-slot hop bitmap plus payload. */
    uint8_t  *state;    /**< Compact occupancy bytes, scanned for empties. */
    uint8_t  *tag;      /**< 8-bit hash fingerprints for candidate filtering. */
    hopscotch_overflow_entry *overflow; /**< Rare fixed-mode spill entries. */

    size_t capacity;      /**< Total number of slots, always a power of two. */
    size_t mask;          /**< Cached capacity - 1 index mask. */
    size_t min_capacity;  /**< Smallest capacity the table may shrink to. */
    size_t size;          /**< Number of live entries currently stored. */
    size_t used;          /**< Live entries plus occupied slots. */
    size_t overflow_size; /**< Number of live entries in the overflow stash. */
    size_t overflow_capacity; /**< Allocated overflow entry capacity. */
    size_t hop_range;     /**< Effective hop range for this capacity. */
    size_t resize_search_limit; /**< Resize-mode empty-slot scan budget. */

    double max_load_factor; /**< Upper load factor threshold for growth. */
    double min_load_factor; /**< Lower load factor threshold for shrink. */
    ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

    ht_hash_fn hash_fn;   /**< Hash function used for all key lookups. */
    uint64_t hash_seed;   /**< Seed passed into `hash_fn`. */

    int collect_stats;  /**< Non-zero when benchmark statistics are tracked. */
    ht_stats stats;     /**< Accumulated operation and memory statistics. */
} hopscotch_table;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Allocate and initialize a hopscotch backend instance.
 *
 * @param cfg Backend configuration used to size and initialize the table.
 *
 * @return An initialized backend object on success, or `NULL` if `cfg` is
 *         invalid or allocation fails.
 */
ht_result hopscotch_create_impl_ex(
    const ht_config *cfg,
    void           **out
);

/**
 * @brief Return the hopscotch backend vtable used by the core wrapper.
 *
 * @return A pointer to the static hopscotch backend dispatch table.
 */
const struct ht_vtable *hopscotch_vtable(
    void
);

/**
 * @brief Populate a benchmark interface with hopscotch direct operations.
 *
 * @param ctx Hopscotch backend instance to expose.
 * @param out Output interface to populate.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
int hopscotch_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

#endif /* HOPSCOTCH_IMPL_H */
