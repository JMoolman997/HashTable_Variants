/**
 * @file    bench_config.c
 * @brief   Benchmark command descriptors and capability helpers.
 */

#include <stddef.h>
#include <string.h>

#include "bench_config.h"

#define BENCH_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

#define BENCH_OPT_WORKLOAD \
    (BENCH_OPT_COMMON | BENCH_OPTBIT_WORKLOAD | BENCH_OPTBIT_TRACE_SEED)

#define BENCH_OPT_RESIZE \
    (BENCH_OPT_COMMON | BENCH_OPTBIT_INITIAL_CAPACITY | \
     BENCH_OPTBIT_RESIZE | BENCH_OPTBIT_GROW_ALPHA | \
     BENCH_OPTBIT_SHRINK_ALPHA)

#define BENCH_OPT_RESIZE_WORKLOAD \
    (BENCH_OPT_RESIZE | BENCH_OPTBIT_WORKLOAD | BENCH_OPTBIT_TRACE_SEED | \
     BENCH_OPTBIT_PREFILL_ALPHA)

#define BENCH_OPT_CONCURRENT_LOOKUP \
    (BENCH_OPT_COMMON | BENCH_OPTBIT_THREAD_COUNT | \
     BENCH_OPTBIT_INITIAL_CAPACITY | BENCH_OPTBIT_TRACE_SEED)

#define BENCH_OPT_CONCURRENT_WORKLOAD \
    (BENCH_OPT_COMMON | BENCH_OPTBIT_THREAD_COUNT | \
     BENCH_OPTBIT_INITIAL_CAPACITY | BENCH_OPTBIT_TRACE_SEED | \
     BENCH_OPTBIT_WORKLOAD | BENCH_OPTBIT_CONCURRENT_KEYSPACE | \
     BENCH_OPTBIT_CONCURRENT_RESIZE | BENCH_OPTBIT_GROW_ALPHA)

typedef struct {
    uint64_t bit;
    const char *name;
} bench_option_name;

static const bench_option_name BENCH_OPTION_NAMES[] = {
    {BENCH_OPTBIT_IMPL, "--impl"},
    {BENCH_OPTBIT_DATASET_SIZE, "--dataset-size"},
    {BENCH_OPTBIT_ALPHA, "--alpha"},
    {BENCH_OPTBIT_TIMED_OPS, "--timed-ops"},
    {BENCH_OPTBIT_WARMUP_OPS, "--warmup-ops"},
    {BENCH_OPTBIT_REPETITIONS, "--repetitions"},
    {BENCH_OPTBIT_HASH_SEED, "--hash-seed"},
    {BENCH_OPTBIT_KEY_SEED, "--key-seed"},
    {BENCH_OPTBIT_TRACE_SEED, "--trace-seed"},
    {BENCH_OPTBIT_WORKLOAD, "--workload"},
    {BENCH_OPTBIT_INITIAL_CAPACITY, "--initial-capacity"},
    {BENCH_OPTBIT_RESIZE, "--resize"},
    {BENCH_OPTBIT_GROW_ALPHA, "--grow-alpha"},
    {BENCH_OPTBIT_SHRINK_ALPHA, "--shrink-alpha"},
    {BENCH_OPTBIT_PREFILL_ALPHA, "--prefill-alpha"},
    {BENCH_OPTBIT_THREAD_COUNT, "--thread-count"},
    {BENCH_OPTBIT_CONCURRENT_KEYSPACE, "--concurrent-keyspace"},
    {BENCH_OPTBIT_CONCURRENT_RESIZE, "--concurrent-resize"},
    {BENCH_OPTBIT_STATS, "--stats"},
    {BENCH_OPTBIT_FORMAT, "--format"},
};

