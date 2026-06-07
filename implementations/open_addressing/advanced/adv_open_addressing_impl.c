/**
 * @file    adv_open_addressing_impl.c
 * @brief   SIMD-optimized open-addressing hashtable backend with backshift
 * deletion.
 *
 * Implements the open-addressing backend using SSE2-based tag scanning for
 * faster probing. The table uses separate arrays for:
 *  1. Control bytes (1-byte tags) for SIMD filtering.
 *  2. Cached 64-bit hashes for secondary verification.
 *  3. Key/Value entries for primary data storage.
 *
 * Deletion is handled via backshift (backward-shift) to maintain cluster
 * contiguity without tombstones.
 *
 * @author  Christiaan Swanepoel
 * @date    2026-04-16
 */

#include <emmintrin.h> /** SSE2 intrinsics */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "adv_open_addressing_impl.h"
#include "backend_config.h"
#include "capacity_util.h"
#include "hash_util.h"
#include "memory_util.h"
#include "resize_stats.h"
#include "stats_util.h"
#include "ht_internal.h"

/* --- constants ------------------------------------------------------------ */

/** Number of slots scanned per SIMD group. */
#define ADV_OPEN_ADDRESSING_GROUP_SIZE 16

/* --- function prototypes -------------------------------------------------- */

static void adv_open_addressing_destroy_impl(void *impl);
static ht_result adv_open_addressing_insert_impl(void *impl, ht_key_t key,
                                                 ht_val_t value);
static ht_result adv_open_addressing_get_impl(const void *impl, ht_key_t key,
                                              ht_val_t *value_out);
static ht_result adv_open_addressing_remove_impl(void *impl, ht_key_t key);
static size_t adv_open_addressing_size_impl(const void *impl);
static size_t adv_open_addressing_capacity_impl(const void *impl);
static double adv_open_addressing_load_factor_impl(const void *impl);
static ht_result adv_open_addressing_reserve_impl(void *impl, size_t capacity);
static ht_result adv_open_addressing_rehash_impl(void *impl, size_t capacity);
static ht_result adv_open_addressing_get_stats_impl(const void *impl,
                                                    ht_stats *out);
static ht_result adv_open_addressing_reset_stats_impl(void *impl);
static void adv_open_addressing_update_bytes_used(adv_open_addressing_table *t);
static void adv_open_addressing_sync_ctrl(adv_open_addressing_table *t);
static size_t adv_open_addressing_get_psl(const adv_open_addressing_table *t,
                                          size_t idx);
static ht_result adv_open_addressing_resize(adv_open_addressing_table *t,
                                            size_t new_capacity);
static ht_result adv_open_addressing_insert_rehash(adv_open_addressing_table *t,
                                                   uint64_t hash, ht_key_t key,
                                                   ht_val_t value);
static ht_result
adv_open_addressing_find_slot(const adv_open_addressing_table *t, ht_key_t key,
                              uint64_t hash, size_t *slot_out,
                              uint64_t *probe_len_out);

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable ADV_OPEN_ADDRESSING_VTABLE = {
    .destroy = adv_open_addressing_destroy_impl,
    .insert = adv_open_addressing_insert_impl,
    .get = adv_open_addressing_get_impl,
    .remove = adv_open_addressing_remove_impl,
    .size = adv_open_addressing_size_impl,
    .capacity = adv_open_addressing_capacity_impl,
    .load_factor = adv_open_addressing_load_factor_impl,
    .reserve = adv_open_addressing_reserve_impl,
    .rehash = adv_open_addressing_rehash_impl,
    .get_stats = adv_open_addressing_get_stats_impl,
    .reset_stats = adv_open_addressing_reset_stats_impl,
    .bind_bench_iface = adv_open_addressing_bind_bench_iface};

/* ------------------------------------------------------------------------- */
/* helpers for SIMD bitmask traversal                                        */
/* ------------------------------------------------------------------------- */

/**
 * @brief Find the first set bit in a mask.
 * @return Bit index [0, 15] or undefined if mask is 0.
 */
static inline int adv_open_addressing_bitmask_first_set(uint16_t mask) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctz(mask);
#else
  // Scalar fallback for non-GCC compilers
  int i = 0;
  while (!(mask & 1)) {
    mask >>= 1;
    i++;
  }
  return i;
#endif
}

/**
 * @brief Clear the first set bit in a mask.
 */
