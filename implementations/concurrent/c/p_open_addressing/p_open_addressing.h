/**
 * @file    p_open_addressing.h
 * @brief   Internal interface and representation for the concurrent
 *          open-addressing backend.
 *
 * `p_open_addressing` keeps the sequential backend's linear-probing and
 * tombstone semantics, but changes the synchronization model:
 *
 * - readers protect the active image with a hazard record and validate each
 *   slot with a per-slot sequence counter;
 * - writers hold the resize barrier in read mode and take ordered stripe locks
 *   over the probe span they may mutate;
 * - resize/rehash takes the resize barrier exclusively and retires old images
 *   until no reader hazard still points at them;
 * - tombstone cleanup builds a private shadow image while readers and writers
 *   continue using the active image, then publishes the replacement under the
 *   exclusive resize barrier;
 * - capacity shrink is intentionally disabled for this backend.
 *
 * This header is internal to the backend/core build. It exposes the private
 * representation so tests, diagnostics, and future backend helpers have one
 * documented source of truth for the concurrent layout.
 */

#ifndef P_OPEN_ADDRESSING_H
#define P_OPEN_ADDRESSING_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"
#include "ht_bench.h"

struct ht_vtable;

/* --- constants ------------------------------------------------------------ */

/** Default writer-stripe target before capacity-based clamping. */
#define P_OPEN_DEFAULT_STRIPES 128u
/** Number of stripe targets requested per configured benchmark thread. */
#define P_OPEN_STRIPES_PER_THREAD 16u
/** Minimum desired number of table slots covered by each writer stripe. */
#define P_OPEN_MIN_SLOTS_PER_STRIPE 8u
/** Minimum number of hazard slots allocated for protecting retired images. */
#define P_OPEN_MIN_HAZARD_SLOTS 64u
/** Hazard slots requested per configured benchmark thread. */
#define P_OPEN_HAZARD_SLOTS_PER_THREAD 4u
/** Tombstone load that requests an asynchronous cleanup pass. */
#define P_OPEN_CLEANUP_REQUEST_LOAD 0.15
/** Tombstone load that marks cleanup as urgent. */
#define P_OPEN_CLEANUP_URGENT_LOAD 0.22
/** Base cleanup debt added by each successful delete. */
#define P_OPEN_CLEANUP_DELETE_DEBT_BASE 1u
/** Maximum extra cleanup debt added for long delete probes. */
#define P_OPEN_CLEANUP_DELETE_DEBT_BONUS_MAX 3u
/** Initial global replay-log capacity. */
#define P_OPEN_LOG_INITIAL_CAPACITY 1024u

/* --- type definitions ----------------------------------------------------- */

/**
 * @brief Cache-line-spaced writer lock.
 *
 * The padding is deliberately conservative. The mutex itself is still the
 * synchronization primitive; the padding only reduces false sharing between
 * adjacent hot stripes.
 */
typedef struct {
    pthread_mutex_t mu; /**< Mutex guarding one writer stripe. */
    char            pad[64]; /**< Spacing to reduce false sharing. */
} p_open_stripe;

/**
 * @brief One concurrent open-addressing slot.
 *
 * `seq` is even when the slot contents are stable and odd while a writer is
 * publishing a state change. Payload fields are atomic to avoid C11 data-race
 * undefined behavior while readers perform seqlock-style validation.
 */
typedef struct {
    _Atomic uint32_t seq;   /**< Seqlock version for stable reads. */
    _Atomic uint8_t  state; /**< Empty, full, or tombstone state. */
    _Atomic uint64_t hash;  /**< Cached hash for the key. */
    _Atomic ht_key_t key;   /**< Stored key when the slot is full. */
    _Atomic ht_val_t value; /**< Stored value when the slot is full. */
} p_open_slot;

/**
 * @brief Stable copy of a slot captured by the reader protocol.
 */
typedef struct {
    uint8_t  state; /**< Slot state captured between sequence reads. */
    uint64_t hash;  /**< Captured hash. */
    ht_key_t key;   /**< Captured key. */
    ht_val_t value; /**< Captured value. */
} p_open_slot_snapshot;

