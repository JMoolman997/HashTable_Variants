#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stddef.h>

#include "test_registry.h"

#define TEST_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef int (*test_fn)(
    ht_impl     impl,
    const char *impl_name
);

typedef struct {
    const char *name;
    test_fn     fn;
} test_case;

int test_run_impl_matrix(
    const test_impl_case *impls,
    size_t                impl_count,
    const test_case      *cases,
    size_t                case_count
);

#endif
