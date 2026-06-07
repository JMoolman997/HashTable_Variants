#define _POSIX_C_SOURCE 200809L

/**
 * @file    p_open_addressing.c
 * @brief   Concurrent tombstone-based open-addressing hashtable backend.
 *
 * The active table state lives in a `p_open_image`. Readers protect that image
 * with a hazard record, while writers hold the resize barrier in read mode
 * before mutating slots. Tombstone cleanup keeps the old image active, builds a
 * private shadow image, records successful writer mutations in a replay log,
 * and publishes the replacement under the resize barrier in write mode.
 */

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend_util.h"
#include "ht_internal.h"
#include "open_addressing_impl.h"
#include "p_open_addressing.h"

/**
 * @brief Thread-local cached hazard record for one backend instance.
 */
typedef struct {
    p_open_table         *table; /**< Table that owns `record`. */
    p_open_hazard_record *record; /**< Claimed hazard record. */
    uint64_t              token; /**< Token matching the owning table. */
} p_open_tls_hazard_cache;

/**
 * @brief Active read-side protection for one lookup.
 */
typedef struct {
    p_open_image         *image; /**< Protected active image. */
    p_open_hazard_record *record; /**< Hazard record, when available. */
    int                  locked; /**< Non-zero when using resize-lock fallback. */
} p_open_read_guard;

static _Thread_local p_open_tls_hazard_cache p_open_tls_hazard;
static _Atomic uint64_t p_open_next_hazard_token = ATOMIC_VAR_INIT(1);

/* --- function prototypes -------------------------------------------------- */

static void p_open_destroy_impl(void *impl);
static ht_result p_open_insert_impl(void *impl, ht_key_t key, ht_val_t value);
static ht_result p_open_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
);
static ht_result p_open_remove_impl(void *impl, ht_key_t key);
static size_t p_open_size_impl(const void *impl);
static size_t p_open_capacity_impl(const void *impl);
static double p_open_load_factor_impl(const void *impl);
static ht_result p_open_reserve_impl(void *impl, size_t capacity);
static ht_result p_open_rehash_impl(void *impl, size_t capacity);
static ht_result p_open_get_stats_impl(const void *impl, ht_stats *out);
static ht_result p_open_reset_stats_impl(void *impl);

static size_t p_open_normalize_stripe_count(
    size_t capacity,
    size_t thread_count
);
static int p_open_stripes_init(p_open_stripe *stripes, size_t count);
static void p_open_stripes_destroy(p_open_stripe *stripes, size_t count);
static void p_open_slots_init(p_open_slot *slots, size_t capacity);
static p_open_image *p_open_image_create(
    size_t capacity,
    size_t thread_count
);
static void p_open_image_destroy(p_open_image *img);
static size_t p_open_image_bytes_used(const p_open_image *img);
static int p_open_hazards_init(p_open_table *t, size_t thread_count);
static void p_open_hazards_destroy(p_open_table *t);
static p_open_hazard_record *p_open_hazard_acquire_record(p_open_table *t);
static int p_open_read_enter(p_open_table *t, p_open_read_guard *guard);
static void p_open_read_leave(p_open_table *t, p_open_read_guard *guard);
static void p_open_retire_image(p_open_table *t, p_open_image *img);
static void p_open_reclaim_retired(p_open_table *t);
static void p_open_destroy_retired_all(p_open_table *t);

static void p_open_stats_init(p_open_atomic_stats *stats);
static void p_open_stats_reset(p_open_atomic_stats *stats);
static void p_open_stats_inc(_Atomic uint64_t *counter);
static int p_open_hot_stats_enabled(const p_open_table *t);
static void p_open_note_probe(p_open_table *t, uint64_t probe_len);
static void p_open_stats_max_update(
    _Atomic uint64_t *counter,
    uint64_t value
);
static size_t p_open_bytes_used_snapshot(p_open_table *t);
static int p_open_size_add(size_t *total, size_t add);
static int p_open_size_mul_add(size_t *total, size_t count, size_t size);

static int p_open_slot_read_consistent(
    const p_open_slot *slot,
    p_open_slot_snapshot *out
);
static void p_open_slot_read_spin(
    const p_open_slot *slot,
    p_open_slot_snapshot *out
);
static void p_open_slot_write_begin(p_open_slot *slot);
static void p_open_slot_write_end(p_open_slot *slot);
static void p_open_publish_full_slot(
    p_open_slot *slot,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value
);
static void p_open_publish_tombstone(p_open_slot *slot);

static size_t p_open_stripe_for_index(
    const p_open_image *img,
    size_t idx
);
static void p_open_span_for_probe(
    const p_open_image *img,
    size_t base,
    uint64_t probe_len,
    p_open_stripe_span *span
);
static int p_open_span_contains(
    const p_open_image *img,
    const p_open_stripe_span *span,
    size_t idx
);
static int p_open_lock_span(
    p_open_image *img,
    const p_open_stripe_span *span
);
static void p_open_unlock_span(
    p_open_image *img,
    const p_open_stripe_span *span
);

static ht_result p_open_scan_find(
    const p_open_image *img,
    ht_key_t key,
    uint64_t hash,
    const p_open_stripe_span *locked_span,
    p_open_scan_result *out
);
static ht_result p_open_scan_insert(
    const p_open_image *img,
    ht_key_t key,
    uint64_t hash,
    const p_open_stripe_span *locked_span,
    p_open_scan_result *out
);

static p_open_account_result p_open_try_account_insert(
    p_open_table *t,
    p_open_image *img,
    int needs_empty_slot
);
static int p_open_resize_enabled(const p_open_table *t);
static int p_open_maintenance_wrlock(p_open_table *t);
static ht_result p_open_grow_for_insert(p_open_table *t, int force);
static ht_result p_open_resize_locked(
    p_open_table *t,
    size_t requested_capacity,
    int force_rebuild
);
static ht_result p_open_resize_locked_quiescent(
    p_open_table *t,
    size_t requested_capacity,
    int force_rebuild
);
static int p_open_log_init(p_open_mutation_log *log, size_t capacity);
static void p_open_log_destroy(p_open_mutation_log *log);
static void p_open_log_reset(p_open_mutation_log *log);
static ht_result p_open_log_append(
    p_open_table *t,
    p_open_log_op op,
    ht_key_t key,
    ht_val_t value
);

static void p_open_cleanup_stats_init(p_open_cleanup_atomic_stats *stats);
static void p_open_cleanup_stats_reset(p_open_cleanup_atomic_stats *stats);
static void p_open_cleanup_state_init(p_open_table *t);
static void p_open_cleanup_debt_dec(p_open_table *t);
static void p_open_maybe_request_cleanup(
    p_open_table *t,
    size_t capacity,
    size_t size,
    size_t used,
    uint64_t delete_probe_len
);
static int p_open_claim_cleaner(p_open_table *t);
static void p_open_release_cleaner(p_open_table *t);
static void p_open_cleanup_reset_after_locked(p_open_table *t);

static size_t p_open_choose_cleanup_capacity(
    const p_open_table *t,
    const p_open_image *old_img
);
static ht_result p_open_shadow_bulk_copy(
    const p_open_table *t,
    const p_open_image *old_img,
    p_open_image *shadow
);
static ht_result p_open_shadow_insert(
    p_open_image *img,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value
);
static ht_result p_open_shadow_remove(
    p_open_image *img,
    uint64_t hash,
    ht_key_t key
);
static ht_result p_open_shadow_replay_log(
    p_open_table *t,
    p_open_image *shadow,
    uint64_t start_seq,
    uint64_t end_seq
);
static void p_open_abandon_shadow(p_open_table *t, p_open_image *shadow);
static ht_result p_open_run_cleanup_exclusive_fallback_locked(
    p_open_table *t
);
static void p_open_run_cleanup_shadow_rebuild(p_open_table *t);

/* --- vtable --------------------------------------------------------------- */

static const struct ht_vtable P_OPEN_VTABLE = {
    .destroy = p_open_destroy_impl,
    .insert = p_open_insert_impl,
    .get = p_open_get_impl,
    .remove = p_open_remove_impl,
    .size = p_open_size_impl,
    .capacity = p_open_capacity_impl,
    .load_factor = p_open_load_factor_impl,
    .reserve = p_open_reserve_impl,
    .rehash = p_open_rehash_impl,
    .get_stats = p_open_get_stats_impl,
    .reset_stats = p_open_reset_stats_impl,
    .bind_bench_iface = p_open_addressing_bind_bench_iface
};

/* --- public backend entry points ------------------------------------------ */

void *p_open_addressing_create_impl(
    const ht_config *cfg
) {
    p_open_table *t;
    p_open_image *img;
    size_t capacity;
    size_t min_capacity;
    size_t thread_count;

    if (cfg == NULL) {
        return NULL;
    }

    capacity = (cfg->init_capacity > 0)
        ? cfg->init_capacity
        : DEFAULT_INITIAL_CAPACITY;
    capacity = next_pow2(capacity);
    if (capacity == 0) {
        return NULL;
    }

    min_capacity = (cfg->min_capacity > 0)
        ? cfg->min_capacity
        : DEFAULT_MIN_CAPACITY;
    min_capacity = next_pow2(min_capacity);
    if (min_capacity == 0) {
        return NULL;
    }

    if (capacity < min_capacity) {
        capacity = min_capacity;
    }

    thread_count = (cfg->thread_count > 0) ? cfg->thread_count : 1;
    img = p_open_image_create(capacity, thread_count);
    if (img == NULL) {
        return NULL;
    }

    t = calloc(1, sizeof(*t));
    if (t == NULL) {
        p_open_image_destroy(img);
        return NULL;
    }

    if (pthread_rwlock_init(&t->resize_lock, NULL) != 0) {
        p_open_image_destroy(img);
        free(t);
        return NULL;
    }

    if (pthread_mutex_init(&t->retire_mu, NULL) != 0) {
        pthread_rwlock_destroy(&t->resize_lock);
        p_open_image_destroy(img);
        free(t);
        return NULL;
    }

    if (p_open_hazards_init(t, thread_count) != 0) {
        pthread_mutex_destroy(&t->retire_mu);
        pthread_rwlock_destroy(&t->resize_lock);
        p_open_image_destroy(img);
        free(t);
        return NULL;
    }

    if (p_open_log_init(&t->cleanup_log, P_OPEN_LOG_INITIAL_CAPACITY) != 0) {
        p_open_hazards_destroy(t);
        pthread_mutex_destroy(&t->retire_mu);
        pthread_rwlock_destroy(&t->resize_lock);
        p_open_image_destroy(img);
        free(t);
        return NULL;
    }

    atomic_init(&t->active, img);
    t->min_capacity = min_capacity;
    t->max_load_factor = (cfg->max_load_factor > 0.0)
        ? cfg->max_load_factor
        : DEFAULT_MAX_LOAD;
    t->min_load_factor = (cfg->min_load_factor > 0.0)
        ? cfg->min_load_factor
        : DEFAULT_MIN_LOAD;
    t->resize_mode = cfg->rsz_mode;
    t->hash_fn = (cfg->hash_fn != NULL) ? cfg->hash_fn : default_hash;
    t->hash_seed = cfg->hash_seed;
    t->thread_count = thread_count;
    t->collect_stats = cfg->collect_stats;
    p_open_stats_init(&t->op_stats);
    memset(&t->resize_stats, 0, sizeof(t->resize_stats));
    ht_resize_stats_init(&t->resize_stats, t->collect_stats, capacity);
    p_open_cleanup_stats_init(&t->cleanup_stats);
    p_open_cleanup_state_init(t);

    return t;
}

