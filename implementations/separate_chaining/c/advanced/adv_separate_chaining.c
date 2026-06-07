/**
 * @file    adv_separate_chaining.c
 * @brief   High-performance separate-chaining hashtable with bitmask tag scanning.
 *
 * @author  J.W. Moolman
 * @date    2026-05-08
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend_util.h"
#include "adv_separate_chaining.h"
#include "ht_internal.h"
#include "slab_pool.h"

/* --- compiler hints ------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif

/* --- internal bit-twiddling ----------------------------------------------- */

/**
 * @brief Broadcast a byte to all bytes of a 64-bit word.
 */
static inline uint64_t broadcast_byte(
    uint8_t b
) {
    return (uint64_t)b * 0x0101010101010101ULL;
}

/**
 * @brief Find bytes in 'tags' that match 'tag'.
 * Returns a mask where the high bit of each matching byte is set.
 */
static inline uint64_t match_tags_64(
    uint64_t tags,
    uint8_t tag
) {
    uint64_t diff = tags ^ broadcast_byte(tag);
    /* Standard bit-twiddling hack to find zero bytes */
    return ((diff - 0x0101010101010101ULL) & ~diff & 0x8080808080808080ULL);
}

static inline uint64_t live_tag_mask(
    uint8_t used
) {
    uint64_t mask = 0;

    for (uint8_t i = 0; i < used; i++) {
        mask |= 0x80ULL << (i * 8u);
    }
    return mask;
}

static inline uint64_t segment_tag_word(
    const adv_segment *seg
) {
    uint64_t word = 0;

    memcpy(&word, seg->tags, sizeof(seg->tags));
    return word;
}

/* --- function prototypes -------------------------------------------------- */

static void adv_separate_chaining_destroy_impl(void *impl);
static ht_result adv_separate_chaining_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
);
static ht_result adv_separate_chaining_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
);
static ht_result adv_separate_chaining_remove_impl(void *impl, ht_key_t key);
static size_t adv_separate_chaining_size_impl(const void *impl);
static size_t adv_separate_chaining_capacity_impl(const void *impl);
static double adv_separate_chaining_load_factor_impl(const void *impl);
static ht_result adv_separate_chaining_reserve_impl(void *impl, size_t capacity);
static ht_result adv_separate_chaining_rehash_impl(void *impl, size_t capacity);
static ht_result adv_separate_chaining_get_stats_impl(const void *impl, ht_stats *out);
static ht_result adv_separate_chaining_reset_stats_impl(void *impl);
static void adv_update_bytes_used(adv_separate_chaining_table *t);
static ht_result adv_insert_rehashed(
    adv_separate_chaining_table *t,
    ht_key_t key,
    ht_val_t value
);
static ht_result adv_resize(adv_separate_chaining_table *t, size_t new_capacity);

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable ADV_SEPARATE_CHAINING_VTABLE = {
    .destroy = adv_separate_chaining_destroy_impl,
    .insert = adv_separate_chaining_insert_impl,
    .get = adv_separate_chaining_get_impl,
    .remove = adv_separate_chaining_remove_impl,
    .size = adv_separate_chaining_size_impl,
    .capacity = adv_separate_chaining_capacity_impl,
    .load_factor = adv_separate_chaining_load_factor_impl,
    .reserve = adv_separate_chaining_reserve_impl,
    .rehash = adv_separate_chaining_rehash_impl,
    .get_stats = adv_separate_chaining_get_stats_impl,
    .reset_stats = adv_separate_chaining_reset_stats_impl,
    .bind_bench_iface = adv_separate_chaining_bind_bench_iface
};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

void *adv_separate_chaining_create_impl(
    const ht_config *cfg
) {
    adv_separate_chaining_table *t;
    size_t init_capacity;

    if (cfg == NULL) { return NULL; }

    t = calloc(1, sizeof(*t));
    if (t == NULL) { return NULL; }

    init_capacity = (cfg->init_capacity > 0)
        ? cfg->init_capacity
        : ADV_SEPARATE_CHAINING_DEFAULT_INITIAL_CAPACITY
    ;
    init_capacity = next_pow2(init_capacity);

    t->buckets = calloc(init_capacity, sizeof(*t->buckets));
    if (t->buckets == NULL) {
        free(t);
        return NULL;
    }

    if (slab_pool_init(
            &t->pool,
            sizeof(adv_segment),
            SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK
        ) != 0) {
        free(t->buckets);
        free(t);
        return NULL;
    }

    t->capacity     = init_capacity;
    t->min_capacity = (cfg->min_capacity > 0)
        ? cfg->min_capacity
        : ADV_SEPARATE_CHAINING_DEFAULT_MIN_CAPACITY
    ;
    t->size         = 0;
    
    t->max_load_factor = (cfg->max_load_factor > 0.0)
        ? cfg->max_load_factor
        : ADV_SEPARATE_CHAINING_DEFAULT_MAX_LOAD
    ;
    t->min_load_factor = (cfg->min_load_factor > 0.0)
        ? cfg->min_load_factor
        : ADV_SEPARATE_CHAINING_DEFAULT_MIN_LOAD
    ;
    t->resize_mode   = cfg->rsz_mode;
    t->hash_fn       = (cfg->hash_fn != NULL)
        ? cfg->hash_fn
        : default_hash
    ;
    t->hash_seed     = cfg->hash_seed;
    t->collect_stats = cfg->collect_stats;

    adv_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    return t;
}

