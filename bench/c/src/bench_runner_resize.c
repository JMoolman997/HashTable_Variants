/**
 * @file    bench_runner_resize.c
 * @brief   Resize-aware benchmark runners.
 *
 * Implements the resize benchmark family as separate runners so resize
 * behavior can be analyzed without changing the steady-state path.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_dataset.h"
#include "bench_runner_resize.h"
#include "bench_trace.h"

static int bench_run_resize_timed_benchmark(
    const bench_plan   *plan,
    const ht_key_t     *prepopulate_keys,
    size_t              prepopulate_count,
    bench_public_step_fn warmup_step,
    const void         *warmup_ctx,
    bench_timed_step_fn timed_step,
    const void         *timed_ctx,
    bench_run_result   *result
);

static void bench_resize_fill_metrics(
    const bench_plan          *plan,
    const bench_result        *core,
    const ht_stats            *stats,
    bench_resize_result       *resize
);

int bench_run_resize_build(
    const bench_plan *plan,
    bench_run_result *result
) {
    ht_key_t *keys = NULL;
    bench_env env = {0};
    int rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (bench_dataset_generate_unique_keys(
            plan->timed_ops,
            plan->key_seed,
            &keys
        ) != 0) {
        fprintf(stderr, "Failed to generate resize-build benchmark dataset\n");
        goto cleanup;
    } /* Resize-build times insertion of the measured key stream only. */

    if (bench_env_init(plan, &env) != 0) {
        goto cleanup;
    }

    if (bench_run_timed_repetition(
            &env,
            plan,
            0,
            bench_step_key_array_timed,
            &(bench_key_step_ctx) {
                .keys = keys,
                .op = BENCH_KEY_OP_INSERT,
                .error_msg = "Resize-build benchmark failed during timed loop"
            },
            &result->core
        ) != 0) {
        goto cleanup;
    }

    bench_resize_fill_metrics(
        plan,
        &result->core,
        &env.stats,
        &result->resize
    );
    rc = 0;

cleanup:
    bench_env_destroy(&env);
    free(keys);
    return rc;
}

int bench_run_resize_lookup_hit(
    const bench_plan *plan,
    bench_run_result *result
) {
    ht_key_t *lookup_keys = NULL;
    bench_lookup_ctx lookup_ctx;
    size_t initial_live = 0;
    int rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (bench_generate_populated_dataset(
            plan,
            "resize lookup-hit",
            &initial_live,
            &lookup_keys
        ) != 0) {
        goto cleanup;
    }

    lookup_ctx.keys = lookup_keys;
    lookup_ctx.key_count = initial_live;
    lookup_ctx.expected_result = HT_OK;
    lookup_ctx.bench_name = "resize-lookup-hit";
    /* The helper uses this context for both warm-up and timed queries. */

    rc = bench_run_resize_timed_benchmark(
        plan,
        lookup_keys,
        initial_live,
        bench_step_lookup_warmup,
        &lookup_ctx,
        bench_step_lookup,
        &lookup_ctx,
        result
    );

cleanup:
    free(lookup_keys);
    return rc;
}