const struct ht_vtable *p_open_addressing_vtable(
    void
) {
    return &P_OPEN_VTABLE;
}

int p_open_addressing_bind_bench_iface(
    void *ctx,
    bench_iface *out
) {
    if (ctx == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    out->ctx = ctx;
    out->insert = p_open_insert_impl;
    out->get = p_open_get_impl;
    out->remove = p_open_remove_impl;

    return HT_OK;
}

int p_open_addressing_force_cleanup_for_test(
    void *ctx
) {
    p_open_table *t = ctx;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    atomic_store_explicit(&t->cleanup_requested, 1, memory_order_release);
    if (!p_open_claim_cleaner(t)) {
        return HT_ERR;
    }

    p_open_run_cleanup_shadow_rebuild(t);
    return HT_OK;
}

void p_open_addressing_set_log_limit_for_test(
    void  *ctx,
    size_t limit
) {
    p_open_table *t = ctx;

    if (t == NULL) {
        return;
    }

    pthread_mutex_lock(&t->cleanup_log.mu);
    t->cleanup_log.limit = limit;
    pthread_mutex_unlock(&t->cleanup_log.mu);
}

void p_open_addressing_set_cleanup_pause_for_test(
    void                    *ctx,
    p_open_cleanup_pause_fn  fn,
    void                    *arg
) {
    p_open_table *t = ctx;

    if (t == NULL) {
        return;
    }

    t->cleanup_pause_before_publish = fn;
    t->cleanup_pause_before_publish_arg = arg;
}

void p_open_addressing_get_cleanup_snapshot_for_test(
    void                         *ctx,
    p_open_cleanup_test_snapshot *out
) {
    p_open_table *t = ctx;
    p_open_image *active;
    p_open_image *shadow;

    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (t == NULL) {
        return;
    }

    if (pthread_rwlock_rdlock(&t->resize_lock) != 0) {
        return;
    }

    active = atomic_load_explicit(&t->active, memory_order_acquire);
    shadow = atomic_load_explicit(&t->cleanup_shadow, memory_order_acquire);
    if (active != NULL) {
        out->active_capacity = active->capacity;
        out->active_size = atomic_load_explicit(&active->size, memory_order_acquire);
        out->active_used = atomic_load_explicit(&active->used, memory_order_acquire);
    }
    if (shadow != NULL && shadow != active) {
        out->shadow_capacity = shadow->capacity;
        out->shadow_size = atomic_load_explicit(&shadow->size, memory_order_acquire);
        out->shadow_used = atomic_load_explicit(&shadow->used, memory_order_acquire);
    }

    pthread_mutex_lock(&t->cleanup_log.mu);
    out->log_count = t->cleanup_log.count;
    out->log_capacity = t->cleanup_log.capacity;
    out->log_limit = t->cleanup_log.limit;
    out->log_peak_entries = t->cleanup_log.peak;
    pthread_mutex_unlock(&t->cleanup_log.mu);

    out->tombstone_debt = atomic_load_explicit(
        &t->tombstone_debt,
        memory_order_acquire
    );
    out->cleanup_requested = atomic_load_explicit(
        &t->cleanup_requested,
        memory_order_acquire
    );
    out->cleanup_urgent = atomic_load_explicit(
        &t->cleanup_urgent,
        memory_order_acquire
    );
    out->cleanup_in_progress = atomic_load_explicit(
        &t->cleanup_in_progress,
        memory_order_acquire
    );
    out->cleanup_building = atomic_load_explicit(
        &t->cleanup_building,
        memory_order_acquire
    );
    out->cleanup_fallback_required = atomic_load_explicit(
        &t->cleanup_fallback_required,
        memory_order_acquire
    );
    out->cleanup_epoch = atomic_load_explicit(
        &t->cleanup_epoch,
        memory_order_acquire
    );
    out->requested_count = atomic_load_explicit(
        &t->cleanup_stats.requested_count,
        memory_order_relaxed
    );
    out->started_count = atomic_load_explicit(
        &t->cleanup_stats.started_count,
        memory_order_relaxed
    );
    out->completed_count = atomic_load_explicit(
        &t->cleanup_stats.completed_count,
        memory_order_relaxed
    );
    out->fallback_count = atomic_load_explicit(
        &t->cleanup_stats.fallback_count,
        memory_order_relaxed
    );
    out->shadow_publish_count = atomic_load_explicit(
        &t->cleanup_stats.shadow_publish_count,
        memory_order_relaxed
    );
    out->shadow_abandon_count = atomic_load_explicit(
        &t->cleanup_stats.shadow_abandon_count,
        memory_order_relaxed
    );

    pthread_rwlock_unlock(&t->resize_lock);
}

/* --- core operations ------------------------------------------------------ */

static void p_open_destroy_impl(
    void *impl
) {
    p_open_table *t = impl;
    p_open_image *active;
    p_open_image *shadow;

    if (t == NULL) {
        return;
    }

    active = atomic_load_explicit(&t->active, memory_order_relaxed);
    shadow = atomic_load_explicit(&t->cleanup_shadow, memory_order_relaxed);
    if (shadow != NULL && shadow != active) {
        p_open_image_destroy(shadow);
    }
    p_open_image_destroy(active);
    p_open_destroy_retired_all(t);
    p_open_hazards_destroy(t);
    p_open_log_destroy(&t->cleanup_log);
    pthread_mutex_destroy(&t->retire_mu);
    pthread_rwlock_destroy(&t->resize_lock);
    free(t);
}

static ht_result p_open_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
) {
    p_open_table *t = impl;
    p_open_image *img;
    p_open_scan_result scan = {0};
    p_open_scan_result locked_scan;
    p_open_stripe_span span;
    p_open_account_result account_rc;
    uint64_t hash;
    size_t base;
    ht_result rc;
    int reused_tombstone;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (p_open_hot_stats_enabled(t)) {
        p_open_stats_inc(&t->op_stats.inserts);
    }

    hash = t->hash_fn(key, t->hash_seed);

retry:
    if (pthread_rwlock_rdlock(&t->resize_lock) != 0) {
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return HT_ERR_INVALID;
    }

    img = atomic_load_explicit(&t->active, memory_order_acquire);
    if (img == NULL) {
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return HT_ERR_INVALID;
    }
    base = HT_INDEX_FOR_U64(hash, img->capacity);

    rc = p_open_scan_insert(img, key, hash, NULL, &scan);
    if (rc == HT_ERR_FULL) {
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_resize_enabled(t)) {
            rc = p_open_grow_for_insert(t, 1);
            if (rc == HT_OK) {
                goto retry;
            }
        }
        p_open_note_probe(t, scan.probe_len);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return HT_ERR_FULL;
    }
    if (rc != HT_OK) {
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return rc;
    }

    p_open_note_probe(t, scan.probe_len);
    p_open_span_for_probe(img, base, scan.probe_len, &span);
    /* The first scan is advisory. After locking its stripe span, rescan before
     * publishing because another writer may have changed the probe path. */
    if (p_open_lock_span(img, &span) != 0) {
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return HT_ERR_INVALID;
    }

    rc = p_open_scan_insert(img, key, hash, &span, &locked_scan);
    if (locked_scan.needs_more_stripes) {
        /* A concurrent probe extended outside the advisory span. Drop locks
         * and retry so stripe locks are still acquired in canonical order. */
        p_open_unlock_span(img, &span);
        pthread_rwlock_unlock(&t->resize_lock);
        goto retry;
    }
    if (rc == HT_ERR_FULL) {
        p_open_unlock_span(img, &span);
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_resize_enabled(t)) {
            rc = p_open_grow_for_insert(t, 1);
            if (rc == HT_OK) {
                goto retry;
            }
        }
        p_open_note_probe(t, locked_scan.probe_len);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return HT_ERR_FULL;
    }
    if (rc != HT_OK) {
        p_open_unlock_span(img, &span);
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return rc;
    }

    if (locked_scan.found_existing) {
        p_open_unlock_span(img, &span);
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return HT_ERR_EXISTS;
    }

    reused_tombstone =
        (locked_scan.slot_state == OPEN_ADDRESSING_SLOT_TOMBSTONE);
    account_rc = p_open_try_account_insert(
        t,
        img,
        locked_scan.slot_state == OPEN_ADDRESSING_SLOT_EMPTY
    );
    if (account_rc == P_OPEN_ACCOUNT_FULL) {
        p_open_unlock_span(img, &span);
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.insert_failures);
        }
        return HT_ERR_FULL;
    }
    if (account_rc == P_OPEN_ACCOUNT_GROW) {
        p_open_unlock_span(img, &span);
        pthread_rwlock_unlock(&t->resize_lock);
        rc = p_open_grow_for_insert(t, 0);
        if (rc != HT_OK) {
            if (p_open_hot_stats_enabled(t)) {
                p_open_stats_inc(&t->op_stats.insert_failures);
            }
            return rc;
        }
        goto retry;
    }

    p_open_publish_full_slot(&img->slots[locked_scan.slot], hash, key, value);
    atomic_fetch_add_explicit(&img->size, 1, memory_order_release);
    if (reused_tombstone) {
        p_open_cleanup_debt_dec(t);
    }
    if (atomic_load_explicit(&t->cleanup_building, memory_order_acquire)) {
        /* Shadow cleanup copied an earlier image; log this write so publish can
         * replay mutations that happened during the build. */
        (void)p_open_log_append(t, P_OPEN_LOG_INSERT, key, value);
    }

    p_open_unlock_span(img, &span);
    pthread_rwlock_unlock(&t->resize_lock);
    return HT_OK;
}

