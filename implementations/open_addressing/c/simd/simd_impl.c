/**
 * @file    simd_impl.c
 * @brief   SIMD-accelerated open-addressing hashtable backend.
 *
 * Implements a variant that uses SSE2 intrinsics to scan metadata tags
 * in batches of 16, significantly accelerating lookups in dense tables.
 *
 * @author  Christiaan Swanepoel
 * @date    2026-03-23
 */

#include <emmintrin.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend_util.h"
#include "ht_internal.h"
#include "simd_impl.h"

#define GROUP_SIZE 16

/* --- function prototypes -------------------------------------------------- */

static void simd_destroy_impl(void *impl);
static ht_result simd_insert_impl(void *impl, ht_key_t key, ht_val_t value);
static ht_result simd_get_impl(const void *impl, ht_key_t key,
                               ht_val_t *value_out);
static ht_result simd_remove_impl(void *impl, ht_key_t key);
static size_t simd_size_impl(const void *impl);
static size_t simd_capacity_impl(const void *impl);
static double simd_load_factor_impl(const void *impl);
static ht_result simd_reserve_impl(void *impl, size_t capacity);
static ht_result simd_rehash_impl(void *impl, size_t capacity);
static ht_result simd_get_stats_impl(const void *impl, ht_stats *out);
static ht_result simd_reset_stats_impl(void *impl);
static void simd_update_bytes_used(simd_table *t);
static ht_result simd_resize(simd_table *t, size_t new_capacity);
static ht_result simd_insert_rehash(simd_table *t, uint64_t hash, ht_key_t key,
                                    ht_val_t value);
static ht_result simd_find_slot(const simd_table *t, ht_key_t key,
                                uint64_t hash, size_t *slot_out,
                                uint64_t *probe_len_out);
static ht_result simd_find_insert_slot(const simd_table *t, ht_key_t key,
                                       uint64_t hash, size_t *slot_out,
                                       int *found_existing,
                                       uint64_t *probe_len_out);

static inline void simd_mirror_ctrl(simd_table *t) {
  for (size_t i = 0; i < GROUP_SIZE; i++) {
    t->ctrl[t->capacity + i] = t->ctrl[i % t->capacity];
  }
}

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable SIMD_VTABLE = {
    .destroy = simd_destroy_impl,
    .insert = simd_insert_impl,
    .get = simd_get_impl,
    .remove = simd_remove_impl,
    .size = simd_size_impl,
    .capacity = simd_capacity_impl,
    .load_factor = simd_load_factor_impl,
    .reserve = simd_reserve_impl,
    .rehash = simd_rehash_impl,
    .get_stats = simd_get_stats_impl,
    .reset_stats = simd_reset_stats_impl,
    .bind_bench_iface = simd_bind_bench_iface};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

void *simd_create_impl(const ht_config *cfg) {
  simd_table *t;
  size_t capacity;
  size_t min_capacity;

  if (cfg == NULL) {
    return NULL;
  }

  t = calloc(1, sizeof(*t));
  if (t == NULL) {
    return NULL;
  }

  capacity =
      (cfg->init_capacity > 0) ? cfg->init_capacity : DEFAULT_INITIAL_CAPACITY;
  capacity = next_pow2(capacity);

  min_capacity =
      (cfg->min_capacity > 0) ? cfg->min_capacity : DEFAULT_MIN_CAPACITY;
  min_capacity = next_pow2(min_capacity);

  if (capacity < min_capacity) {
    capacity = min_capacity;
  }

  /* Extra control bytes let group loads read past the logical end safely. */
  if (posix_memalign((void **)&t->ctrl, 16, capacity + GROUP_SIZE) != 0) {
    free(t);
    return NULL;
  }
  memset(t->ctrl, SIMD_EMPTY, capacity + GROUP_SIZE);

  t->data = calloc(capacity, sizeof(*t->data));
  if (t->data == NULL) {
    free(t->ctrl);
    free(t);
    return NULL;
  }

  t->capacity = capacity;
  t->min_capacity = min_capacity;
  t->size = 0;
  t->used = 0;
  t->max_load_factor =
      (cfg->max_load_factor > 0.0) ? cfg->max_load_factor : DEFAULT_MAX_LOAD;
  t->min_load_factor =
      (cfg->min_load_factor > 0.0) ? cfg->min_load_factor : DEFAULT_MIN_LOAD;
  t->resize_mode = cfg->rsz_mode;
  t->hash_fn = (cfg->hash_fn != NULL) ? cfg->hash_fn : default_hash;
  t->hash_seed = cfg->hash_seed;
  t->collect_stats = cfg->collect_stats;

  simd_update_bytes_used(t);
  ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
  return t;
}

const struct ht_vtable *simd_vtable(void) { return &SIMD_VTABLE; }

