/**
 * @file    metadata_impl.c
 * @brief   Metadata-separated open-addressing hashtable backend.
 *
 * Implements a variant where metadata (state/tags) is stored in a separate
 * array from the data (key/value) for better cache locality during probing.
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
#include "metadata_impl.h"

/* --- function prototypes -------------------------------------------------- */

static void metadata_destroy_impl(void *impl);
static ht_result metadata_insert_impl(void *impl, ht_key_t key, ht_val_t value);
static ht_result metadata_get_impl(const void *impl, ht_key_t key,
                                   ht_val_t *value_out);
static ht_result metadata_remove_impl(void *impl, ht_key_t key);
static size_t metadata_size_impl(const void *impl);
static size_t metadata_capacity_impl(const void *impl);
static double metadata_load_factor_impl(const void *impl);
static ht_result metadata_reserve_impl(void *impl, size_t capacity);
static ht_result metadata_rehash_impl(void *impl, size_t capacity);
static ht_result metadata_get_stats_impl(const void *impl, ht_stats *out);
static ht_result metadata_reset_stats_impl(void *impl);
static void metadata_update_bytes_used(metadata_table *t);
static ht_result metadata_resize(metadata_table *t, size_t new_capacity);
static ht_result metadata_insert_rehash(metadata_table *t, uint64_t hash,
                                        ht_key_t key, ht_val_t value);
static ht_result metadata_find_slot(const metadata_table *t, ht_key_t key,
                                    uint64_t hash, size_t *slot_out,
                                    uint64_t *probe_len_out);
static ht_result metadata_find_insert_slot(const metadata_table *t,
                                           ht_key_t key, uint64_t hash,
                                           size_t *slot_out,
                                           int *found_existing,
                                           uint64_t *probe_len_out);

/* ------------------------------------------------------------------------- */
/* vtable                                                                    */
/* ------------------------------------------------------------------------- */

static const struct ht_vtable METADATA_VTABLE = {
    .destroy = metadata_destroy_impl,
    .insert = metadata_insert_impl,
    .get = metadata_get_impl,
    .remove = metadata_remove_impl,
    .size = metadata_size_impl,
    .capacity = metadata_capacity_impl,
    .load_factor = metadata_load_factor_impl,
    .reserve = metadata_reserve_impl,
    .rehash = metadata_rehash_impl,
    .get_stats = metadata_get_stats_impl,
    .reset_stats = metadata_reset_stats_impl,
    .bind_bench_iface = metadata_bind_bench_iface};

/* ------------------------------------------------------------------------- */
/* public backend entry points                                               */
/* ------------------------------------------------------------------------- */

