/**
 * @file    ht.c
 * @brief   Core public API dispatcher for generic hashtable operations.
 *
 * Implements the public hashtable interface declared in ht.h by creating
 * the selected backend, validating generic wrapper objects, and forwarding
 * operations through the internal backend vtable.
 *
 * This file is the bridge between user-facing API calls and concrete
 * implementation modules.
 *
 * @author  J.W Moolman
 * @date    2026-03-23
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdlib.h>

#include "ht.h"
#include "ht_internal.h"
#include "hopscotch_impl.h"
#include "lf_hopscotch_impl.h"
#include "open_addressing_impl.h"
#include "p_open_addressing.h"
#include "p_separate_chaining.h"
#include "adv_open_addressing_impl.h"
#include "separate_chaining_impl.h"
#include "mod_separate_chaining_impl.h"
#include "backshift_impl.h"
#include "robin_hood_impl.h"
#include "metadata_impl.h"
#include "simd_impl.h"
#include "fingerprint_impl.h"
#include "linear_hashing_impl.h"
#include "adv_separate_chaining.h"

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Check whether a generic wrapper object is unusable for dispatch.
 *
 * @param map Hashtable wrapper to validate.
 *
 * @return Non-zero if the wrapper or its dispatch state is invalid, otherwise
 *         `0`.
 */
static int ht_map_is_invalid(
    const ht_map *map
);

static ht_result ht_create_backend(
    const ht_config       *cfg,
    void                 **impl_out,
    const struct ht_vtable **vt_out
);

static int ht_map_is_invalid(
    const ht_map *map
) {
    return (map == NULL || map->vt == NULL || map->impl == NULL);
}

static ht_result ht_create_backend(
    const ht_config       *cfg,
    void                 **impl_out,
    const struct ht_vtable **vt_out
) {
    if (cfg == NULL || impl_out == NULL || vt_out == NULL) {
        return HT_ERR_INVALID;
    }

    *impl_out = NULL;
    *vt_out   = NULL;

    switch (cfg->impl_kind) {
    case HT_IMPL_OPEN_ADDRESSING:
        *vt_out = open_addressing_vtable();
        *impl_out = open_addressing_create_impl(cfg);
        break;

    case HT_IMPL_P_OPEN_ADDRESSING:
        *vt_out = p_open_addressing_vtable();
        *impl_out = p_open_addressing_create_impl(cfg);
        break;

    case HT_IMPL_P_SEPARATE_CHAINING:
        *vt_out = p_separate_chaining_vtable();
        *impl_out = p_separate_chaining_create_impl(cfg);
        break;

    case HT_IMPL_ADV_OPEN_ADDRESSING:
        *vt_out = adv_open_addressing_vtable();
        *impl_out = adv_open_addressing_create_impl(cfg);
        break;

    case HT_IMPL_SEPARATE_CHAINING:
        *vt_out = separate_chaining_vtable();
        *impl_out = separate_chaining_create_impl(cfg);
        break;

    case HT_IMPL_BUCKET_MOD_SEPARATE_CHAINING:
    case HT_IMPL_LINKED_MOD_SEPARATE_CHAINING:
    case HT_IMPL_SEGMENTED_MOD_SEPARATE_CHAINING:
        *vt_out = mod_separate_chaining_vtable();
        *impl_out = mod_separate_chaining_create_impl(cfg);
        break;

    case HT_IMPL_HOPSCOTCH:
        *vt_out = hopscotch_vtable();
        *impl_out = hopscotch_create_impl(cfg);
        break;

    case HT_IMPL_LF_HOPSCOTCH:
        *vt_out = lf_hopscotch_vtable();
        *impl_out = lf_hopscotch_create_impl(cfg);
        break;

    case HT_IMPL_ROBIN_HOOD:
        *vt_out = robin_hood_vtable();
        *impl_out = robin_hood_create_impl(cfg);
        break;

    case HT_IMPL_BACKSHIFT:
        *vt_out = backshift_vtable();
        *impl_out = backshift_create_impl(cfg);
        break;

    case HT_IMPL_METADATA:
        *vt_out = metadata_vtable();
        *impl_out = metadata_create_impl(cfg);
        break;

    case HT_IMPL_SIMD:
        *vt_out = simd_vtable();
        *impl_out = simd_create_impl(cfg);
        break;

    case HT_IMPL_FINGERPRINT:
        *vt_out = fingerprint_vtable();
        *impl_out = fingerprint_create_impl(cfg);
        break;

    case HT_IMPL_LINEAR_HASHING:
        *vt_out = linear_hashing_vtable();
        *impl_out = linear_hashing_create_impl(cfg);
        break;

    case HT_IMPL_ADV_SEPARATE_CHAINING:
        *vt_out = adv_separate_chaining_vtable();
        *impl_out = adv_separate_chaining_create_impl(cfg);
        break;

    default:
        return HT_ERR_UNSUPPORTED;
    }

    if (*vt_out == NULL) {
        return HT_ERR_UNSUPPORTED;
    }

    if (*impl_out == NULL) {
        *vt_out = NULL;
        return HT_ERR_OOM;
    }

    return HT_OK;
}