/**
 * @brief One concrete hash-table image.
 *
 * Slot storage and stripe storage are immutable for the lifetime of the image.
 * `size` and `used` are per-image atomics because writers continue mutating the
 * active image while a cleaner builds a private shadow image.
 */
typedef struct {
    p_open_slot   *slots; /**< Slot array owned by this image. */
    size_t         capacity; /**< Slot count, always a power of two. */
    _Atomic size_t size; /**< Number of live entries. */
    _Atomic size_t used; /**< Live entries plus tombstones. */

    p_open_stripe *stripes; /**< Writer stripe locks for this image. */
    size_t         stripe_count; /**< Number of writer stripes. */
} p_open_image;

/**
 * @brief Logged successful writer operation while shadow cleanup is building.
 */
typedef enum {
    P_OPEN_LOG_INSERT = 0,
    P_OPEN_LOG_REMOVE = 1
} p_open_log_op;

/**
 * @brief One mutation replay entry captured during shadow cleanup.
 */
typedef struct {
    uint64_t      seq; /**< Monotonic writer sequence number. */
    p_open_log_op op;  /**< Insert or remove operation kind. */
    ht_key_t      key; /**< Mutated key. */
    ht_val_t      value; /**< Insert value, unused for removes. */
} p_open_log_entry;

/**
 * @brief Protected replay log for writes that race with shadow cleanup.
 */
typedef struct {
    pthread_mutex_t   mu; /**< Serializes append and replay access. */
    p_open_log_entry *entries; /**< Replay entries. */
    size_t            count; /**< Number of populated entries. */
    size_t            capacity; /**< Allocated entry capacity. */
    size_t            limit; /**< Optional test limit before fallback. */
    size_t            peak; /**< Highest observed `count`. */
} p_open_mutation_log;

/**
 * @brief Hot-path operation statistics stored as independent atomics.
 *
 * The concurrent backend does not mutate `ht_stats` directly from table
 * operations. `get_stats` aggregates these counters with resize stats.
 */
typedef struct {
    _Atomic uint64_t inserts; /**< Insert attempt counter. */
    _Atomic uint64_t insert_failures; /**< Failed insert counter. */
    _Atomic uint64_t lookups; /**< Lookup counter. */
    _Atomic uint64_t lookup_misses; /**< Lookup miss counter. */
    _Atomic uint64_t removes; /**< Remove counter. */
    _Atomic uint64_t remove_misses; /**< Remove miss counter. */
    _Atomic uint64_t probes; /**< Total probes across operations. */
    _Atomic uint64_t max_probe_len; /**< Largest probe length seen. */
} p_open_atomic_stats;

/**
 * @brief Internal cleanup counters used by diagnostics and tests.
 */
typedef struct {
    _Atomic uint64_t requested_count; /**< Cleanup request counter. */
    _Atomic uint64_t started_count; /**< Cleanup attempt counter. */
    _Atomic uint64_t completed_count; /**< Successful cleanup counter. */
    _Atomic uint64_t fallback_count; /**< Exclusive fallback counter. */
    _Atomic uint64_t shadow_publish_count; /**< Shadow publish counter. */
    _Atomic uint64_t shadow_abandon_count; /**< Abandoned shadow counter. */
    _Atomic uint64_t log_peak_entries; /**< Largest replay log size seen. */
} p_open_cleanup_atomic_stats;

/**
 * @brief One reusable reader hazard record.
 *
 * Threads claim a record lazily and keep it for the lifetime of the table.
 * The record's protected image is cleared after every read-side critical
 * section, so claimed records do not retain retired images while idle.
 */
typedef struct {
    _Atomic int             claimed; /**< Non-zero once assigned to a thread. */
    _Atomic(p_open_image *) image; /**< Image protected by the current read. */
} p_open_hazard_record;

/**
 * @brief Retired table image awaiting hazard-pointer reclamation.
 */
