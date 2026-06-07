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
#include "ht_registry.h"

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

static ht_result ht_config_validate(
    const ht_config *cfg
);

static int ht_resize_mode_is_valid(
    ht_rsz_mode mode
);

static int ht_defaultable_max_load_is_valid(
    double value
);

static int ht_defaultable_min_load_is_valid(
    double value
);

static int ht_map_is_invalid(
    const ht_map *map
) {
    return (map == NULL || map->vt == NULL || map->impl == NULL);
}

static int ht_resize_mode_is_valid(
    ht_rsz_mode mode
) {
    return (mode == HT_RESIZE_NONE ||
            mode == HT_RESIZE_GROW ||
            mode == HT_RESIZE_GROW_SHRINK);
}

static int ht_defaultable_max_load_is_valid(
    double value
) {
    return (value == 0.0 || (value == value && value > 0.0 && value <= 1.0));
}

static int ht_defaultable_min_load_is_valid(
    double value
) {
    return (value == 0.0 || (value == value && value > 0.0 && value < 1.0));
}

static ht_result ht_config_validate(
    const ht_config *cfg
) {
    if (cfg == NULL) {
        return HT_ERR_INVALID;
    }

    if (!ht_resize_mode_is_valid(cfg->rsz_mode)) {
        return HT_ERR_INVALID;
    }

    if (cfg->init_capacity != 0 &&
        cfg->min_capacity > cfg->init_capacity) {
        return HT_ERR_INVALID;
    }

    if (!ht_defaultable_max_load_is_valid(cfg->max_load_factor) ||
        !ht_defaultable_min_load_is_valid(cfg->min_load_factor)) {
        return HT_ERR_INVALID;
    }

    if (cfg->max_load_factor > 0.0 &&
        cfg->min_load_factor > 0.0 &&
        cfg->min_load_factor >= cfg->max_load_factor) {
        return HT_ERR_INVALID;
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

    rc = ht_config_validate(cfg);
    if (rc != HT_OK) {
        return rc;
    }

    map = malloc(sizeof(*map));
    if (map == NULL) {
        return HT_ERR_OOM;
    }

    map->vt   = NULL;
    map->impl = NULL;
    map->kind = cfg->impl_kind;

    rc = ht_registry_create_backend(cfg, &map->impl, &map->vt);
    if (rc != HT_OK) {
        free(map);
        return rc;
    }
    if (map->impl == NULL || map->vt == NULL) {
        free(map);
        return HT_ERR;
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
    return ht_registry_impl_name(impl);
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
