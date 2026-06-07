#include "test_registry.h"

static const test_impl_case TEST_ALL_IMPLS[] = {
    { HT_IMPL_OPEN_ADDRESSING, "open_addressing" },
    { HT_IMPL_P_OPEN_ADDRESSING, "p_open_addressing" },
    { HT_IMPL_SEPARATE_CHAINING, "separate_chaining" },
    { HT_IMPL_HOPSCOTCH, "hopscotch" },
    { HT_IMPL_LF_HOPSCOTCH, "lf_hopscotch" },
    { HT_IMPL_ADV_OPEN_ADDRESSING, "adv_open_addressing" },
    { HT_IMPL_BACKSHIFT, "backshift" },
    { HT_IMPL_ROBIN_HOOD, "robin-hood" },
    { HT_IMPL_METADATA, "metadata" },
    { HT_IMPL_SIMD, "simd" },
    { HT_IMPL_BUCKET_MOD_SEPARATE_CHAINING, "bucket_mod_separate_chaining" },
    { HT_IMPL_LINKED_MOD_SEPARATE_CHAINING, "linked_mod_separate_chaining" },
    { HT_IMPL_SEGMENTED_MOD_SEPARATE_CHAINING, "segmented_mod_separate_chaining" },
    { HT_IMPL_P_SEPARATE_CHAINING, "p_separate_chaining" },
    { HT_IMPL_FINGERPRINT, "fingerprint" },
    { HT_IMPL_LINEAR_HASHING, "linear_hashing" },
    { HT_IMPL_ADV_SEPARATE_CHAINING, "adv_separate_chaining" }
};

const test_impl_case *test_all_impls(
    size_t *count
) {
    if (count != NULL) {
        *count = sizeof(TEST_ALL_IMPLS) / sizeof(TEST_ALL_IMPLS[0]);
    }

    return TEST_ALL_IMPLS;
}