static ht_result p_open_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
) {
    p_open_table *t = (p_open_table *)impl;
    p_open_image *img;
    p_open_read_guard guard;
    p_open_scan_result scan = {0};
    uint64_t hash;
    ht_result rc;

    if (t == NULL || value_out == NULL) {
        return HT_ERR_INVALID;
    }

    if (p_open_hot_stats_enabled(t)) {
        p_open_stats_inc(&t->op_stats.lookups);
    }

    hash = t->hash_fn(key, t->hash_seed);

    if (p_open_read_enter(t, &guard) != 0) {
        return HT_ERR_INVALID;
    }

    /* The guard pins the image until the seqlock-style slot scan completes. */
    img = guard.image;
    rc = (img == NULL)
        ? HT_ERR_INVALID
        : p_open_scan_find(img, key, hash, NULL, &scan);
    p_open_read_leave(t, &guard);

    p_open_note_probe(t, scan.probe_len);
    if (rc != HT_OK) {
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.lookup_misses);
        }
        return (rc == HT_ERR_INVALID) ? rc : HT_ERR_NOT_FOUND;
    }

    *value_out = scan.value;
    return HT_OK;
}

static ht_result p_open_remove_impl(
    void *impl,
    ht_key_t key
) {
    p_open_table *t = impl;
    p_open_image *img;
    p_open_scan_result scan;
    p_open_scan_result locked_scan;
    p_open_stripe_span span;
    uint64_t hash;
    size_t base;
    size_t size_after;
    size_t used_snapshot;
    size_t capacity_snapshot;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (p_open_hot_stats_enabled(t)) {
        p_open_stats_inc(&t->op_stats.removes);
    }

    hash = t->hash_fn(key, t->hash_seed);

retry:
    if (pthread_rwlock_rdlock(&t->resize_lock) != 0) {
        return HT_ERR_INVALID;
    }

    img = atomic_load_explicit(&t->active, memory_order_acquire);
    if (img == NULL) {
        pthread_rwlock_unlock(&t->resize_lock);
        return HT_ERR_INVALID;
    }
    base = HT_INDEX_FOR_U64(hash, img->capacity);

    rc = p_open_scan_find(img, key, hash, NULL, &scan);
    if (rc != HT_OK) {
        pthread_rwlock_unlock(&t->resize_lock);
        p_open_note_probe(t, scan.probe_len);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.remove_misses);
        }
        return HT_ERR_NOT_FOUND;
    }

    p_open_note_probe(t, scan.probe_len);
    p_open_span_for_probe(img, base, scan.probe_len, &span);
    /* As with insert, the locked scan validates that the target slot is still
     * in the probe span protected by the acquired stripes. */
    if (p_open_lock_span(img, &span) != 0) {
        pthread_rwlock_unlock(&t->resize_lock);
        return HT_ERR_INVALID;
    }

    rc = p_open_scan_find(img, key, hash, &span, &locked_scan);
    if (locked_scan.needs_more_stripes) {
        /* The remove target moved beyond the locked span; retry with a fresh
         * span instead of locking stripes out of order. */
        p_open_unlock_span(img, &span);
        pthread_rwlock_unlock(&t->resize_lock);
        goto retry;
    }
    if (rc != HT_OK) {
        p_open_unlock_span(img, &span);
        pthread_rwlock_unlock(&t->resize_lock);
        if (p_open_hot_stats_enabled(t)) {
            p_open_stats_inc(&t->op_stats.remove_misses);
        }
        return HT_ERR_NOT_FOUND;
    }

    p_open_publish_tombstone(&img->slots[locked_scan.slot]);
    size_after = atomic_fetch_sub_explicit(
        &img->size,
        1,
        memory_order_release
    ) - 1;
    used_snapshot = atomic_load_explicit(&img->used, memory_order_acquire);
    capacity_snapshot = img->capacity;
    if (atomic_load_explicit(&t->cleanup_building, memory_order_acquire)) {
        /* Removing from the active image must be mirrored into a shadow image
         * before that shadow can replace the active image. */
        (void)p_open_log_append(t, P_OPEN_LOG_REMOVE, key, 0);
    }

    p_open_unlock_span(img, &span);
    pthread_rwlock_unlock(&t->resize_lock);

    p_open_maybe_request_cleanup(
        t,
        capacity_snapshot,
        size_after,
        used_snapshot,
        locked_scan.probe_len
    );
    return HT_OK;
}

/* --- metadata/stat ops ---------------------------------------------------- */

static size_t p_open_size_impl(
    const void *impl
) {
    p_open_table *t = (p_open_table *)impl;
    p_open_image *img;
    p_open_read_guard guard;
    size_t size = 0;

    if (t == NULL) {
        return 0;
    }

    if (p_open_read_enter(t, &guard) != 0) {
        return 0;
    }
    img = guard.image;
    if (img != NULL) {
        size = atomic_load_explicit(&img->size, memory_order_acquire);
    }
    p_open_read_leave(t, &guard);
    return size;
}

static size_t p_open_capacity_impl(
    const void *impl
) {
    p_open_table *t = (p_open_table *)impl;
    p_open_image *img;
    p_open_read_guard guard;
    size_t capacity = 0;

    if (t == NULL) {
        return 0;
    }

    if (p_open_read_enter(t, &guard) != 0) {
        return 0;
    }
    img = guard.image;
    if (img != NULL) {
        capacity = img->capacity;
    }
    p_open_read_leave(t, &guard);
    return capacity;
}

static double p_open_load_factor_impl(
    const void *impl
) {
    p_open_table *t = (p_open_table *)impl;
    p_open_image *img;
    p_open_read_guard guard;
    size_t size = 0;
    size_t capacity = 0;

    if (t == NULL) {
        return 0.0;
    }

    if (p_open_read_enter(t, &guard) != 0) {
        return 0.0;
    }
    img = guard.image;
    if (img != NULL) {
        size = atomic_load_explicit(&img->size, memory_order_acquire);
        capacity = img->capacity;
    }
    p_open_read_leave(t, &guard);
    return ht_load_factor_snapshot(size, capacity);
}

static ht_result p_open_reserve_impl(
    void *impl,
    size_t capacity
) {
    p_open_table *t = impl;
    p_open_image *img;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (p_open_maintenance_wrlock(t) != 0) {
        return HT_ERR_INVALID;
    }

    img = atomic_load_explicit(&t->active, memory_order_acquire);
    if (img == NULL) {
        pthread_rwlock_unlock(&t->resize_lock);
        return HT_ERR_INVALID;
    }
    if (capacity <= img->capacity) {
        pthread_rwlock_unlock(&t->resize_lock);
        return HT_OK;
    }

    rc = p_open_resize_locked(t, capacity, 0);
    pthread_rwlock_unlock(&t->resize_lock);
    return rc;
}

static ht_result p_open_rehash_impl(
    void *impl,
    size_t capacity
) {
    p_open_table *t = impl;
    p_open_image *img;
    size_t current_size;
    size_t target;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (p_open_maintenance_wrlock(t) != 0) {
        return HT_ERR_INVALID;
    }

    img = atomic_load_explicit(&t->active, memory_order_acquire);
    if (img == NULL) {
        pthread_rwlock_unlock(&t->resize_lock);
        return HT_ERR_INVALID;
    }

    current_size = atomic_load_explicit(&img->size, memory_order_acquire);
    target = (capacity > current_size) ? capacity : current_size;
    if (target < t->min_capacity) {
        target = t->min_capacity;
    }
    if (target < img->capacity) {
        target = img->capacity;
    }

    rc = p_open_resize_locked(t, target, 1);
    pthread_rwlock_unlock(&t->resize_lock);
    return rc;
}