static inline uint16_t adv_open_addressing_bitmask_clear_first(uint16_t mask) {
  return mask & (mask - 1);
}

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

ht_result adv_open_addressing_create_impl_ex(const ht_config *cfg, void **out) {
  adv_open_addressing_table *t;
  ht_backend_config resolved;
  size_t ctrl_bytes;
  size_t hashes_bytes;
  size_t entries_bytes;
  ht_result rc;

  if (out == NULL) {
    return HT_ERR_INVALID;
  }
  *out = NULL;

  if (cfg == NULL) {
    return HT_ERR_INVALID;
  }

  rc = ht_backend_config_resolve(
      cfg, DEFAULT_INITIAL_CAPACITY, DEFAULT_MIN_CAPACITY, DEFAULT_MAX_LOAD,
      DEFAULT_MIN_LOAD, &resolved);
  if (rc != HT_OK) {
    return rc;
  }
  rc = ht_control_bytes_for_group(resolved.capacity,
                                  ADV_OPEN_ADDRESSING_GROUP_SIZE,
                                  ADV_OPEN_ADDRESSING_GROUP_SIZE - 1u,
                                  &ctrl_bytes);
  if (rc != HT_OK) {
    return rc;
  }
  rc = ht_checked_mul_size(resolved.capacity, sizeof(*t->hashes),
                           &hashes_bytes);
  if (rc != HT_OK) {
    return rc;
  }
  rc = ht_checked_mul_size(resolved.capacity, sizeof(*t->entries),
                           &entries_bytes);
  if (rc != HT_OK) {
    return rc;
  }

  t = calloc(1, sizeof(*t));
  if (t == NULL) {
    return HT_ERR_OOM;
  }

  /* Pad the control array by 15 bytes so unaligned SIMD loads can safely read
   * the final probe group without special-case bounds checks. */
  t->ctrl = malloc(ctrl_bytes);
  if (t->ctrl == NULL) {
    free(t);
    return HT_ERR_OOM;
  }
  memset(t->ctrl, ADV_OPEN_ADDRESSING_CTRL_EMPTY, ctrl_bytes);

  t->hashes = malloc(hashes_bytes);
  if (t->hashes == NULL) {
    free(t->ctrl);
    free(t);
    return HT_ERR_OOM;
  }

  t->entries = malloc(entries_bytes);
  if (t->entries == NULL) {
    free(t->hashes);
    free(t->ctrl);
    free(t);
    return HT_ERR_OOM;
  }

  t->capacity = resolved.capacity;
  t->min_capacity = resolved.min_capacity;
  t->size = 0;
  t->used = 0;

  t->max_load_factor = resolved.max_load_factor;
  t->min_load_factor = resolved.min_load_factor;
  t->resize_mode = resolved.resize_mode;
  t->hash_fn = resolved.hash_fn;
  t->hash_seed = resolved.hash_seed;
  t->collect_stats = resolved.collect_stats;

  adv_open_addressing_update_bytes_used(t);
  ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
  adv_open_addressing_sync_ctrl(t);
  *out = t;
  return HT_OK;
}

const struct ht_vtable *adv_open_addressing_vtable(void) {
  return &ADV_OPEN_ADDRESSING_VTABLE;
}