ht_config ht_config_default(ht_impl impl) {
    ht_config cfg = {0};

    cfg.impl_kind    = impl;
    cfg.rsz_mode     = HT_RESIZE_GROW;
    cfg.thread_count = 1;

    return cfg;
}

ht_config ht_config_fixed(ht_impl impl, size_t capacity) {
    ht_config cfg;

    cfg = ht_config_default(impl);
    cfg.init_capacity = capacity;
    cfg.min_capacity  = capacity;
    cfg.rsz_mode      = HT_RESIZE_NONE;

    return cfg;
}

ht_config ht_config_resizing(ht_impl impl, size_t capacity) {
    ht_config cfg;

    cfg = ht_config_default(impl);
    cfg.init_capacity = capacity;
    cfg.min_capacity  = capacity;
    cfg.rsz_mode      = HT_RESIZE_GROW_SHRINK;

    return cfg;
}

ht_result ht_create_ex(const ht_config *cfg, ht_map **out) {
    ht_map *map;
    ht_result rc;

    if (out == NULL) {
        return HT_ERR_INVALID;
    }

    *out = NULL;

    if (cfg == NULL) {
        return HT_ERR_INVALID;
    }

    map = malloc(sizeof(*map));
    if (map == NULL) {
        return HT_ERR_OOM;
    }

    map->vt   = NULL;
    map->impl = NULL;
    map->kind = cfg->impl_kind;

    rc = ht_create_backend(cfg, &map->impl, &map->vt);
    if (rc != HT_OK) {
        free(map);
        return rc;
    }

    *out = map;
    return HT_OK;
}

ht_map *ht_create(const ht_config *cfg) {
    ht_map *map = NULL;

    if (ht_create_ex(cfg, &map) != HT_OK) {
        return NULL;
    }

    return map;
}

void ht_destroy(ht_map *map) {
    if (map == NULL) {
        return;
    }

    if (map->vt != NULL && map->vt->destroy != NULL && map->impl != NULL) {
        map->vt->destroy(map->impl);
    }

    free(map);
}

ht_result ht_insert(ht_map *map, ht_key_t key, ht_val_t value) {
    if (ht_map_is_invalid(map) || map->vt->insert == NULL) {
        return HT_ERR_INVALID;
    }

    return map->vt->insert(map->impl, key, value);
}

ht_result ht_get(const ht_map *map, ht_key_t key, ht_val_t *value_out) {
    if (value_out == NULL) {
        return HT_ERR_INVALID;
    }

    if (ht_map_is_invalid(map) || map->vt->get == NULL) {
        return HT_ERR_INVALID;
    }

    return map->vt->get(map->impl, key, value_out);
}

ht_result ht_remove(ht_map *map, ht_key_t key) {
    if (ht_map_is_invalid(map) || map->vt->remove == NULL) {
        return HT_ERR_INVALID;
    }

    return map->vt->remove(map->impl, key);
}

int ht_contains(const ht_map *map, ht_key_t key) {
    ht_val_t value;

    return ht_get(map, key, &value) == HT_OK ? 1 : 0;
}