int simd_bind_bench_iface(void *ctx, bench_iface *out) {
  if (ctx == NULL || out == NULL) {
    return HT_ERR_INVALID;
  }

  out->ctx = ctx;
  out->insert = simd_insert_impl;
  out->get = simd_get_impl;
  out->remove = simd_remove_impl;
  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void simd_destroy_impl(void *impl) {
  simd_table *t = impl;

  if (t == NULL) {
    return;
  }

  free(t->ctrl);
  free(t->data);
  free(t);
}

static ht_result simd_insert_impl(void *impl, ht_key_t key, ht_val_t value) {
  simd_table *t = impl;
  uint64_t hash;
  size_t slot;
  int found_existing;
  uint64_t probe_len = 0;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }
  HT_STATS_INC(t, inserts);

  hash = t->hash_fn(key, t->hash_seed);

retry:
  rc = simd_find_insert_slot(t, key, hash, &slot, &found_existing, &probe_len);
  if (rc != HT_OK) {
    if (rc == HT_ERR_FULL && t->resize_mode != HT_RESIZE_NONE) {
      rc = simd_resize(t, t->capacity * 2);
      if (rc == HT_OK) {
        goto retry;
      }
    }
    HT_UPDATE_PROBE_STATS(t, probe_len);
    HT_RECORD_INSERT_FAILURE(t);
    return rc;
  }

  if (found_existing) {
    HT_UPDATE_PROBE_STATS(t, probe_len);
    HT_RECORD_INSERT_FAILURE(t);
    return HT_ERR_EXISTS;
  }

  int was_empty = (t->ctrl[slot] == SIMD_EMPTY);
  if (t->resize_mode != HT_RESIZE_NONE && was_empty &&
      HT_SHOULD_GROW_COUNT(t, used)) {
    rc = simd_resize(t, t->capacity * 2);
    if (rc != HT_OK) {
      HT_RECORD_INSERT_FAILURE(t);
      return rc;
    }
    goto retry;
  }

  HT_UPDATE_PROBE_STATS(t, probe_len);

  if (t->resize_mode == HT_RESIZE_NONE && was_empty &&
      HT_SHOULD_GROW_COUNT(t, used)) {
    HT_RECORD_INSERT_FAILURE(t);
    return HT_ERR_FULL;
  }

  if (was_empty) {
    t->used++;
  }
  t->ctrl[slot] = (uint8_t)(hash & SIMD_TAG_MASK);
  simd_mirror_ctrl(t);

  t->data[slot].hash = hash;
  t->data[slot].key = key;
  t->data[slot].value = value;
  t->size++;

  return HT_OK;
}

