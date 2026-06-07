/**
 * @file    lf_hopscotch_impl.c
 * @brief   Concurrent hopscotch backend with writer-stop resize and overflow.
 *
 * Resizing model:
 *   - readers are wait-free with respect to resize: they load the current table
 *     generation and scan it without taking a lock;
 *   - writers enter a small resize gate before mutating;
 *   - a resizer sets resize_requested, waits for active writers to drain, builds
 *     a new generation, publishes it with release ordering, and retires the old
 *     generation without freeing it until destroy.
 *
 * This is intentionally not a fully lock-free resize.  The hot lookup path and
 * primary bucket operations remain C11 atomic operations.  The resize escape
 * hatch matches the practical assumption used by hopscotch insertion: if the
 * bounded displacement search cannot create a neighbourhood slot, grow and try
 * again.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lf_hopscotch_impl.h"

#if defined(__has_include)
#if __has_include("ht_internal.h")
#include "ht_internal.h"
#define LF_HOPSCOTCH_HAVE_HT_INTERNAL 1
#endif
#endif

#ifndef LF_HOPSCOTCH_HAVE_HT_INTERNAL
struct ht_vtable {
    void (*destroy)(void *ctx);
    ht_result (*insert)(void *ctx, ht_key_t key, ht_val_t value);
    ht_result (*get)(const void *ctx, ht_key_t key, ht_val_t *value_out);
    ht_result (*remove)(void *ctx, ht_key_t key);
    size_t (*size)(const void *ctx);
    size_t (*capacity)(const void *ctx);
    double (*load_factor)(const void *ctx);
    ht_result (*reserve)(void *ctx, size_t capacity);
    ht_result (*rehash)(void *ctx, size_t capacity);
    ht_result (*get_stats)(const void *ctx, ht_stats *out);
    ht_result (*reset_stats)(void *ctx);
    int (*bind_bench_iface)(void *ctx, bench_iface *out);
};
#endif

#define LF_STATE_BITS        3ull
#define LF_STATE_MASK        ((UINT64_C(1) << LF_STATE_BITS) - 1ull)
#define LF_STATE_VERSION_INC (UINT64_C(1) << LF_STATE_BITS)
#define LF_STATE(w)          ((unsigned)((w) & LF_STATE_MASK))
#define LF_STATE_WITH(w, s)  (((w) & ~LF_STATE_MASK) | (uint64_t)(s))

#define LF_MIN(a, b) ((a) < (b) ? (a) : (b))
#define LF_MAX(a, b) ((a) > (b) ? (a) : (b))

static uint64_t
lf_default_hash(ht_key_t key, uint64_t seed)
{
    uint64_t x = key + seed + UINT64_C(0x9e3779b97f4a7c15);

    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static size_t
lf_next_pow2(size_t x)
{
    size_t p = 1;

    if (x <= 1) {
        return 1;
    }
    if (x > ((size_t)1 << (sizeof(size_t) * 8 - 1))) {
        return 0;
    }
    while (p < x) {
        p <<= 1;
    }
    return p;
}

static size_t
lf_index(uint64_t hash, size_t mask)
{
    return (size_t)hash & mask;
}

static size_t
lf_core_dist(const lf_hopscotch_core *core, size_t from, size_t to)
{
    return (to - from) & core->mask;
}

static unsigned
lf_ctz64(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(x);
#else
    unsigned n = 0;

    while (((x >> n) & UINT64_C(1)) == 0) {
        n++;
    }
    return n;
#endif
}

static void
lf_cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause" ::: "memory");
#else
    atomic_signal_fence(memory_order_seq_cst);
#endif
}

static void
lf_note_probe(lf_hopscotch_table *t, uint64_t probes)
{
    uint64_t old;

    if (t == NULL || !t->collect_stats) {
        return;
    }

    atomic_fetch_add_explicit(&t->probes, probes, memory_order_relaxed);
    old = atomic_load_explicit(&t->max_probe_len, memory_order_relaxed);
    while (old < probes &&
           !atomic_compare_exchange_weak_explicit(
               &t->max_probe_len,
               &old,
               probes,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
        ;
    }
}

static int
lf_state_is_search_visible(unsigned state)
{
    return state == LF_HOPSCOTCH_MEMBER || state == LF_HOPSCOTCH_MOVING;
}

static int
lf_state_is_duplicate_visible(unsigned state)
{
    return state == LF_HOPSCOTCH_MEMBER ||
           state == LF_HOPSCOTCH_MOVING ||
           state == LF_HOPSCOTCH_INSERTING;
}

static int
lf_cas_state(
    lf_hopscotch_bucket *bucket,
    unsigned expected_state,
    unsigned desired_state
) {
    uint64_t word = atomic_load_explicit(&bucket->state, memory_order_acquire);

    for (;;) {
        if (LF_STATE(word) != expected_state) {
            return 0;
        }
        if (atomic_compare_exchange_weak_explicit(
                &bucket->state,
                &word,
                LF_STATE_WITH(word + LF_STATE_VERSION_INC, desired_state),
                memory_order_acq_rel,
                memory_order_acquire
            )) {
            return 1;
        }
    }
}

static int
lf_try_claim_empty(lf_hopscotch_bucket *bucket)
{
    return lf_cas_state(bucket, LF_HOPSCOTCH_EMPTY, LF_HOPSCOTCH_BUSY);
}

static int
lf_release_expected_empty(lf_hopscotch_bucket *bucket, unsigned expected_state)
{
    return lf_cas_state(bucket, expected_state, LF_HOPSCOTCH_EMPTY);
}

static int
lf_read_payload_if_stable(
    lf_hopscotch_bucket *bucket,
    unsigned *state_out,
    uint64_t *hash_out,
    ht_key_t *key_out,
    ht_val_t *value_out
) {
    uint64_t s1;
    uint64_t s2;
    unsigned state;

    /* The low state bits share a version word; matching reads mean the payload
     * was not republished while this function copied it. */
    s1 = atomic_load_explicit(&bucket->state, memory_order_acquire);
    state = LF_STATE(s1);
    if (!lf_state_is_duplicate_visible(state)) {
        return 0;
    }

    if (hash_out != NULL) {
        *hash_out = atomic_load_explicit(&bucket->hash, memory_order_acquire);
    }
    if (key_out != NULL) {
        *key_out = atomic_load_explicit(&bucket->key, memory_order_acquire);
    }
    if (value_out != NULL) {
        *value_out = atomic_load_explicit(&bucket->value, memory_order_acquire);
    }

    s2 = atomic_load_explicit(&bucket->state, memory_order_acquire);
    if (s1 != s2) {
        return 0;
    }

    if (state_out != NULL) {
        *state_out = state;
    }
    return 1;
}

