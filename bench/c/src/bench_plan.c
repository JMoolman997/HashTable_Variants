/**
 * @file    bench_plan.c
 * @brief   Normalization and validation for benchmark execution plans.
 *
 * Converts raw CLI input into execution-ready benchmark settings and applies
 * mode-specific defaults in one place.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bench_plan.h"
#include "bench_names.h"

/* --- function prototypes -------------------------------------------------- */

static size_t bench_next_pow2(
    size_t x
);

static size_t bench_derive_capacity(
    size_t dataset_size,
    double target_alpha
);

static int bench_kind_is_valid(
    bench_kind kind
);

static int bench_kind_is_resize(
    bench_kind kind
);

static int bench_kind_is_concurrent(
    bench_kind kind
);

static int bench_impl_is_valid(
    ht_impl impl_kind
);

static int bench_capacity_mode_is_valid(
    bench_capacity_mode capacity_mode
);

static int bench_resize_mode_is_valid(
    bench_resize_mode resize_mode
);

static int bench_alpha_is_valid(
    double alpha
);

int bench_plan_build(
    const bench_spec *spec,
    bench_plan       *plan
) {
    if (spec == NULL || plan == NULL) {
        return -1;
    }

    memset(plan, 0, sizeof(*plan));
    plan->kind            = spec->kind;
    plan->workload        = spec->workload;
    plan->impl_kind       = spec->impl_kind;
    plan->hash_fn         = spec->hash_fn;
    plan->hash_seed       = spec->hash_seed;
    plan->key_seed        = spec->key_seed;
    plan->trace_seed      = spec->trace_seed;
    plan->capacity_mode   = spec->capacity_mode;
    plan->resize_mode     = spec->resize_mode;
    plan->concurrent_resize_mode = spec->concurrent_resize_mode;
    plan->concurrent_keyspace_mode = spec->concurrent_keyspace_mode;
    plan->dataset_size    = spec->dataset_size;
    plan->timed_ops       = spec->timed_ops;
    plan->warmup_ops      = spec->warmup_ops;
    plan->repetitions     = spec->repetitions;
    plan->target_alpha    = spec->target_alpha;
    plan->prefill_alpha   = spec->prefill_alpha;
    if (plan->kind == BENCH_RESIZE_WORKLOAD && !spec->prefill_alpha_set) {
        plan->prefill_alpha = plan->target_alpha;
    } /* Resize workloads default prefill density to the main target alpha. */
    plan->grow_alpha      = spec->grow_alpha;
    plan->shrink_alpha    = spec->shrink_alpha;
    plan->thread_count    = (spec->thread_count == 0) ? 1 : spec->thread_count;
    plan->is_resize_family = 0;
    plan->planned_capacity = bench_derive_capacity(
        spec->dataset_size,
        spec->target_alpha
    );

    if (plan->planned_capacity == 0) {
        fprintf(stderr, "Derived capacity is too large\n");
        return -1;
    }

    plan->exec_mode = BENCH_EXEC_STEADY;

    if (bench_kind_is_resize(plan->kind)) {
        plan->exec_mode      = BENCH_EXEC_RESIZE;
        plan->is_resize_family = 1;
        plan->capacity_mode = BENCH_CAPACITY_RESIZING;
        if (!spec->resize_mode_set) {
            plan->resize_mode = BENCH_RESIZE_IMPL_DEFAULT;
        } /* Resize subcommands use the implementation default unless set. */
    } /* Resize kinds always run through the resize runner family. */
    if (bench_kind_is_concurrent(plan->kind)) {
        plan->exec_mode = BENCH_EXEC_CONCURRENT;
        if (plan->kind == BENCH_CONCURRENT_LOOKUP) {
            plan->concurrent_keyspace_mode = BENCH_KEYSPACE_SHARED_READ;
        } /* Lookup concurrency is read-only against one shared keyset. */
        if (plan->concurrent_resize_mode != BENCH_RESIZE_DISABLED) {
            plan->capacity_mode = BENCH_CAPACITY_RESIZING;
        } /* Concurrent growth needs a resizable table configuration. */
    } /* Concurrent kinds switch to the threaded runner family. */

    if (plan->capacity_mode == BENCH_CAPACITY_RESIZING ||
        (bench_kind_is_concurrent(plan->kind) && spec->initial_capacity != 0)) {
        plan->initial_capacity = (spec->initial_capacity == 0)
            ? plan->planned_capacity
            : spec->initial_capacity;
        plan->min_capacity = (spec->min_capacity_override == 0)
            ? plan->initial_capacity
            : spec->min_capacity_override;
    } else {
        plan->initial_capacity = plan->planned_capacity;
        plan->min_capacity     = plan->planned_capacity;
    } /* Fixed-capacity runs keep min and initial capacity identical. */

    plan->min_capacity_override = spec->min_capacity_override;

    return 0;
}