typedef struct p_open_retired_image {
    p_open_image                 *image; /**< Retired image awaiting cleanup. */
    struct p_open_retired_image *next; /**< Next retired image in the list. */
} p_open_retired_image;

/**
 * @brief Private state for the concurrent open-addressing backend.
 *
 * `active` points at the image currently used by public operations. Writers
 * serialize table-slot mutations through the active image's stripes and take
 * `resize_lock` in read mode. Resizes and cleanup take it in write mode and
 * retire old images only after no hazard record protects them.
 */
typedef struct {
    _Atomic(p_open_image *) active; /**< Current published table image. */

    size_t min_capacity; /**< Smallest image capacity allowed. */

    double max_load_factor; /**< Upper load factor threshold for growth. */
    double min_load_factor; /**< Lower load factor threshold, shrink unused. */
    ht_rsz_mode resize_mode; /**< Resize policy selected at creation time. */

    ht_hash_fn hash_fn; /**< Hash function used for key lookup. */
    uint64_t hash_seed; /**< Seed passed into `hash_fn`. */

    size_t thread_count; /**< Thread-count hint for stripe/hazard sizing. */
    pthread_rwlock_t resize_lock; /**< Shared writer barrier, exclusive resize. */
    uint64_t             hazard_token; /**< Distinguishes reused table memory. */
    p_open_hazard_record *hazards; /**< Fixed hazard-record registry. */
    size_t                hazard_count; /**< Number of hazard records. */
    pthread_mutex_t       retire_mu; /**< Guards retired image list. */
    p_open_retired_image *retired; /**< Images pending hazard reclamation. */

    int collect_stats; /**< Non-zero when statistics are tracked. */
    p_open_atomic_stats op_stats; /**< Hot-path operation counters. */
    ht_stats resize_stats; /**< Resize and memory statistics. */

    _Atomic size_t tombstone_debt; /**< Delete pressure for cleanup requests. */
    _Atomic int cleanup_requested; /**< Non-zero when cleanup should run. */
    _Atomic int cleanup_urgent; /**< Non-zero when cleanup is urgent. */
    _Atomic int cleanup_in_progress; /**< Cleaner ownership flag. */
    _Atomic int cleanup_building; /**< Writers must append to replay log. */
    _Atomic int cleanup_fallback_required; /**< Shadow path must fall back. */
    _Atomic uint64_t cleanup_epoch; /**< Changes after each cleanup reset. */
    _Atomic uint64_t write_seq; /**< Global sequence for replay entries. */
    _Atomic(p_open_image *) cleanup_shadow; /**< Shadow image under build. */
    p_open_mutation_log cleanup_log; /**< Replay log for shadow cleanup. */
    p_open_cleanup_atomic_stats cleanup_stats; /**< Cleanup diagnostics. */

    void (*cleanup_pause_before_publish)(void *arg); /**< Test hook. */
    void *cleanup_pause_before_publish_arg; /**< Test hook argument. */
} p_open_table;

/**
 * @brief Inclusive stripe range locked for one writer operation.
 *
 * `all` is set when a probe span wraps around the end of the table or when the
 * span cannot be represented as one contiguous stripe interval.
 */
typedef struct {
    size_t first; /**< First stripe to lock. */
    size_t last;  /**< Last stripe to lock. */
    int    all;   /**< Non-zero when all stripes must be locked. */
} p_open_stripe_span;

/**
 * @brief Result from a probe scan.
 *
 * Advisory scans use this to describe candidate slots. Locked rescans also use
 * `needs_more_stripes` to ask the caller to retry with a wider lock span.
 */
typedef struct {
    int      found_existing; /**< Matching key was found. */
    int      found_usable; /**< Empty/tombstone insert slot was found. */
    int      needs_more_stripes; /**< Retry with wider writer lock span. */
    size_t   slot; /**< Slot selected by the scan. */
    uint8_t  slot_state; /**< State observed at `slot`. */
    uint64_t probe_len; /**< Probe count recorded for stats. */
    ht_val_t value; /**< Value captured by lookup scans. */
} p_open_scan_result;

