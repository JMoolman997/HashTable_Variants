/**
 * @file    bench_runner_steady.c
 * @brief   Legacy single-thread benchmark runners.
 *
 * Preserves the current steady-state benchmark behavior and CSV schema while
 * reading from a normalized execution plan rather than the raw CLI spec.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bench_dataset.h"
#include "bench_runner_steady.h"
#include "bench_trace.h"

/* --- function prototypes: helpers ---------------------------------------- */

static int bench_run_lookup_benchmark(
    bench_env           *env,
    const bench_plan    *plan,
    bench_result        *result,
    const ht_key_t      *prepopulate_keys,
    size_t               prepopulate_count,
    const ht_key_t      *query_keys,
    size_t               query_count,
    ht_result            expected_result,
    const char          *bench_name
);

int bench_run_insert_build(
    const bench_plan *plan,
    bench_result     *result
) {
    ht_key_t *keys = NULL;
    bench_env env = {0};
    int       rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (plan->timed_ops > plan->dataset_size) {
        fprintf(
            stderr,
            "--timed-ops must be less than or equal to --dataset-size\n"
        );
        return -1;
    } /* Build benchmarks insert each generated key at most once. */

    if (plan->warmup_ops > plan->dataset_size) {
        fprintf(
            stderr,
            "--warmup-ops must be less than or equal to --dataset-size\n"
        );
        return -1;
    }

    if (bench_dataset_generate_unique_keys(
            plan->dataset_size,
            plan->key_seed,
            &keys
        ) != 0) {
        fprintf(stderr, "Failed to generate benchmark dataset\n");
        goto cleanup;
    }

    if (bench_env_init(plan, &env) != 0) {
        goto cleanup;
    }

    if (bench_run_warmup(
            plan,
            NULL,
            0,
            bench_step_key_array_warmup,
            &(bench_key_step_ctx) {
                .keys = keys,
                .op = BENCH_KEY_OP_INSERT,
                .error_msg =
                    "Warm-up failed during insert-build benchmark"
            }) != 0) {
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
                .error_msg = "Insert-build benchmark failed during timed loop"
            },
            result
        ) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    bench_env_destroy(&env);
    free(keys);
    return rc;
}