static lf_hopscotch_core *
lf_core_create(size_t requested_capacity)
{
    lf_hopscotch_core *core;
    size_t capacity;

    capacity = lf_next_pow2(requested_capacity);
    if (capacity == 0) {
        return NULL;
    }
    if (capacity < LF_HOPSCOTCH_HOP_RANGE) {
        capacity = lf_next_pow2((size_t)LF_HOPSCOTCH_HOP_RANGE);
        if (capacity == 0) {
            return NULL;
        }
    }

    core = calloc(1, sizeof(*core));
    if (core == NULL) {
        return NULL;
    }

    core->buckets = calloc(capacity, sizeof(*core->buckets));
    if (core->buckets == NULL) {
        free(core);
        return NULL;
    }

    core->capacity = capacity;
    core->mask = capacity - 1;
    core->hop_range = LF_MIN((size_t)LF_HOPSCOTCH_HOP_RANGE, capacity);
    core->add_range = LF_MIN((size_t)LF_HOPSCOTCH_ADD_RANGE, capacity);
    atomic_init(&core->size, 0);
    core->retired_next = NULL;

    for (size_t i = 0; i < capacity; i++) {
        atomic_init(&core->buckets[i].state, LF_HOPSCOTCH_EMPTY);
        atomic_init(&core->buckets[i].hop_info, 0);
        atomic_init(&core->buckets[i].reloc_counter, 0);
        atomic_init(&core->buckets[i].hash, 0);
        atomic_init(&core->buckets[i].key, 0);
        atomic_init(&core->buckets[i].value, 0);
    }

    return core;
}

static void
lf_core_destroy(lf_hopscotch_core *core)
{
    if (core == NULL) {
        return;
    }
    free(core->buckets);
    free(core);
}

static int
lf_core_find_member(
    lf_hopscotch_core *core,
    ht_key_t key,
    uint64_t hash,
    ht_val_t *value_out,
    size_t *index_out,
    uint64_t *probe_out
) {
    size_t home;
    uint64_t probes = 0;

    if (core == NULL) {
        if (probe_out != NULL) {
            *probe_out = 0;
        }
        return 0;
    }

    home = lf_index(hash, core->mask);

    for (;;) {
        uint64_t rc_before;
        uint64_t rc_after;
        uint64_t bitmap;

        /* Relocation counter brackets the bitmap scan. If a writer moves any
         * member in this home bucket, retry so lookups do not miss it. */
        rc_before = atomic_load_explicit(
            &core->buckets[home].reloc_counter,
            memory_order_acquire
        );
        bitmap = atomic_load_explicit(
            &core->buckets[home].hop_info,
            memory_order_acquire
        );

        while (bitmap != 0) {
            unsigned bit = lf_ctz64(bitmap);
            size_t idx = (home + bit) & core->mask;
            lf_hopscotch_bucket *bucket = &core->buckets[idx];
            unsigned state;
            uint64_t bucket_hash;
            ht_key_t bucket_key;
            ht_val_t bucket_value;

            probes++;
            if (lf_read_payload_if_stable(
                    bucket,
                    &state,
                    &bucket_hash,
                    &bucket_key,
                    &bucket_value
                ) &&
                lf_state_is_search_visible(state) &&
                bucket_hash == hash &&
                bucket_key == key) {
                if (value_out != NULL) {
                    *value_out = bucket_value;
                }
                if (index_out != NULL) {
                    *index_out = idx;
                }
                if (probe_out != NULL) {
                    *probe_out = probes;
                }
                return 1;
            }
            bitmap &= bitmap - 1;
        }

        rc_after = atomic_load_explicit(
            &core->buckets[home].reloc_counter,
            memory_order_acquire
        );
        if (rc_before == rc_after) {
            if (probe_out != NULL) {
                *probe_out = probes;
            }
            return 0;
        }
    }
}

static int
lf_core_find_empty(
    lf_hopscotch_core *core,
    size_t home,
    size_t *empty_out,
    uint64_t *probe_out
) {
    size_t limit;
    uint64_t probes = 0;

    limit = LF_MIN(core->add_range, core->capacity);
    for (size_t off = 0; off < limit; off++) {
        size_t idx = (home + off) & core->mask;

        probes++;
        if (lf_try_claim_empty(&core->buckets[idx])) {
            *empty_out = idx;
            if (probe_out != NULL) {
                *probe_out = probes;
            }
            return 1;
        }
    }

    if (probe_out != NULL) {
        *probe_out = probes;
    }
    return 0;
}

