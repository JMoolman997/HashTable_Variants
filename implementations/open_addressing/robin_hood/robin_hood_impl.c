/**
 * @file    robin_hood_impl.c
 * @brief   Robin Hood hashing with backward-shift deletion.
 *
 * Implements a Robin Hood open-addressing hashtable with "rich-take-from-poor"
 * swapping during insertion and backward-shift deletion to maintain probe
 * sequence length (PSL) invariants without using tombstones.
 *
 * @author  Christiaan Swanepoel
 * @date    2026-03-23
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend_config.h"
#include "capacity_util.h"
#include "hash_util.h"
#include "memory_util.h"
#include "resize_stats.h"
#include "stats_util.h"
#include "ht_internal.h"
#include "robin_hood_impl.h"

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Release all memory owned by a open-addressing table backend.
 *
 * @param impl Open-addressing backend instance to destroy.
 */
static void robin_hood_destroy_impl(void *impl);

/**
 * @brief Insert a key/value pair through the open-addressing backend
 * implementation.
 *
 * @param impl Open-addressing backend instance to modify.
 * @param key Key to insert.
 * @param value Value to associate with `key`.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`,
 *         `HT_ERR_EXISTS`, `HT_ERR_FULL`, or `HT_ERR_OOM`.
 */
static ht_result robin_hood_insert_impl(void *impl, ht_key_t key,
                                        ht_val_t value);

/**
 * @brief Look up a key through the open-addressing backend implementation.
 *
 * @param impl Open-addressing backend instance to query.
 * @param key Key to search for.
 * @param value_out Output location that receives the value on success.
 *
 * @return `HT_OK` when found, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result robin_hood_get_impl(const void *impl, ht_key_t key,
                                     ht_val_t *value_out);

/**
 * @brief Remove a key through the open-addressing backend implementation.
 *
 * @param impl Open-addressing backend instance to modify.
 * @param key Key to remove.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result robin_hood_remove_impl(void *impl, ht_key_t key);

/**
 * @brief Return the live entry count from a open-addressing backend instance.
 *
 * @param impl Open-addressing backend instance to query.
 *
 * @return The number of live entries, or `0` if `impl` is invalid.
 */
static size_t robin_hood_size_impl(const void *impl);

/**
 * @brief Return the current slot capacity from a open-addressing backend
 * instance.
 *
 * @param impl Open-addressing backend instance to query.
 *
 * @return The current slot capacity, or `0` if `impl` is invalid.
 */
static size_t robin_hood_capacity_impl(const void *impl);

/**
 * @brief Return the current load factor from a open-addressing backend
 * instance.
 *
 * @param impl Open-addressing backend instance to query.
 *
 * @return The load factor, or `0.0` if `impl` is invalid.
 */
static double robin_hood_load_factor_impl(const void *impl);

/**
 * @brief Ensure the open-addressing backend can hold at least the requested
 * capacity.
 *
 * @param impl Open-addressing backend instance to resize if needed.
 * @param capacity Minimum target capacity.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result robin_hood_reserve_impl(void *impl, size_t capacity);

/**
 * @brief Rehash the open-addressing backend around a requested capacity.
 *
 * @param impl Open-addressing backend instance to rebuild.
 * @param capacity Requested target capacity.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_OOM`.
 */
static ht_result robin_hood_rehash_impl(void *impl, size_t capacity);

/**
 * @brief Copy the statistics snapshot out of a open-addressing backend
 * instance.
 *
 * @param impl Open-addressing backend instance to query.
 * @param out Output structure that receives the copied statistics.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
static ht_result robin_hood_get_stats_impl(const void *impl, ht_stats *out);

/**
 * @brief Reset the statistics counters stored by a open-addressing backend
 * instance.
 *
 * @param impl Open-addressing backend instance whose counters should be
 * cleared.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if `impl` is invalid.
 */
static ht_result robin_hood_reset_stats_impl(void *impl);

/**
 * @brief Rebuild the table storage at a new capacity.
 *
 * @param t Table to resize.
 * @param new_capacity Requested target capacity before normalization.
 *
 * @return `HT_OK` on success, or an error such as `HT_ERR_INVALID`,
 *         `HT_ERR_OOM`, or `HT_ERR_FULL`.
 */