int bench_run_lookup_hit(
    const bench_plan *plan,
    bench_result     *result
) {
    ht_key_t *lookup_keys = NULL;
    size_t    initial_live;
    bench_env env = {0};
    int       rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (bench_generate_populated_dataset(
            plan,
            "lookup-hit",
            &initial_live,
            &lookup_keys
        ) != 0) {
        goto cleanup;
    } /* Hit lookups query exactly the keys used to populate the table. */

    if (bench_env_init(plan, &env) != 0) {
        goto cleanup;
    }

    if (bench_run_lookup_benchmark(
            &env,
            plan,
            result,
            lookup_keys,
            initial_live,
            lookup_keys,
            initial_live,
            HT_OK,
            "lookup-hit"
        ) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    bench_env_destroy(&env);
    free(lookup_keys);
    return rc;
}

int bench_run_lookup_miss(
    const bench_plan *plan,
    bench_result     *result
) {
    ht_key_t *existing_keys = NULL;
    ht_key_t *missing_keys = NULL;
    size_t    initial_live;
    bench_env env = {0};
    int       rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (bench_generate_populated_dataset(
            plan,
            "lookup-miss",
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
        fprintf(stderr, "Failed to generate missing lookup keys\n");
        goto cleanup;
    } /* Miss lookups must avoid every preloaded key. */

    if (bench_env_init(plan, &env) != 0) {
        goto cleanup;
    }

    if (bench_run_lookup_benchmark(
            &env,
            plan,
            result,
            existing_keys,
            initial_live,
            missing_keys,
            initial_live,
            HT_ERR_NOT_FOUND,
            "lookup-miss"
        ) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    bench_env_destroy(&env);
    free(existing_keys);
    free(missing_keys);
    return rc;
}

int bench_run_erase_existing(
    const bench_plan *plan,
    bench_result     *result
) {
    ht_key_t *existing_keys = NULL;
    ht_key_t *deletion_keys = NULL;
    size_t    initial_live;
    bench_env env = {0};
    int       rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (bench_generate_populated_dataset(
            plan,
            "erase-existing",
            &initial_live,
            &existing_keys
        ) != 0) {
        goto cleanup;
    }

    if (plan->timed_ops > initial_live) {
        fprintf(
            stderr,
            "--timed-ops must be less than or equal to initial live size\n"
        );
        goto cleanup;
    }

    if (plan->warmup_ops > initial_live) {
        fprintf(
            stderr,
            "--warmup-ops must be less than or equal to initial live size\n"
        );
        goto cleanup;
    }

    if (bench_dataset_shuffle_keys(
            existing_keys,
            initial_live,
            plan->key_seed,
            &deletion_keys
        ) != 0) {
        fprintf(stderr, "Failed to build deletion order\n");
        goto cleanup;
    } /* Deletions reuse the live set in deterministic shuffled order. */

    if (bench_env_init(plan, &env) != 0) {
        goto cleanup;
    }

    if (bench_prepopulate_table(env.map, existing_keys, initial_live) != 0) {
        goto cleanup;
    }

    if (bench_run_warmup(
            plan,
            existing_keys,
            initial_live,
            bench_step_key_array_warmup,
            &(bench_key_step_ctx) {
                .keys = deletion_keys,
                .op = BENCH_KEY_OP_REMOVE,
                .error_msg =
                    "Warm-up failed during erase-existing benchmark"
            }) != 0) {
        goto cleanup;
    }

    if (bench_run_timed_repetition(
            &env,
            plan,
            initial_live,
            bench_step_key_array_timed,
            &(bench_key_step_ctx) {
                .keys = deletion_keys,
                .op = BENCH_KEY_OP_REMOVE,
                .error_msg =
                    "Erase-existing benchmark failed during timed loop"
            },
            result
        ) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    bench_env_destroy(&env);
    free(existing_keys);
    free(deletion_keys);
    return rc;
}

int bench_run_workload(
    const bench_plan *plan,
    bench_result     *result
) {
    ht_key_t *initial_keys = NULL;
    bench_op *ops = NULL;
    size_t    initial_live;
    bench_env env = {0};
    int       rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (plan->warmup_ops > plan->timed_ops) {
        fprintf(
            stderr,
            "--warmup-ops must be less than or equal to --timed-ops\n"
        );
        return -1;
    }

    initial_live = plan->dataset_size;

    if (bench_dataset_generate_unique_keys(
            initial_live,
            plan->key_seed,
            &initial_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate workload dataset\n");
        goto cleanup;
    }

    if (initial_live == 0) {
        fprintf(stderr, "Workload benchmark requires a non-zero dataset\n");
        goto cleanup;
    }

    if (bench_trace_build_workload(
            plan->workload,
            plan->timed_ops,
            plan->trace_seed,
            initial_keys,
            initial_live,
            &ops
        ) != 0) {
        fprintf(stderr, "Failed to build workload trace\n");
        goto cleanup;
    } /* Workload decisions are precomputed outside the timed loop. */

    if (bench_env_init(plan, &env) != 0) {
        goto cleanup;
    }

    if (bench_prepopulate_table(env.map, initial_keys, initial_live) != 0) {
        goto cleanup;
    }

    if (bench_run_warmup(
            plan,
            initial_keys,
            initial_live,
            bench_step_workload_warmup,
            &(bench_workload_step_ctx) {
                .ops = ops,
                .error_msg = "Warm-up failed during workload benchmark"
            }
        ) != 0) {
        goto cleanup;
    }

    if (bench_run_timed_repetition(
            &env,
            plan,
            initial_live,
            bench_step_workload,
            &(bench_workload_step_ctx) {
                .ops = ops,
                .error_msg = "Workload benchmark failed during timed loop"
            },
            result
        ) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    bench_env_destroy(&env);
    free(initial_keys);
    bench_trace_free(ops);
    return rc;
}

static int bench_run_lookup_benchmark(
    bench_env           *env,
    const bench_plan    *plan,
    bench_result        *result,
    const ht_key_t      *prepopulate_keys,
    size_t               prepopulate_count,
    const ht_key_t      *query_keys,
    size_t               query_count,
    ht_result            expected_result,
    const char          *bench_name
) {
    if (env == NULL || plan == NULL || result == NULL) {
        return -1;
    }

    if (prepopulate_keys == NULL || query_keys == NULL) {
        return -1;
    }

    if (bench_prepopulate_table(env->map, prepopulate_keys, prepopulate_count)
        != 0) {
        return -1;
    } /* Lookup benchmarks time queries, not table construction. */

    if (bench_run_warmup(
            plan,
            prepopulate_keys,
            prepopulate_count,
            bench_step_lookup_warmup,
            &(bench_lookup_ctx) {
                .keys = query_keys,
                .key_count = query_count,
                .expected_result = expected_result,
                .bench_name = bench_name
            }
        ) != 0) {
        return -1;
    }

    return bench_run_timed_repetition(
        env,
        plan,
        prepopulate_count,
        bench_step_lookup,
        &(bench_lookup_ctx) {
            .keys = query_keys,
            .key_count = query_count,
            .expected_result = expected_result,
            .bench_name = bench_name
        },
        result
    );
}