static int
lf_core_try_move_member_into_busy(
    lf_hopscotch_core *core,
    size_t candidate_home,
    unsigned old_off,
    unsigned new_off,
    size_t src,
    size_t dst
) {
    lf_hopscotch_bucket *src_bucket = &core->buckets[src];
    lf_hopscotch_bucket *dst_bucket = &core->buckets[dst];
    uint64_t moved_hash;
    ht_key_t moved_key;
    ht_val_t moved_value;
    uint64_t old_bit = UINT64_C(1) << old_off;
    uint64_t new_bit = UINT64_C(1) << new_off;

    if (!lf_cas_state(src_bucket, LF_HOPSCOTCH_MEMBER, LF_HOPSCOTCH_MOVING)) {
        return 0;
    }

    moved_hash = atomic_load_explicit(&src_bucket->hash, memory_order_acquire);
    moved_key = atomic_load_explicit(&src_bucket->key, memory_order_acquire);
    moved_value = atomic_load_explicit(&src_bucket->value, memory_order_acquire);

    atomic_store_explicit(&dst_bucket->hash, moved_hash, memory_order_relaxed);
    atomic_store_explicit(&dst_bucket->key, moved_key, memory_order_relaxed);
    atomic_store_explicit(&dst_bucket->value, moved_value, memory_order_relaxed);

    if (!lf_cas_state(dst_bucket, LF_HOPSCOTCH_BUSY, LF_HOPSCOTCH_MEMBER)) {
        (void)lf_cas_state(src_bucket, LF_HOPSCOTCH_MOVING, LF_HOPSCOTCH_MEMBER);
        return 0;
    }

    /* Publish the destination bit before clearing the source bit. Readers may
     * see both positions briefly, but should not see neither. */
    atomic_fetch_or_explicit(
        &core->buckets[candidate_home].hop_info,
        new_bit,
        memory_order_acq_rel
    );
    atomic_fetch_add_explicit(
        &core->buckets[candidate_home].reloc_counter,
        1,
        memory_order_acq_rel
    );
    atomic_fetch_and_explicit(
        &core->buckets[candidate_home].hop_info,
        ~old_bit,
        memory_order_acq_rel
    );

    if (!lf_cas_state(src_bucket, LF_HOPSCOTCH_MOVING, LF_HOPSCOTCH_BUSY)) {
        return 0;
    }

    return 1;
}

static int
lf_core_move_empty_toward_home(
    lf_hopscotch_core *core,
    size_t home,
    size_t *empty_io
) {
    size_t empty = *empty_io;

    while (lf_core_dist(core, home, empty) >= core->hop_range) {
        int moved = 0;

        for (size_t delta = core->hop_range - 1; delta > 0 && !moved; delta--) {
            size_t candidate_home = (empty - delta) & core->mask;
            uint64_t rc_before;
            uint64_t rc_after;
            uint64_t bitmap;

            rc_before = atomic_load_explicit(
                &core->buckets[candidate_home].reloc_counter,
                memory_order_acquire
            );
            bitmap = atomic_load_explicit(
                &core->buckets[candidate_home].hop_info,
                memory_order_acquire
            );

            while (bitmap != 0) {
                unsigned old_off = lf_ctz64(bitmap);
                size_t src = (candidate_home + old_off) & core->mask;

                if (old_off < delta && src != empty) {
                    if (lf_core_try_move_member_into_busy(
                            core,
                            candidate_home,
                            old_off,
                            (unsigned)delta,
                            src,
                            empty
                        )) {
                        empty = src;
                        moved = 1;
                        break;
                    }
                }

                bitmap &= bitmap - 1;
            }

            if (!moved) {
                rc_after = atomic_load_explicit(
                    &core->buckets[candidate_home].reloc_counter,
                    memory_order_acquire
                );
                if (rc_before != rc_after) {
                    /* Another writer changed this neighbourhood; retry the
                     * outer search from the current empty slot. */
                    moved = 1;
                }
            }
        }

        if (!moved) {
            *empty_io = empty;
            return 0;
        }
    }

    *empty_io = empty;
    return 1;
}

static ht_result
lf_core_publish_insert(
    lf_hopscotch_core *core,
    size_t idx,
    size_t home,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value
) {
    lf_hopscotch_bucket *bucket = &core->buckets[idx];
    size_t dist = lf_core_dist(core, home, idx);
    uint64_t bit = UINT64_C(1) << dist;

    atomic_store_explicit(&bucket->hash, hash, memory_order_relaxed);
    atomic_store_explicit(&bucket->key, key, memory_order_relaxed);
    atomic_store_explicit(&bucket->value, value, memory_order_relaxed);

    atomic_fetch_or_explicit(
        &core->buckets[home].hop_info,
        bit,
        memory_order_acq_rel
    );

    if (!lf_cas_state(bucket, LF_HOPSCOTCH_BUSY, LF_HOPSCOTCH_INSERTING)) {
        atomic_fetch_and_explicit(
            &core->buckets[home].hop_info,
            ~bit,
            memory_order_acq_rel
        );
        return HT_ERR;
    }

    for (;;) {
        uint64_t rc_before;
        uint64_t rc_after;
        uint64_t bitmap;
        int duplicate = 0;

        /* INSERTING is visible for duplicate detection. The lower offset wins
         * so two racing inserts for the same key cannot both publish. */
        rc_before = atomic_load_explicit(
            &core->buckets[home].reloc_counter,
            memory_order_acquire
        );
        bitmap = atomic_load_explicit(
            &core->buckets[home].hop_info,
            memory_order_acquire
        );

        while (bitmap != 0) {
            unsigned off = lf_ctz64(bitmap);
            size_t other = (home + off) & core->mask;
            lf_hopscotch_bucket *other_bucket = &core->buckets[other];
            unsigned other_state;
            uint64_t other_hash;
            ht_key_t other_key;

            if (other != idx &&
                lf_read_payload_if_stable(
                    other_bucket,
                    &other_state,
                    &other_hash,
                    &other_key,
                    NULL
                ) &&
                lf_state_is_duplicate_visible(other_state) &&
                other_hash == hash &&
                other_key == key) {
                if (other_state == LF_HOPSCOTCH_MEMBER ||
                    other_state == LF_HOPSCOTCH_MOVING) {
                    duplicate = 1;
                    break;
                }
                if (other_state == LF_HOPSCOTCH_INSERTING && off < dist) {
                    duplicate = 1;
                    break;
                }
            }

            bitmap &= bitmap - 1;
        }

        rc_after = atomic_load_explicit(
            &core->buckets[home].reloc_counter,
            memory_order_acquire
        );
        if (rc_before != rc_after) {
            continue;
        }

        if (duplicate) {
            atomic_fetch_and_explicit(
                &core->buckets[home].hop_info,
                ~bit,
                memory_order_acq_rel
            );
            (void)lf_release_expected_empty(bucket, LF_HOPSCOTCH_INSERTING);
            return HT_ERR_EXISTS;
        }

        if (lf_cas_state(bucket, LF_HOPSCOTCH_INSERTING, LF_HOPSCOTCH_MEMBER)) {
            atomic_fetch_add_explicit(&core->size, 1, memory_order_relaxed);
            return HT_OK;
        }

        atomic_fetch_and_explicit(
            &core->buckets[home].hop_info,
            ~bit,
            memory_order_acq_rel
        );
        return HT_ERR;
    }
}