ht_result ht_upsert(ht_map *map, ht_key_t key, ht_val_t value) {
    ht_result rc;
    ht_val_t existing;

    if (ht_map_is_invalid(map)) {
        return HT_ERR_INVALID;
    }

    if (map->vt->upsert != NULL) {
        return map->vt->upsert(map->impl, key, value);
    }

    if (map->vt->get == NULL || map->vt->insert == NULL || map->vt->remove == NULL) {
        return HT_ERR_INVALID;
    }

    rc = map->vt->get(map->impl, key, &existing);
    if (rc == HT_ERR_NOT_FOUND) {
        return map->vt->insert(map->impl, key, value);
    }
    if (rc != HT_OK) {
        return rc;
    }

    rc = map->vt->remove(map->impl, key);
    if (rc != HT_OK) {
        return rc;
    }

    return map->vt->insert(map->impl, key, value);
}

size_t ht_size(const ht_map *map) {
    if (ht_map_is_invalid(map) || map->vt->size == NULL) {
        return 0;
    }

    return map->vt->size(map->impl);
}

size_t ht_capacity(const ht_map *map) {
    if (ht_map_is_invalid(map) || map->vt->capacity == NULL) {
        return 0;
    }

    return map->vt->capacity(map->impl);
}

double ht_load_factor(const ht_map *map) {
    if (ht_map_is_invalid(map) || map->vt->load_factor == NULL) {
        return 0.0;
    }

    return map->vt->load_factor(map->impl);
}

ht_result ht_reserve(ht_map *map, size_t capacity) {
    if (ht_map_is_invalid(map) || map->vt->reserve == NULL) {
        return HT_ERR_INVALID;
    }

    return map->vt->reserve(map->impl, capacity);
}

ht_result ht_rehash(ht_map *map, size_t capacity) {
    if (ht_map_is_invalid(map) || map->vt->rehash == NULL) {
        return HT_ERR_INVALID;
    }

    return map->vt->rehash(map->impl, capacity);
}

ht_result ht_get_stats(const ht_map *map, ht_stats *out) {
    if (out == NULL) {
        return HT_ERR_INVALID;
    }

    if (ht_map_is_invalid(map) || map->vt->get_stats == NULL) {
        return HT_ERR_INVALID;
    }

    return map->vt->get_stats(map->impl, out);
}

ht_result ht_reset_stats(ht_map *map) {
    if (ht_map_is_invalid(map) || map->vt->reset_stats == NULL) {
        return HT_ERR_INVALID;
    }

    return map->vt->reset_stats(map->impl);
}

const char *ht_impl_name(ht_impl impl) {
    switch (impl) {
    case HT_IMPL_OPEN_ADDRESSING:
        return "open_addressing";
    case HT_IMPL_ROBIN_HOOD:
        return "robin-hood";
    case HT_IMPL_SEPARATE_CHAINING:
        return "separate_chaining";
    case HT_IMPL_HOPSCOTCH:
        return "hopscotch";
    case HT_IMPL_ADV_OPEN_ADDRESSING:
        return "adv_open_addressing";
    case HT_IMPL_P_OPEN_ADDRESSING:
        return "p_open_addressing";
    case HT_IMPL_BACKSHIFT:
        return "backshift";
    case HT_IMPL_METADATA:
        return "metadata";
    case HT_IMPL_SIMD:
        return "simd";
    case HT_IMPL_BUCKET_MOD_SEPARATE_CHAINING:
        return "bucket_mod_separate_chaining";
    case HT_IMPL_LINKED_MOD_SEPARATE_CHAINING:
        return "linked_mod_separate_chaining";
    case HT_IMPL_SEGMENTED_MOD_SEPARATE_CHAINING:
        return "segmented_mod_separate_chaining";
    case HT_IMPL_P_SEPARATE_CHAINING:
        return "p_separate_chaining";
    case HT_IMPL_FINGERPRINT:
        return "fingerprint";
    case HT_IMPL_LINEAR_HASHING:
        return "linear_hashing";
    case HT_IMPL_ADV_SEPARATE_CHAINING:
        return "adv_separate_chaining";
    case HT_IMPL_LF_HOPSCOTCH:
        return "lf_hopscotch";
    default:
        return "unknown";
    }
}

const char *ht_result_name(ht_result result) {
    switch (result) {
    case HT_OK:
        return "ok";
    case HT_ERR:
        return "error";
    case HT_ERR_OOM:
        return "out_of_memory";
    case HT_ERR_NOT_FOUND:
        return "not_found";
    case HT_ERR_EXISTS:
        return "exists";
    case HT_ERR_FULL:
        return "full";
    case HT_ERR_INVALID:
        return "invalid";
    case HT_ERR_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}