static ht_result p_open_get_stats_impl(
    const void *impl,
    ht_stats *out
) {
    p_open_table *t = (p_open_table *)impl;
#if HT_ENABLE_RESIZE_INSTRUMENTATION
    ht_stats resize_stats;
#endif

    if (t == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    if (pthread_rwlock_rdlock(&t->resize_lock) != 0) {
        return HT_ERR_INVALID;
    }

#if HT_ENABLE_RESIZE_INSTRUMENTATION
    resize_stats = t->resize_stats;
#endif
    memset(out, 0, sizeof(*out));
    out->inserts = atomic_load_explicit(
        &t->op_stats.inserts,
        memory_order_relaxed
    );
    out->insert_failures = atomic_load_explicit(
        &t->op_stats.insert_failures,
        memory_order_relaxed
    );
    out->lookups = atomic_load_explicit(
        &t->op_stats.lookups,
        memory_order_relaxed
    );
    out->lookup_misses = atomic_load_explicit(
        &t->op_stats.lookup_misses,
        memory_order_relaxed
    );
    out->removes = atomic_load_explicit(
        &t->op_stats.removes,
        memory_order_relaxed
    );
    out->remove_misses = atomic_load_explicit(
        &t->op_stats.remove_misses,
        memory_order_relaxed
    );
    out->probes = atomic_load_explicit(
        &t->op_stats.probes,
        memory_order_relaxed
    );
    out->max_probe_len = atomic_load_explicit(
        &t->op_stats.max_probe_len,
        memory_order_relaxed
    );
    out->cleanup_requested_count = atomic_load_explicit(
        &t->cleanup_stats.requested_count,
        memory_order_relaxed
    );
    out->cleanup_started_count = atomic_load_explicit(
        &t->cleanup_stats.started_count,
        memory_order_relaxed
    );
    out->cleanup_completed_count = atomic_load_explicit(
        &t->cleanup_stats.completed_count,
        memory_order_relaxed
    );
    out->cleanup_fallback_count = atomic_load_explicit(
        &t->cleanup_stats.fallback_count,
        memory_order_relaxed
    );
    out->cleanup_shadow_publish_count = atomic_load_explicit(
        &t->cleanup_stats.shadow_publish_count,
        memory_order_relaxed
    );
    out->cleanup_shadow_abandon_count = atomic_load_explicit(
        &t->cleanup_stats.shadow_abandon_count,
        memory_order_relaxed
    );
    out->cleanup_log_peak_entries = atomic_load_explicit(
        &t->cleanup_stats.log_peak_entries,
        memory_order_relaxed
    );
#if HT_ENABLE_RESIZE_INSTRUMENTATION
    out->resize_count = resize_stats.resize_count;
    out->grow_count = resize_stats.grow_count;
    out->shrink_count = resize_stats.shrink_count;
    out->rehash_count = resize_stats.rehash_count;
    out->resize_entries_moved = resize_stats.resize_entries_moved;
    out->resize_total_ns = resize_stats.resize_total_ns;
    out->resize_max_ns = resize_stats.resize_max_ns;
    out->resize_min_capacity = resize_stats.resize_min_capacity;
    out->resize_max_capacity = resize_stats.resize_max_capacity;
#endif
    out->bytes_used = p_open_bytes_used_snapshot(t);

    pthread_rwlock_unlock(&t->resize_lock);
    return HT_OK;
}

static ht_result p_open_reset_stats_impl(
    void *impl
) {
    p_open_table *t = impl;
    p_open_image *img;
    size_t capacity = 0;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (p_open_maintenance_wrlock(t) != 0) {
        return HT_ERR_INVALID;
    }

    img = atomic_load_explicit(&t->active, memory_order_acquire);
    if (img != NULL) {
        capacity = img->capacity;
    }
    p_open_stats_reset(&t->op_stats);
    p_open_cleanup_stats_reset(&t->cleanup_stats);
    pthread_mutex_lock(&t->cleanup_log.mu);
    t->cleanup_log.peak = 0;
    pthread_mutex_unlock(&t->cleanup_log.mu);
    memset(&t->resize_stats, 0, sizeof(t->resize_stats));
    ht_resize_stats_init(&t->resize_stats, t->collect_stats, capacity);

    pthread_rwlock_unlock(&t->resize_lock);
    return HT_OK;
}

/* --- initialization helpers ----------------------------------------------- */

static size_t p_open_normalize_stripe_count(
    size_t capacity,
    size_t thread_count
) {
    size_t threads = (thread_count > 0) ? thread_count : 1;
    size_t target;
    size_t max_stripes;
    size_t rounded;

    if (capacity == 0) {
        return 0;
    }

    target = (threads > SIZE_MAX / P_OPEN_STRIPES_PER_THREAD)
        ? SIZE_MAX
        : threads * P_OPEN_STRIPES_PER_THREAD;
    if (target < P_OPEN_DEFAULT_STRIPES) {
        target = P_OPEN_DEFAULT_STRIPES;
    }

    max_stripes = capacity / P_OPEN_MIN_SLOTS_PER_STRIPE;
    if (max_stripes == 0) {
        max_stripes = 1;
    }
    if (target > max_stripes) {
        target = max_stripes;
    }

    rounded = next_pow2(target);
    if (rounded == 0 || rounded > max_stripes) {
        rounded = floor_pow2(max_stripes);
    }

    return (rounded == 0) ? 1 : rounded;
}

static int p_open_stripes_init(
    p_open_stripe *stripes,
    size_t count
) {
    size_t i;

    if (stripes == NULL || count == 0) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (pthread_mutex_init(&stripes[i].mu, NULL) != 0) {
            p_open_stripes_destroy(stripes, i);
            return -1;
        }
    }

    return 0;
}

static void p_open_stripes_destroy(
    p_open_stripe *stripes,
    size_t count
) {
    size_t i;

    if (stripes == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        pthread_mutex_destroy(&stripes[i].mu);
    }
}

static void p_open_slots_init(
    p_open_slot *slots,
    size_t capacity
) {
    size_t i;

    if (slots == NULL) {
        return;
    }

    for (i = 0; i < capacity; i++) {
        atomic_init(&slots[i].seq, 0u);
        atomic_init(&slots[i].state, OPEN_ADDRESSING_SLOT_EMPTY);
        atomic_init(&slots[i].hash, 0);
        atomic_init(&slots[i].key, 0);
        atomic_init(&slots[i].value, 0);
    }
}

static p_open_image *p_open_image_create(
    size_t capacity,
    size_t thread_count
) {
    p_open_image *img;
    size_t stripe_count;

    if (capacity == 0) {
        return NULL;
    }

    stripe_count = p_open_normalize_stripe_count(capacity, thread_count);
    if (stripe_count == 0) {
        return NULL;
    }

    img = calloc(1, sizeof(*img));
    if (img == NULL) {
        return NULL;
    }

    img->slots = calloc(capacity, sizeof(*img->slots));
    if (img->slots == NULL) {
        free(img);
        return NULL;
    }
    p_open_slots_init(img->slots, capacity);

    img->stripes = calloc(stripe_count, sizeof(*img->stripes));
    if (img->stripes == NULL) {
        free(img->slots);
        free(img);
        return NULL;
    }
    if (p_open_stripes_init(img->stripes, stripe_count) != 0) {
        free(img->stripes);
        free(img->slots);
        free(img);
        return NULL;
    }

    img->capacity = capacity;
    atomic_init(&img->size, 0);
    atomic_init(&img->used, 0);
    img->stripe_count = stripe_count;
    return img;
}

static void p_open_image_destroy(
    p_open_image *img
) {
    if (img == NULL) {
        return;
    }

    p_open_stripes_destroy(img->stripes, img->stripe_count);
    free(img->stripes);
    free(img->slots);
    free(img);
}

static size_t p_open_image_bytes_used(
    const p_open_image *img
) {
    size_t bytes;

    if (img == NULL) {
        return 0;
    }

    bytes = sizeof(*img);
    if (p_open_size_mul_add(&bytes, img->capacity, sizeof(p_open_slot)) != 0) {
        return SIZE_MAX;
    }
    if (p_open_size_mul_add(
            &bytes,
            img->stripe_count,
            sizeof(p_open_stripe)
        ) != 0) {
        return SIZE_MAX;
    }

    return bytes;
}

static int p_open_hazards_init(
    p_open_table *t,
    size_t thread_count
) {
    size_t threads;
    size_t count;
    size_t i;

    if (t == NULL) {
        return -1;
    }

    t->hazard_token = atomic_fetch_add_explicit(
        &p_open_next_hazard_token,
        1,
        memory_order_relaxed
    );
    if (t->hazard_token == 0) {
        t->hazard_token = atomic_fetch_add_explicit(
            &p_open_next_hazard_token,
            1,
            memory_order_relaxed
        );
    }

    threads = (thread_count > 0) ? thread_count : 1;
    count = (threads > SIZE_MAX / P_OPEN_HAZARD_SLOTS_PER_THREAD)
        ? SIZE_MAX
        : threads * P_OPEN_HAZARD_SLOTS_PER_THREAD;
    if (count < P_OPEN_MIN_HAZARD_SLOTS) {
        count = P_OPEN_MIN_HAZARD_SLOTS;
    }
    if (count > SIZE_MAX / sizeof(*t->hazards)) {
        return -1;
    }

    t->hazards = calloc(count, sizeof(*t->hazards));
    if (t->hazards == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        atomic_init(&t->hazards[i].claimed, 0);
        atomic_init(&t->hazards[i].image, NULL);
    }
    t->hazard_count = count;
    t->retired = NULL;
    return 0;
}

static void p_open_hazards_destroy(
    p_open_table *t
) {
    if (t == NULL) {
        return;
    }

    free(t->hazards);
    t->hazards = NULL;
    t->hazard_count = 0;
    if (p_open_tls_hazard.table == t &&
        p_open_tls_hazard.token == t->hazard_token) {
        p_open_tls_hazard.table = NULL;
        p_open_tls_hazard.record = NULL;
        p_open_tls_hazard.token = 0;
    }
}

static p_open_hazard_record *p_open_hazard_acquire_record(
    p_open_table *t
) {
    size_t i;

    if (t == NULL || t->hazards == NULL || t->hazard_count == 0) {
        return NULL;
    }

    if (p_open_tls_hazard.table == t &&
        p_open_tls_hazard.token == t->hazard_token &&
        p_open_tls_hazard.record != NULL) {
        return p_open_tls_hazard.record;
    }
    if (p_open_tls_hazard.table == t) {
        /* A matching table pointer with a stale token means the address was
         * reused after destroy; discard the old thread-local record. */
        p_open_tls_hazard.table = NULL;
        p_open_tls_hazard.record = NULL;
        p_open_tls_hazard.token = 0;
    }

    for (i = 0; i < t->hazard_count; i++) {
        int expected = 0;

        if (atomic_compare_exchange_strong_explicit(
                &t->hazards[i].claimed,
                &expected,
                1,
                memory_order_relaxed,
                memory_order_relaxed
            )) {
            p_open_tls_hazard.table = t;
            p_open_tls_hazard.record = &t->hazards[i];
            p_open_tls_hazard.token = t->hazard_token;
            return &t->hazards[i];
        }
    }

    return NULL;
}

static int p_open_read_enter(
    p_open_table *t,
    p_open_read_guard *guard
) {
    p_open_hazard_record *record;
    p_open_image *img;

    if (t == NULL || guard == NULL) {
        return -1;
    }

    guard->image = NULL;
    guard->record = NULL;
    guard->locked = 0;
    record = p_open_hazard_acquire_record(t);
    if (record != NULL) {
        /* Publish the image before validating that active still points at it.
         * A concurrent swap can then retire the old image, but cannot free it
         * while this record still advertises the pointer. */
        for (;;) {
            img = atomic_load_explicit(&t->active, memory_order_acquire);
            atomic_store_explicit(&record->image, img, memory_order_release);
            if (img == atomic_load_explicit(&t->active, memory_order_acquire)) {
                guard->image = img;
                guard->record = record;
                return 0;
            }
        }
    }

    /* The hazard registry is intentionally bounded. Extra transient threads
     * fall back to the resize lock instead of growing a hot-path structure. */
    if (pthread_rwlock_rdlock(&t->resize_lock) != 0) {
        return -1;
    }
    guard->locked = 1;
    guard->image = atomic_load_explicit(&t->active, memory_order_acquire);
    return 0;
}