static ht_result
lf_core_insert_known_hash(
    lf_hopscotch_core *core,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value,
    uint64_t *probe_out
) {
    size_t home;
    size_t empty;
    uint64_t probes = 0;
    uint64_t op_probes = 0;
    ht_result result;

    home = lf_index(hash, core->mask);

    if (lf_core_find_member(core, key, hash, NULL, NULL, &op_probes)) {
        if (probe_out != NULL) {
            *probe_out = op_probes;
        }
        return HT_ERR_EXISTS;
    }
    probes += op_probes;

    if (!lf_core_find_empty(core, home, &empty, &op_probes)) {
        if (probe_out != NULL) {
            *probe_out = probes + op_probes;
        }
        return HT_ERR_FULL;
    }
    probes += op_probes;

    if (lf_core_dist(core, home, empty) >= core->hop_range) {
        if (!lf_core_move_empty_toward_home(core, home, &empty)) {
            (void)lf_release_expected_empty(&core->buckets[empty], LF_HOPSCOTCH_BUSY);
            if (probe_out != NULL) {
                *probe_out = probes;
            }
            return HT_ERR_FULL;
        }
    }

    result = lf_core_publish_insert(core, empty, home, hash, key, value);
    if (probe_out != NULL) {
        *probe_out = probes;
    }
    return result;
}

static ht_result
lf_core_remove_known_hash(
    lf_hopscotch_core *core,
    uint64_t hash,
    ht_key_t key,
    uint64_t *probe_out
) {
    for (;;) {
        size_t idx;
        size_t home;
        size_t dist;
        uint64_t bit;
        lf_hopscotch_bucket *bucket;
        uint64_t state_word;

        if (!lf_core_find_member(core, key, hash, NULL, &idx, probe_out)) {
            return HT_ERR_NOT_FOUND;
        }

        bucket = &core->buckets[idx];
        state_word = atomic_load_explicit(&bucket->state, memory_order_acquire);
        if (LF_STATE(state_word) == LF_HOPSCOTCH_MOVING) {
            continue;
        }
        if (LF_STATE(state_word) != LF_HOPSCOTCH_MEMBER) {
            continue;
        }
        if (!atomic_compare_exchange_strong_explicit(
                &bucket->state,
                &state_word,
                LF_STATE_WITH(state_word + LF_STATE_VERSION_INC, LF_HOPSCOTCH_BUSY),
                memory_order_acq_rel,
                memory_order_acquire
            )) {
            continue;
        }

        home = lf_index(hash, core->mask);
        dist = lf_core_dist(core, home, idx);
        bit = UINT64_C(1) << dist;

        atomic_fetch_add_explicit(
            &core->buckets[home].reloc_counter,
            1,
            memory_order_acq_rel
        );
        /* Bump the relocation counter before clearing the home bitmap so any
         * concurrent lookup retries the now-stale neighbourhood scan. */
        atomic_fetch_and_explicit(
            &core->buckets[home].hop_info,
            ~bit,
            memory_order_acq_rel
        );
        atomic_store_explicit(&bucket->hash, 0, memory_order_relaxed);
        atomic_store_explicit(&bucket->key, 0, memory_order_relaxed);
        atomic_store_explicit(&bucket->value, 0, memory_order_relaxed);
        (void)lf_release_expected_empty(bucket, LF_HOPSCOTCH_BUSY);
        atomic_fetch_sub_explicit(&core->size, 1, memory_order_relaxed);
        return HT_OK;
    }
}

static int
lf_overflow_get(
    const lf_hopscotch_table *table,
    uint64_t hash,
    ht_key_t key,
    ht_val_t *value_out
) {
    lf_hopscotch_overflow_node *node;

    node = atomic_load_explicit(&table->overflow_head, memory_order_acquire);
    while (node != NULL) {
        if (node->hash == hash && node->key == key &&
            atomic_load_explicit(&node->live, memory_order_acquire)) {
            if (value_out != NULL) {
                *value_out = atomic_load_explicit(&node->value, memory_order_acquire);
            }
            return 1;
        }
        node = node->next;
    }
    return 0;
}

static void
lf_overflow_lock(lf_hopscotch_table *table)
{
    /* Overflow is a fixed-mode escape hatch, so a simple spin lock keeps the
     * uncommon stash path small. */
    while (atomic_flag_test_and_set_explicit(&table->overflow_lock, memory_order_acquire)) {
        lf_cpu_relax();
    }
}

static void
lf_overflow_unlock(lf_hopscotch_table *table)
{
    atomic_flag_clear_explicit(&table->overflow_lock, memory_order_release);
}

static ht_result
lf_overflow_insert(
    lf_hopscotch_table *table,
    uint64_t hash,
    ht_key_t key,
    ht_val_t value
) {
    lf_hopscotch_overflow_node *node;

    lf_overflow_lock(table);

    if (lf_overflow_get(table, hash, key, NULL)) {
        lf_overflow_unlock(table);
        return HT_ERR_EXISTS;
    }

    node = malloc(sizeof(*node));
    if (node == NULL) {
        lf_overflow_unlock(table);
        return HT_ERR_OOM;
    }

    node->hash = hash;
    node->key = key;
    atomic_init(&node->value, value);
    atomic_init(&node->live, 1u);
    node->next = atomic_load_explicit(&table->overflow_head, memory_order_relaxed);
    atomic_store_explicit(&table->overflow_head, node, memory_order_release);
    atomic_fetch_add_explicit(&table->overflow_size, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&table->overflow_allocated, 1, memory_order_relaxed);

    lf_overflow_unlock(table);
    return HT_OK;
}

