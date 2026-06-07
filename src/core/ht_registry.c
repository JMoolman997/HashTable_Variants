/**
 * @file ht_registry.c
 * @brief Canonical backend registry.
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#include <stddef.h>

#include "ht_registry.h"

#define HT_REGISTRY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

ht_result open_addressing_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *open_addressing_vtable(void);
ht_result robin_hood_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *robin_hood_vtable(void);
ht_result separate_chaining_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *separate_chaining_vtable(void);
ht_result hopscotch_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *hopscotch_vtable(void);
ht_result adv_open_addressing_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *adv_open_addressing_vtable(void);
ht_result p_open_addressing_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *p_open_addressing_vtable(void);
ht_result backshift_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *backshift_vtable(void);
ht_result metadata_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *metadata_vtable(void);
ht_result simd_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *simd_vtable(void);
ht_result mod_separate_chaining_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *mod_separate_chaining_vtable(void);
ht_result p_separate_chaining_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *p_separate_chaining_vtable(void);
ht_result fingerprint_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *fingerprint_vtable(void);
ht_result linear_hashing_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *linear_hashing_vtable(void);
ht_result adv_separate_chaining_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *adv_separate_chaining_vtable(void);
ht_result lf_hopscotch_create_impl_ex(const ht_config *cfg, void **out);
const struct ht_vtable *lf_hopscotch_vtable(void);

static const ht_registry_entry HT_REGISTRY[] = {
    {
        HT_IMPL_OPEN_ADDRESSING,
        "open_addressing",
        open_addressing_create_impl_ex,
        open_addressing_vtable
    },
    {
        HT_IMPL_ROBIN_HOOD,
        "robin_hood",
        robin_hood_create_impl_ex,
        robin_hood_vtable
    },
    {
        HT_IMPL_SEPARATE_CHAINING,
        "separate_chaining",
        separate_chaining_create_impl_ex,
        separate_chaining_vtable
    },
    {
        HT_IMPL_HOPSCOTCH,
        "hopscotch",
        hopscotch_create_impl_ex,
        hopscotch_vtable
    },
    {
        HT_IMPL_ADV_OPEN_ADDRESSING,
        "adv_open_addressing",
        adv_open_addressing_create_impl_ex,
        adv_open_addressing_vtable
    },
    {
        HT_IMPL_P_OPEN_ADDRESSING,
        "p_open_addressing",
        p_open_addressing_create_impl_ex,
        p_open_addressing_vtable
    },
    {
        HT_IMPL_BACKSHIFT,
        "backshift",
        backshift_create_impl_ex,
        backshift_vtable
    },
    {
        HT_IMPL_METADATA,
        "metadata",
        metadata_create_impl_ex,
        metadata_vtable
    },
    {
        HT_IMPL_SIMD,
        "simd",
        simd_create_impl_ex,
        simd_vtable
    },
    {
        HT_IMPL_BUCKET_MOD_SEPARATE_CHAINING,
        "bucket_mod_separate_chaining",
        mod_separate_chaining_create_impl_ex,
        mod_separate_chaining_vtable
    },
    {
        HT_IMPL_LINKED_MOD_SEPARATE_CHAINING,
        "linked_mod_separate_chaining",
        mod_separate_chaining_create_impl_ex,
        mod_separate_chaining_vtable
    },
    {
        HT_IMPL_SEGMENTED_MOD_SEPARATE_CHAINING,
        "segmented_mod_separate_chaining",
        mod_separate_chaining_create_impl_ex,
        mod_separate_chaining_vtable
    },
    {
        HT_IMPL_P_SEPARATE_CHAINING,
        "p_separate_chaining",
        p_separate_chaining_create_impl_ex,
        p_separate_chaining_vtable
    },
    {
        HT_IMPL_FINGERPRINT,
        "fingerprint",
        fingerprint_create_impl_ex,
        fingerprint_vtable
    },
    {
        HT_IMPL_LINEAR_HASHING,
        "linear_hashing",
        linear_hashing_create_impl_ex,
        linear_hashing_vtable
    },
    {
        HT_IMPL_ADV_SEPARATE_CHAINING,
        "adv_separate_chaining",
        adv_separate_chaining_create_impl_ex,
        adv_separate_chaining_vtable
    },
    {
        HT_IMPL_LF_HOPSCOTCH,
        "lf_hopscotch",
        lf_hopscotch_create_impl_ex,
        lf_hopscotch_vtable
    },
};

const ht_registry_entry *ht_registry_entries(size_t *count) {
    if (count != NULL) {
        *count = HT_REGISTRY_COUNT(HT_REGISTRY);
    }

    return HT_REGISTRY;
}

const ht_registry_entry *ht_registry_find(ht_impl impl) {
    size_t i;

    for (i = 0; i < HT_REGISTRY_COUNT(HT_REGISTRY); i++) {
        if (HT_REGISTRY[i].impl == impl) {
            return &HT_REGISTRY[i];
        }
    }

    return NULL;
}

const char *ht_registry_impl_name(ht_impl impl) {
    const ht_registry_entry *entry = ht_registry_find(impl);

    return (entry != NULL) ? entry->name : "unknown";
}

int ht_registry_impl_is_known(ht_impl impl) {
    return ht_registry_find(impl) != NULL ? 1 : 0;
}

ht_result ht_registry_create_backend(
    const ht_config *cfg,
    void **impl_out,
    const struct ht_vtable **vt_out
) {
    const ht_registry_entry *entry;
    ht_result rc;

    if (cfg == NULL || impl_out == NULL || vt_out == NULL) {
        return HT_ERR_INVALID;
    }

    *impl_out = NULL;
    *vt_out = NULL;

    entry = ht_registry_find(cfg->impl_kind);
    if (entry == NULL || entry->create == NULL || entry->vtable == NULL) {
        return HT_ERR_UNSUPPORTED;
    }

    rc = entry->create(cfg, impl_out);
    if (rc != HT_OK) {
        return rc;
    }

    *vt_out = entry->vtable();
    if (*impl_out == NULL || *vt_out == NULL) {
        return HT_ERR;
    }

    return HT_OK;
}
