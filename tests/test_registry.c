#include "test_registry.h"

const test_impl_case *test_all_impls(
    size_t *count
) {
    return ht_registry_entries(count);
}