static ht_result
lf_overflow_remove(
    lf_hopscotch_table *table,
    uint64_t hash,
    ht_key_t key
) {
    lf_hopscotch_overflow_node *node;
    ht_result result = HT_ERR_NOT_FOUND;

    lf_overflow_lock(table);

    node = atomic_load_explicit(&table->overflow_head, memory_order_acquire);
    while (node != NULL) {
        if (node->hash == hash && node->key == key &&
            atomic_load_explicit(&node->live, memory_order_acquire)) {
            atomic_store_explicit(&node->live, 0u, memory_order_release);
            atomic_fetch_sub_explicit(&table->overflow_size, 1, memory_order_relaxed);
            result = HT_OK;
            break;
        }
        node = node->next;
    }

    lf_overflow_unlock(table);
    return result;
}

static void
lf_writer_enter(lf_hopscotch_table *table)
{
    for (;;) {
        /* Writers announce themselves only after observing that resize is not
         * requested, then recheck to close the race with a new resizer. */
        while (atomic_load_explicit(&table->resize_requested, memory_order_acquire)) {
            lf_cpu_relax();
        }

        atomic_fetch_add_explicit(&table->active_writers, 1, memory_order_acq_rel);
        if (!atomic_load_explicit(&table->resize_requested, memory_order_acquire)) {
            return;
        }
        atomic_fetch_sub_explicit(&table->active_writers, 1, memory_order_acq_rel);
    }
}

static void
lf_writer_leave(lf_hopscotch_table *table)
{
    atomic_fetch_sub_explicit(&table->active_writers, 1, memory_order_acq_rel);
}

static int
lf_resize_begin(lf_hopscotch_table *table, int caller_is_active_writer)
{
    if (caller_is_active_writer) {
        if (atomic_flag_test_and_set_explicit(&table->resize_lock, memory_order_acquire)) {
            return 0;
        }
    } else {
        while (atomic_flag_test_and_set_explicit(&table->resize_lock, memory_order_acquire)) {
            lf_cpu_relax();
        }
    }

    atomic_store_explicit(&table->resize_requested, 1u, memory_order_release);

    for (;;) {
        size_t active = atomic_load_explicit(&table->active_writers, memory_order_acquire);
        /* If the resizing caller is already inside the writer gate, it is the
         * one active writer allowed to remain while the new core is built. */
        if (active == (caller_is_active_writer ? 1u : 0u)) {
            return 1;
        }
        lf_cpu_relax();
    }
}

static void
lf_resize_end(lf_hopscotch_table *table)
{
    atomic_store_explicit(&table->resize_requested, 0u, memory_order_release);
    atomic_flag_clear_explicit(&table->resize_lock, memory_order_release);
}

static size_t
lf_total_size(const lf_hopscotch_table *table, const lf_hopscotch_core *core)
{
    size_t primary_size = 0;

    if (core != NULL) {
        primary_size = atomic_load_explicit(&core->size, memory_order_relaxed);
    }
    return primary_size + atomic_load_explicit(&table->overflow_size, memory_order_relaxed);
}

static size_t
lf_capacity_for_live_count(const lf_hopscotch_table *table, size_t live_count)
{
    double max_lf;
    double needed_d;
    size_t needed;
    size_t capacity;

    max_lf = table->max_load_factor > 0.0 ? table->max_load_factor : 0.90;
    if (max_lf <= 0.0 || max_lf > 0.99) {
        max_lf = 0.90;
    }

    needed_d = (double)(live_count + 1u) / max_lf;
    needed = (size_t)needed_d;
    if ((double)needed < needed_d) {
        needed++;
    }
    needed = LF_MAX(needed, table->min_capacity);
    needed = LF_MAX(needed, (size_t)LF_HOPSCOTCH_HOP_RANGE);
    capacity = lf_next_pow2(needed);
    return capacity;
}

static int
lf_should_grow_for_insert(const lf_hopscotch_table *table, const lf_hopscotch_core *core)
{
    size_t live;

    if (table->resize_mode == HT_RESIZE_NONE || core == NULL) {
        return 0;
    }

    live = lf_total_size(table, core);
    return (double)(live + 1u) > (double)core->capacity * table->max_load_factor;
}

static ht_result
lf_resize_to_capacity(
    lf_hopscotch_table *table,
    size_t requested_capacity,
    int caller_is_active_writer
) {
    lf_hopscotch_core *old_core;
    lf_hopscotch_core *new_core;
    size_t target;
    size_t live;
    uint64_t moved = 0;

    if (!lf_resize_begin(table, caller_is_active_writer)) {
        return HT_ERR;
    }

    /* Resize runs with writers drained. Readers can still use old_core because
     * retired cores are kept alive until table destruction. */
    old_core = atomic_load_explicit(&table->current, memory_order_acquire);
    if (old_core == NULL) {
        lf_resize_end(table);
        return HT_ERR_INVALID;
    }

    live = lf_total_size(table, old_core);
    target = LF_MAX(requested_capacity, lf_capacity_for_live_count(table, live));
    target = lf_next_pow2(target);
    if (target == 0) {
        lf_resize_end(table);
        return HT_ERR_OOM;
    }

    for (;;) {
        ht_result copy_result = HT_OK;

        new_core = lf_core_create(target);
        if (new_core == NULL) {
            lf_resize_end(table);
            return HT_ERR_OOM;
        }

        for (size_t i = 0; i < old_core->capacity; i++) {
            lf_hopscotch_bucket *bucket = &old_core->buckets[i];
            uint64_t state_word;
            uint64_t hash;
            ht_key_t key;
            ht_val_t value;

            state_word = atomic_load_explicit(&bucket->state, memory_order_acquire);
            if (LF_STATE(state_word) != LF_HOPSCOTCH_MEMBER) {
                continue;
            }

            hash = atomic_load_explicit(&bucket->hash, memory_order_acquire);
            key = atomic_load_explicit(&bucket->key, memory_order_acquire);
            value = atomic_load_explicit(&bucket->value, memory_order_acquire);

            copy_result = lf_core_insert_known_hash(new_core, hash, key, value, NULL);
            if (copy_result == HT_OK) {
                moved++;
                continue;
            }
            if (copy_result == HT_ERR_EXISTS) {
                continue;
            }
            break;
        }

        if (copy_result == HT_OK || copy_result == HT_ERR_EXISTS) {
            break;
        }

        lf_core_destroy(new_core);
        if (target > ((size_t)-1 / 2u)) {
            lf_resize_end(table);
            return HT_ERR_OOM;
        }
        target <<= 1;
    }

    /* Publishing the new generation is one release-store; old readers keep a
     * valid old_core pointer through the retired list. */
    atomic_store_explicit(&table->current, new_core, memory_order_release);
    old_core->retired_next = table->retired_cores;
    table->retired_cores = old_core;

#if HT_ENABLE_RESIZE_INSTRUMENTATION
    atomic_fetch_add_explicit(&table->resize_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&table->rehash_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&table->resize_entries_moved, moved, memory_order_relaxed);
    if (new_core->capacity > old_core->capacity) {
        atomic_fetch_add_explicit(&table->grow_count, 1, memory_order_relaxed);
    } else if (new_core->capacity < old_core->capacity) {
        atomic_fetch_add_explicit(&table->shrink_count, 1, memory_order_relaxed);
    }
#else
    (void)moved;
#endif

    lf_resize_end(table);
    return HT_OK;
}

