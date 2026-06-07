#include "test_runner.h"

#include <stdio.h>
#include <string.h>

#define TEST_RUNNER_GREEN "\033[32m"
#define TEST_RUNNER_RED   "\033[31m"
#define TEST_RUNNER_RESET "\033[0m"

static size_t test_runner_case_name_width(
    const test_case *cases,
    size_t           case_count
);
static void test_runner_print_result(
    const char *case_name,
    size_t      width,
    int         passed
);

int test_run_impl_matrix(
    const test_impl_case *impls,
    size_t                impl_count,
    const test_case      *cases,
    size_t                case_count
) {
    size_t impl_idx;
    size_t test_idx;
    size_t test_name_width;

    test_name_width = test_runner_case_name_width(cases, case_count);

    for (impl_idx = 0; impl_idx < impl_count; impl_idx++) {
        fprintf(stdout, "%s\n", impls[impl_idx].name);
        fflush(stdout);

        for (test_idx = 0; test_idx < case_count; test_idx++) {
            if (cases[test_idx].fn(impls[impl_idx].impl, impls[impl_idx].name) !=
                0) {
                test_runner_print_result(
                    cases[test_idx].name,
                    test_name_width,
                    0
                );
                return 1;
            }

            test_runner_print_result(cases[test_idx].name, test_name_width, 1);
        }
    }

    return 0;
}

static size_t test_runner_case_name_width(
    const test_case *cases,
    size_t           case_count
) {
    size_t i;
    size_t width = 0;

    for (i = 0; i < case_count; i++) {
        if (strlen(cases[i].name) > width) {
            width = strlen(cases[i].name);
        }
    }

    return width;
}

static void test_runner_print_result(
    const char *case_name,
    size_t      width,
    int         passed
) {
    fprintf(
        stdout,
        "  %-*s %s%s%s\n",
        (int)width,
        case_name,
        passed ? TEST_RUNNER_GREEN : TEST_RUNNER_RED,
        passed ? "PASS" : "FAIL",
        TEST_RUNNER_RESET
    );
    fflush(stdout);
}