static void p_open_read_leave(
    p_open_table *t,
    p_open_read_guard *guard
) {
    if (t == NULL || guard == NULL) {
        return;
    }

    if (guard->record != NULL) {
        atomic_store_explicit(&guard->record->image, NULL, memory_order_release);
    }
    if (guard->locked) {
        pthread_rwlock_unlock(&t->resize_lock);
    }
}

static int p_open_image_is_hazard_protected(
    const p_open_table *t,
    const p_open_image *img
) {
    size_t i;

    if (t == NULL || img == NULL) {
        return 0;
    }

    for (i = 0; i < t->hazard_count; i++) {
        if (atomic_load_explicit(
                &t->hazards[i].image,
                memory_order_acquire
            ) == img) {
            return 1;
        }
    }

    return 0;
}

static void p_open_retire_image(
    p_open_table *t,
    p_open_image *img
) {
    p_open_retired_image *node;

    if (t == NULL || img == NULL) {
        return;
    }

    node = malloc(sizeof(*node));
    if (node == NULL) {
        while (p_open_image_is_hazard_protected(t, img)) {
            sched_yield();
        }
        p_open_image_destroy(img);
        return;
    }

    node->image = img;
    pthread_mutex_lock(&t->retire_mu);
    node->next = t->retired;
    t->retired = node;
    pthread_mutex_unlock(&t->retire_mu);
    p_open_reclaim_retired(t);
}

static void p_open_reclaim_retired(
    p_open_table *t
) {
    p_open_retired_image **link;

    if (t == NULL) {
        return;
    }

    pthread_mutex_lock(&t->retire_mu);
    link = &t->retired;
    while (*link != NULL) {
        p_open_retired_image *node = *link;

        if (p_open_image_is_hazard_protected(t, node->image)) {
            link = &node->next;
            continue;
        }

        *link = node->next;
        p_open_image_destroy(node->image);
        free(node);
    }
    pthread_mutex_unlock(&t->retire_mu);
}

static void p_open_destroy_retired_all(
    p_open_table *t
) {
    p_open_retired_image *node;

    if (t == NULL) {
        return;
    }

    pthread_mutex_lock(&t->retire_mu);
    node = t->retired;
    t->retired = NULL;
    pthread_mutex_unlock(&t->retire_mu);

    while (node != NULL) {
        p_open_retired_image *next = node->next;

        p_open_image_destroy(node->image);
        free(node);
        node = next;
    }
}

/* --- statistics helpers --------------------------------------------------- */

static void p_open_stats_init(
    p_open_atomic_stats *stats
) {
    if (stats == NULL) {
        return;
    }

    atomic_init(&stats->inserts, 0);
    atomic_init(&stats->insert_failures, 0);
    atomic_init(&stats->lookups, 0);
    atomic_init(&stats->lookup_misses, 0);
    atomic_init(&stats->removes, 0);
    atomic_init(&stats->remove_misses, 0);
    atomic_init(&stats->probes, 0);
    atomic_init(&stats->max_probe_len, 0);
}

static void p_open_stats_reset(
    p_open_atomic_stats *stats
) {
    if (stats == NULL) {
        return;
    }

    atomic_store_explicit(&stats->inserts, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->insert_failures, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->lookups, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->lookup_misses, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->removes, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->remove_misses, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->probes, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->max_probe_len, 0, memory_order_relaxed);
}

static void p_open_stats_inc(
    _Atomic uint64_t *counter
) {
    atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
}

static int p_open_hot_stats_enabled(
    const p_open_table *t
) {
    return t != NULL && t->collect_stats;
}

static void p_open_note_probe(
    p_open_table *t,
    uint64_t probe_len
) {
    if (!p_open_hot_stats_enabled(t)) {
        return;
    }

    atomic_fetch_add_explicit(
        &t->op_stats.probes,
        probe_len,
        memory_order_relaxed
    );
    p_open_stats_max_update(&t->op_stats.max_probe_len, probe_len);
}

static void p_open_stats_max_update(
    _Atomic uint64_t *counter,
    uint64_t value
) {
    uint64_t current;

    if (counter == NULL) {
        return;
    }

    current = atomic_load_explicit(counter, memory_order_relaxed);
    while (value > current &&
           !atomic_compare_exchange_weak_explicit(
                counter,
                &current,
                value,
                memory_order_relaxed,
                memory_order_relaxed
            )) {
    }
}

static size_t p_open_bytes_used_snapshot(
    p_open_table *t
) {
    p_open_image *active;
    p_open_image *shadow;
    p_open_retired_image *retired;
    size_t bytes;
    size_t log_capacity;

    if (t == NULL) {
        return 0;
    }

    bytes = sizeof(*t);
    active = atomic_load_explicit(&t->active, memory_order_acquire);
    shadow = atomic_load_explicit(&t->cleanup_shadow, memory_order_acquire);
    if (p_open_size_add(&bytes, p_open_image_bytes_used(active)) != 0) {
        return SIZE_MAX;
    }
    if (shadow != NULL && shadow != active &&
        p_open_size_add(&bytes, p_open_image_bytes_used(shadow)) != 0) {
        return SIZE_MAX;
    }

    pthread_mutex_lock(&t->retire_mu);
    retired = t->retired;
    while (retired != NULL) {
        if (p_open_size_add(
                &bytes,
                p_open_image_bytes_used(retired->image)
            ) != 0 ||
            p_open_size_add(&bytes, sizeof(*retired)) != 0) {
            pthread_mutex_unlock(&t->retire_mu);
            return SIZE_MAX;
        }
        retired = retired->next;
    }
    pthread_mutex_unlock(&t->retire_mu);

    pthread_mutex_lock(&t->cleanup_log.mu);
    log_capacity = t->cleanup_log.capacity;
    pthread_mutex_unlock(&t->cleanup_log.mu);
    if (p_open_size_mul_add(
            &bytes,
            log_capacity,
            sizeof(p_open_log_entry)
        ) != 0) {
        return SIZE_MAX;
    }

    return bytes;
}

static int p_open_size_add(
    size_t *total,
    size_t add
) {
    if (total == NULL || *total > SIZE_MAX - add) {
        return -1;
    }

    *total += add;
    return 0;
}

static int p_open_size_mul_add(
    size_t *total,
    size_t count,
    size_t size
) {
    if (size != 0 && count > SIZE_MAX / size) {
        return -1;
    }

    return p_open_size_add(total, count * size);
}

/* --- slot publication helpers --------------------------------------------- */

static int p_open_slot_read_consistent(
    const p_open_slot *slot,
    p_open_slot_snapshot *out
) {
    uint32_t v1;
    uint32_t v2;

    if (slot == NULL || out == NULL) {
        return 0;
    }

    v1 = atomic_load_explicit(&slot->seq, memory_order_acquire);
    if ((v1 & 1u) != 0) {
        return 0;
    }

    out->state = atomic_load_explicit(&slot->state, memory_order_acquire);
    out->hash = atomic_load_explicit(&slot->hash, memory_order_relaxed);
    out->key = atomic_load_explicit(&slot->key, memory_order_relaxed);
    out->value = atomic_load_explicit(&slot->value, memory_order_relaxed);

    v2 = atomic_load_explicit(&slot->seq, memory_order_acquire);
    if (v1 != v2 || (v2 & 1u) != 0) {
        return 0;
    }

    return 1;
}

static void p_open_slot_read_spin(
    const p_open_slot *slot,
    p_open_slot_snapshot *out
) {
    while (!p_open_slot_read_consistent(slot, out)) {
    }
}

static void p_open_slot_write_begin(
    p_open_slot *slot
) {
    uint32_t version;

    version = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    atomic_store_explicit(&slot->seq, version + 1u, memory_order_release);
}

static void p_open_slot_write_end(
    p_open_slot *slot
) {
    uint32_t version;

    version = atomic_load_explicit(&slot->seq, memory_order_relaxed);
    atomic_store_explicit(&slot->seq, version + 1u, memory_order_release);
}

static void p_open_publish_full_slot(
    p_open_slot *slot,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value
) {
    p_open_slot_write_begin(slot);
    atomic_store_explicit(&slot->hash, hash, memory_order_relaxed);
    atomic_store_explicit(&slot->key, key, memory_order_relaxed);
    atomic_store_explicit(&slot->value, value, memory_order_relaxed);
    atomic_store_explicit(
        &slot->state,
        OPEN_ADDRESSING_SLOT_FULL,
        memory_order_release
    );
    p_open_slot_write_end(slot);
}

static void p_open_publish_tombstone(
    p_open_slot *slot
) {
    p_open_slot_write_begin(slot);
    atomic_store_explicit(
        &slot->state,
        OPEN_ADDRESSING_SLOT_TOMBSTONE,
        memory_order_release
    );
    p_open_slot_write_end(slot);
}

/* --- stripe helpers ------------------------------------------------------- */

static size_t p_open_stripe_for_index(
    const p_open_image *img,
    size_t idx
) {
    size_t slots_per_stripe;
    size_t stripe;

    if (img == NULL || img->stripe_count == 0 || img->capacity == 0) {
        return 0;
    }

    slots_per_stripe = img->capacity / img->stripe_count;
    if (slots_per_stripe == 0) {
        return 0;
    }

    stripe = idx / slots_per_stripe;
    return (stripe >= img->stripe_count) ? (img->stripe_count - 1) : stripe;
}

static void p_open_span_for_probe(
    const p_open_image *img,
    size_t base,
    uint64_t probe_len,
    p_open_stripe_span *span
) {
    size_t end;

    if (span == NULL) {
        return;
    }

    span->first = 0;
    span->last = 0;
    span->all = 1;

    if (img == NULL || img->stripe_count <= 1 || img->capacity == 0 ||
        probe_len == 0 || probe_len >= img->capacity ||
        probe_len > (uint64_t)(img->capacity - base)) {
        if (img != NULL && img->stripe_count > 0) {
            span->last = img->stripe_count - 1;
        }
        return;
    }

    end = base + (size_t)probe_len - 1;
    span->first = p_open_stripe_for_index(img, base);
    span->last = p_open_stripe_for_index(img, end);
    span->all = 0;
}

static int p_open_span_contains(
    const p_open_image *img,
    const p_open_stripe_span *span,
    size_t idx
) {
    size_t stripe;

    if (span == NULL) {
        return 1;
    }
    if (img == NULL || span->all) {
        return 1;
    }

    stripe = p_open_stripe_for_index(img, idx);
    return stripe >= span->first && stripe <= span->last;
}