/**
 * @brief Outcome of reserving table accounting for a new inserted slot.
 */
typedef enum {
    P_OPEN_ACCOUNT_OK = 0,
    P_OPEN_ACCOUNT_FULL = 1,
    P_OPEN_ACCOUNT_GROW = 2
} p_open_account_result;

/**
 * @brief Test-visible snapshot of cleanup internals.
 *
 * This is intentionally declared in the internal backend header, not in the
 * public API.
 */
typedef struct {
    size_t active_capacity; /**< Active image capacity. */
    size_t active_size; /**< Active live-entry count. */
    size_t active_used; /**< Active live plus tombstones count. */
    size_t shadow_capacity; /**< Shadow image capacity, or zero. */
    size_t shadow_size; /**< Shadow live-entry count. */
    size_t shadow_used; /**< Shadow used-slot count. */
    size_t tombstone_debt; /**< Current cleanup debt. */
    size_t log_count; /**< Current replay log entry count. */
    size_t log_capacity; /**< Replay log allocation size. */
    size_t log_limit; /**< Optional replay log test limit. */
    size_t log_peak_entries; /**< Peak replay log entries. */
    int cleanup_requested; /**< Snapshot of cleanup request flag. */
    int cleanup_urgent; /**< Snapshot of urgent cleanup flag. */
    int cleanup_in_progress; /**< Snapshot of cleaner ownership flag. */
    int cleanup_building; /**< Snapshot of shadow build flag. */
    int cleanup_fallback_required; /**< Snapshot of fallback flag. */
    uint64_t cleanup_epoch; /**< Cleanup generation counter. */
    uint64_t requested_count; /**< Cleanup request counter. */
    uint64_t started_count; /**< Cleanup start counter. */
    uint64_t completed_count; /**< Cleanup completion counter. */
    uint64_t fallback_count; /**< Cleanup fallback counter. */
    uint64_t shadow_publish_count; /**< Shadow publish counter. */
    uint64_t shadow_abandon_count; /**< Shadow abandon counter. */
} p_open_cleanup_test_snapshot;

typedef void (*p_open_cleanup_pause_fn)(void *arg);

/* --- public backend entry points ------------------------------------------ */

/**
 * @brief Allocate and initialize a concurrent open-addressing backend.
 *
 * @param cfg Backend configuration, including `thread_count` as a stripe
 *        sizing hint.
 *
 * @return Initialized backend state, or `NULL` on invalid input/allocation
 *         failure.
 */
ht_result p_open_addressing_create_impl_ex(
    const ht_config *cfg,
    void           **out
);

/**
 * @brief Return the vtable used by the generic hashtable wrapper.
 *
 * @return Static concurrent open-addressing backend vtable.
 */
const struct ht_vtable *p_open_addressing_vtable(
    void
);

/**
 * @brief Populate the low-overhead benchmark operation interface.
 *
 * @param ctx Backend instance to bind.
 * @param out Output benchmark interface.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID`.
 */
int p_open_addressing_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

/**
 * @brief Force one cleanup attempt for internal tests.
 */
int p_open_addressing_force_cleanup_for_test(
    void *ctx
);

/**
 * @brief Set a replay-log entry limit for internal fallback tests.
 *
 * A value of zero removes the artificial test limit.
 */
void p_open_addressing_set_log_limit_for_test(
    void  *ctx,
    size_t limit
);

/**
 * @brief Install a pause hook called after shadow bulk copy and before publish.
 */
void p_open_addressing_set_cleanup_pause_for_test(
    void                    *ctx,
    p_open_cleanup_pause_fn  fn,
    void                    *arg
);

/**
 * @brief Copy cleanup diagnostic state for internal tests.
 */
void p_open_addressing_get_cleanup_snapshot_for_test(
    void                         *ctx,
    p_open_cleanup_test_snapshot *out
);

#endif /* P_OPEN_ADDRESSING_H */