int adv_open_addressing_bind_bench_iface(void *ctx, bench_iface *out) {
  if (ctx == NULL || out == NULL) {
    return HT_ERR_INVALID;
  }

  out->ctx = ctx;
  out->insert = adv_open_addressing_insert_impl;
  out->get = adv_open_addressing_get_impl;
  out->remove = adv_open_addressing_remove_impl;

  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void adv_open_addressing_destroy_impl(void *impl) {
  adv_open_addressing_table *t = impl;

  if (t == NULL) {
    return;
  }

  free(t->ctrl);
  free(t->hashes);
  free(t->entries);
  free(t);
}

static ht_result adv_open_addressing_insert_impl(void *impl, ht_key_t key,
                                                 ht_val_t value) {
  adv_open_addressing_table *t = impl;
  uint64_t hash;
  size_t idx;
  size_t existing_slot;
  size_t psl_incoming = 0;
  uint64_t probe_len = 0;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }
  HT_STATS_INC(t, inserts);

  hash = t->hash_fn(key, t->hash_seed);
  rc = adv_open_addressing_find_slot(t, key, hash, &existing_slot, &probe_len);
  if (rc == HT_OK) {
    HT_UPDATE_PROBE_STATS(t, probe_len);
    HT_RECORD_INSERT_FAILURE(t);
    return HT_ERR_EXISTS;
  }

  /* Resize if needed. */
  if (t->resize_mode != HT_RESIZE_NONE && HT_SHOULD_GROW_COUNT(t, used)) {
    rc = adv_open_addressing_resize(t, t->capacity * 2);
    if (rc != HT_OK) {
      HT_RECORD_INSERT_FAILURE(t);
      return rc;
    }
  }

  /* No-resize occupancy budget check. */
  if (t->resize_mode == HT_RESIZE_NONE &&
      (double)(t->used + 1) > (double)t->capacity * t->max_load_factor) {
    HT_UPDATE_PROBE_STATS(t, probe_len);
    HT_RECORD_INSERT_FAILURE(t);
    return HT_ERR_FULL;
  }

  idx = HT_INDEX_FOR_U64(hash, t->capacity);
  size_t cap_mask = t->capacity - 1;

  for (size_t probe = 0; probe < t->capacity; probe++) {
    uint8_t ctrl = t->ctrl[idx];

    if (ctrl == ADV_OPEN_ADDRESSING_CTRL_EMPTY) {
      t->ctrl[idx] = (uint8_t)(hash & ADV_OPEN_ADDRESSING_CTRL_FULL_MASK);
      t->hashes[idx] = hash;
      t->entries[idx].key = key;
      t->entries[idx].value = value;
      t->size++;
      t->used++;
      adv_open_addressing_sync_ctrl(t);
      HT_UPDATE_PROBE_STATS(t, probe + 1);
      return HT_OK;
    }

    /* Check for duplicate key. */
    if (t->hashes[idx] == hash && t->entries[idx].key == key) {
      HT_UPDATE_PROBE_STATS(t, probe + 1);
      HT_RECORD_INSERT_FAILURE(t);
      return HT_ERR_EXISTS;
    }

    /* Robin Hood insertion: if the incoming item has probed farther, swap it
     * with the current occupant and continue pushing the displaced entry. */
    size_t psl_existing = adv_open_addressing_get_psl(t, idx);
    if (psl_incoming > psl_existing) {
      /* Swap incoming and existing. */
      uint64_t tmp_hash = t->hashes[idx];
      ht_key_t tmp_key = t->entries[idx].key;
      ht_val_t tmp_value = t->entries[idx].value;

      t->ctrl[idx] = (uint8_t)(hash & ADV_OPEN_ADDRESSING_CTRL_FULL_MASK);
      t->hashes[idx] = hash;
      t->entries[idx].key = key;
      t->entries[idx].value = value;

      hash = tmp_hash;
      key = tmp_key;
      value = tmp_value;
      psl_incoming = psl_existing;
    }

    idx = (idx + 1) & cap_mask;
    psl_incoming++;
  }

  HT_UPDATE_PROBE_STATS(t, t->capacity);
  HT_RECORD_INSERT_FAILURE(t);
  return HT_ERR_FULL;
}