int bench_run_resize_lookup_miss(
    const bench_plan *plan,
    bench_run_result *result
) {
    ht_key_t *existing_keys = NULL;
    ht_key_t *missing_keys = NULL;
    bench_lookup_ctx lookup_ctx;
    size_t initial_live = 0;
    int rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (bench_generate_populated_dataset(
            plan,
            "resize lookup-miss",
            &initial_live,
            &existing_keys
        ) != 0) {
        goto cleanup;
    }

    if (bench_dataset_generate_missing_keys(
            existing_keys,
            initial_live,
            plan->key_seed,
            initial_live,
            &missing_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate resize missing lookup keys\n");
        goto cleanup;
    }

    lookup_ctx.keys = missing_keys;
    lookup_ctx.key_count = initial_live;
    lookup_ctx.expected_result = HT_ERR_NOT_FOUND;
    lookup_ctx.bench_name = "resize-lookup-miss";
    /* Miss queries run against the populated table without changing it. */

    rc = bench_run_resize_timed_benchmark(
        plan,
        existing_keys,
        initial_live,
        bench_step_lookup_warmup,
        &lookup_ctx,
        bench_step_lookup,
        &lookup_ctx,
        result
    );

cleanup:
    free(existing_keys);
    free(missing_keys);
    return rc;
}

int bench_run_resize_erase_existing(
    const bench_plan *plan,
    bench_run_result *result
) {
    ht_key_t *existing_keys = NULL;
    ht_key_t *deletion_keys = NULL;
    bench_key_step_ctx erase_ctx;
    size_t initial_live = 0;
    int rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (bench_generate_populated_dataset(
            plan,
            "resize erase-existing",
            &initial_live,
            &existing_keys
        ) != 0) {
        goto cleanup;
    }

    if (plan->timed_ops > initial_live) {
        fprintf(stderr, "--timed-ops must be less than or equal to initial live size\n");
        goto cleanup;
    }

    if (plan->warmup_ops > initial_live) {
        fprintf(stderr, "--warmup-ops must be less than or equal to initial live size\n");
        goto cleanup;
    }

    if (bench_dataset_shuffle_keys(
            existing_keys,
            initial_live,
            plan->key_seed,
            &deletion_keys
        ) != 0) {
        fprintf(stderr, "Failed to build resize deletion order\n");
        goto cleanup;
    }

    erase_ctx.keys = deletion_keys;
    erase_ctx.op = BENCH_KEY_OP_REMOVE;
    erase_ctx.error_msg = "Resize erase-existing benchmark failed";
    /* The same deletion order is replayed for warm-up and timing. */

    rc = bench_run_resize_timed_benchmark(
        plan,
        existing_keys,
        initial_live,
        bench_step_key_array_warmup,
        &erase_ctx,
        bench_step_key_array_timed,
        &erase_ctx,
        result
    );

cleanup:
    free(existing_keys);
    free(deletion_keys);
    return rc;
}

int bench_run_resize_workload(
    const bench_plan *plan,
    bench_run_result *result
) {
    ht_key_t *prefill_keys = NULL;
    bench_op *ops = NULL;
    bench_env env = {0};
    size_t prefill_live = 0;
    uint64_t elapsed_ns = 0;
    int rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (plan->timed_ops == 0) {
        fprintf(stderr, "--timed-ops must be greater than zero\n");
        return -1;
    }

    if (plan->warmup_ops > plan->timed_ops) {
        fprintf(
            stderr,
            "--warmup-ops must be less than or equal to --timed-ops\n"
        );
        return -1;
    }

    if (plan->dataset_size == 0) {
        fprintf(stderr, "--dataset-size must be greater than zero\n");
        return -1;
    }

    if (plan->initial_capacity == 0) {
        fprintf(stderr, "Initial capacity must be greater than zero\n");
        return -1;
    }

    if (plan->trace_seed == 0) {
        fprintf(stderr, "--trace-seed must be provided for workload benchmarks\n");
        return -1;
    }

    if (plan->workload < WORKLOAD_READ_ONLY ||
        plan->workload > WORKLOAD_BALANCED) {
        fprintf(stderr, "Invalid workload kind\n");
        return -1;
    }

    if (bench_dataset_generate_populated_keys(
            plan->initial_capacity,
            plan->prefill_alpha,
            plan->key_seed,
            &prefill_live,
            &prefill_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate resize-workload prefill dataset\n");
        goto cleanup;
    } /* Prefill density controls where resizing pressure begins. */

    if (bench_trace_build_workload(
            plan->workload,
            plan->timed_ops,
            plan->trace_seed,
            prefill_keys,
            prefill_live,
            &ops
        ) != 0) {
        fprintf(stderr, "Failed to build resize-workload trace\n");
        goto cleanup;
    }

    if (bench_env_init(plan, &env) != 0) {
        goto cleanup;
    }

    if (bench_prepopulate_table(
            env.map,
            prefill_keys,
            prefill_live
        ) != 0) {
        goto cleanup;
    }

    if (bench_run_warmup(
            plan,
            prefill_keys,
            prefill_live,
            bench_step_workload_warmup,
            &(bench_workload_step_ctx) {
                .ops = ops,
                .error_msg = "Warm-up failed during resize-workload benchmark"
            }
        ) != 0) {
        goto cleanup;
    }

    if (bench_env_prepare_timed_phase(&env) != 0) {
        goto cleanup;
    }

    if (bench_measure_timed_loop(
            plan->timed_ops,
            bench_step_workload,
            &env.bi,
            &(bench_workload_step_ctx) {
                .ops = ops,
                .error_msg = "Resize-workload benchmark failed during timed loop"
            },
            &elapsed_ns
        ) != 0) {
        goto cleanup;
    }

    if (bench_env_collect_stats(&env) != 0) {
        goto cleanup;
    }

    bench_fill_result(
        plan,
        env.map,
        &env.stats,
        prefill_live,
        elapsed_ns,
        &result->core
    );
    bench_resize_fill_metrics(
        plan,
        &result->core,
        &env.stats,
        &result->resize
    );
    rc = 0;

cleanup:
    bench_env_destroy(&env);
    free(prefill_keys);
    bench_trace_free(ops);
    return rc;
}

static void bench_resize_fill_metrics(
    const bench_plan          *plan,
    const bench_result        *core,
    const ht_stats            *stats,
    bench_resize_result       *resize
) {
    size_t fallback_min;
    size_t fallback_max;

    if (plan == NULL || core == NULL || stats == NULL || resize == NULL) {
        return;
    }

    memset(resize, 0, sizeof(*resize));
    fallback_min = (plan->initial_capacity < core->final_capacity)
        ? plan->initial_capacity
        : core->final_capacity;
    fallback_max = (plan->initial_capacity > core->final_capacity)
        ? plan->initial_capacity
        : core->final_capacity;
    /* Fallback bounds still describe capacity movement without instrumentation. */

#if HT_ENABLE_RESIZE_INSTRUMENTATION
    resize->resize_events = stats->resize_count;
    resize->grow_events   = stats->grow_count;
    resize->shrink_events = stats->shrink_count;
    resize->resize_entries_moved = stats->resize_entries_moved;
    resize->total_resize_ns = stats->resize_total_ns;
    resize->max_resize_ns = stats->resize_max_ns;
    resize->min_capacity_seen = (stats->resize_min_capacity != 0)
        ? stats->resize_min_capacity
        : fallback_min;
    resize->max_capacity_seen = (stats->resize_max_capacity != 0)
        ? stats->resize_max_capacity
        : fallback_max;
#else
    resize->min_capacity_seen = fallback_min;
    resize->max_capacity_seen = fallback_max;
#endif
}

static int bench_run_resize_timed_benchmark(
    const bench_plan   *plan,
    const ht_key_t     *prepopulate_keys,
    size_t              prepopulate_count,
    bench_public_step_fn warmup_step,
    const void         *warmup_ctx,
    bench_timed_step_fn timed_step,
    const void         *timed_ctx,
    bench_run_result   *result
) {
    bench_env env = {0};
    int rc = -1;

    if (plan == NULL || result == NULL ||
        warmup_step == NULL || timed_step == NULL) {
        return -1;
    }

    if (prepopulate_count > 0 && prepopulate_keys == NULL) {
        return -1;
    } /* Non-empty setup phases need a concrete key array to preload. */

    if (bench_env_init(plan, &env) != 0) {
        goto cleanup;
    }

    if (bench_prepopulate_table(
            env.map,
            prepopulate_keys,
            prepopulate_count
        ) != 0) {
        goto cleanup;
    }

    if (bench_run_warmup(
            plan,
            prepopulate_keys,
            prepopulate_count,
            warmup_step,
            warmup_ctx
        ) != 0) {
        goto cleanup;
    }

    if (bench_run_timed_repetition(
            &env,
            plan,
            prepopulate_count,
            timed_step,
            timed_ctx,
            &result->core
        ) != 0) {
        goto cleanup;
    }

    bench_resize_fill_metrics(
        plan,
        &result->core,
        &env.stats,
        &result->resize
    );
    rc = 0;

cleanup:
    bench_env_destroy(&env);
    return rc;
}