int bench_plan_validate(
    const bench_plan *plan
) {
    if (plan == NULL) {
        fprintf(stderr, "Missing benchmark plan\n");
        return -1;
    }

    if (!bench_kind_is_valid(plan->kind)) {
        fprintf(stderr, "Invalid benchmark kind\n");
        return -1;
    }

    if (plan->dataset_size == 0) {
        fprintf(stderr, "--dataset-size must be greater than zero\n");
        return -1;
    }

    if (plan->timed_ops == 0) {
        fprintf(stderr, "--timed-ops must be greater than zero\n");
        return -1;
    }

    if (plan->repetitions == 0) {
        fprintf(stderr, "--repetitions must be greater than zero\n");
        return -1;
    }

    if (plan->target_alpha <= 0.0 || plan->target_alpha >= 1.0) {
        fprintf(stderr, "--alpha must be greater than 0 and less than 1\n");
        return -1;
    }

    if (!bench_impl_is_valid(plan->impl_kind)) {
        fprintf(stderr, "Invalid implementation kind\n");
        return -1;
    }

    if (!bench_capacity_mode_is_valid(plan->capacity_mode)) {
        fprintf(stderr, "Invalid benchmark capacity mode\n");
        return -1;
    }

    if (!bench_resize_mode_is_valid(plan->resize_mode)) {
        fprintf(stderr, "Invalid benchmark resize mode\n");
        return -1;
    }

    if (!bench_resize_mode_is_valid(plan->concurrent_resize_mode)) {
        fprintf(stderr, "Invalid concurrent resize mode\n");
        return -1;
    }

    if (plan->kind == BENCH_RESIZE_WORKLOAD &&
        plan->resize_mode == BENCH_RESIZE_DISABLED) {
        fprintf(stderr, "resize-workload requires resizing to be enabled\n");
        return -1;
    }

    if (plan->planned_capacity == 0) {
        fprintf(stderr, "Derived capacity is invalid\n");
        return -1;
    }

    if (plan->exec_mode < BENCH_EXEC_STEADY ||
        plan->exec_mode > BENCH_EXEC_CONCURRENT) {
        fprintf(stderr, "Invalid benchmark execution mode\n");
        return -1;
    } /* Exec mode must match one of the runner dispatch tables. */

    if (plan->exec_mode == BENCH_EXEC_STEADY) {
        if (plan->thread_count != 1 || plan->is_resize_family) {
            fprintf(stderr, "Steady-state runs cannot be resize-family or threaded\n");
            return -1;
        }
    } else if (plan->exec_mode == BENCH_EXEC_RESIZE) {
        if (plan->thread_count != 1 || !plan->is_resize_family) {
            fprintf(stderr, "Resize-family runs must be single-threaded\n");
            return -1;
        }
    } else {
        if (plan->thread_count < 1) {
            fprintf(stderr, "Concurrent runs require at least one thread\n");
            return -1;
        }
        if (plan->is_resize_family) {
            fprintf(stderr, "Concurrent runs cannot also be resize-family runs\n");
            return -1;
        }
        if (plan->thread_count > plan->timed_ops) {
            fprintf(stderr, "--thread-count cannot exceed --timed-ops for concurrent runs\n");
            return -1;
        }
        if (plan->concurrent_keyspace_mode < BENCH_KEYSPACE_DISJOINT ||
            plan->concurrent_keyspace_mode > BENCH_KEYSPACE_SHARED_MIXED) {
            fprintf(stderr, "Invalid concurrent keyspace mode\n");
            return -1;
        }
        if (plan->kind == BENCH_CONCURRENT_LOOKUP &&
            plan->concurrent_keyspace_mode != BENCH_KEYSPACE_SHARED_READ) {
            fprintf(stderr, "concurrent-lookup always uses shared-read keyspace\n");
            return -1;
        } /* Lookup workers must not mutate or partition the keyspace. */
        if (plan->kind == BENCH_CONCURRENT_WORKLOAD &&
            plan->concurrent_keyspace_mode == BENCH_KEYSPACE_SHARED_READ) {
            fprintf(stderr, "concurrent-workload keyspace must be disjoint or shared\n");
            return -1;
        } /* Shared-read is an internal output label, not a workload mode. */
        if (plan->concurrent_keyspace_mode == BENCH_KEYSPACE_SHARED_MIXED &&
            (plan->thread_count > SIZE_MAX / 2 ||
             plan->dataset_size < plan->thread_count * 2)) {
            fprintf(
                stderr,
                "shared concurrent keyspace requires "
                "dataset-size >= 2 * thread-count\n"
            );
            return -1;
        }
        if (plan->concurrent_resize_mode != BENCH_RESIZE_DISABLED) {
            if (plan->kind != BENCH_CONCURRENT_WORKLOAD) {
                fprintf(stderr, "Concurrent resize is only valid for concurrent-workload\n");
                return -1;
            }
            if (plan->concurrent_resize_mode != BENCH_RESIZE_GROW_ONLY) {
                fprintf(stderr, "Concurrent resize currently supports grow-only\n");
                return -1;
            }
            if (!bench_impl_supports_concurrent_mutation(plan->impl_kind) ||
                !bench_impl_supports_concurrent_resize(plan->impl_kind)) {
                fprintf(stderr, "Selected implementation does not support concurrent resize\n");
                return -1;
            }
            if (plan->grow_alpha != 0.0 &&
                !bench_alpha_is_valid(plan->grow_alpha)) {
                fprintf(stderr, "Invalid concurrent grow alpha\n");
                return -1;
            }
        }
    }

    if (plan->thread_count == 0) {
        fprintf(stderr, "--thread-count must be greater than zero\n");
        return -1;
    }

    if (plan->initial_capacity == 0) {
        fprintf(stderr, "Initial capacity must be greater than zero\n");
        return -1;
    }

    if (plan->min_capacity == 0) {
        fprintf(stderr, "Minimum capacity must be greater than zero\n");
        return -1;
    }

    if (plan->min_capacity > plan->initial_capacity) {
        fprintf(stderr, "Minimum capacity cannot exceed initial capacity\n");
        return -1;
    }

    if (plan->kind == BENCH_RESIZE_BUILD &&
        plan->timed_ops != plan->dataset_size) {
        fprintf(stderr, "resize-build requires --timed-ops to match --dataset-size\n");
        return -1;
    }

    if (plan->kind == BENCH_RESIZE_BUILD &&
        (plan->prefill_alpha < 0.0 || plan->prefill_alpha >= 1.0)) {
        fprintf(stderr, "Invalid prefill alpha\n");
        return -1;
    }

    if (plan->kind == BENCH_RESIZE_WORKLOAD) {
        size_t prefill_live;

        if (plan->prefill_alpha <= 0.0 || plan->prefill_alpha >= 1.0) {
            fprintf(stderr, "--prefill-alpha must be greater than 0 and less than 1\n");
            return -1;
        }

        prefill_live = (size_t)(plan->prefill_alpha * (double)plan->initial_capacity);
        if (prefill_live == 0) {
            fprintf(stderr, "--prefill-alpha must produce at least one live key\n");
            return -1;
        }
    }

    if (plan->resize_mode == BENCH_RESIZE_GROW_ONLY ||
        plan->resize_mode == BENCH_RESIZE_GROW_SHRINK ||
        plan->resize_mode == BENCH_RESIZE_IMPL_DEFAULT) {
        if (plan->grow_alpha != 0.0 && !bench_alpha_is_valid(plan->grow_alpha)) {
            fprintf(stderr, "Invalid grow alpha\n");
            return -1;
        }
    }

    if (plan->resize_mode == BENCH_RESIZE_GROW_SHRINK ||
        plan->resize_mode == BENCH_RESIZE_IMPL_DEFAULT) {
        if (plan->shrink_alpha != 0.0 &&
            !bench_alpha_is_valid(plan->shrink_alpha)) {
            fprintf(stderr, "Invalid shrink alpha\n");
            return -1;
        }
    }

    return 0;
}