static ht_result simd_get_impl(const void *impl, ht_key_t key,
                               ht_val_t *value_out) {
  const simd_table *t = impl;
  size_t slot;
  uint64_t probe_len = 0;
  uint64_t hash;
  ht_result rc;

  if (t == NULL || value_out == NULL) {
    return HT_ERR_INVALID;
  }
  if (t->collect_stats) {
    ((simd_table *)t)->stats.lookups++;
  }

  hash = t->hash_fn(key, t->hash_seed);
  rc = simd_find_slot(t, key, hash, &slot, &probe_len);

  if (rc != HT_OK) {
    if (t->collect_stats) {
      ((simd_table *)t)->stats.lookup_misses++;
      HT_UPDATE_PROBE_STATS((simd_table *)t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
  }

  HT_UPDATE_PROBE_STATS((simd_table *)t, probe_len);
  *value_out = t->data[slot].value;
  return HT_OK;
}

static ht_result simd_remove_impl(void *impl, ht_key_t key) {
  simd_table *t = impl;
  size_t slot;
  uint64_t probe_len = 0;
  uint64_t hash;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }
  if (t->collect_stats) {
    t->stats.removes++;
  }

  hash = t->hash_fn(key, t->hash_seed);
  rc = simd_find_slot(t, key, hash, &slot, &probe_len);

  if (rc != HT_OK) {
    if (t->collect_stats) {
      t->stats.remove_misses++;
      HT_UPDATE_PROBE_STATS(t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
  }

  HT_UPDATE_PROBE_STATS(t, probe_len);
  t->ctrl[slot] = SIMD_TOMBSTONE;
  simd_mirror_ctrl(t);
  t->size--;

  if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
      HT_SHOULD_SHRINK_COUNT(t, size)) {
    size_t new_cap =
        (t->capacity / 2 < t->min_capacity) ? t->min_capacity : t->capacity / 2;
    return simd_resize(t, new_cap);
  }
  return HT_OK;
}

static size_t simd_size_impl(const void *impl) {
  const simd_table *t = impl;

  return (t != NULL) ? t->size : 0;
}

static size_t simd_capacity_impl(const void *impl) {
  const simd_table *t = impl;

  return (t != NULL) ? t->capacity : 0;
}

static double simd_load_factor_impl(const void *impl) {
  const simd_table *t = impl;

  return (t != NULL) ? ht_load_factor_snapshot(t->size, t->capacity) : 0.0;
}

static ht_result simd_reserve_impl(void *impl, size_t capacity) {
  simd_table *t = impl;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }
  if (capacity <= t->capacity) {
    return HT_OK;
  }
  return simd_resize(t, next_pow2(capacity));
}

static ht_result simd_rehash_impl(void *impl, size_t capacity) {
  simd_table *t = impl;
  size_t target;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  target = (capacity > t->size) ? capacity : t->size;
  if (target < t->min_capacity) {
    target = t->min_capacity;
  }
  return simd_resize(t, next_pow2(target));
}

static ht_result simd_get_stats_impl(const void *impl, ht_stats *out) {
  if (impl == NULL || out == NULL) {
    return HT_ERR_INVALID;
  }

  *out = ((const simd_table *)impl)->stats;
  return HT_OK;
}

static ht_result simd_reset_stats_impl(void *impl) {
  simd_table *t = impl;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  memset(&t->stats, 0, sizeof(t->stats));
  simd_update_bytes_used(t);
  ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void simd_update_bytes_used(simd_table *t) {
  if (t != NULL && t->collect_stats) {
    size_t bytes = ht_bytes_used_snapshot(sizeof(*t), t->capacity + GROUP_SIZE,
                                          sizeof(uint8_t));

    if (bytes != SIZE_MAX &&
        t->capacity <= (SIZE_MAX - bytes) / sizeof(simd_data_slot)) {
      bytes += t->capacity * sizeof(simd_data_slot);
    } else {
      bytes = SIZE_MAX;
    }

    t->stats.bytes_used = bytes;
  }
}

static ht_result simd_resize(simd_table *t, size_t new_capacity) {
  uint8_t *old_ctrl = t->ctrl;
  simd_data_slot *old_data = t->data;
  size_t old_cap = t->capacity;
  size_t old_size = t->size;
  uint64_t start_ns = ht_resize_instrumentation_start(t->collect_stats);
  uint8_t *new_ctrl;
  simd_data_slot *new_data;

  new_capacity = next_pow2((new_capacity < t->min_capacity) ? t->min_capacity
                                                            : new_capacity);
  if (new_capacity == t->capacity) {
    return HT_OK;
  }

  if (posix_memalign((void **)&new_ctrl, 16, new_capacity + GROUP_SIZE) != 0) {
    return HT_ERR_OOM;
  }
  memset(new_ctrl, SIMD_EMPTY, new_capacity + GROUP_SIZE);

  new_data = calloc(new_capacity, sizeof(*new_data));
  if (new_data == NULL) {
    free(new_ctrl);
    return HT_ERR_OOM;
  }

  t->ctrl = new_ctrl;
  t->data = new_data;
  t->capacity = new_capacity;
  t->size = 0;
  t->used = 0;

  for (size_t i = 0; i < old_cap; i++) {
    if (old_ctrl[i] != SIMD_EMPTY && old_ctrl[i] != SIMD_TOMBSTONE) {
      ht_result rc = simd_insert_rehash(t, old_data[i].hash, old_data[i].key,
                                        old_data[i].value);
      if (rc != HT_OK) {
        size_t used = 0;

        free(t->ctrl);
        free(t->data);
        t->ctrl = old_ctrl;
        t->data = old_data;
        t->capacity = old_cap;
        t->size = old_size;
        for (size_t j = 0; j < old_cap; j++) {
          if (old_ctrl[j] != SIMD_EMPTY) {
            used++;
          }
        }
        t->used = used;
        return rc;
      }
    }
  }

  free(old_ctrl);
  free(old_data);
  simd_update_bytes_used(t);
  ht_resize_stats_record(&t->stats, t->collect_stats, old_cap, t->capacity,
                         old_size, start_ns);
  return HT_OK;
}

static ht_result simd_insert_rehash(simd_table *t, uint64_t hash, ht_key_t key,
                                    ht_val_t value) {
  size_t base = HT_INDEX_FOR_U64(hash, t->capacity);

  for (size_t i = 0; i < t->capacity; i++) {
    size_t idx = (base + i) & (t->capacity - 1);
    if (t->ctrl[idx] == SIMD_EMPTY) {
      t->ctrl[idx] = (uint8_t)(hash & SIMD_TAG_MASK);
      simd_mirror_ctrl(t);
      t->data[idx].hash = hash;
      t->data[idx].key = key;
      t->data[idx].value = value;
      t->size++;
      t->used++;
      return HT_OK;
    }
  }

  return HT_ERR_FULL;
}

static ht_result simd_find_slot(const simd_table *t, ht_key_t key,
                                uint64_t hash, size_t *slot_out,
                                uint64_t *probe_len_out) {
  size_t base = HT_INDEX_FOR_U64(hash, t->capacity);
  uint8_t tag = (uint8_t)(hash & SIMD_TAG_MASK);
  __m128i tag_vec = _mm_set1_epi8((char)tag);
  __m128i empty_vec = _mm_set1_epi8((char)SIMD_EMPTY);

  for (size_t i = 0; i < t->capacity; i += GROUP_SIZE) {
    size_t idx = (base + i) & (t->capacity - 1);
    __m128i ctrl = _mm_loadu_si128((__m128i *)(t->ctrl + idx));

    int match_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(ctrl, tag_vec));
    while (match_mask != 0) {
      int bit = __builtin_ctz(match_mask);
      size_t cand_idx = (idx + bit) & (t->capacity - 1);
      if (t->data[cand_idx].hash == hash && t->data[cand_idx].key == key) {
        *slot_out = cand_idx;
        if (probe_len_out) {
          *probe_len_out = (uint64_t)i + bit + 1;
        }
        return HT_OK;
      }
      match_mask &= (match_mask - 1);
    }

    int empty_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(ctrl, empty_vec));
    if (empty_mask != 0) {
      if (probe_len_out) {
        *probe_len_out = (uint64_t)i + __builtin_ctz(empty_mask) + 1;
      }
      return HT_ERR_NOT_FOUND;
    }
  }

  if (probe_len_out) {
    *probe_len_out = t->capacity;
  }
  return HT_ERR_NOT_FOUND;
}

