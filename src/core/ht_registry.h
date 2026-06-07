/**
 * @file ht_registry.h
 * @brief Internal backend registry for implementation names and constructors.
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#ifndef HT_CORE_HT_REGISTRY_H
#define HT_CORE_HT_REGISTRY_H

#include "ht_internal.h"
#include "ht_types.h"

typedef ht_result (*ht_backend_create_fn)(const ht_config *cfg, void **out);
typedef const struct ht_vtable *(*ht_backend_vtable_fn)(void);

typedef struct {
    ht_impl impl;
    const char *name;
    ht_backend_create_fn create;
    ht_backend_vtable_fn vtable;
} ht_registry_entry;

const ht_registry_entry *ht_registry_entries(size_t *count);
const ht_registry_entry *ht_registry_find(ht_impl impl);
const char *ht_registry_impl_name(ht_impl impl);

ht_result ht_registry_create_backend(
    const ht_config *cfg,
    void **impl_out,
    const struct ht_vtable **vt_out
);

#endif /* HT_CORE_HT_REGISTRY_H */