static size_t bench_next_pow2(
    size_t x
) {
    if (x <= 1) {
        return 1;
    }

    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    if (sizeof(size_t) >= 8) {
        x |= x >> 32;
    } /* Include the high half when size_t is 64-bit. */

    return x + 1;
}

static size_t bench_derive_capacity(
    size_t dataset_size,
    double target_alpha
) {
    double raw;
    size_t rounded;

    if (dataset_size == 0 || target_alpha <= 0.0 || target_alpha >= 1.0) {
        return 0;
    }

    raw = (double)dataset_size / target_alpha;
    if (raw > (double)SIZE_MAX) {
        return 0;
    } /* A zero capacity signals overflow to the caller. */

    rounded = (size_t)raw;
    if ((double)rounded < raw) {
        rounded++;
    }

    return bench_next_pow2(rounded);
}

static int bench_kind_is_valid(
    bench_kind kind
) {
    switch (kind) {
    case BENCH_INSERT_BUILD:
    case BENCH_LOOKUP_HIT:
    case BENCH_LOOKUP_MISS:
    case BENCH_ERASE_EXISTING:
    case BENCH_WORKLOAD:
    case BENCH_RESIZE_BUILD:
    case BENCH_RESIZE_LOOKUP_HIT:
    case BENCH_RESIZE_LOOKUP_MISS:
    case BENCH_RESIZE_ERASE_EXISTING:
    case BENCH_RESIZE_WORKLOAD:
    case BENCH_CONCURRENT_LOOKUP:
    case BENCH_CONCURRENT_WORKLOAD:
        return 1;
    default:
        return 0;
    }
}

static int bench_kind_is_resize(
    bench_kind kind
) {
    return (kind >= BENCH_RESIZE_BUILD &&
            kind <= BENCH_RESIZE_WORKLOAD);
}

static int bench_kind_is_concurrent(
    bench_kind kind
) {
    return (kind == BENCH_CONCURRENT_LOOKUP ||
            kind == BENCH_CONCURRENT_WORKLOAD);
}

static int bench_impl_is_valid(
    ht_impl impl_kind
) {
    return bench_impl_is_known(impl_kind);
}

static int bench_capacity_mode_is_valid(
    bench_capacity_mode capacity_mode
) {
    return (capacity_mode >= BENCH_CAPACITY_FIXED &&
            capacity_mode <= BENCH_CAPACITY_RESIZING);
}

static int bench_resize_mode_is_valid(
    bench_resize_mode resize_mode
) {
    return (resize_mode >= BENCH_RESIZE_DISABLED &&
            resize_mode <= BENCH_RESIZE_IMPL_DEFAULT);
}

static int bench_alpha_is_valid(
    double alpha
) {
    return (alpha > 0.0 && alpha < 1.0);
}