static int p_open_lock_span(
    p_open_image *img,
    const p_open_stripe_span *span
) {
    size_t first;
    size_t last;
    size_t i;

    if (img == NULL || img->stripes == NULL || img->stripe_count == 0 ||
        span == NULL) {
        return -1;
    }

    first = span->all ? 0 : span->first;
    last = span->all ? (img->stripe_count - 1) : span->last;
    if (first >= img->stripe_count || last >= img->stripe_count ||
        first > last) {
        return -1;
    }

    for (i = first; i <= last; i++) {
        if (pthread_mutex_lock(&img->stripes[i].mu) != 0) {
            while (i > first) {
                i--;
                pthread_mutex_unlock(&img->stripes[i].mu);
            }
            return -1;
        }
    }

    return 0;
}

static void p_open_unlock_span(
    p_open_image *img,
    const p_open_stripe_span *span
) {
    size_t first;
    size_t last;
    size_t i;

    if (img == NULL || img->stripes == NULL || span == NULL ||
        img->stripe_count == 0) {
        return;
    }

    first = span->all ? 0 : span->first;
    last = span->all ? (img->stripe_count - 1) : span->last;
    if (first >= img->stripe_count || last >= img->stripe_count ||
        first > last) {
        return;
    }

    for (i = first; i <= last; i++) {
        pthread_mutex_unlock(&img->stripes[i].mu);
    }
}

/* --- probe scans ---------------------------------------------------------- */

static ht_result p_open_scan_find(
    const p_open_image *img,
    ht_key_t key,
    uint64_t hash,
    const p_open_stripe_span *locked_span,
    p_open_scan_result *out
) {
    size_t base;
    size_t i;

    if (img == NULL || img->slots == NULL || img->capacity == 0 ||
        out == NULL) {
        return HT_ERR_INVALID;
    }

    memset(out, 0, sizeof(*out));
    base = HT_INDEX_FOR_U64(hash, img->capacity);

    for (i = 0; i < img->capacity; i++) {
        size_t idx = (base + i) & (img->capacity - 1);
        p_open_slot_snapshot snap;

        if (!p_open_span_contains(img, locked_span, idx)) {
            out->needs_more_stripes = 1;
            out->probe_len = i + 1;
            return HT_ERR;
        }

        p_open_slot_read_spin(&img->slots[idx], &snap);
        out->probe_len = i + 1;

        if (snap.state == OPEN_ADDRESSING_SLOT_EMPTY) {
            return HT_ERR_NOT_FOUND;
        }

        if (snap.state == OPEN_ADDRESSING_SLOT_FULL &&
            snap.hash == hash &&
            snap.key == key) {
            out->found_existing = 1;
            out->slot = idx;
            out->slot_state = snap.state;
            out->value = snap.value;
            return HT_OK;
        }
    }

    return HT_ERR_NOT_FOUND;
}

static ht_result p_open_scan_insert(
    const p_open_image *img,
    ht_key_t key,
    uint64_t hash,
    const p_open_stripe_span *locked_span,
    p_open_scan_result *out
) {
    size_t base;
    size_t i;
    size_t first_tombstone = 0;
    int have_tombstone = 0;

    if (img == NULL || img->slots == NULL || img->capacity == 0 ||
        out == NULL) {
        return HT_ERR_INVALID;
    }

    memset(out, 0, sizeof(*out));
    base = HT_INDEX_FOR_U64(hash, img->capacity);

    for (i = 0; i < img->capacity; i++) {
        size_t idx = (base + i) & (img->capacity - 1);
        p_open_slot_snapshot snap;

        if (!p_open_span_contains(img, locked_span, idx)) {
            out->needs_more_stripes = 1;
            out->probe_len = i + 1;
            return HT_ERR;
        }

        p_open_slot_read_spin(&img->slots[idx], &snap);
        out->probe_len = i + 1;

        if (snap.state == OPEN_ADDRESSING_SLOT_FULL) {
            if (snap.hash == hash && snap.key == key) {
                out->found_existing = 1;
                out->slot = idx;
                out->slot_state = snap.state;
                return HT_OK;
            }
            continue;
        }

        if (snap.state == OPEN_ADDRESSING_SLOT_TOMBSTONE &&
            !have_tombstone) {
            first_tombstone = idx;
            have_tombstone = 1;
            continue;
        }

        if (snap.state == OPEN_ADDRESSING_SLOT_EMPTY) {
            out->found_usable = 1;
            out->slot = have_tombstone ? first_tombstone : idx;
            out->slot_state = have_tombstone
                ? OPEN_ADDRESSING_SLOT_TOMBSTONE
                : OPEN_ADDRESSING_SLOT_EMPTY;
            return HT_OK;
        }
    }

    if (have_tombstone) {
        out->found_usable = 1;
        out->slot = first_tombstone;
        out->slot_state = OPEN_ADDRESSING_SLOT_TOMBSTONE;
        out->probe_len = img->capacity;
        return HT_OK;
    }

    out->probe_len = img->capacity;
    return HT_ERR_FULL;
}

/* --- resize helpers ------------------------------------------------------- */

static p_open_account_result p_open_try_account_insert(
    p_open_table *t,
    p_open_image *img,
    int needs_empty_slot
) {
    size_t used;

    if (t == NULL || img == NULL || !needs_empty_slot) {
        return P_OPEN_ACCOUNT_OK;
    }

    for (;;) {
        used = atomic_load_explicit(&img->used, memory_order_acquire);

        if (ht_should_grow_snapshot(
                used,
                img->capacity,
                t->max_load_factor
            )) {
            return p_open_resize_enabled(t)
                ? P_OPEN_ACCOUNT_GROW
                : P_OPEN_ACCOUNT_FULL;
        }

        if (atomic_compare_exchange_weak_explicit(
                &img->used,
                &used,
                used + 1,
                memory_order_acq_rel,
                memory_order_acquire
            )) {
            return P_OPEN_ACCOUNT_OK;
        }
    }
}

static int p_open_resize_enabled(
    const p_open_table *t
) {
    return t != NULL && t->resize_mode != HT_RESIZE_NONE;
}

static int p_open_maintenance_wrlock(
    p_open_table *t
) {
    if (t == NULL) {
        return -1;
    }

    for (;;) {
        /* Cleanup also takes the resize lock exclusively. Waiting here keeps
         * resize and cleanup publication from interleaving. */
        while (atomic_load_explicit(
                   &t->cleanup_in_progress,
                   memory_order_acquire
               )) {
            sched_yield();
        }

        if (pthread_rwlock_wrlock(&t->resize_lock) != 0) {
            return -1;
        }

        if (!atomic_load_explicit(
                &t->cleanup_in_progress,
                memory_order_acquire
            )) {
            return 0;
        }

        pthread_rwlock_unlock(&t->resize_lock);
    }
}

static ht_result p_open_grow_for_insert(
    p_open_table *t,
    int force
) {
    p_open_image *img;
    size_t used;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (p_open_maintenance_wrlock(t) != 0) {
        return HT_ERR_INVALID;
    }

    img = atomic_load_explicit(&t->active, memory_order_acquire);
    if (img == NULL) {
        pthread_rwlock_unlock(&t->resize_lock);
        return HT_ERR_INVALID;
    }

    used = atomic_load_explicit(&img->used, memory_order_acquire);
    if (!force &&
        !ht_should_grow_snapshot(used, img->capacity, t->max_load_factor)) {
        pthread_rwlock_unlock(&t->resize_lock);
        return HT_OK;
    }

    if (img->capacity > SIZE_MAX / 2) {
        pthread_rwlock_unlock(&t->resize_lock);
        return HT_ERR_OOM;
    }

    rc = p_open_resize_locked(t, img->capacity * 2, 0);
    pthread_rwlock_unlock(&t->resize_lock);
    return rc;
}

static ht_result p_open_resize_locked(
    p_open_table *t,
    size_t requested_capacity,
    int force_rebuild
) {
    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    return p_open_resize_locked_quiescent(t, requested_capacity, force_rebuild);
}

static ht_result p_open_resize_locked_quiescent(
    p_open_table *t,
    size_t requested_capacity,
    int force_rebuild
) {
    p_open_image *old_img;
    p_open_image *new_img;
    size_t old_capacity;
    size_t old_size;
    size_t new_capacity;
    size_t moved = 0;
    uint64_t resize_start_ns;
    size_t i;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    old_img = atomic_load_explicit(&t->active, memory_order_acquire);
    if (old_img == NULL) {
        return HT_ERR_INVALID;
    }
    /* Caller owns the resize write lock, so no writer can mutate the active
     * image while this rebuild copies full slots into the replacement image. */
    old_capacity = old_img->capacity;
    old_size = atomic_load_explicit(&old_img->size, memory_order_acquire);

    new_capacity = requested_capacity;
    if (new_capacity < t->min_capacity) {
        new_capacity = t->min_capacity;
    }
    if (new_capacity < old_capacity) {
        new_capacity = old_capacity;
    }
    if (new_capacity < old_size) {
        new_capacity = old_size;
    }
    new_capacity = next_pow2(new_capacity);
    if (new_capacity == 0) {
        return HT_ERR_OOM;
    }

    if (new_capacity == old_capacity && !force_rebuild) {
        return HT_OK;
    }

    resize_start_ns = ht_resize_instrumentation_start(t->collect_stats);
    new_img = p_open_image_create(new_capacity, t->thread_count);
    if (new_img == NULL) {
        return HT_ERR_OOM;
    }

    for (i = 0; i < old_capacity; i++) {
        p_open_slot_snapshot snap;

        p_open_slot_read_spin(&old_img->slots[i], &snap);
        if (snap.state != OPEN_ADDRESSING_SLOT_FULL) {
            continue;
        }

        rc = p_open_shadow_insert(
            new_img,
            snap.hash,
            snap.key,
            snap.value
        );
        if (rc != HT_OK) {
            p_open_image_destroy(new_img);
            return rc;
        }
        moved++;
    }

    atomic_store_explicit(&t->active, new_img, memory_order_release);
    p_open_retire_image(t, old_img);

    ht_resize_stats_record(
        &t->resize_stats,
        t->collect_stats,
        old_capacity,
        new_capacity,
        moved,
        resize_start_ns
    );

    return HT_OK;
}

/* --- mutation log --------------------------------------------------------- */