static const bench_case BENCH_CASES[] = {
    {
        "insert-build",
        BENCH_SCENARIO_INSERT_BUILD,
        BENCH_BACKEND_SINGLE,
        BENCH_CAP_FIXED,
        BENCH_RESIZE_DISABLED,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_COMMON,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS,
        0
    },
    {
        "lookup-hit",
        BENCH_SCENARIO_LOOKUP_HIT,
        BENCH_BACKEND_SINGLE,
        BENCH_CAP_FIXED,
        BENCH_RESIZE_DISABLED,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_COMMON,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS,
        0
    },
    {
        "lookup-miss",
        BENCH_SCENARIO_LOOKUP_MISS,
        BENCH_BACKEND_SINGLE,
        BENCH_CAP_FIXED,
        BENCH_RESIZE_DISABLED,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_COMMON,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS,
        0
    },
    {
        "erase-existing",
        BENCH_SCENARIO_ERASE_EXISTING,
        BENCH_BACKEND_SINGLE,
        BENCH_CAP_FIXED,
        BENCH_RESIZE_DISABLED,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_COMMON,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS,
        0
    },
    {
        "workload",
        BENCH_SCENARIO_WORKLOAD,
        BENCH_BACKEND_SINGLE,
        BENCH_CAP_FIXED,
        BENCH_RESIZE_DISABLED,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_WORKLOAD,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS |
            BENCH_OPTBIT_TRACE_SEED,
        BENCH_CASE_REQUIRES_TRACE_SEED
    },
    {
        "resize-build",
        BENCH_SCENARIO_INSERT_BUILD,
        BENCH_BACKEND_SINGLE,
        BENCH_CAP_RESIZING,
        BENCH_RESIZE_GROW_SHRINK,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_RESIZE,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS,
        0
    },
    {
        "resize-erase-existing",
        BENCH_SCENARIO_ERASE_EXISTING,
        BENCH_BACKEND_SINGLE,
        BENCH_CAP_RESIZING,
        BENCH_RESIZE_GROW_SHRINK,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_RESIZE,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS,
        0
    },
    {
        "resize-workload",
        BENCH_SCENARIO_WORKLOAD,
        BENCH_BACKEND_SINGLE,
        BENCH_CAP_RESIZING,
        BENCH_RESIZE_GROW_SHRINK,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_RESIZE_WORKLOAD,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS |
            BENCH_OPTBIT_TRACE_SEED,
        BENCH_CASE_REQUIRES_TRACE_SEED
    },
    {
        "concurrent-lookup",
        BENCH_SCENARIO_LOOKUP_HIT,
        BENCH_BACKEND_PTHREAD,
        BENCH_CAP_FIXED,
        BENCH_RESIZE_DISABLED,
        BENCH_KEYSPACE_SHARED_READ,
        BENCH_OPT_CONCURRENT_LOOKUP,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS,
        0
    },
    {
        "concurrent-workload",
        BENCH_SCENARIO_WORKLOAD,
        BENCH_BACKEND_PTHREAD,
        BENCH_CAP_FIXED,
        BENCH_RESIZE_DISABLED,
        BENCH_KEYSPACE_DISJOINT,
        BENCH_OPT_CONCURRENT_WORKLOAD,
        BENCH_OPTBIT_DATASET_SIZE | BENCH_OPTBIT_TIMED_OPS |
            BENCH_OPTBIT_TRACE_SEED,
        BENCH_CASE_REQUIRES_TRACE_SEED
    },
};

const bench_case *bench_cases(
    size_t *count
) {
    if (count != NULL) {
        *count = BENCH_ARRAY_LEN(BENCH_CASES);
    }

    return BENCH_CASES;
}

const bench_case *bench_case_find(
    const char *name
) {
    size_t i;

    if (name == NULL) {
        return NULL;
    }

    for (i = 0; i < BENCH_ARRAY_LEN(BENCH_CASES); i++) {
        if (strcmp(name, BENCH_CASES[i].name) == 0) {
            return &BENCH_CASES[i];
        }
    }

    return NULL;
}

const char *bench_option_set_first_name(
    uint64_t bits
) {
    size_t i;

    for (i = 0; i < BENCH_ARRAY_LEN(BENCH_OPTION_NAMES); i++) {
        if ((bits & BENCH_OPTION_NAMES[i].bit) != 0) {
            return BENCH_OPTION_NAMES[i].name;
        }
    }

    return "option";
}

int bench_impl_supports_concurrent_mutation(
    ht_impl impl
) {
    return (impl == HT_IMPL_P_OPEN_ADDRESSING ||
            impl == HT_IMPL_P_SEPARATE_CHAINING ||
            impl == HT_IMPL_LF_HOPSCOTCH);
}

int bench_impl_supports_concurrent_resize(
    ht_impl impl
) {
    return bench_impl_supports_concurrent_mutation(impl);
}