const struct ht_vtable *adv_separate_chaining_vtable(
    void
) {
    return &ADV_SEPARATE_CHAINING_VTABLE;
}

int adv_separate_chaining_bind_bench_iface(
    void *ctx,
    bench_iface *out
) {
    if (ctx == NULL || out == NULL) { return HT_ERR_INVALID; }

    out->ctx = ctx;
    out->insert = adv_separate_chaining_insert_impl;
    out->get = adv_separate_chaining_get_impl;
    out->remove = adv_separate_chaining_remove_impl;
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void adv_separate_chaining_destroy_impl(
    void *impl
) {
    adv_separate_chaining_table *t = impl;

    if (t == NULL) { return; }

    slab_pool_destroy(&t->pool);
    free(t->buckets);
    free(t);
}

static ht_result adv_separate_chaining_insert_impl(
    void *impl,
    ht_key_t key,
    ht_val_t value
) {
    adv_separate_chaining_table *t = impl;
    if (UNLIKELY(t == NULL)) { return HT_ERR_INVALID; }
    HT_STATS_INC(t, inserts);

    const uint64_t hash = t->hash_fn(key, t->hash_seed);
    size_t bucket_idx = HT_INDEX_FOR_U64(hash, t->capacity);
    const uint8_t tag = ht_hash_tag_u8(hash);
    uint64_t probe_len = 1;

    adv_bucket *b = &t->buckets[bucket_idx];
    
    /* 1. Fast path: check main bucket (unrolled) */
    if (b->used > 0) {
        if (b->tags[0] == tag && b->keys[0] == key) { goto exists; }
        probe_len++;
        if (b->used > 1) {
            if (b->tags[1] == tag && b->keys[1] == key) { goto exists; }
            probe_len++;
            if (b->used > 2) {
                if (b->tags[2] == tag && b->keys[2] == key) { goto exists; }
                probe_len++;
            }
        }
    }

    /* 2. Check overflow segments with bitmasking */
    adv_segment *seg = b->next;
    while (seg != NULL) {
        uint64_t tag_word = segment_tag_word(seg);
        uint64_t matches = match_tags_64(tag_word, tag) & live_tag_mask(seg->used);
        
        while (matches) {
            int i = __builtin_ctzll(matches) >> 3;
            if (seg->keys[i] == key) {
                probe_len += i;
                goto exists;
            }
            matches &= (matches - 1);
        }
        probe_len += seg->used;
        seg = seg->next;
    }

    if (t->resize_mode != HT_RESIZE_NONE && HT_SHOULD_GROW_COUNT(t, size)) {
        ht_result rc = adv_resize(t, t->capacity * 2);
        if (rc != HT_OK) {
            HT_RECORD_INSERT_FAILURE(t);
            return rc;
        }
        bucket_idx = HT_INDEX_FOR_U64(hash, t->capacity);
        b = &t->buckets[bucket_idx];
        probe_len = 1;
        probe_len += b->used;
        for (seg = b->next; seg != NULL; seg = seg->next) {
            probe_len += seg->used;
        }
    } else if (
        t->resize_mode == HT_RESIZE_NONE &&
        UNLIKELY(
            (double)(t->size + 1) >
            (double)t->capacity * t->max_load_factor
        )
    ) {
        HT_UPDATE_PROBE_STATS(t, probe_len);
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_FULL;
    }

    /* 3. Insert into main bucket if space exists */
    if (LIKELY(b->used < ADV_BUCKET_CAPACITY)) {
        uint8_t pos = b->used++;
        b->keys[pos] = key;
        b->values[pos] = value;
        b->tags[pos] = tag;
    } else {
        /* 4. Insert into first overflow segment with space */
        seg = b->next;
        if (seg == NULL || seg->used == ADV_SEGMENT_CAPACITY) {
            adv_segment *new_seg = slab_pool_alloc(&t->pool);
            if (new_seg == NULL) {
                HT_RECORD_INSERT_FAILURE(t);
                return HT_ERR_OOM;
            }
            new_seg->next = b->next;
            new_seg->used = 0;
            b->next = new_seg;
            seg = new_seg;
        }

        uint8_t pos = seg->used++;
        seg->keys[pos] = key;
        seg->values[pos] = value;
        seg->tags[pos] = tag;
    }

    t->size++;
    HT_UPDATE_PROBE_STATS(t, probe_len);
    adv_update_bytes_used(t);
    return HT_OK;

exists:
    HT_UPDATE_PROBE_STATS(t, probe_len);
    HT_RECORD_INSERT_FAILURE(t);
    return HT_ERR_EXISTS;
}

static ht_result adv_separate_chaining_get_impl(
    const void *impl,
    ht_key_t key,
    ht_val_t *value_out
) {
    const adv_separate_chaining_table *t = impl;
    if (UNLIKELY(t == NULL || value_out == NULL)) { return HT_ERR_INVALID; }
    if (t->collect_stats) { ((adv_separate_chaining_table *)t)->stats.lookups++; }

    const uint64_t hash = t->hash_fn(key, t->hash_seed);
    const size_t bucket_idx = HT_INDEX_FOR_U64(hash, t->capacity);
    const uint8_t tag = ht_hash_tag_u8(hash);
    uint64_t probe_len = 1;

    adv_bucket *const b = &((adv_separate_chaining_table *)t)->buckets[bucket_idx];
    
    /* 1. Fast path: check main bucket (unrolled) */
    if (b->used > 0) {
        if (b->tags[0] == tag && b->keys[0] == key) {
            *value_out = b->values[0];
            HT_UPDATE_PROBE_STATS((adv_separate_chaining_table *)t, probe_len);
            return HT_OK;
        }
        probe_len++;
        if (b->used > 1) {
            if (b->tags[1] == tag && b->keys[1] == key) {
                *value_out = b->values[1];
                HT_UPDATE_PROBE_STATS((adv_separate_chaining_table *)t, probe_len);
                return HT_OK;
            }
            probe_len++;
            if (b->used > 2) {
                if (b->tags[2] == tag && b->keys[2] == key) {
                    *value_out = b->values[2];
                    HT_UPDATE_PROBE_STATS((adv_separate_chaining_table *)t, probe_len);
                    return HT_OK;
                }
                probe_len++;
            }
        }
    }

    /* 2. Check overflow segments with bitmasking and move-to-front promotion. */
    adv_segment *seg = b->next;
    while (seg != NULL) {
        uint64_t tag_word = segment_tag_word(seg);
        uint64_t matches = match_tags_64(tag_word, tag) & live_tag_mask(seg->used);
        
        while (matches) {
            int i = __builtin_ctzll(matches) >> 3;
            if (seg->keys[i] == key) {
                *value_out = seg->values[i];
                HT_UPDATE_PROBE_STATS(
                    (adv_separate_chaining_table *)t,
                    probe_len + i
                );

                /* Promote an overflow hit into the hot first bucket slot. */
                if (LIKELY(b->used > 0)) {
                    ht_key_t tmp_k = b->keys[0];
                    ht_val_t tmp_v = b->values[0];
                    uint8_t  tmp_t = b->tags[0];

                    b->keys[0] = seg->keys[i];
                    b->values[0] = seg->values[i];
                    b->tags[0] = seg->tags[i];

                    seg->keys[i] = tmp_k;
                    seg->values[i] = tmp_v;
                    seg->tags[i] = tmp_t;
                }
                return HT_OK;
            }
            matches &= (matches - 1);
        }
        probe_len += seg->used;
        seg = seg->next;
    }

    if (t->collect_stats) {
        ((adv_separate_chaining_table *)t)->stats.lookup_misses++;
        HT_UPDATE_PROBE_STATS((adv_separate_chaining_table *)t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
}

static ht_result adv_separate_chaining_remove_impl(
    void *impl,
    ht_key_t key
) {
    adv_separate_chaining_table *t = impl;
    uint64_t hash, probe_len = 1;
    size_t bucket_idx;
    uint8_t tag;

    if (UNLIKELY(t == NULL)) { return HT_ERR_INVALID; }
    if (t->collect_stats) { t->stats.removes++; }

    hash = t->hash_fn(key, t->hash_seed);
    bucket_idx = HT_INDEX_FOR_U64(hash, t->capacity);
    tag = ht_hash_tag_u8(hash);

    adv_bucket *b = &t->buckets[bucket_idx];
    
    /* 1. Check main bucket */
    for (uint8_t i = 0; i < b->used; i++) {
        if (b->tags[i] == tag && b->keys[i] == key) {
            HT_UPDATE_PROBE_STATS(t, probe_len);
            
            if (b->next != NULL) {
                adv_segment *last_seg = b->next;
                adv_segment *prev_seg = NULL;
                while (last_seg->next != NULL) {
                    prev_seg = last_seg;
                    last_seg = last_seg->next;
                }
                
                uint8_t last_idx = last_seg->used - 1;
                b->tags[i] = last_seg->tags[last_idx];
                b->keys[i] = last_seg->keys[last_idx];
                b->values[i] = last_seg->values[last_idx];
                last_seg->used--;
                
                if (last_seg->used == 0) {
                    if (prev_seg == NULL) {
                        b->next = NULL;
                    } else {
                        prev_seg->next = NULL;
                    }
                    slab_pool_free(&t->pool, last_seg);
                }
            } else {
                uint8_t last_idx = b->used - 1;
                b->tags[i] = b->tags[last_idx];
                b->keys[i] = b->keys[last_idx];
                b->values[i] = b->values[last_idx];
                b->used--;
            }

            t->size--;
            adv_update_bytes_used(t);
            goto check_shrink;
        }
        probe_len++;
    }

    /* 2. Check overflow segments */
    adv_segment *seg = b->next;
    adv_segment *prev = NULL;
    while (seg != NULL) {
        uint64_t tag_word = segment_tag_word(seg);
        uint64_t matches = match_tags_64(tag_word, tag) & live_tag_mask(seg->used);
        
        while (matches) {
            int i = __builtin_ctzll(matches) >> 3;
            if (seg->keys[i] == key) {
                HT_UPDATE_PROBE_STATS(t, probe_len + i);
                
                uint8_t last_idx = seg->used - 1;
                seg->tags[i] = seg->tags[last_idx];
                seg->keys[i] = seg->keys[last_idx];
                seg->values[i] = seg->values[last_idx];
                seg->used--;

                if (seg->used == 0) {
                    if (prev == NULL) {
                        b->next = seg->next;
                    } else {
                        prev->next = seg->next;
                    }
                    slab_pool_free(&t->pool, seg);
                }
                
                t->size--;
                adv_update_bytes_used(t);
                goto check_shrink;
            }
            matches &= (matches - 1);
        }
        probe_len += seg->used;
        prev = seg;
        seg = seg->next;
    }

    if (t->collect_stats) {
        t->stats.remove_misses++;
        HT_UPDATE_PROBE_STATS(t, probe_len);
    }
    return HT_ERR_NOT_FOUND;

check_shrink:
    if (t->resize_mode == HT_RESIZE_GROW_SHRINK && HT_SHOULD_SHRINK_COUNT(t, size)) {
        size_t new_cap = t->capacity / 2;
        if (new_cap < t->min_capacity) { new_cap = t->min_capacity; }
        return adv_resize(t, new_cap);
    }
    return HT_OK;
}

static size_t adv_separate_chaining_size_impl(
    const void *impl
) {
    const adv_separate_chaining_table *t = impl;

    return (t != NULL)
        ? t->size
        : 0
    ;
}

static size_t adv_separate_chaining_capacity_impl(
    const void *impl
) {
    const adv_separate_chaining_table *t = impl;

    return (t != NULL)
        ? t->capacity
        : 0
    ;
}

static double adv_separate_chaining_load_factor_impl(
    const void *impl
) {
    const adv_separate_chaining_table *t = impl;

    return (t != NULL)
        ? ht_load_factor_snapshot(t->size, t->capacity)
        : 0.0
    ;
}

static ht_result adv_separate_chaining_reserve_impl(
    void *impl,
    size_t capacity
) {
    adv_separate_chaining_table *t = impl;

    if (t == NULL) { return HT_ERR_INVALID; }
    if (capacity <= t->capacity) { return HT_OK; }
    return adv_resize(t, capacity);
}

static ht_result adv_separate_chaining_rehash_impl(
    void *impl,
    size_t capacity
) {
    adv_separate_chaining_table *t = impl;

    if (t == NULL) { return HT_ERR_INVALID; }
    return adv_resize(t, capacity);
}

static ht_result adv_separate_chaining_get_stats_impl(
    const void *impl,
    ht_stats *out
) {
    if (impl == NULL || out == NULL) { return HT_ERR_INVALID; }

    *out = ((const adv_separate_chaining_table *)impl)->stats;
    return HT_OK;
}

static ht_result adv_separate_chaining_reset_stats_impl(
    void *impl
) {
    adv_separate_chaining_table *t = impl;

    if (t == NULL) { return HT_ERR_INVALID; }

    memset(&t->stats, 0, sizeof(t->stats));
    adv_update_bytes_used(t);
    ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
    return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void adv_update_bytes_used(adv_separate_chaining_table *t) {
    size_t bytes;

    if (t != NULL && t->collect_stats) {
        bytes = sizeof(*t);
        bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->buckets));
        bytes = ht_bytes_add_or_max(bytes, slab_pool_bytes_owned(&t->pool));
        t->stats.bytes_used = bytes;
    }
}

static ht_result adv_insert_rehashed(
    adv_separate_chaining_table *t,
    ht_key_t key,
    ht_val_t value
) {
    adv_bucket *bucket;
    adv_segment *segment;
    uint64_t hash;
    size_t bucket_index;
    uint8_t tag;
    uint8_t pos;

    hash = t->hash_fn(key, t->hash_seed);
    bucket_index = HT_INDEX_FOR_U64(hash, t->capacity);
    tag = ht_hash_tag_u8(hash);
    bucket = &t->buckets[bucket_index];

    if (bucket->used < ADV_BUCKET_CAPACITY) {
        pos = bucket->used++;
        bucket->keys[pos] = key;
        bucket->values[pos] = value;
        bucket->tags[pos] = tag;
    } else {
        segment = bucket->next;
        if (segment == NULL || segment->used == ADV_SEGMENT_CAPACITY) {
            segment = slab_pool_alloc(&t->pool);
            if (segment == NULL) {
                return HT_ERR_OOM;
            }
            segment->next = bucket->next;
            segment->used = 0;
            bucket->next = segment;
        }

        pos = segment->used++;
        segment->keys[pos] = key;
        segment->values[pos] = value;
        segment->tags[pos] = tag;
    }

    t->size++;
    return HT_OK;
}

static ht_result adv_resize(adv_separate_chaining_table *t, size_t new_capacity) {
    adv_bucket *old_buckets;
    adv_bucket *new_buckets;
    slab_pool old_pool;
    slab_pool new_pool;
    size_t old_capacity;
    size_t old_size;
    uint64_t start_ns;
    ht_result rc = HT_OK;

    if (t == NULL) {
        return HT_ERR_INVALID;
    }

    new_capacity = next_pow2(
        (new_capacity < t->min_capacity)
            ? t->min_capacity
            : new_capacity
    );
    if (new_capacity == 0) {
        return HT_ERR_OOM;
    }
    if (new_capacity == t->capacity) {
        return HT_OK;
    }

    new_buckets = calloc(new_capacity, sizeof(*new_buckets));
    if (new_buckets == NULL) {
        return HT_ERR_OOM;
    }
    if (slab_pool_init(
            &new_pool,
            sizeof(adv_segment),
            SLAB_POOL_DEFAULT_OBJECTS_PER_BLOCK
        ) != 0) {
        free(new_buckets);
        return HT_ERR_OOM;
    }

    old_buckets = t->buckets;
    old_pool = t->pool;
    old_capacity = t->capacity;
    old_size = t->size;
    start_ns = ht_resize_instrumentation_start(t->collect_stats);

    /* Rebuild into separate storage first; OOM can discard it and keep old chains. */
    t->buckets = new_buckets;
    t->pool = new_pool;
    t->capacity = new_capacity;
    t->size = 0;

    for (size_t i = 0; i < old_capacity; i++) {
        adv_bucket *ob = &old_buckets[i];

        for (uint8_t j = 0; j < ob->used; j++) {
            rc = adv_insert_rehashed(t, ob->keys[j], ob->values[j]);
            if (rc != HT_OK) {
                goto rollback;
            }
        }

        adv_segment *seg = ob->next;
        while (seg != NULL) {
            for (uint8_t j = 0; j < seg->used; j++) {
                rc = adv_insert_rehashed(t, seg->keys[j], seg->values[j]);
                if (rc != HT_OK) {
                    goto rollback;
                }
            }
            seg = seg->next;
        }
    }

    slab_pool_destroy(&old_pool);
    free(old_buckets);
    
    ht_resize_stats_record(
        &t->stats,
        t->collect_stats,
        old_capacity,
        t->capacity,
        old_size,
        start_ns
    );
    adv_update_bytes_used(t);
    return HT_OK;

rollback:
    slab_pool_destroy(&t->pool);
    free(new_buckets);
    t->buckets = old_buckets;
    t->pool = old_pool;
    t->capacity = old_capacity;
    t->size = old_size;
    return rc;
}