static int p_open_log_init(
    p_open_mutation_log *log,
    size_t capacity
) {
    if (log == NULL || capacity == 0) {
        return -1;
    }

    memset(log, 0, sizeof(*log));
    if (pthread_mutex_init(&log->mu, NULL) != 0) {
        return -1;
    }

    log->entries = calloc(capacity, sizeof(*log->entries));
    if (log->entries == NULL) {
        pthread_mutex_destroy(&log->mu);
        return -1;
    }
    log->capacity = capacity;
    return 0;
}

static void p_open_log_destroy(
    p_open_mutation_log *log
) {
    if (log == NULL) {
        return;
    }

    free(log->entries);
    log->entries = NULL;
    log->count = 0;
    log->capacity = 0;
    pthread_mutex_destroy(&log->mu);
}

static void p_open_log_reset(
    p_open_mutation_log *log
) {
    if (log == NULL) {
        return;
    }

    pthread_mutex_lock(&log->mu);
    log->count = 0;
    pthread_mutex_unlock(&log->mu);
}

static ht_result p_open_log_append(
    p_open_table *t,
    p_open_log_op op,
    ht_key_t key,
    ht_val_t value
) {
    p_open_mutation_log *log;
    p_open_log_entry *new_entries;
    size_t new_capacity;
    uint64_t seq;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    if (!atomic_load_explicit(&t->cleanup_building, memory_order_acquire)) {
        return HT_OK;
    }

    log = &t->cleanup_log;
    pthread_mutex_lock(&log->mu);
    if (log->limit != 0 && log->count >= log->limit) {
        pthread_mutex_unlock(&log->mu);
        atomic_store_explicit(
            &t->cleanup_fallback_required,
            1,
            memory_order_release
        );
        return HT_ERR_FULL;
    }

    if (log->count == log->capacity) {
        if (log->capacity > SIZE_MAX / 2) {
            pthread_mutex_unlock(&log->mu);
            atomic_store_explicit(
                &t->cleanup_fallback_required,
                1,
                memory_order_release
            );
            return HT_ERR_OOM;
        }
        new_capacity = log->capacity * 2;
        new_entries = realloc(
            log->entries,
            new_capacity * sizeof(*log->entries)
        );
        if (new_entries == NULL) {
            pthread_mutex_unlock(&log->mu);
            atomic_store_explicit(
                &t->cleanup_fallback_required,
                1,
                memory_order_release
            );
            return HT_ERR_OOM;
        }
        log->entries = new_entries;
        log->capacity = new_capacity;
    }

    seq = atomic_fetch_add_explicit(
        &t->write_seq,
        1,
        memory_order_acq_rel
    ) + 1;
    log->entries[log->count].seq = seq;
    log->entries[log->count].op = op;
    log->entries[log->count].key = key;
    log->entries[log->count].value = value;
    log->count++;
    if (log->count > log->peak) {
        log->peak = log->count;
        atomic_store_explicit(
            &t->cleanup_stats.log_peak_entries,
            (uint64_t)log->peak,
            memory_order_relaxed
        );
    }
    pthread_mutex_unlock(&log->mu);
    return HT_OK;
}

/* --- cleanup control ------------------------------------------------------ */

static void p_open_cleanup_stats_init(
    p_open_cleanup_atomic_stats *stats
) {
    if (stats == NULL) {
        return;
    }

    atomic_init(&stats->requested_count, 0);
    atomic_init(&stats->started_count, 0);
    atomic_init(&stats->completed_count, 0);
    atomic_init(&stats->fallback_count, 0);
    atomic_init(&stats->shadow_publish_count, 0);
    atomic_init(&stats->shadow_abandon_count, 0);
    atomic_init(&stats->log_peak_entries, 0);
}

static void p_open_cleanup_stats_reset(
    p_open_cleanup_atomic_stats *stats
) {
    if (stats == NULL) {
        return;
    }

    atomic_store_explicit(&stats->requested_count, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->started_count, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->completed_count, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->fallback_count, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->shadow_publish_count, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->shadow_abandon_count, 0, memory_order_relaxed);
    atomic_store_explicit(&stats->log_peak_entries, 0, memory_order_relaxed);
}

static void p_open_cleanup_state_init(
    p_open_table *t
) {
    if (t == NULL) {
        return;
    }

    atomic_init(&t->tombstone_debt, 0);
    atomic_init(&t->cleanup_requested, 0);
    atomic_init(&t->cleanup_urgent, 0);
    atomic_init(&t->cleanup_in_progress, 0);
    atomic_init(&t->cleanup_building, 0);
    atomic_init(&t->cleanup_fallback_required, 0);
    atomic_init(&t->cleanup_epoch, 0);
    atomic_init(&t->write_seq, 0);
    atomic_init(&t->cleanup_shadow, NULL);
    t->cleanup_pause_before_publish = NULL;
    t->cleanup_pause_before_publish_arg = NULL;
}

static void p_open_cleanup_debt_dec(
    p_open_table *t
) {
    size_t current;

    if (t == NULL) {
        return;
    }

    current = atomic_load_explicit(&t->tombstone_debt, memory_order_acquire);
    while (current > 0 &&
           !atomic_compare_exchange_weak_explicit(
                &t->tombstone_debt,
                &current,
                current - 1,
                memory_order_acq_rel,
                memory_order_acquire
            )) {
    }
}

static void p_open_maybe_request_cleanup(
    p_open_table *t,
    size_t capacity,
    size_t size,
    size_t used,
    uint64_t delete_probe_len
) {
    size_t tombstones;
    size_t debt_add;
    size_t bonus;
    int expected;

    /* Fixed-capacity benchmark runs still need tombstone reclamation; only
     * growth/shrink decisions are gated by the resize mode. */
    if (t == NULL || capacity == 0) {
        return;
    }

    tombstones = (used > size) ? (used - size) : 0;
    bonus = (size_t)(delete_probe_len / 16u);
    if (bonus > P_OPEN_CLEANUP_DELETE_DEBT_BONUS_MAX) {
        bonus = P_OPEN_CLEANUP_DELETE_DEBT_BONUS_MAX;
    }
    debt_add = P_OPEN_CLEANUP_DELETE_DEBT_BASE + bonus;
    atomic_fetch_add_explicit(
        &t->tombstone_debt,
        debt_add,
        memory_order_acq_rel
    );

    if ((double)tombstones >=
        (double)capacity * P_OPEN_CLEANUP_REQUEST_LOAD) {
        expected = 0;
        if (atomic_compare_exchange_strong_explicit(
                &t->cleanup_requested,
                &expected,
                1,
                memory_order_acq_rel,
                memory_order_acquire
            )) {
            p_open_stats_inc(&t->cleanup_stats.requested_count);
        }
    }

    if ((double)tombstones >=
            (double)capacity * P_OPEN_CLEANUP_URGENT_LOAD ||
        ht_should_grow_snapshot(used, capacity, t->max_load_factor)) {
        atomic_store_explicit(&t->cleanup_requested, 1, memory_order_release);
        atomic_store_explicit(&t->cleanup_urgent, 1, memory_order_release);
    }
}

static int p_open_claim_cleaner(
    p_open_table *t
) {
    int expected = 0;

    if (t == NULL) {
        return 0;
    }

    if (!atomic_compare_exchange_strong_explicit(
            &t->cleanup_in_progress,
            &expected,
            1,
            memory_order_acq_rel,
            memory_order_acquire
        )) {
        return 0;
    }

    p_open_stats_inc(&t->cleanup_stats.started_count);
    return 1;
}

static void p_open_release_cleaner(
    p_open_table *t
) {
    if (t == NULL) {
        return;
    }

    atomic_store_explicit(&t->cleanup_in_progress, 0, memory_order_release);
}

static void p_open_cleanup_reset_after_locked(
    p_open_table *t
) {
    if (t == NULL) {
        return;
    }

    atomic_store_explicit(&t->cleanup_building, 0, memory_order_release);
    atomic_store_explicit(&t->cleanup_requested, 0, memory_order_release);
    atomic_store_explicit(&t->cleanup_urgent, 0, memory_order_release);
    atomic_store_explicit(
        &t->cleanup_fallback_required,
        0,
        memory_order_release
    );
    atomic_store_explicit(&t->tombstone_debt, 0, memory_order_release);
    atomic_store_explicit(&t->cleanup_shadow, NULL, memory_order_release);
    p_open_log_reset(&t->cleanup_log);
    atomic_fetch_add_explicit(&t->cleanup_epoch, 1, memory_order_acq_rel);
    atomic_store_explicit(&t->cleanup_in_progress, 0, memory_order_release);
}

/* --- shadow cleanup ------------------------------------------------------- */

static size_t p_open_choose_cleanup_capacity(
    const p_open_table *t,
    const p_open_image *old_img
) {
    (void)t;

    return (old_img == NULL) ? 0 : old_img->capacity;
}

static ht_result p_open_shadow_bulk_copy(
    const p_open_table *t,
    const p_open_image *old_img,
    p_open_image *shadow
) {
    size_t i;
    ht_result rc;

    (void)t;
    if (old_img == NULL || shadow == NULL) {
        return HT_ERR_INVALID;
    }

    for (i = 0; i < old_img->capacity; i++) {
        p_open_slot_snapshot snap;

        p_open_slot_read_spin(&old_img->slots[i], &snap);
        if (snap.state != OPEN_ADDRESSING_SLOT_FULL) {
            continue;
        }

        rc = p_open_shadow_insert(
            shadow,
            snap.hash,
            snap.key,
            snap.value
        );
        if (rc != HT_OK) {
            return rc;
        }
    }

    return HT_OK;
}

