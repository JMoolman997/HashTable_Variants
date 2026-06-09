/**
 * @file    bench_plan.c
 * @brief   Normalization and validation for benchmark execution plans.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "bench_plan.h"
#include "ht_registry.h"

static size_t bench_next_pow2(
    size_t x
);

static size_t bench_derive_capacity(
    size_t dataset_size,
    double target_alpha
);

static int bench_alpha_is_valid(
    double alpha
);

static int bench_resize_mode_is_valid(
    bench_resize_mode resize_mode
);

static int bench_stats_mode_is_valid(
    bench_stats_mode stats_mode
);

static int bench_output_format_is_valid(
    bench_output_format output_format
);

static int bench_workload_is_valid(
    workload_kind workload
);

static int bench_keyspace_is_valid(
    bench_keyspace_mode keyspace_mode
);

static int bench_plan_validate_option_set(
    const bench_plan *plan
);

static int bench_plan_validate_numeric_values(
    const bench_plan *plan
);

static int bench_plan_validate_selected_impl(
    const bench_plan *plan
);

static int bench_plan_validate_modes(
    const bench_plan *plan
);

static int bench_plan_validate_trace_seed(
    const bench_plan *plan
);

static int bench_plan_normalize(
    bench_plan *plan
);

static int bench_plan_validate_resize_policy(
    const bench_plan *plan
);

static int bench_plan_validate_scenario(
    const bench_plan *plan
);

static int bench_plan_validate_backend(
    const bench_plan *plan
);

static int bench_plan_validate_pthread(
    const bench_plan *plan
);

static size_t bench_live_count_for_plan(
    const bench_plan *plan
);

int bench_plan_validate(
    bench_plan *plan
) {
    if (plan == NULL || plan->bench_case == NULL) {
        fprintf(stderr, "Missing benchmark plan\n");
        return -1;
    }

    if (bench_plan_validate_option_set(plan) != 0 ||
        bench_plan_validate_numeric_values(plan) != 0 ||
        bench_plan_validate_selected_impl(plan) != 0 ||
        bench_plan_validate_modes(plan) != 0 ||
        bench_plan_validate_trace_seed(plan) != 0) {
        return -1;
    }

    if (bench_plan_normalize(plan) != 0 ||
        bench_plan_validate_resize_policy(plan) != 0 ||
        bench_plan_validate_scenario(plan) != 0) {
        return -1;
    }

    return bench_plan_validate_backend(plan);
}

static int bench_plan_validate_option_set(
    const bench_plan *plan
) {
    uint64_t invalid_options;
    uint64_t missing_options;

    invalid_options =
        plan->set_options & ~plan->bench_case->allowed_options;
    if (invalid_options != 0) {
        fprintf(
            stderr,
            "%s is not valid for benchmark %s\n",
            bench_option_set_first_name(invalid_options),
            plan->benchmark_name
        );
        return -1;
    }

    missing_options =
        plan->bench_case->required_options & ~plan->set_options;
    if (missing_options != 0) {
        fprintf(
            stderr,
            "%s is required for benchmark %s\n",
            bench_option_set_first_name(missing_options),
            plan->benchmark_name
        );
        return -1;
    }

    return 0;
}

static int bench_plan_validate_numeric_values(
    const bench_plan *plan
) {
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
    if (!bench_alpha_is_valid(plan->target_alpha)) {
        fprintf(stderr, "--alpha must be greater than 0 and less than 1\n");
        return -1;
    }

    return 0;
}

static int bench_plan_validate_selected_impl(
    const bench_plan *plan
) {
    if (ht_registry_find(plan->impl_kind) == NULL) {
        fprintf(stderr, "Invalid implementation kind\n");
        return -1;
    }

    return 0;
}

static int bench_plan_validate_modes(
    const bench_plan *plan
) {
    if (!bench_resize_mode_is_valid(plan->resize_mode) ||
        !bench_resize_mode_is_valid(plan->concurrent_resize_mode)) {
        fprintf(stderr, "Invalid resize mode\n");
        return -1;
    }
    if (!bench_stats_mode_is_valid(plan->stats_mode)) {
        fprintf(stderr, "Invalid stats mode\n");
        return -1;
    }
    if (!bench_output_format_is_valid(plan->output_format)) {
        fprintf(stderr, "Invalid output format\n");
        return -1;
    }
    if (!bench_workload_is_valid(plan->workload)) {
        fprintf(stderr, "Invalid workload kind\n");
        return -1;
    }
    if (!bench_keyspace_is_valid(plan->keyspace_mode)) {
        fprintf(stderr, "Invalid concurrent keyspace mode\n");
        return -1;
    }

    return 0;
}

static int bench_plan_validate_trace_seed(
    const bench_plan *plan
) {
    if ((plan->bench_case->flags & BENCH_CASE_REQUIRES_TRACE_SEED) != 0 &&
        plan->trace_seed == 0) {
        fprintf(stderr, "--trace-seed must be provided for workload benchmarks\n");
        return -1;
    }

    return 0;
}

static int bench_plan_validate_resize_policy(
    const bench_plan *plan
) {
    if (plan->capacity_policy != BENCH_CAP_RESIZING) {
        return 0;
    }

    if (plan->backend == BENCH_BACKEND_SINGLE &&
        plan->resize_mode == BENCH_RESIZE_DISABLED) {
        fprintf(stderr, "--resize disabled is not valid for resize benchmarks\n");
        return -1;
    }
    if (!bench_alpha_is_valid(plan->grow_alpha)) {
        fprintf(stderr, "--grow-alpha must be greater than 0 and less than 1\n");
        return -1;
    }
    if (plan->resize_mode == BENCH_RESIZE_GROW_SHRINK &&
        !bench_alpha_is_valid(plan->shrink_alpha)) {
        fprintf(stderr, "--shrink-alpha must be greater than 0 and less than 1\n");
        return -1;
    }
    if (plan->resize_mode == BENCH_RESIZE_GROW_SHRINK &&
        plan->shrink_alpha >= plan->grow_alpha) {
        fprintf(stderr, "--shrink-alpha must be less than --grow-alpha\n");
        return -1;
    }

    return 0;
}

static int bench_plan_validate_scenario(
    const bench_plan *plan
) {
    size_t live_count;

    if (plan->scenario == BENCH_SCENARIO_INSERT_BUILD) {
        if (plan->timed_ops > plan->dataset_size ||
            plan->warmup_ops > plan->dataset_size) {
            fprintf(
                stderr,
                "insert-build operations must not exceed --dataset-size\n"
            );
            return -1;
        }
        if (plan->capacity_policy == BENCH_CAP_RESIZING &&
            plan->timed_ops != plan->dataset_size) {
            fprintf(stderr, "resize-build requires --timed-ops to match --dataset-size\n");
            return -1;
        }
    }

    if (plan->scenario == BENCH_SCENARIO_LOOKUP_HIT ||
        plan->scenario == BENCH_SCENARIO_LOOKUP_MISS ||
        plan->scenario == BENCH_SCENARIO_ERASE_EXISTING) {
        live_count = bench_live_count_for_plan(plan);
        if (live_count == 0) {
            fprintf(stderr, "%s benchmark requires a non-zero live key set\n",
                    plan->benchmark_name);
            return -1;
        }
        if (plan->scenario == BENCH_SCENARIO_ERASE_EXISTING &&
            (plan->timed_ops > live_count || plan->warmup_ops > live_count)) {
            fprintf(
                stderr,
                "erase-existing operations must not exceed initial live size\n"
            );
            return -1;
        }
    }

    if (plan->scenario == BENCH_SCENARIO_WORKLOAD) {
        if (plan->warmup_ops > plan->timed_ops) {
            fprintf(stderr, "--warmup-ops must be less than or equal to --timed-ops\n");
            return -1;
        }
        if (plan->capacity_policy == BENCH_CAP_RESIZING &&
            plan->backend == BENCH_BACKEND_SINGLE) {
            size_t prefill_live;

            if (!bench_alpha_is_valid(plan->prefill_alpha)) {
                fprintf(stderr, "--prefill-alpha must be greater than 0 and less than 1\n");
                return -1;
            }
            prefill_live =
                (size_t)(plan->prefill_alpha * (double)plan->initial_capacity);
            if (prefill_live == 0) {
                fprintf(stderr, "--prefill-alpha must produce at least one live key\n");
                return -1;
            }
        }
    }

    return 0;
}

static int bench_plan_validate_backend(
    const bench_plan *plan
) {
    if (plan->backend == BENCH_BACKEND_SINGLE) {
        if (plan->thread_count != 1) {
            fprintf(stderr, "Single-thread benchmarks require thread-count 1\n");
            return -1;
        }
        return 0;
    }
    if (plan->backend == BENCH_BACKEND_PTHREAD) {
        return bench_plan_validate_pthread(plan);
    }

    fprintf(stderr, "Invalid benchmark backend\n");
    return -1;
}

static int bench_plan_validate_pthread(
    const bench_plan *plan
) {
    if (plan->thread_count == 0) {
        fprintf(stderr, "--thread-count must be greater than zero\n");
        return -1;
    }
    if (plan->thread_count > plan->timed_ops) {
        fprintf(stderr, "--thread-count cannot exceed --timed-ops\n");
        return -1;
    }
    if (plan->scenario == BENCH_SCENARIO_LOOKUP_HIT &&
        plan->keyspace_mode != BENCH_KEYSPACE_SHARED_READ) {
        fprintf(stderr, "concurrent-lookup always uses shared-read keyspace\n");
        return -1;
    }
    if (plan->scenario == BENCH_SCENARIO_WORKLOAD) {
        if (plan->keyspace_mode != BENCH_KEYSPACE_DISJOINT) {
            fprintf(stderr, "concurrent-workload keyspace must be disjoint\n");
            return -1;
        }
        if (plan->dataset_size < plan->thread_count) {
            fprintf(
                stderr,
                "concurrent-workload requires --dataset-size >= --thread-count\n"
            );
            return -1;
        }
    }
    if (plan->concurrent_resize_mode != BENCH_RESIZE_DISABLED) {
        if (plan->scenario != BENCH_SCENARIO_WORKLOAD) {
            fprintf(stderr, "Concurrent resize is only valid for concurrent-workload\n");
            return -1;
        }
        if (plan->concurrent_resize_mode != BENCH_RESIZE_GROW_ONLY) {
            fprintf(stderr, "Concurrent resize currently supports grow-only\n");
            return -1;
        }
        if (!bench_impl_supports_concurrent_mutation(plan->impl_kind) ||
            !bench_impl_supports_concurrent_resize(plan->impl_kind)) {
            fprintf(stderr,
                    "Selected implementation does not support concurrent resize\n");
            return -1;
        }
    }
    if ((plan->set_options & BENCH_OPTBIT_GROW_ALPHA) != 0 &&
        plan->concurrent_resize_mode == BENCH_RESIZE_DISABLED) {
        fprintf(
            stderr,
            "--grow-alpha for concurrent benchmarks requires --concurrent-resize grow-only\n"
        );
        return -1;
    }

    return 0;
}

static int bench_plan_normalize(
    bench_plan *plan
) {
    if ((plan->set_options & BENCH_OPTBIT_RESIZE) == 0) {
        plan->resize_mode = plan->bench_case->default_resize_mode;
    }
    if ((plan->set_options & BENCH_OPTBIT_CONCURRENT_KEYSPACE) == 0) {
        plan->keyspace_mode = plan->bench_case->default_keyspace_mode;
    }
    if (plan->backend == BENCH_BACKEND_PTHREAD &&
        plan->scenario == BENCH_SCENARIO_LOOKUP_HIT &&
        (plan->set_options & BENCH_OPTBIT_TRACE_SEED) == 0) {
        plan->trace_seed = plan->key_seed;
    }
    if (plan->backend == BENCH_BACKEND_PTHREAD &&
        plan->scenario == BENCH_SCENARIO_LOOKUP_HIT) {
        plan->workload = WORKLOAD_READ_ONLY;
    }
    if (plan->backend == BENCH_BACKEND_PTHREAD &&
        plan->concurrent_resize_mode != BENCH_RESIZE_DISABLED) {
        plan->capacity_policy = BENCH_CAP_RESIZING;
    }

    plan->planned_capacity = bench_derive_capacity(
        plan->dataset_size,
        plan->target_alpha
    );
    if (plan->planned_capacity == 0) {
        fprintf(stderr, "Derived capacity is too large\n");
        return -1;
    }
    if (plan->initial_capacity == 0) {
        plan->initial_capacity = plan->planned_capacity;
    }
    if (plan->initial_capacity == 0) {
        fprintf(stderr, "Initial capacity must be greater than zero\n");
        return -1;
    }

    if (plan->capacity_policy == BENCH_CAP_RESIZING) {
        if ((plan->set_options & BENCH_OPTBIT_GROW_ALPHA) == 0) {
            plan->grow_alpha = plan->target_alpha;
        }
        if ((plan->set_options & BENCH_OPTBIT_SHRINK_ALPHA) == 0) {
            plan->shrink_alpha = plan->target_alpha * 0.5;
        }
        if (plan->scenario == BENCH_SCENARIO_WORKLOAD &&
            (plan->set_options & BENCH_OPTBIT_PREFILL_ALPHA) == 0) {
            plan->prefill_alpha = plan->target_alpha;
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
    }

    return x + 1;
}

static size_t bench_derive_capacity(
    size_t dataset_size,
    double target_alpha
) {
    double raw;
    size_t rounded;

    if (dataset_size == 0 || !bench_alpha_is_valid(target_alpha)) {
        return 0;
    }

    raw = (double)dataset_size / target_alpha;
    if (raw > (double)SIZE_MAX) {
        return 0;
    }

    rounded = (size_t)raw;
    if ((double)rounded < raw) {
        rounded++;
    }

    return bench_next_pow2(rounded);
}

static int bench_alpha_is_valid(
    double alpha
) {
    return isfinite(alpha) && alpha > 0.0 && alpha < 1.0;
}

static int bench_resize_mode_is_valid(
    bench_resize_mode resize_mode
) {
    return resize_mode >= BENCH_RESIZE_DISABLED &&
           resize_mode <= BENCH_RESIZE_GROW_SHRINK;
}

static int bench_stats_mode_is_valid(
    bench_stats_mode stats_mode
) {
    return stats_mode == BENCH_STATS_OFF || stats_mode == BENCH_STATS_ON;
}

static int bench_output_format_is_valid(
    bench_output_format output_format
) {
    return output_format == BENCH_FORMAT_TEXT ||
           output_format == BENCH_FORMAT_CSV;
}

static int bench_workload_is_valid(
    workload_kind workload
) {
    return workload >= WORKLOAD_READ_ONLY && workload <= WORKLOAD_BALANCED;
}

static int bench_keyspace_is_valid(
    bench_keyspace_mode keyspace_mode
) {
    return keyspace_mode == BENCH_KEYSPACE_DISJOINT ||
           keyspace_mode == BENCH_KEYSPACE_SHARED_READ;
}

static size_t bench_live_count_for_plan(
    const bench_plan *plan
) {
    if (plan == NULL) {
        return 0;
    }

    return (size_t)(plan->target_alpha * (double)plan->planned_capacity);
}