ht_result metadata_create_impl_ex(const ht_config *cfg, void **out) {
  metadata_table *t;
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

  t->ctrl = malloc(resolved.capacity);
  if (t->ctrl == NULL) {
    free(t);
    return HT_ERR_OOM;
  }
  memset(t->ctrl, METADATA_EMPTY, resolved.capacity);

  t->data = calloc(resolved.capacity, sizeof(*t->data));
  if (t->data == NULL) {
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

  metadata_update_bytes_used(t);
  ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
  *out = t;
  return HT_OK;
}

const struct ht_vtable *metadata_vtable(void) { return &METADATA_VTABLE; }

int metadata_bind_bench_iface(void *ctx, bench_iface *out) {
  if (ctx == NULL || out == NULL) {
    return HT_ERR_INVALID;
  }

  out->ctx = ctx;
  out->insert = metadata_insert_impl;
  out->get = metadata_get_impl;
  out->remove = metadata_remove_impl;
  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* core operations                                                           */
/* ------------------------------------------------------------------------- */

static void metadata_destroy_impl(void *impl) {
  metadata_table *t = impl;

  if (t == NULL) {
    return;
  }

  free(t->ctrl);
  free(t->data);
  free(t);
}

static ht_result metadata_insert_impl(void *impl, ht_key_t key,
                                      ht_val_t value) {
  metadata_table *t = impl;
  uint64_t hash;
  size_t slot;
  int found_existing;
  uint64_t probe_len = 0;
  size_t new_capacity;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }
  HT_STATS_INC(t, inserts);

  hash = t->hash_fn(key, t->hash_seed);

retry:
  rc = metadata_find_insert_slot(t, key, hash, &slot, &found_existing,
                                 &probe_len);
  if (rc != HT_OK) {
    if (rc == HT_ERR_FULL && t->resize_mode != HT_RESIZE_NONE) {
      rc = ht_grow_capacity_pow2(t->capacity, &new_capacity);
      if (rc == HT_OK) {
        rc = metadata_resize(t, new_capacity);
      }
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

  int was_empty = (t->ctrl[slot] == METADATA_EMPTY);
  if (t->resize_mode != HT_RESIZE_NONE && was_empty &&
      HT_SHOULD_GROW_COUNT(t, used)) {
    rc = ht_grow_capacity_pow2(t->capacity, &new_capacity);
    if (rc == HT_OK) {
      rc = metadata_resize(t, new_capacity);
    }
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
  t->ctrl[slot] = ht_hash_tag_u7_high(hash);
  t->data[slot].hash = hash;
  t->data[slot].key = key;
  t->data[slot].value = value;
  t->size++;

  return HT_OK;
}

static ht_result metadata_get_impl(const void *impl, ht_key_t key,
                                   ht_val_t *value_out) {
  const metadata_table *t = impl;
  size_t slot;
  uint64_t probe_len = 0;
  uint64_t hash;
  ht_result rc;

  if (t == NULL || value_out == NULL) {
    return HT_ERR_INVALID;
  }
  if (t->collect_stats) {
    ((metadata_table *)t)->stats.lookups++;
  }

  hash = t->hash_fn(key, t->hash_seed);
  rc = metadata_find_slot(t, key, hash, &slot, &probe_len);

  if (rc != HT_OK) {
    if (t->collect_stats) {
      ((metadata_table *)t)->stats.lookup_misses++;
      HT_UPDATE_PROBE_STATS((metadata_table *)t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
  }

  HT_UPDATE_PROBE_STATS((metadata_table *)t, probe_len);
  *value_out = t->data[slot].value;
  return HT_OK;
}

static ht_result metadata_remove_impl(void *impl, ht_key_t key) {
  metadata_table *t = impl;
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
  rc = metadata_find_slot(t, key, hash, &slot, &probe_len);

  if (rc != HT_OK) {
    if (t->collect_stats) {
      t->stats.remove_misses++;
      HT_UPDATE_PROBE_STATS(t, probe_len);
    }
    return HT_ERR_NOT_FOUND;
  }

  HT_UPDATE_PROBE_STATS(t, probe_len);
  t->ctrl[slot] = METADATA_TOMBSTONE;
  t->size--;

  if (t->resize_mode == HT_RESIZE_GROW_SHRINK &&
      HT_SHOULD_SHRINK_COUNT(t, size)) {
    size_t new_cap =
        (t->capacity / 2 < t->min_capacity) ? t->min_capacity : t->capacity / 2;
    (void)metadata_resize(t, new_cap);
  }
  return HT_OK;
}

static size_t metadata_size_impl(const void *impl) {
  const metadata_table *t = impl;

  return (t != NULL) ? t->size : 0;
}

static size_t metadata_capacity_impl(const void *impl) {
  const metadata_table *t = impl;

  return (t != NULL) ? t->capacity : 0;
}

static double metadata_load_factor_impl(const void *impl) {
  const metadata_table *t = impl;

  return (t != NULL) ? ht_load_factor_snapshot(t->size, t->capacity) : 0.0;
}

static ht_result metadata_reserve_impl(void *impl, size_t capacity) {
  metadata_table *t = impl;
  size_t target;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }
  rc = ht_reserve_target(t->capacity, capacity, &target);
  if (rc != HT_OK || target == t->capacity) {
    return rc;
  }
  return metadata_resize(t, target);
}

static ht_result metadata_rehash_impl(void *impl, size_t capacity) {
  metadata_table *t = impl;
  size_t target;
  ht_result rc;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  rc = ht_rehash_target(t->size, t->min_capacity, capacity, &target);
  if (rc != HT_OK) {
    return rc;
  }
  return metadata_resize(t, target);
}

static ht_result metadata_get_stats_impl(const void *impl, ht_stats *out) {
  if (impl == NULL || out == NULL) {
    return HT_ERR_INVALID;
  }

  *out = ((const metadata_table *)impl)->stats;
  return HT_OK;
}

static ht_result metadata_reset_stats_impl(void *impl) {
  metadata_table *t = impl;

  if (t == NULL) {
    return HT_ERR_INVALID;
  }

  memset(&t->stats, 0, sizeof(t->stats));
  metadata_update_bytes_used(t);
  ht_resize_stats_init(&t->stats, t->collect_stats, t->capacity);
  return HT_OK;
}

/* ------------------------------------------------------------------------- */
/* internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void metadata_update_bytes_used(metadata_table *t) {
  if (t != NULL && t->collect_stats) {
    size_t bytes = sizeof(*t);
    bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->ctrl));
    bytes = ht_bytes_add_array_or_max(bytes, t->capacity, sizeof(*t->data));
    t->stats.bytes_used = bytes;
  }
}

static ht_result metadata_resize(metadata_table *t, size_t new_capacity) {
  uint8_t *old_ctrl = t->ctrl;
  metadata_data_slot *old_data = t->data;
  size_t old_cap = t->capacity;
  size_t old_size = t->size;
  uint64_t start_ns = ht_resize_instrumentation_start(t->collect_stats);
  uint8_t *new_ctrl;
  metadata_data_slot *new_data;
  ht_result rc;

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

  new_ctrl = malloc(new_capacity);
  if (new_ctrl == NULL) {
    return HT_ERR_OOM;
  }
  memset(new_ctrl, METADATA_EMPTY, new_capacity);

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
    if (old_ctrl[i] != METADATA_EMPTY && old_ctrl[i] != METADATA_TOMBSTONE) {
      ht_result rc = metadata_insert_rehash(t, old_data[i].hash,
                                            old_data[i].key, old_data[i].value);
      if (rc != HT_OK) {
        size_t used = 0;

        free(t->ctrl);
        free(t->data);
        t->ctrl = old_ctrl;
        t->data = old_data;
        t->capacity = old_cap;
        t->size = old_size;
        for (size_t j = 0; j < old_cap; j++) {
          if (old_ctrl[j] != METADATA_EMPTY) {
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
  metadata_update_bytes_used(t);
  ht_resize_stats_record(&t->stats, t->collect_stats, old_cap, t->capacity,
                         old_size, start_ns);
  return HT_OK;
}

static ht_result metadata_insert_rehash(metadata_table *t, uint64_t hash,
                                        ht_key_t key, ht_val_t value) {
  size_t base = HT_INDEX_FOR_U64(hash, t->capacity);

  for (size_t i = 0; i < t->capacity; i++) {
    size_t idx = (base + i) & (t->capacity - 1);
    if (t->ctrl[idx] == METADATA_EMPTY) {
      t->ctrl[idx] = ht_hash_tag_u7_high(hash);
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

static ht_result metadata_find_slot(const metadata_table *t, ht_key_t key,
                                    uint64_t hash, size_t *slot_out,
                                    uint64_t *probe_len_out) {
  size_t base = HT_INDEX_FOR_U64(hash, t->capacity);
  uint8_t tag = ht_hash_tag_u7_high(hash);

  for (size_t i = 0; i < t->capacity; i++) {
    size_t idx = (base + i) & (t->capacity - 1);
    uint8_t ctrl = t->ctrl[idx];
    if (ctrl == METADATA_EMPTY) {
      if (probe_len_out) {
        *probe_len_out = (uint64_t)i + 1;
      }
      return HT_ERR_NOT_FOUND;
    }
    if (ctrl == tag) {
      if (t->data[idx].hash == hash && t->data[idx].key == key) {
        *slot_out = idx;
        if (probe_len_out) {
          *probe_len_out = (uint64_t)i + 1;
        }
        return HT_OK;
      }
    }
  }

  if (probe_len_out) {
    *probe_len_out = t->capacity;
  }
  return HT_ERR_NOT_FOUND;
}

static ht_result metadata_find_insert_slot(const metadata_table *t,
                                           ht_key_t key, uint64_t hash,
                                           size_t *slot_out,
                                           int *found_existing,
                                           uint64_t *probe_len_out) {
  size_t base = HT_INDEX_FOR_U64(hash, t->capacity);
  uint8_t tag = ht_hash_tag_u7_high(hash);
  size_t first_deleted = (size_t)-1;

  *found_existing = 0;

  for (size_t i = 0; i < t->capacity; i++) {
    size_t idx = (base + i) & (t->capacity - 1);
    uint8_t ctrl = t->ctrl[idx];
    if (ctrl == METADATA_EMPTY) {
      /* Prefer an earlier tombstone to preserve probe-chain locality. */
      *slot_out = (first_deleted != (size_t)-1) ? first_deleted : idx;
      if (probe_len_out) {
        *probe_len_out = (uint64_t)i + 1;
      }
      return HT_OK;
    }
    if (ctrl == METADATA_TOMBSTONE) {
      /* Keep the first tombstone; later ones have longer probe distance. */
      if (first_deleted == (size_t)-1) {
        first_deleted = idx;
      }
    } else if (ctrl == tag) {
      if (t->data[idx].hash == hash && t->data[idx].key == key) {
        *slot_out = idx;
        *found_existing = 1;
        if (probe_len_out) {
          *probe_len_out = (uint64_t)i + 1;
        }
        return HT_OK;
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
  if (probe_len_out) {
    *probe_len_out = t->capacity;
  }
  return HT_ERR_FULL;
}