static ht_result robin_hood_resize(robin_hood_table *t, size_t new_capacity);

/**
 * @brief Insert an existing entry into a freshly allocated table during rehash.
 *
 * @param t Destination table being rebuilt.
 * @param hash Cached hash for the key.
 * @param key Key to insert.
 * @param value Value to insert.
 *
 * @return `HT_OK` on success, or `HT_ERR_FULL` if no slot is available.
 */
static ht_result robin_hood_insert_rehash(robin_hood_table *t, uint64_t hash,
                                          ht_key_t key, ht_val_t value);

/**
 * @brief Find the slot containing a key.
 *
 * @param t Table to search.
 * @param key Key to look up.
 * @param hash Cached hash for the key.
 * @param slot_out Output location for the matching slot index.
 * @param probe_len_out Optional output for the number of probes performed.
 *
 * @return `HT_OK` when found, or an error such as `HT_ERR_INVALID` or
 *         `HT_ERR_NOT_FOUND`.
 */
static ht_result robin_hood_find_slot(const robin_hood_table *t, ht_key_t key,
                                      uint64_t hash, size_t *slot_out,
                                      uint64_t *probe_len_out);

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable ROBIN_HOOD_VTABLE = {
    .destroy = robin_hood_destroy_impl,
    .insert = robin_hood_insert_impl,
    .get = robin_hood_get_impl,
    .remove = robin_hood_remove_impl,
    .size = robin_hood_size_impl,
    .capacity = robin_hood_capacity_impl,
    .load_factor = robin_hood_load_factor_impl,
    .reserve = robin_hood_reserve_impl,
    .rehash = robin_hood_rehash_impl,
    .get_stats = robin_hood_get_stats_impl,
    .reset_stats = robin_hood_reset_stats_impl,
    .bind_bench_iface = robin_hood_bind_bench_iface};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