static ht_result
lf_hopscotch_insert_impl(void *ctx, ht_key_t key, ht_val_t value)
{
    lf_hopscotch_table *table = ctx;
    uint64_t hash;
    ht_result result;

    if (table == NULL) {
        return HT_ERR_INVALID;
    }

    if (table->collect_stats) {
        atomic_fetch_add_explicit(&table->inserts, 1, memory_order_relaxed);
    }

    hash = table->hash_fn(key, table->hash_seed);

    for (;;) {
        lf_hopscotch_core *core;
        uint64_t probes = 0;

        /* Writer gate prevents resize from publishing a different generation
         * while this insert mutates the loaded core. */
        lf_writer_enter(table);
        core = atomic_load_explicit(&table->current, memory_order_acquire);
        if (core == NULL) {
            lf_writer_leave(table);
            return HT_ERR_INVALID;
        }

        if (lf_core_find_member(core, key, hash, NULL, NULL, &probes)) {
            lf_note_probe(table, probes);
            lf_writer_leave(table);
            if (table->collect_stats) {
                atomic_fetch_add_explicit(&table->insert_failures, 1, memory_order_relaxed);
            }
            return HT_ERR_EXISTS;
        }
        if (atomic_load_explicit(&table->overflow_head, memory_order_acquire) != NULL &&
            lf_overflow_get(table, hash, key, NULL)) {
            lf_note_probe(table, probes);
            lf_writer_leave(table);
            if (table->collect_stats) {
                atomic_fetch_add_explicit(&table->insert_failures, 1, memory_order_relaxed);
            }
            return HT_ERR_EXISTS;
        }

        if (lf_should_grow_for_insert(table, core)) {
            size_t requested = core->capacity > ((size_t)-1 / 2u)
                                 ? core->capacity
                                 : core->capacity << 1;
            result = lf_resize_to_capacity(table, requested, 1);
            lf_writer_leave(table);
            if (result == HT_OK || result == HT_ERR) {
                continue;
            }
            if (table->collect_stats) {
                atomic_fetch_add_explicit(&table->insert_failures, 1, memory_order_relaxed);
            }
            return result;
        }

        result = lf_core_insert_known_hash(core, hash, key, value, &probes);
        lf_note_probe(table, probes);

        if (result == HT_OK || result == HT_ERR_EXISTS) {
            lf_writer_leave(table);
            if (result != HT_OK && table->collect_stats) {
                atomic_fetch_add_explicit(&table->insert_failures, 1, memory_order_relaxed);
            }
            return result;
        }

        if (result == HT_ERR_FULL && table->resize_mode != HT_RESIZE_NONE) {
            size_t requested = core->capacity > ((size_t)-1 / 2u)
                                 ? core->capacity
                                 : core->capacity << 1;
            result = lf_resize_to_capacity(table, requested, 1);
            lf_writer_leave(table);
            if (result == HT_OK || result == HT_ERR) {
                continue;
            }
            if (table->collect_stats) {
                atomic_fetch_add_explicit(&table->insert_failures, 1, memory_order_relaxed);
            }
            return result;
        }

        if (result == HT_ERR_FULL && table->resize_mode == HT_RESIZE_NONE) {
            result = lf_overflow_insert(table, hash, key, value);
            lf_writer_leave(table);
            if (result != HT_OK && table->collect_stats) {
                atomic_fetch_add_explicit(&table->insert_failures, 1, memory_order_relaxed);
            }
            return result;
        }

        lf_writer_leave(table);
        if (table->collect_stats) {
            atomic_fetch_add_explicit(&table->insert_failures, 1, memory_order_relaxed);
        }
        return result;
    }
}

static ht_result
lf_hopscotch_get_impl(const void *ctx, ht_key_t key, ht_val_t *value_out)
{
    lf_hopscotch_table *table = (lf_hopscotch_table *)ctx;
    lf_hopscotch_core *core;
    uint64_t hash;
    uint64_t probes = 0;

    if (table == NULL || value_out == NULL) {
        return HT_ERR_INVALID;
    }

    if (table->collect_stats) {
        atomic_fetch_add_explicit(&table->lookups, 1, memory_order_relaxed);
    }

    hash = table->hash_fn(key, table->hash_seed);
    /* Lookups do not enter the writer gate; old generations remain allocated
     * so a concurrent resize cannot invalidate this acquired pointer. */
    core = atomic_load_explicit(&table->current, memory_order_acquire);

    if (lf_core_find_member(core, key, hash, value_out, NULL, &probes)) {
        lf_note_probe(table, probes);
        return HT_OK;
    }

    if (atomic_load_explicit(&table->overflow_head, memory_order_acquire) != NULL &&
        lf_overflow_get(table, hash, key, value_out)) {
        lf_note_probe(table, probes);
        return HT_OK;
    }

    lf_note_probe(table, probes);
    if (table->collect_stats) {
        atomic_fetch_add_explicit(&table->lookup_misses, 1, memory_order_relaxed);
    }
    return HT_ERR_NOT_FOUND;
}