static ht_result adv_open_addressing_get_impl(const void *impl, ht_key_t key,
                                              ht_val_t *value_out) {
  const adv_open_addressing_table *t = impl;
  size_t slot;
  uint64_t probe_len = 0;
  uint64_t hash;
  ht_result rc;

  if (t == NULL || value_out == NULL) {
    return HT_ERR_INVALID;
  }

  if (t->collect_stats) {
    ((adv_open_addressing_table *)t)->stats.lookups++;
  }

  hash = t->hash_fn(key, t->hash_seed);
  rc = adv_open_addressing_find_slot(t, key, hash, &slot, &probe_len);

  if (rc != HT_OK) {
    if (t->collect_stats) {
      ((adv_open_addressing_table *)t)->stats.lookup_misses++;
      HT_UPDATE_PROBE_STATS((adv_open_addressing_table *)t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
  }

  HT_UPDATE_PROBE_STATS((adv_open_addressing_table *)t, probe_len);
  *value_out = t->entries[slot].value;
  return HT_OK;
}

static ht_result adv_open_addressing_remove_impl(void *impl, ht_key_t key) {
  adv_open_addressing_table *t = impl;
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
  rc = adv_open_addressing_find_slot(t, key, hash, &slot, &probe_len);

  if (rc != HT_OK) {
    if (t->collect_stats) {
      t->stats.remove_misses++;
      HT_UPDATE_PROBE_STATS(t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
  }

  HT_UPDATE_PROBE_STATS(t, probe_len);

  /* Backshift deletion. */
  size_t i = slot;
  size_t j = i;
  size_t cap_mask = t->capacity - 1;

  t->ctrl[i] = ADV_OPEN_ADDRESSING_CTRL_EMPTY;
  t->size--;
  t->used--;

  for (;;) {
    j = (j + 1) & cap_mask;
    if (t->ctrl[j] == ADV_OPEN_ADDRESSING_CTRL_EMPTY) {
      break;
    }

    size_t k = HT_INDEX_FOR_U64(t->hashes[j], t->capacity);

    /* Determine if k is circularly within (i, j]. If not, shift j to i. */
    int k_in_range = (i < j) ? (i < k && k <= j) : (i < k || k <= j);
    if (!k_in_range) {
      t->ctrl[i] = t->ctrl[j];
      t->hashes[i] = t->hashes[j];
      t->entries[i] = t->entries[j];
      t->ctrl[j] = ADV_OPEN_ADDRESSING_CTRL_EMPTY;
      i = j;
    }
  }

  adv_open_addressing_sync_ctrl(t);

  if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
      HT_SHOULD_SHRINK_COUNT(t, size)) {
    size_t new_capacity = t->capacity / 2;

    if (new_capacity < t->min_capacity) {
      new_capacity = t->min_capacity;
    }

    return adv_open_addressing_resize(t, new_capacity);
  }

  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* metadata/stat ops                                                         */
/* ------------------------------------------------------------------------- */

static size_t adv_open_addressing_size_impl(const void *impl) {
  const adv_open_addressing_table *t = impl;
  return (t != NULL) ? t->size : 0;
}

static size_t adv_open_addressing_capacity_impl(const void *impl) {
  const adv_open_addressing_table *t = impl;
  return (t != NULL) ? t->capacity : 0;
}

static double adv_open_addressing_load_factor_impl(const void *impl) {
  const adv_open_addressing_table *t = impl;

  if (t == NULL || t->capacity == 0) {
    return 0.0;
  }

  return (double)t->size / (double)t->capacity;
}

static ht_result adv_open_addressing_reserve_impl(void *impl, size_t capacity) {
  adv_open_addressing_table *t = impl;
  size_t target;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  rc = ht_reserve_target(t->capacity, capacity, &target);
  if (rc != HT_OK) {
    return rc;
  }
  if (target == t->capacity) {
    return HT_OK;
  }

  return adv_open_addressing_resize(t, target);
}

static ht_result adv_open_addressing_rehash_impl(void *impl, size_t capacity) {
  adv_open_addressing_table *t = impl;
  size_t target;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  rc = ht_rehash_target(t->size, t->min_capacity, capacity, &target);
  if (rc != HT_OK) {
    return rc;
  }

  return adv_open_addressing_resize(t, target);
}

static ht_result adv_open_addressing_get_stats_impl(const void *impl,
                                                    ht_stats *out) {
  const adv_open_addressing_table *t = impl;

  if (t == NULL || out == NULL) {
    return HT_ERR_INVALID;
  }

  *out = t->stats;
  return HT_OK;
}

static ht_result adv_open_addressing_reset_stats_impl(void *impl) {
  adv_open_addressing_table *t = impl;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  memset(&t->stats, 0, sizeof(t->stats));
  adv_open_addressing_update_bytes_used(t);
  ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void
adv_open_addressing_update_bytes_used(adv_open_addressing_table *t) {
  size_t bytes;
  size_t ctrl_bytes;

  if (t == NULL || !t->collect_stats) {
    return;
  }

  bytes = sizeof(*t);
  if (ht_control_bytes_for_group(t->capacity, ADV_OPEN_ADDRESSING_GROUP_SIZE,
                                 ADV_OPEN_ADDRESSING_GROUP_SIZE - 1u,
                                 &ctrl_bytes) != HT_OK) {
    bytes = SIZE_MAX;
  } else {
    bytes = ht_bytes_add_or_max(bytes, ctrl_bytes);
  }
  bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->hashes));
  bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->entries));
  t->stats.bytes_used = bytes;
}

static void adv_open_addressing_sync_ctrl(adv_open_addressing_table *t) {
  /* Copy the first 15 bytes of control tags to the padding area at the end.
     This allows SIMD loads crossing the boundary to correctly wrap. */
  memmove(&t->ctrl[t->capacity], t->ctrl, ADV_OPEN_ADDRESSING_GROUP_SIZE - 1);
}

static size_t adv_open_addressing_get_psl(const adv_open_addressing_table *t,
                                          size_t idx) {
  uint64_t hash = t->hashes[idx];
  size_t home = HT_INDEX_FOR_U64(hash, t->capacity);
  return (idx - home) & (t->capacity - 1);
}

static ht_result adv_open_addressing_resize(adv_open_addressing_table *t,
                                            size_t new_capacity) {
  uint8_t *old_ctrl;
  uint8_t *new_ctrl;
  uint64_t *old_hashes;
  uint64_t *new_hashes;
  adv_open_addressing_entry *old_entries;
  adv_open_addressing_entry *new_entries;
  size_t old_capacity;
  size_t old_size;
  size_t old_used;
  size_t ctrl_bytes;
  size_t hashes_bytes;
  size_t entries_bytes;
  uint64_t resize_start_ns;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }
  if (new_capacity < t->min_capacity) {
    new_capacity = t->min_capacity;
  }
  rc = ht_checked_next_pow2(new_capacity, &new_capacity);
  if (rc != HT_OK) {
    return rc;
  }
  if (new_capacity == t->capacity) {
    return HT_OK;
  }

  old_ctrl = t->ctrl;
  old_hashes = t->hashes;
  old_entries = t->entries;
  old_capacity = t->capacity;
  old_size = t->size;
  old_used = t->used;
  resize_start_ns = ht_resize_instrumentation_start(t->collect_stats);

  rc = ht_control_bytes_for_group(new_capacity, ADV_OPEN_ADDRESSING_GROUP_SIZE,
                                  ADV_OPEN_ADDRESSING_GROUP_SIZE - 1u,
                                  &ctrl_bytes);
  if (rc != HT_OK) {
    return rc;
  }
  rc = ht_checked_mul_size(new_capacity, sizeof(*new_hashes), &hashes_bytes);
  if (rc != HT_OK) {
    return rc;
  }
  rc = ht_checked_mul_size(new_capacity, sizeof(*new_entries), &entries_bytes);
  if (rc != HT_OK) {
    return rc;
  }

  new_ctrl = malloc(ctrl_bytes);
  if (new_ctrl == NULL) {
    return HT_ERR_OOM;
  }
  memset(new_ctrl, ADV_OPEN_ADDRESSING_CTRL_EMPTY, ctrl_bytes);

  new_hashes = malloc(hashes_bytes);
  if (new_hashes == NULL) {
    free(new_ctrl);
    return HT_ERR_OOM;
  }

  new_entries = malloc(entries_bytes);
  if (new_entries == NULL) {
    free(new_hashes);
    free(new_ctrl);
    return HT_ERR_OOM;
  }

  /* Reinsert through the temporary arrays so a failed build can restore old
   * state. */
  t->ctrl = new_ctrl;
  t->hashes = new_hashes;
  t->entries = new_entries;
  t->capacity = new_capacity;
  t->size = 0;
  t->used = 0;

  for (size_t i = 0; i < old_capacity; i++) {
    if ((old_ctrl[i] & ADV_OPEN_ADDRESSING_CTRL_EMPTY) == 0) {
      rc = adv_open_addressing_insert_rehash(
          t, old_hashes[i], old_entries[i].key, old_entries[i].value);
      if (rc != HT_OK) {
        free(new_entries);
        free(new_hashes);
        free(new_ctrl);
        t->ctrl = old_ctrl;
        t->hashes = old_hashes;
        t->entries = old_entries;
        t->capacity = old_capacity;
        t->size = old_size;
        t->used = old_used;
        return rc;
      }
    }
  }

  free(old_ctrl);
  free(old_hashes);
  free(old_entries);

  ht_resize_stats_record(&t->stats, t->collect_stats, old_capacity, t->capacity,
                         old_size, resize_start_ns);
  adv_open_addressing_update_bytes_used(t);

  adv_open_addressing_sync_ctrl(t);
  return HT_OK;
}