ht_result robin_hood_create_impl_ex(const ht_config *cfg, void **out) {
  robin_hood_table *t;
  ht_backend_config resolved;
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

  t = calloc(1, sizeof(*t));
  if (t == NULL) {
    return HT_ERR_OOM;
  }

  t->slots = calloc(resolved.capacity, sizeof(*t->slots));
  if (t->slots == NULL) {
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

  ht_stats_set_slot_array_bytes(&t->stats, t->collect_stats, sizeof(*t),
                                t->capacity, sizeof(*t->slots));
  ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
  *out = t;
  return HT_OK;
}

const struct ht_vtable *robin_hood_vtable(void) { return &ROBIN_HOOD_VTABLE; }

int robin_hood_bind_bench_iface(void *ctx, bench_iface *out) {
  if (ctx == NULL || out == NULL) {
    return HT_ERR_INVALID;
  }

  out->ctx = ctx;
  out->insert = robin_hood_insert_impl;
  out->get = robin_hood_get_impl;
  out->remove = robin_hood_remove_impl;

  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void robin_hood_destroy_impl(void *impl) {
  robin_hood_table *t = impl;

  if (t == NULL) {
    return;
  }

  free(t->slots);
  free(t);
}

static ht_result robin_hood_insert_impl(void *impl, ht_key_t key,
                                        ht_val_t value) {
  robin_hood_table *t = impl;
  uint64_t hash;
  ht_result rc;
  ht_key_t curr_key;
  ht_val_t curr_val;
  uint64_t curr_hash;
  size_t curr_psl;
  size_t base;
  size_t existing_slot;
  uint64_t find_probe_len;
  int checking_existence;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  HT_STATS_INC(t, inserts);

  hash = t->hash_fn(key, t->hash_seed);

retry:
  find_probe_len = 0;
  rc = robin_hood_find_slot(t, key, hash, &existing_slot, &find_probe_len);
  if (rc == HT_OK) {
    HT_UPDATE_PROBE_STATS(t, find_probe_len);
    HT_RECORD_INSERT_FAILURE(t);
    return HT_ERR_EXISTS;
  }
  if (rc != HT_ERR_NOT_FOUND) {
    HT_UPDATE_PROBE_STATS(t, find_probe_len);
    HT_RECORD_INSERT_FAILURE(t);
    return rc;
  }

  if (t->resize_mode == HT_RESIZE_NONE && HT_SHOULD_GROW_COUNT(t, used)) {
    HT_UPDATE_PROBE_STATS(t, find_probe_len);
    HT_RECORD_INSERT_FAILURE(t);
    return HT_ERR_FULL;
  }
  if (t->resize_mode != HT_RESIZE_NONE &&
      (HT_SHOULD_GROW_COUNT(t, used) || t->used >= t->capacity)) {
    rc = robin_hood_resize(t, t->capacity * 2);
    if (rc != HT_OK) {
      HT_RECORD_INSERT_FAILURE(t);
      return rc;
    }
    goto retry;
  }

  /* Single-pass Robin Hood insertion:
   * 1. Search for existing key.
   * 2. If encounter a slot with smaller PSL, swap and start displacing.
   * 3. If encounter an EMPTY slot, place and return. */
  curr_key = key;
  curr_val = value;
  curr_hash = hash;
  curr_psl = 0;
  base = HT_INDEX_FOR_U64(curr_hash, t->capacity);
  checking_existence = 1;

  for (size_t i = 0; i < t->capacity; i++) {
    size_t idx = (base + i) & (t->capacity - 1);

    if (t->slots[idx].state == ROBIN_HOOD_SLOT_EMPTY) {
      t->used++;
      t->slots[idx].state = ROBIN_HOOD_SLOT_FULL;
      t->slots[idx].hash = curr_hash;
      t->slots[idx].key = curr_key;
      t->slots[idx].value = curr_val;
      t->size++;
      HT_UPDATE_PROBE_STATS(t, i + 1);
      return HT_OK;
    }

    if (checking_existence) {
      if (t->slots[idx].hash == curr_hash && t->slots[idx].key == curr_key) {
        HT_UPDATE_PROBE_STATS(t, i + 1);
        HT_RECORD_INSERT_FAILURE(t);
        return HT_ERR_EXISTS;
      }
    }

    size_t their_base = HT_INDEX_FOR_U64(t->slots[idx].hash, t->capacity);
    size_t their_psl = (idx - their_base) & (t->capacity - 1);

    if (curr_psl > their_psl) {
      /* If we haven't found the key yet, encountering a slot where we would
       * have been swapped means it definitely isn't further in the cluster. */
      checking_existence = 0;

      /* Swap */
      ht_key_t tmp_key = t->slots[idx].key;
      ht_val_t tmp_val = t->slots[idx].value;
      uint64_t tmp_hash = t->slots[idx].hash;

      t->slots[idx].key = curr_key;
      t->slots[idx].value = curr_val;
      t->slots[idx].hash = curr_hash;

      curr_key = tmp_key;
      curr_val = tmp_val;
      curr_hash = tmp_hash;
      curr_psl = their_psl;

      /* No need to reset base/i; (base + i + 1) correctly points to the next
       * slot for the displaced element's own home. */
    }
    curr_psl++;
  }

  if (t->resize_mode != HT_RESIZE_NONE) {
    rc = robin_hood_resize(t, t->capacity * 2);
    if (rc == HT_OK) {
      goto retry;
    }
    HT_RECORD_INSERT_FAILURE(t);
    return rc;
  }

  HT_UPDATE_PROBE_STATS(t, t->capacity);
  HT_RECORD_INSERT_FAILURE(t);
  return HT_ERR_FULL;
}

static ht_result robin_hood_get_impl(const void *impl, ht_key_t key,
                                     ht_val_t *value_out) {
  const robin_hood_table *t = impl;
  size_t slot;
  uint64_t probe_len;
  uint64_t hash;
  ht_result rc;

  if (t == NULL || value_out == NULL) {
    return HT_ERR_INVALID;
  }

  if (t->collect_stats) {
    ((robin_hood_table *)t)->stats.lookups++;
  }

  hash = t->hash_fn(key, t->hash_seed);
  rc = robin_hood_find_slot(t, key, hash, &slot, &probe_len);

  if (rc != HT_OK) {
    if (t->collect_stats) {
      ((robin_hood_table *)t)->stats.lookup_misses++;
      HT_UPDATE_PROBE_STATS((robin_hood_table *)t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
  }

  HT_UPDATE_PROBE_STATS((robin_hood_table *)t, probe_len);
  *value_out = t->slots[slot].value;
  return HT_OK;
}

static ht_result robin_hood_remove_impl(void *impl, ht_key_t key) {
  robin_hood_table *t = impl;
  size_t slot;
  uint64_t probe_len;
  uint64_t hash;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  if (t->collect_stats) {
    t->stats.removes++;
  }

  hash = t->hash_fn(key, t->hash_seed);
  rc = robin_hood_find_slot(t, key, hash, &slot, &probe_len);

  if (rc != HT_OK) {
    if (t->collect_stats) {
      t->stats.remove_misses++;
      HT_UPDATE_PROBE_STATS(t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
  }

  HT_UPDATE_PROBE_STATS(t, probe_len);

  /* Robin Hood deletion: shift back any subsequent element that is not at its
   * ideal slot. */
  size_t i = (slot + 1) & (t->capacity - 1);
  while (t->slots[i].state == ROBIN_HOOD_SLOT_FULL) {
    size_t their_base = HT_INDEX_FOR_U64(t->slots[i].hash, t->capacity);
    size_t their_psl = (i - their_base) & (t->capacity - 1);

    if (their_psl == 0) {
      break;
    }

    t->slots[slot] = t->slots[i];
    slot = i;
    i = (i + 1) & (t->capacity - 1);
  }

  t->slots[slot].state = ROBIN_HOOD_SLOT_EMPTY;
  t->used--;
  t->size--;

  if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
      HT_SHOULD_SHRINK_COUNT(t, size)) {
    size_t new_capacity = t->capacity / 2;

    if (new_capacity < t->min_capacity) {
      new_capacity = t->min_capacity;
    }

    return robin_hood_resize(t, new_capacity);
  }

  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* metadata/stat ops                                                         */
/* ------------------------------------------------------------------------- */

static size_t robin_hood_size_impl(const void *impl) {
  const robin_hood_table *t = impl;
  return (t != NULL) ? t->size : 0;
}

static size_t robin_hood_capacity_impl(const void *impl) {
  const robin_hood_table *t = impl;
  return (t != NULL) ? t->capacity : 0;
}

static double robin_hood_load_factor_impl(const void *impl) {
  const robin_hood_table *t = impl;

  return (t != NULL) ? ht_load_factor_snapshot(t->size, t->capacity) : 0.0;
}

static ht_result robin_hood_reserve_impl(void *impl, size_t capacity) {
  robin_hood_table *t = impl;
  size_t target;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  rc = ht_reserve_target(t->capacity, capacity, &target);
  if (rc != HT_OK || target == t->capacity) {
    return rc;
  }

  return robin_hood_resize(t, target);
}

static ht_result robin_hood_rehash_impl(void *impl, size_t capacity) {
  robin_hood_table *t = impl;
  size_t target;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  rc = ht_rehash_target(t->size, t->min_capacity, capacity, &target);
  if (rc != HT_OK) {
    return rc;
  }

  return robin_hood_resize(t, target);
}

static ht_result robin_hood_get_stats_impl(const void *impl, ht_stats *out) {
  const robin_hood_table *t = impl;

  if (t == NULL || out == NULL) {
    return HT_ERR_INVALID;
  }

  *out = t->stats;
  return HT_OK;
}

static ht_result robin_hood_reset_stats_impl(void *impl) {
  robin_hood_table *t = impl;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  memset(&t->stats, 0, sizeof(t->stats));
  ht_stats_set_slot_array_bytes(&t->stats, t->collect_stats, sizeof(*t),
                                t->capacity, sizeof(*t->slots));
  ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static ht_result robin_hood_resize(robin_hood_table *t, size_t new_capacity) {
  robin_hood_slot *old_slots;
  size_t old_capacity;
  size_t old_size;
  robin_hood_slot *new_slots;
  uint64_t resize_start_ns;
  size_t i;
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

  old_slots = t->slots;
  old_capacity = t->capacity;
  old_size = t->size;
  resize_start_ns = ht_resize_instrumentation_start(t->collect_stats);

  new_slots = calloc(new_capacity, sizeof(*new_slots));
  if (new_slots == NULL) {
    return HT_ERR_OOM;
  }

  t->slots = new_slots;
  t->capacity = new_capacity;
  t->size = 0;
  t->used = 0;

  for (i = 0; i < old_capacity; i++) {
    if (old_slots[i].state == ROBIN_HOOD_SLOT_FULL) {
      /* Reinsert only live entries. */
      rc = robin_hood_insert_rehash(t, old_slots[i].hash, old_slots[i].key,
                                    old_slots[i].value);
      if (rc != HT_OK) {
        free(t->slots);
        t->slots = old_slots;
        t->capacity = old_capacity;
        t->size = old_size;

        {
          size_t j;
          size_t used = 0;

          /* Reconstruct the old non-empty slot count before
           * restoring the previous table state. */
          for (j = 0; j < old_capacity; j++) {
            if (old_slots[j].state != ROBIN_HOOD_SLOT_EMPTY) {
              used++;
            }
          }
          t->used = used;
        }

        return rc;
      }
    }
  }

  free(old_slots);

  ht_resize_stats_record(&t->stats, t->collect_stats, old_capacity, t->capacity,
                         old_size, resize_start_ns);
  ht_stats_set_slot_array_bytes(&t->stats, t->collect_stats, sizeof(*t),
                                t->capacity, sizeof(*t->slots));

  return HT_OK;
}

static ht_result robin_hood_insert_rehash(robin_hood_table *t, uint64_t hash,
                                          ht_key_t key, ht_val_t value) {
  ht_key_t curr_key = key;
  ht_val_t curr_val = value;
  uint64_t curr_hash = hash;
  size_t curr_psl = 0;
  size_t base = HT_INDEX_FOR_U64(curr_hash, t->capacity);
  size_t i;

  for (i = 0; i < t->capacity; i++) {
    size_t idx = (base + i) & (t->capacity - 1);

    if (t->slots[idx].state == ROBIN_HOOD_SLOT_EMPTY) {
      t->slots[idx].state = ROBIN_HOOD_SLOT_FULL;
      t->slots[idx].hash = curr_hash;
      t->slots[idx].key = curr_key;
      t->slots[idx].value = curr_val;
      t->size++;
      t->used++;
      return HT_OK;
    }

    size_t their_base = HT_INDEX_FOR_U64(t->slots[idx].hash, t->capacity);
    size_t their_psl = (idx - their_base) & (t->capacity - 1);

    if (curr_psl > their_psl) {
      ht_key_t tmp_key = t->slots[idx].key;
      ht_val_t tmp_val = t->slots[idx].value;
      uint64_t tmp_hash = t->slots[idx].hash;

      t->slots[idx].key = curr_key;
      t->slots[idx].value = curr_val;
      t->slots[idx].hash = curr_hash;

      curr_key = tmp_key;
      curr_val = tmp_val;
      curr_hash = tmp_hash;
      curr_psl = their_psl;

      base = their_base;
      i = their_psl;
    }
    curr_psl++;
  }

  return HT_ERR_FULL;
}

static ht_result robin_hood_find_slot(const robin_hood_table *t, ht_key_t key,
                                      uint64_t hash, size_t *slot_out,
                                      uint64_t *probe_len_out) {
  size_t base;
  size_t i;

  if (t == NULL || slot_out == NULL) {
    return HT_ERR_INVALID;
  }

  if (probe_len_out != NULL) {
    *probe_len_out = 0;
  }

  base = HT_INDEX_FOR_U64(hash, t->capacity);

  for (i = 0; i < t->capacity; i++) {
    size_t idx = (base + i) & (t->capacity - 1);

    if (probe_len_out != NULL) {
      *probe_len_out = i + 1;
    }

    if (t->slots[idx].state == ROBIN_HOOD_SLOT_EMPTY) {
      return HT_ERR_NOT_FOUND;
    }

    if (t->slots[idx].state == ROBIN_HOOD_SLOT_FULL &&
        t->slots[idx].hash == hash && t->slots[idx].key == key) {
      *slot_out = idx;
      return HT_OK;
    }

    if (t->slots[idx].state == ROBIN_HOOD_SLOT_FULL) {
      size_t their_base = HT_INDEX_FOR_U64(t->slots[idx].hash, t->capacity);
      size_t their_psl = (idx - their_base) & (t->capacity - 1);
      if (their_psl < i) {
        return HT_ERR_NOT_FOUND;
      }
    }
  }

  return HT_ERR_NOT_FOUND;
}