static ht_result
lf_hopscotch_remove_impl(void *ctx, ht_key_t key)
{
    lf_hopscotch_table *table = ctx;
    lf_hopscotch_core *core;
    uint64_t hash;
    uint64_t probes = 0;
    ht_result result;

    if (table == NULL) {
        return HT_ERR_INVALID;
    }

    if (table->collect_stats) {
        atomic_fetch_add_explicit(&table->removes, 1, memory_order_relaxed);
    }

    hash = table->hash_fn(key, table->hash_seed);
    /* Remove mutates primary buckets or the overflow stash, so it uses the
     * writer gate even though the lookup portion is atomic. */
    lf_writer_enter(table);

    core = atomic_load_explicit(&table->current, memory_order_acquire);
    result = lf_core_remove_known_hash(core, hash, key, &probes);
    if (result == HT_OK) {
        lf_note_probe(table, probes);
        lf_writer_leave(table);
        return HT_OK;
    }

    if (atomic_load_explicit(&table->overflow_head, memory_order_acquire) != NULL) {
        result = lf_overflow_remove(table, hash, key);
        if (result == HT_OK) {
            lf_note_probe(table, probes);
            lf_writer_leave(table);
            return HT_OK;
        }
    }

    lf_note_probe(table, probes);
    lf_writer_leave(table);
    if (table->collect_stats) {
        atomic_fetch_add_explicit(&table->remove_misses, 1, memory_order_relaxed);
    }
    return HT_ERR_NOT_FOUND;
}

static void
lf_hopscotch_destroy_impl(void *ctx)
{
    lf_hopscotch_table *table = ctx;
    lf_hopscotch_core *core;
    lf_hopscotch_core *retired;
    lf_hopscotch_overflow_node *node;

    if (table == NULL) {
        return;
    }

    core = atomic_load_explicit(&table->current, memory_order_acquire);
    lf_core_destroy(core);

    retired = table->retired_cores;
    while (retired != NULL) {
        lf_hopscotch_core *next = retired->retired_next;
        lf_core_destroy(retired);
        retired = next;
    }

    node = atomic_load_explicit(&table->overflow_head, memory_order_acquire);
    while (node != NULL) {
        lf_hopscotch_overflow_node *next = node->next;
        free(node);
        node = next;
    }

    free(table);
}

static size_t
lf_hopscotch_size_impl(const void *ctx)
{
    const lf_hopscotch_table *table = ctx;
    lf_hopscotch_core *core;

    if (table == NULL) {
        return 0;
    }

    core = atomic_load_explicit(&table->current, memory_order_acquire);
    return lf_total_size(table, core);
}

static size_t
lf_hopscotch_capacity_impl(const void *ctx)
{
    const lf_hopscotch_table *table = ctx;
    lf_hopscotch_core *core;

    if (table == NULL) {
        return 0;
    }
    core = atomic_load_explicit(&table->current, memory_order_acquire);
    return core == NULL ? 0 : core->capacity;
}

static double
lf_hopscotch_load_factor_impl(const void *ctx)
{
    const lf_hopscotch_table *table = ctx;
    lf_hopscotch_core *core;

    if (table == NULL) {
        return 0.0;
    }
    core = atomic_load_explicit(&table->current, memory_order_acquire);
    if (core == NULL || core->capacity == 0) {
        return 0.0;
    }
    return (double)lf_total_size(table, core) / (double)core->capacity;
}

static ht_result
lf_hopscotch_reserve_impl(void *ctx, size_t capacity)
{
    lf_hopscotch_table *table = ctx;
    lf_hopscotch_core *core;

    if (table == NULL) {
        return HT_ERR_INVALID;
    }

    core = atomic_load_explicit(&table->current, memory_order_acquire);
    if (core != NULL && capacity <= core->capacity) {
        return HT_OK;
    }
    return lf_resize_to_capacity(table, capacity, 0);
}

static ht_result
lf_hopscotch_rehash_impl(void *ctx, size_t capacity)
{
    lf_hopscotch_table *table = ctx;
    lf_hopscotch_core *core;
    size_t live;
    size_t target;

    if (table == NULL) {
        return HT_ERR_INVALID;
    }

    core = atomic_load_explicit(&table->current, memory_order_acquire);
    live = lf_total_size(table, core);
    target = LF_MAX(capacity, lf_capacity_for_live_count(table, live));
    return lf_resize_to_capacity(table, target, 0);
}

static size_t
lf_retired_bytes(const lf_hopscotch_table *table)
{
    const lf_hopscotch_core *core = table->retired_cores;
    size_t bytes = 0;

    while (core != NULL) {
        bytes += sizeof(*core) + core->capacity * sizeof(*core->buckets);
        core = core->retired_next;
    }
    return bytes;
}