static ht_result adv_open_addressing_insert_rehash(adv_open_addressing_table *t,
                                                   uint64_t hash, ht_key_t key,
                                                   ht_val_t value) {
  size_t idx;
  size_t cap_mask;
  size_t psl_incoming = 0;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  idx = HT_INDEX_FOR_U64(hash, t->capacity);
  cap_mask = t->capacity - 1;

  for (size_t probe = 0; probe < t->capacity; probe++) {
    uint8_t ctrl = t->ctrl[idx];

    if (ctrl == ADV_OPEN_ADDRESSING_CTRL_EMPTY) {
      t->ctrl[idx] = (uint8_t)(hash & ADV_OPEN_ADDRESSING_CTRL_FULL_MASK);
      t->hashes[idx] = hash;
      t->entries[idx].key = key;
      t->entries[idx].value = value;
      t->size++;
      t->used++;
      return HT_OK;
    }

    size_t psl_existing = adv_open_addressing_get_psl(t, idx);
    if (psl_incoming > psl_existing) {
      uint64_t tmp_hash = t->hashes[idx];
      ht_key_t tmp_key = t->entries[idx].key;
      ht_val_t tmp_value = t->entries[idx].value;

      t->ctrl[idx] = (uint8_t)(hash & ADV_OPEN_ADDRESSING_CTRL_FULL_MASK);
      t->hashes[idx] = hash;
      t->entries[idx].key = key;
      t->entries[idx].value = value;

      hash = tmp_hash;
      key = tmp_key;
      value = tmp_value;
      psl_incoming = psl_existing;
    }

    idx = (idx + 1) & cap_mask;
    psl_incoming++;
  }

  return HT_ERR_FULL;
}