static ht_result p_open_shadow_insert(
    p_open_image *img,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value
) {
    size_t base;
    size_t i;
    size_t first_tombstone = 0;
    int have_tombstone = 0;

    if (img == NULL || img->slots == NULL || img->capacity == 0) {
        return HT_ERR_INVALID;
    }

    base = HT_INDEX_FOR_U64(hash, img->capacity);
    for (i = 0; i < img->capacity; i++) {
        size_t idx = (base + i) & (img->capacity - 1);
        uint8_t state = atomic_load_explicit(
            &img->slots[idx].state,
            memory_order_relaxed
        );

        if (state == OPEN_ADDRESSING_SLOT_FULL) {
            uint64_t slot_hash = atomic_load_explicit(
                &img->slots[idx].hash,
                memory_order_relaxed
            );
            ht_key_t slot_key = atomic_load_explicit(
                &img->slots[idx].key,
                memory_order_relaxed
            );

            if (slot_hash == hash && slot_key == key) {
                atomic_store_explicit(
                    &img->slots[idx].value,
                    value,
                    memory_order_relaxed
                );
                return HT_OK;
            }
            continue;
        }

        if (state == OPEN_ADDRESSING_SLOT_TOMBSTONE &&
            !have_tombstone) {
            first_tombstone = idx;
            have_tombstone = 1;
            continue;
        }

        if (state == OPEN_ADDRESSING_SLOT_EMPTY) {
            idx = have_tombstone ? first_tombstone : idx;
            atomic_store_explicit(
                &img->slots[idx].hash,
                hash,
                memory_order_relaxed
            );
            atomic_store_explicit(
                &img->slots[idx].key,
                key,
                memory_order_relaxed
            );
            atomic_store_explicit(
                &img->slots[idx].value,
                value,
                memory_order_relaxed
            );
            atomic_store_explicit(
                &img->slots[idx].state,
                OPEN_ADDRESSING_SLOT_FULL,
                memory_order_relaxed
            );
            atomic_fetch_add_explicit(&img->size, 1, memory_order_relaxed);
            if (!have_tombstone) {
                atomic_fetch_add_explicit(&img->used, 1, memory_order_relaxed);
            }
            return HT_OK;
        }
    }

    if (have_tombstone) {
        atomic_store_explicit(
            &img->slots[first_tombstone].hash,
            hash,
            memory_order_relaxed
        );
        atomic_store_explicit(
            &img->slots[first_tombstone].key,
            key,
            memory_order_relaxed
        );
        atomic_store_explicit(
            &img->slots[first_tombstone].value,
            value,
            memory_order_relaxed
        );
        atomic_store_explicit(
            &img->slots[first_tombstone].state,
            OPEN_ADDRESSING_SLOT_FULL,
            memory_order_relaxed
        );
        atomic_fetch_add_explicit(&img->size, 1, memory_order_relaxed);
        return HT_OK;
    }

    return HT_ERR_FULL;
}

static ht_result p_open_shadow_remove(
    p_open_image *img,
    uint64_t hash,
    ht_key_t key
) {
    size_t base;
    size_t i;

    if (img == NULL || img->slots == NULL || img->capacity == 0) {
        return HT_ERR_INVALID;
    }

    base = HT_INDEX_FOR_U64(hash, img->capacity);
    for (i = 0; i < img->capacity; i++) {
        size_t idx = (base + i) & (img->capacity - 1);
        uint8_t state = atomic_load_explicit(
            &img->slots[idx].state,
            memory_order_relaxed
        );

        if (state == OPEN_ADDRESSING_SLOT_EMPTY) {
            return HT_OK;
        }

        if (state == OPEN_ADDRESSING_SLOT_FULL) {
            uint64_t slot_hash = atomic_load_explicit(
                &img->slots[idx].hash,
                memory_order_relaxed
            );
            ht_key_t slot_key = atomic_load_explicit(
                &img->slots[idx].key,
                memory_order_relaxed
            );

            if (slot_hash == hash && slot_key == key) {
                atomic_store_explicit(
                    &img->slots[idx].state,
                    OPEN_ADDRESSING_SLOT_TOMBSTONE,
                    memory_order_relaxed
                );
                atomic_fetch_sub_explicit(
                    &img->size,
                    1,
                    memory_order_relaxed
                );
                return HT_OK;
            }
        }
    }

    return HT_OK;
}

static ht_result p_open_shadow_replay_log(
    p_open_table *t,
    p_open_image *shadow,
    uint64_t start_seq,
    uint64_t end_seq
) {
    p_open_mutation_log *log;
    size_t i;
    ht_result rc = HT_OK;

    if (t == NULL || shadow == NULL) {
        return HT_ERR_INVALID;
    }

    log = &t->cleanup_log;
    pthread_mutex_lock(&log->mu);
    for (i = 0; i < log->count; i++) {
        p_open_log_entry *entry = &log->entries[i];
        uint64_t hash;

        if (entry->seq <= start_seq || entry->seq > end_seq) {
            continue;
        }

        hash = t->hash_fn(entry->key, t->hash_seed);
        if (entry->op == P_OPEN_LOG_INSERT) {
            rc = p_open_shadow_insert(
                shadow,
                hash,
                entry->key,
                entry->value
            );
        } else {
            rc = p_open_shadow_remove(shadow, hash, entry->key);
        }
        if (rc != HT_OK) {
            break;
        }
    }
    pthread_mutex_unlock(&log->mu);
    return rc;
}

static void p_open_abandon_shadow(
    p_open_table *t,
    p_open_image *shadow
) {
    p_open_image *expected;

    if (t == NULL || shadow == NULL) {
        return;
    }

    expected = shadow;
    atomic_compare_exchange_strong_explicit(
        &t->cleanup_shadow,
        &expected,
        NULL,
        memory_order_acq_rel,
        memory_order_acquire
    );
    p_open_image_destroy(shadow);
    p_open_stats_inc(&t->cleanup_stats.shadow_abandon_count);
}

static ht_result p_open_run_cleanup_exclusive_fallback_locked(
    p_open_table *t
) {
    p_open_image *active;
    ht_result rc;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    active = atomic_load_explicit(&t->active, memory_order_acquire);
    if (active == NULL) {
        return HT_ERR_INVALID;
    }

    p_open_stats_inc(&t->cleanup_stats.fallback_count);
    rc = p_open_resize_locked_quiescent(t, active->capacity, 1);
    if (rc == HT_OK) {
        p_open_stats_inc(&t->cleanup_stats.completed_count);
    }
    p_open_cleanup_reset_after_locked(t);
    return rc;
}

static void p_open_run_cleanup_shadow_rebuild(
    p_open_table *t
) {
    p_open_image *old_img;
    p_open_image *shadow = NULL;
    p_open_image *current;
    size_t target_capacity;
    uint64_t build_start_seq;
    uint64_t final_seq;
    uint64_t resize_start_ns;
    size_t old_capacity;
    size_t moved;
    ht_result rc = HT_OK;

    if (t == NULL) {
        return;
    }

    if (pthread_rwlock_wrlock(&t->resize_lock) != 0) {
        p_open_release_cleaner(t);
        return;
    }

    old_img = atomic_load_explicit(&t->active, memory_order_acquire);
    if (old_img == NULL) {
        pthread_rwlock_unlock(&t->resize_lock);
        p_open_release_cleaner(t);
        return;
    }

    old_capacity = old_img->capacity;
    target_capacity = p_open_choose_cleanup_capacity(t, old_img);
    p_open_log_reset(&t->cleanup_log);
    build_start_seq = atomic_load_explicit(&t->write_seq, memory_order_acquire);
    /* Enable replay logging while writers are blocked by the write lock. Once
     * this lock is released, every successful mutation must append to the log
     * until cleanup publishes or abandons the shadow image. */
    atomic_store_explicit(&t->cleanup_building, 1, memory_order_release);
    pthread_rwlock_unlock(&t->resize_lock);

    shadow = p_open_image_create(target_capacity, t->thread_count);
    if (shadow == NULL) {
        atomic_store_explicit(
            &t->cleanup_fallback_required,
            1,
            memory_order_release
        );
    } else {
        atomic_store_explicit(&t->cleanup_shadow, shadow, memory_order_release);
        rc = p_open_shadow_bulk_copy(t, old_img, shadow);
        if (rc != HT_OK) {
            atomic_store_explicit(
                &t->cleanup_fallback_required,
                1,
                memory_order_release
            );
        }
    }

    if (shadow != NULL && t->cleanup_pause_before_publish != NULL) {
        t->cleanup_pause_before_publish(t->cleanup_pause_before_publish_arg);
    }

    if (pthread_rwlock_wrlock(&t->resize_lock) != 0) {
        if (shadow != NULL) {
            p_open_abandon_shadow(t, shadow);
        }
        p_open_release_cleaner(t);
        return;
    }
    /* With the write lock held, all writers that can append to the replay log
     * have drained. This sequence number is the closed upper bound to replay. */
    final_seq = atomic_load_explicit(&t->write_seq, memory_order_acquire);

    if (atomic_load_explicit(
            &t->cleanup_fallback_required,
            memory_order_acquire
        )) {
        if (shadow != NULL) {
            p_open_abandon_shadow(t, shadow);
        }
        (void)p_open_run_cleanup_exclusive_fallback_locked(t);
        pthread_rwlock_unlock(&t->resize_lock);
        return;
    }

    current = atomic_load_explicit(&t->active, memory_order_acquire);
    if (current != old_img) {
        if (shadow != NULL) {
            p_open_abandon_shadow(t, shadow);
        }
        atomic_store_explicit(
            &t->cleanup_fallback_required,
            1,
            memory_order_release
        );
        (void)p_open_run_cleanup_exclusive_fallback_locked(t);
        pthread_rwlock_unlock(&t->resize_lock);
        return;
    }

    rc = p_open_shadow_replay_log(t, shadow, build_start_seq, final_seq);
    if (rc != HT_OK) {
        if (shadow != NULL) {
            p_open_abandon_shadow(t, shadow);
        }
        atomic_store_explicit(
            &t->cleanup_fallback_required,
            1,
            memory_order_release
        );
        (void)p_open_run_cleanup_exclusive_fallback_locked(t);
        pthread_rwlock_unlock(&t->resize_lock);
        return;
    }

    resize_start_ns = ht_resize_instrumentation_start(t->collect_stats);
    moved = atomic_load_explicit(&shadow->size, memory_order_acquire);
    atomic_store_explicit(&t->cleanup_shadow, NULL, memory_order_release);
    atomic_store_explicit(&t->active, shadow, memory_order_release);
    p_open_retire_image(t, old_img);
    p_open_stats_inc(&t->cleanup_stats.shadow_publish_count);
    p_open_stats_inc(&t->cleanup_stats.completed_count);
    ht_resize_stats_record(
        &t->resize_stats,
        t->collect_stats,
        old_capacity,
        shadow->capacity,
        moved,
        resize_start_ns
    );
    p_open_cleanup_reset_after_locked(t);
    pthread_rwlock_unlock(&t->resize_lock);
}
