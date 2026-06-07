#ifndef TEST_REGISTRY_H
#define TEST_REGISTRY_H

#include <stddef.h>

#include "ht_registry.h"

typedef ht_registry_entry test_impl_case;

const test_impl_case *test_all_impls(size_t *count);

#endif