static ht_result
lf_hopscotch_get_stats_impl(const void *ctx, ht_stats *out)
{
    const lf_hopscotch_table *table = ctx;
    lf_hopscotch_core *core;
    size_t bytes;

    if (table == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    memset(out, 0, sizeof(*out));
    out->inserts = atomic_load_explicit(&table->inserts, memory_order_relaxed);
    out->insert_failures = atomic_load_explicit(&table->insert_failures, memory_order_relaxed);
    out->lookups = atomic_load_explicit(&table->lookups, memory_order_relaxed);
    out->lookup_misses = atomic_load_explicit(&table->lookup_misses, memory_order_relaxed);
    out->removes = atomic_load_explicit(&table->removes, memory_order_relaxed);
    out->remove_misses = atomic_load_explicit(&table->remove_misses, memory_order_relaxed);
    out->probes = atomic_load_explicit(&table->probes, memory_order_relaxed);
    out->max_probe_len = atomic_load_explicit(&table->max_probe_len, memory_order_relaxed);

#if HT_ENABLE_RESIZE_INSTRUMENTATION
    out->resize_count = atomic_load_explicit(&table->resize_count, memory_order_relaxed);
    out->grow_count = atomic_load_explicit(&table->grow_count, memory_order_relaxed);
    out->shrink_count = atomic_load_explicit(&table->shrink_count, memory_order_relaxed);
    out->rehash_count = atomic_load_explicit(&table->rehash_count, memory_order_relaxed);
    out->resize_entries_moved = atomic_load_explicit(
        &table->resize_entries_moved,
        memory_order_relaxed
    );
#endif

    core = atomic_load_explicit(&table->current, memory_order_acquire);
    bytes = sizeof(*table);
    if (core != NULL) {
        bytes += sizeof(*core) + core->capacity * sizeof(*core->buckets);
    }
    bytes += lf_retired_bytes(table);
    bytes += atomic_load_explicit(&table->overflow_allocated, memory_order_relaxed) *
             sizeof(lf_hopscotch_overflow_node);
    out->bytes_used = bytes;
    return HT_OK;
}

static ht_result
lf_hopscotch_reset_stats_impl(void *ctx)
{
    lf_hopscotch_table *table = ctx;

    if (table == NULL) {
        return HT_ERR_INVALID;
    }

    atomic_store_explicit(&table->inserts, 0, memory_order_relaxed);
    atomic_store_explicit(&table->insert_failures, 0, memory_order_relaxed);
    atomic_store_explicit(&table->lookups, 0, memory_order_relaxed);
    atomic_store_explicit(&table->lookup_misses, 0, memory_order_relaxed);
    atomic_store_explicit(&table->removes, 0, memory_order_relaxed);
    atomic_store_explicit(&table->remove_misses, 0, memory_order_relaxed);
    atomic_store_explicit(&table->probes, 0, memory_order_relaxed);
    atomic_store_explicit(&table->max_probe_len, 0, memory_order_relaxed);

#if HT_ENABLE_RESIZE_INSTRUMENTATION
    atomic_store_explicit(&table->resize_count, 0, memory_order_relaxed);
    atomic_store_explicit(&table->grow_count, 0, memory_order_relaxed);
    atomic_store_explicit(&table->shrink_count, 0, memory_order_relaxed);
    atomic_store_explicit(&table->rehash_count, 0, memory_order_relaxed);
    atomic_store_explicit(&table->resize_entries_moved, 0, memory_order_relaxed);
#endif

    return HT_OK;
}

static const struct ht_vtable LF_HOPSCOTCH_VTABLE = {
    .destroy = lf_hopscotch_destroy_impl,
    .insert = lf_hopscotch_insert_impl,
    .get = lf_hopscotch_get_impl,
    .remove = lf_hopscotch_remove_impl,
    .size = lf_hopscotch_size_impl,
    .capacity = lf_hopscotch_capacity_impl,
    .load_factor = lf_hopscotch_load_factor_impl,
    .reserve = lf_hopscotch_reserve_impl,
    .rehash = lf_hopscotch_rehash_impl,
    .get_stats = lf_hopscotch_get_stats_impl,
    .reset_stats = lf_hopscotch_reset_stats_impl,
    .bind_bench_iface = lf_hopscotch_bind_bench_iface
};

void *
lf_hopscotch_create_impl(const ht_config *cfg)
{
    lf_hopscotch_table *table;
    lf_hopscotch_core *core;
    size_t capacity;
    size_t min_capacity;

    if (cfg == NULL) {
        return NULL;
    }

    capacity = cfg->init_capacity != 0 ? cfg->init_capacity : LF_HOPSCOTCH_MIN_CAPACITY;
    min_capacity = cfg->min_capacity != 0 ? cfg->min_capacity : LF_HOPSCOTCH_MIN_CAPACITY;
    capacity = lf_next_pow2(capacity);
    min_capacity = lf_next_pow2(min_capacity);
    if (capacity == 0 || min_capacity == 0) {
        return NULL;
    }
    if (capacity < min_capacity) {
        capacity = min_capacity;
    }

    table = calloc(1, sizeof(*table));
    if (table == NULL) {
        return NULL;
    }

    table->min_capacity = LF_MAX(min_capacity, (size_t)LF_HOPSCOTCH_MIN_CAPACITY);
    table->max_load_factor = cfg->max_load_factor > 0.0 ? cfg->max_load_factor : 0.90;
    if (table->max_load_factor <= 0.0 || table->max_load_factor > 0.99) {
        table->max_load_factor = 0.90;
    }
    table->min_load_factor = cfg->min_load_factor;
    table->resize_mode = cfg->rsz_mode;
    table->hash_fn = cfg->hash_fn != NULL ? cfg->hash_fn : lf_default_hash;
    table->hash_seed = cfg->hash_seed;
    table->collect_stats = cfg->collect_stats;

    atomic_init(&table->current, NULL);
    atomic_init(&table->overflow_head, NULL);
    atomic_init(&table->overflow_size, 0);
    atomic_init(&table->overflow_allocated, 0);
    atomic_init(&table->resize_requested, 0u);
    atomic_init(&table->active_writers, 0);
    atomic_flag_clear(&table->resize_lock);
    atomic_flag_clear(&table->overflow_lock);

    core = lf_core_create(capacity);
    if (core == NULL) {
        free(table);
        return NULL;
    }
    atomic_store_explicit(&table->current, core, memory_order_release);

    if (!atomic_is_lock_free(&core->buckets[0].state) ||
        !atomic_is_lock_free(&core->buckets[0].hop_info) ||
        !atomic_is_lock_free(&core->buckets[0].reloc_counter) ||
        !atomic_is_lock_free(&core->buckets[0].hash) ||
        !atomic_is_lock_free(&core->buckets[0].key) ||
        !atomic_is_lock_free(&core->buckets[0].value) ||
        !atomic_is_lock_free(&core->size)) {
        lf_hopscotch_destroy_impl(table);
        return NULL;
    }

    return table;
}

const struct ht_vtable *
lf_hopscotch_vtable(void)
{
    return &LF_HOPSCOTCH_VTABLE;
}

int
lf_hopscotch_bind_bench_iface(void *ctx, bench_iface *out)
{
    if (ctx == NULL || out == NULL) {
        return HT_ERR_INVALID;
    }

    out->ctx = ctx;
    out->insert = lf_hopscotch_insert_impl;
    out->get = lf_hopscotch_get_impl;
    out->remove = lf_hopscotch_remove_impl;
    return HT_OK;
}