/* ------------------------------------------------------------------------- */
/* SIMD Probing Logic                                                        */
/* ------------------------------------------------------------------------- */

static ht_result
adv_open_addressing_find_slot(const adv_open_addressing_table *t, ht_key_t key,
                              uint64_t hash, size_t *slot_out,
                              uint64_t *probe_len_out) {
  size_t cap_mask = t->capacity - 1;
  size_t base = HT_INDEX_FOR_U64(hash, t->capacity);
  uint8_t tag = (uint8_t)(hash & ADV_OPEN_ADDRESSING_CTRL_FULL_MASK);
  __m128i target = _mm_set1_epi8(tag);
  __m128i empty_val = _mm_set1_epi8((char)ADV_OPEN_ADDRESSING_CTRL_EMPTY);

  for (size_t probe = 0; probe < t->capacity;
       probe += ADV_OPEN_ADDRESSING_GROUP_SIZE) {
    size_t idx = (base + probe) & cap_mask;

    /* We can do an unaligned load if idx + 16 doesn't cross the allocation end.
       Thanks to our padding (capacity + 15), if idx < capacity, idx + 15 is
       safe. */
    __m128i group = _mm_loadu_si128((__m128i *)&t->ctrl[idx]);

    /* Check for matches. */
    uint16_t match_mask =
        (uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(group, target));
    while (match_mask) {
      int bit = adv_open_addressing_bitmask_first_set(match_mask);
      size_t candidate = (idx + bit) & cap_mask;
      if (t->hashes[candidate] == hash && t->entries[candidate].key == key) {
        if (slot_out) {
          *slot_out = candidate;
        }
        if (probe_len_out) {
          *probe_len_out = probe + bit + 1;
        }
        return HT_OK;
      }
      match_mask = adv_open_addressing_bitmask_clear_first(match_mask);
    }

    /* Check for empty slots or Robin Hood early termination.
       If an empty slot exists in this group, the search ends. */
    uint16_t empty_mask =
        (uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(group, empty_val));
    if (empty_mask) {
      if (probe_len_out) {
        *probe_len_out =
            probe + adv_open_addressing_bitmask_first_set(empty_mask) + 1;
      }
      return HT_ERR_NOT_FOUND;
    }

    /* Robin Hood early termination: if the tail of this probe group is closer
       to its home slot than our current probe distance, the key cannot be
       present later in the cluster. */
    size_t last_idx = (idx + ADV_OPEN_ADDRESSING_GROUP_SIZE - 1) & cap_mask;
    if (adv_open_addressing_get_psl(t, last_idx) <
        (probe + ADV_OPEN_ADDRESSING_GROUP_SIZE - 1)) {
      if (probe_len_out) {
        *probe_len_out = probe + ADV_OPEN_ADDRESSING_GROUP_SIZE;
      }
      return HT_ERR_NOT_FOUND;
    }
  }

  if (probe_len_out) {
    *probe_len_out = t->capacity;
  }
  return HT_ERR_NOT_FOUND;
}