static ht_result simd_find_insert_slot(const simd_table *t, ht_key_t key,
                                       uint64_t hash, size_t *slot_out,
                                       int *found_existing,
                                       uint64_t *probe_len_out) {
  size_t base = HT_INDEX_FOR_U64(hash, t->capacity);
  uint8_t tag = (uint8_t)(hash & SIMD_TAG_MASK);
  __m128i tag_vec = _mm_set1_epi8((char)tag);
  __m128i empty_vec = _mm_set1_epi8((char)SIMD_EMPTY);
  size_t first_deleted = (size_t)-1;

  *found_existing = 0;

  for (size_t i = 0; i < t->capacity; i += GROUP_SIZE) {
    size_t idx = (base + i) & (t->capacity - 1);
    __m128i ctrl = _mm_loadu_si128((__m128i *)(t->ctrl + idx));

    int match_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(ctrl, tag_vec));
    while (match_mask != 0) {
      int bit = __builtin_ctz(match_mask);
      size_t cand_idx = (idx + bit) & (t->capacity - 1);
      if (t->data[cand_idx].hash == hash && t->data[cand_idx].key == key) {
        *slot_out = cand_idx;
        *found_existing = 1;
        if (probe_len_out) {
          *probe_len_out = (uint64_t)i + bit + 1;
        }
        return HT_OK;
      }
      match_mask &= (match_mask - 1);
    }

    int empty_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(ctrl, empty_vec));
    if (empty_mask != 0) {
      int empty_bit = __builtin_ctz(empty_mask);
      if (first_deleted == (size_t)-1) {
        /* A tombstone before the first empty slot is a better insert target. */
        __m128i tomb_vec = _mm_set1_epi8((char)SIMD_TOMBSTONE);
        int tomb_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(ctrl, tomb_vec));
        if (tomb_mask != 0) {
          int tomb_bit = __builtin_ctz(tomb_mask);
          if (tomb_bit < empty_bit) {
            first_deleted = (idx + tomb_bit) & (t->capacity - 1);
          }
        }
      }

      if (first_deleted != (size_t)-1) {
        *slot_out = first_deleted;
      } else {
        *slot_out = (idx + empty_bit) & (t->capacity - 1);
      }
      if (probe_len_out) {
        *probe_len_out = (uint64_t)i + empty_bit + 1;
      }
      return HT_OK;
    }

    /* Remember the earliest tombstone across groups for reuse. */
    if (first_deleted == (size_t)-1) {
      __m128i tomb_vec = _mm_set1_epi8((char)SIMD_TOMBSTONE);
      int tomb_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(ctrl, tomb_vec));
      if (tomb_mask != 0) {
        first_deleted = (idx + __builtin_ctz(tomb_mask)) & (t->capacity - 1);
      }
    }
  }

  if (first_deleted != (size_t)-1) {
    *slot_out = first_deleted;
    if (probe_len_out) {
      *probe_len_out = t->capacity;
    }
    return HT_OK;
  }

  return HT_ERR_FULL;
}
