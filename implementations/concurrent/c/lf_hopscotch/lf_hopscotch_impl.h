/**
 * @file    lf_hopscotch_impl.h
 * @brief   Concurrent hopscotch backend with stop-the-world writer resize.
 *
 * The primary table is a bitmap-neighbourhood hopscotch table using C11
 * atomics.  Lookups never take the resize gate: they load the currently
 * published table generation and scan that immutable generation.  Writers enter
 * a lightweight gate.  A resize blocks new writers, waits for active writers to
 * drain, builds a new generation, publishes it, and keeps old generations alive
 * until destroy so readers that already loaded an old generation remain safe.
 *
 * When automatic resize is disabled, insertion overflow falls back to a small
 * lock-protected stash.  The stash is only touched after the primary table
 * misses/fails, so the normal hit path remains the hopscotch bitmap path.
 */

#ifndef LF_HOPSCOTCH_IMPL_H
#define LF_HOPSCOTCH_IMPL_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "ht_bench.h"
#include "ht_types.h"

struct ht_vtable;

#ifndef LF_HOPSCOTCH_HOP_RANGE
#define LF_HOPSCOTCH_HOP_RANGE 64u
#endif

#ifndef LF_HOPSCOTCH_ADD_RANGE
#define LF_HOPSCOTCH_ADD_RANGE 4096u
#endif

#ifndef LF_HOPSCOTCH_MIN_CAPACITY
#define LF_HOPSCOTCH_MIN_CAPACITY 64u
#endif

_Static_assert(
    LF_HOPSCOTCH_HOP_RANGE > 0u && LF_HOPSCOTCH_HOP_RANGE <= 64u,
    "lf hopscotch requires 1 <= LF_HOPSCOTCH_HOP_RANGE <= 64"
);

typedef enum {
    LF_HOPSCOTCH_EMPTY     = 0,
    LF_HOPSCOTCH_BUSY      = 1,
    LF_HOPSCOTCH_MOVING    = 2,
    LF_HOPSCOTCH_INSERTING = 3,
    LF_HOPSCOTCH_MEMBER    = 4
} lf_hopscotch_slot_state;

/**
 * @brief One atomic hopscotch slot in a published table generation.
 */
typedef struct {
    _Atomic uint64_t state; /**< Versioned slot state word. */
    _Atomic uint64_t hop_info; /**< Bitmap of entries in this home bucket. */
    _Atomic uint64_t reloc_counter; /**< Changes when the bitmap is relocated. */
    _Atomic uint64_t hash; /**< Cached hash for the stored key. */
    _Atomic ht_key_t key; /**< Stored key. */
    _Atomic ht_val_t value; /**< Stored value. */
} lf_hopscotch_bucket;

/**
 * @brief Published hopscotch table generation.
 */
typedef struct lf_hopscotch_core {
    lf_hopscotch_bucket *buckets; /**< Bucket array for this generation. */
    size_t capacity; /**< Bucket count, always a power of two. */
    size_t mask; /**< Cached capacity - 1 index mask. */
    size_t hop_range; /**< Legal neighbourhood range. */
    size_t add_range; /**< Empty-slot search range. */
    _Atomic size_t size; /**< Live entries in this generation. */
    struct lf_hopscotch_core *retired_next; /**< Next retired generation. */
} lf_hopscotch_core;

/**
 * @brief Overflow stash node used when resize is disabled.
 */
typedef struct lf_hopscotch_overflow_node {
    uint64_t hash; /**< Cached hash for the stored key. */
    ht_key_t key; /**< Stored key. */
    _Atomic ht_val_t value; /**< Stored value. */
    _Atomic unsigned live; /**< Non-zero while the node is visible. */
    struct lf_hopscotch_overflow_node *next; /**< Next stash node. */
} lf_hopscotch_overflow_node;

/**
 * @brief Private state for the concurrent hopscotch backend.
 */
typedef struct {
    _Atomic(lf_hopscotch_core *) current; /**< Active published generation. */
    lf_hopscotch_core *retired_cores; /**< Old generations kept for readers. */

    _Atomic(lf_hopscotch_overflow_node *) overflow_head; /**< Stash head. */
    _Atomic size_t overflow_size; /**< Live entries in the stash. */
    _Atomic size_t overflow_allocated; /**< Nodes allocated over table life. */
    atomic_flag overflow_lock; /**< Spin lock for stash mutation. */

    size_t min_capacity; /**< Smallest primary capacity allowed. */
    double max_load_factor; /**< Upper load factor threshold for growth. */
    double min_load_factor; /**< Lower load factor threshold for shrink. */
    ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */
    ht_hash_fn hash_fn; /**< Hash function used for key lookup. */
    uint64_t hash_seed; /**< Seed passed into `hash_fn`. */

    _Atomic unsigned resize_requested; /**< Blocks new writers during resize. */
    _Atomic size_t active_writers; /**< Writers currently inside the gate. */
    atomic_flag resize_lock; /**< Serializes resize attempts. */

    int collect_stats; /**< Non-zero when statistics are tracked. */
    _Atomic uint64_t inserts; /**< Insert attempt counter. */
    _Atomic uint64_t insert_failures; /**< Failed insert counter. */
    _Atomic uint64_t lookups; /**< Lookup counter. */
    _Atomic uint64_t lookup_misses; /**< Lookup miss counter. */
    _Atomic uint64_t removes; /**< Remove counter. */
    _Atomic uint64_t remove_misses; /**< Remove miss counter. */
    _Atomic uint64_t probes; /**< Total probes across operations. */
    _Atomic uint64_t max_probe_len; /**< Largest probe length seen. */

#if HT_ENABLE_RESIZE_INSTRUMENTATION
    _Atomic uint64_t resize_count; /**< Rebuild counter. */
    _Atomic uint64_t grow_count; /**< Grow counter. */
    _Atomic uint64_t shrink_count; /**< Shrink counter. */
    _Atomic uint64_t rehash_count; /**< Explicit rebuild counter. */
    _Atomic uint64_t resize_entries_moved; /**< Entries copied by rebuilds. */
#endif
} lf_hopscotch_table;

void *lf_hopscotch_create_impl(const ht_config *cfg);
const struct ht_vtable *lf_hopscotch_vtable(void);

int lf_hopscotch_bind_bench_iface(void *ctx, bench_iface *out);

#endif /* LF_HOPSCOTCH_IMPL_H */
