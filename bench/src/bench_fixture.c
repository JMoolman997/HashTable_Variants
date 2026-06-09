/**
 * @file    bench_fixture.c
 * @brief   Generated benchmark data owned by one repetition.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_dataset.h"
#include "bench_fixture.h"

static int bench_fixture_build_insert_build(
    const bench_plan *plan,
    bench_fixture    *fixture
);

static int bench_fixture_build_lookup(
    const bench_plan *plan,
    bench_fixture    *fixture,
    int               hit
);

static int bench_fixture_build_erase_existing(
    const bench_plan *plan,
    bench_fixture    *fixture
);

static int bench_fixture_build_workload(
    const bench_plan *plan,
    bench_fixture    *fixture
);

static int bench_fixture_build_concurrent_lookup(
    const bench_plan *plan,
    bench_fixture    *fixture
);

static int bench_fixture_build_concurrent_workload(
    const bench_plan *plan,
    bench_fixture    *fixture
);

static int bench_fixture_generate_populated_keys(
    const bench_plan *plan,
    size_t           *live_out,
    ht_key_t        **keys_out
);

static int bench_fixture_build_ops_from_keys(
    bench_op_kind   kind,
    const ht_key_t *keys,
    size_t          key_count,
    size_t          op_count,
    bench_op      **ops_out
);

static int bench_fixture_build_phase_ops(
    const bench_plan *plan,
    bench_fixture    *fixture,
    bench_op_kind     kind,
    const ht_key_t   *keys,
    size_t            key_count
);

static size_t bench_fixture_max_size(
    size_t a,
    size_t b
);

static void bench_fixture_free_thread_ops(
    bench_op **ops,
    size_t     thread_count
);

int bench_fixture_build(
    const bench_plan *plan,
    bench_fixture    *fixture
) {
    int rc = -1;

    if (plan == NULL || fixture == NULL) {
        return -1;
    }

    memset(fixture, 0, sizeof(*fixture));
    fixture->thread_count = plan->thread_count;

    if (plan->backend == BENCH_BACKEND_PTHREAD) {
        if (plan->scenario == BENCH_SCENARIO_LOOKUP_HIT) {
            rc = bench_fixture_build_concurrent_lookup(plan, fixture);
        } else if (plan->scenario == BENCH_SCENARIO_WORKLOAD) {
            rc = bench_fixture_build_concurrent_workload(plan, fixture);
        }
    } else {
        switch (plan->scenario) {
        case BENCH_SCENARIO_INSERT_BUILD:
            rc = bench_fixture_build_insert_build(plan, fixture);
            break;
        case BENCH_SCENARIO_LOOKUP_HIT:
            rc = bench_fixture_build_lookup(plan, fixture, 1);
            break;
        case BENCH_SCENARIO_LOOKUP_MISS:
            rc = bench_fixture_build_lookup(plan, fixture, 0);
            break;
        case BENCH_SCENARIO_ERASE_EXISTING:
            rc = bench_fixture_build_erase_existing(plan, fixture);
            break;
        case BENCH_SCENARIO_WORKLOAD:
            rc = bench_fixture_build_workload(plan, fixture);
            break;
        }
    }

    if (rc != 0) {
        bench_fixture_destroy(fixture, plan);
    }
    return rc;
}

void bench_fixture_destroy(
    bench_fixture   *fixture,
    const bench_plan *plan
) {
    (void)plan;

    if (fixture == NULL) {
        return;
    }

    free(fixture->initial_keys);
    bench_trace_free(fixture->warmup_ops);
    bench_trace_free(fixture->timed_ops);
    bench_fixture_free_thread_ops(fixture->thread_ops, fixture->thread_count);
    bench_fixture_free_thread_ops(
        fixture->warmup_thread_ops,
        fixture->thread_count
    );
    free(fixture->thread_op_counts);
    free(fixture->warmup_thread_op_counts);
    memset(fixture, 0, sizeof(*fixture));
}

static int bench_fixture_build_insert_build(
    const bench_plan *plan,
    bench_fixture    *fixture
) {
    ht_key_t *keys = NULL;
    int rc = -1;

    if (bench_dataset_generate_unique_keys(
            plan->dataset_size,
            plan->key_seed,
            &keys
        ) != 0) {
        fprintf(stderr, "Failed to generate insert-build dataset\n");
        return -1;
    }

    if (bench_fixture_build_phase_ops(
            plan,
            fixture,
            OP_INSERT,
            keys,
            plan->dataset_size
        ) != 0) {
        fprintf(stderr, "Failed to build insert-build operation trace\n");
        goto cleanup;
    }

    rc = 0;

cleanup:
    free(keys);
    return rc;
}

static int bench_fixture_build_lookup(
    const bench_plan *plan,
    bench_fixture    *fixture,
    int               hit
) {
    ht_key_t *missing_keys = NULL;
    const ht_key_t *op_keys;
    size_t op_key_count;
    size_t missing_count;
    bench_op_kind kind;
    int rc = -1;

    if (bench_fixture_generate_populated_keys(
            plan,
            &fixture->initial_count,
            &fixture->initial_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate lookup dataset\n");
        return -1;
    }

    if (hit) {
        op_keys = fixture->initial_keys;
        op_key_count = fixture->initial_count;
        kind = OP_GET_HIT;
    } else {
        missing_count = bench_fixture_max_size(
            plan->timed_ops,
            plan->warmup_ops
        );
        if (bench_dataset_generate_missing_keys(
                fixture->initial_keys,
                fixture->initial_count,
                plan->key_seed,
                missing_count,
                &missing_keys
            ) != 0) {
            fprintf(stderr, "Failed to generate missing lookup keys\n");
            return -1;
        }
        op_keys = missing_keys;
        op_key_count = missing_count;
        kind = OP_GET_MISS;
    }

    if (bench_fixture_build_phase_ops(
            plan,
            fixture,
            kind,
            op_keys,
            op_key_count
        ) != 0) {
        fprintf(stderr, "Failed to build lookup operation trace\n");
        goto cleanup;
    }

    rc = 0;

cleanup:
    free(missing_keys);
    return rc;
}

static int bench_fixture_build_erase_existing(
    const bench_plan *plan,
    bench_fixture    *fixture
) {
    ht_key_t *delete_keys = NULL;
    int rc = -1;

    if (bench_fixture_generate_populated_keys(
            plan,
            &fixture->initial_count,
            &fixture->initial_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate erase-existing dataset\n");
        return -1;
    }

    if (bench_dataset_shuffle_keys(
            fixture->initial_keys,
            fixture->initial_count,
            plan->key_seed,
            &delete_keys
        ) != 0) {
        fprintf(stderr, "Failed to build deletion order\n");
        return -1;
    }

    if (bench_fixture_build_phase_ops(
            plan,
            fixture,
            OP_REMOVE,
            delete_keys,
            fixture->initial_count
        ) != 0) {
        fprintf(stderr, "Failed to build erase-existing operation trace\n");
        goto cleanup;
    }

    rc = 0;

cleanup:
    free(delete_keys);
    return rc;
}

static int bench_fixture_build_workload(
    const bench_plan *plan,
    bench_fixture    *fixture
) {
    if (plan->capacity_policy == BENCH_CAP_RESIZING) {
        if (bench_dataset_generate_populated_keys(
                plan->initial_capacity,
                plan->prefill_alpha,
                plan->key_seed,
                &fixture->initial_count,
                &fixture->initial_keys
            ) != 0) {
            fprintf(stderr, "Failed to generate resize-workload prefill dataset\n");
            return -1;
        }
    } else {
        fixture->initial_count = plan->dataset_size;
        if (bench_dataset_generate_unique_keys(
                fixture->initial_count,
                plan->key_seed,
                &fixture->initial_keys
            ) != 0) {
            fprintf(stderr, "Failed to generate workload dataset\n");
            return -1;
        }
    }

    if (fixture->initial_count == 0) {
        fprintf(stderr, "Workload benchmark requires a non-zero dataset\n");
        return -1;
    }

    if (bench_trace_build_workload(
            plan->workload,
            plan->warmup_ops,
            plan->trace_seed,
            fixture->initial_keys,
            fixture->initial_count,
            &fixture->warmup_ops
        ) != 0 ||
        bench_trace_build_workload(
            plan->workload,
            plan->timed_ops,
            plan->trace_seed,
            fixture->initial_keys,
            fixture->initial_count,
            &fixture->timed_ops
        ) != 0) {
        fprintf(stderr, "Failed to build workload operation trace\n");
        return -1;
    }

    fixture->warmup_op_count = plan->warmup_ops;
    fixture->timed_op_count = plan->timed_ops;
    return 0;
}

static int bench_fixture_build_concurrent_lookup(
    const bench_plan *plan,
    bench_fixture    *fixture
) {
    if (bench_fixture_generate_populated_keys(
            plan,
            &fixture->initial_count,
            &fixture->initial_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent lookup dataset\n");
        return -1;
    }

    if (plan->warmup_ops > 0 &&
        bench_trace_build_partitioned_workloads(
            WORKLOAD_READ_ONLY,
            plan->warmup_ops,
            plan->thread_count,
            plan->trace_seed,
            fixture->initial_keys,
            fixture->initial_count,
            NULL,
            0,
            BENCH_KEYSPACE_SHARED_READ,
            &fixture->warmup_thread_ops,
            &fixture->warmup_thread_op_counts
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent lookup warm-up traces\n");
        return -1;
    }

    if (bench_trace_build_partitioned_workloads(
            WORKLOAD_READ_ONLY,
            plan->timed_ops,
            plan->thread_count,
            plan->trace_seed,
            fixture->initial_keys,
            fixture->initial_count,
            NULL,
            0,
            BENCH_KEYSPACE_SHARED_READ,
            &fixture->thread_ops,
            &fixture->thread_op_counts
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent lookup traces\n");
        return -1;
    }

    return 0;
}

static int bench_fixture_build_concurrent_workload(
    const bench_plan *plan,
    bench_fixture    *fixture
) {
    ht_key_t *timed_insert_keys = NULL;
    ht_key_t *warmup_insert_keys = NULL;
    int rc = -1;

    if (bench_dataset_generate_unique_keys(
            plan->dataset_size,
            plan->key_seed,
            &fixture->initial_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent workload dataset\n");
        return -1;
    }
    fixture->initial_count = plan->dataset_size;

    if (bench_dataset_generate_missing_keys(
            fixture->initial_keys,
            fixture->initial_count,
            plan->trace_seed,
            plan->timed_ops,
            &timed_insert_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent workload insert keys\n");
        return -1;
    }

    if (plan->warmup_ops > 0 &&
        bench_dataset_generate_missing_keys(
            fixture->initial_keys,
            fixture->initial_count,
            plan->trace_seed,
            plan->warmup_ops,
            &warmup_insert_keys
        ) != 0) {
        fprintf(stderr,
                "Failed to generate concurrent workload warm-up insert keys\n");
        goto cleanup;
    }

    if (plan->warmup_ops > 0 &&
        bench_trace_build_partitioned_workloads(
            plan->workload,
            plan->warmup_ops,
            plan->thread_count,
            plan->trace_seed,
            fixture->initial_keys,
            fixture->initial_count,
            warmup_insert_keys,
            plan->warmup_ops,
            plan->keyspace_mode,
            &fixture->warmup_thread_ops,
            &fixture->warmup_thread_op_counts
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent workload warm-up traces\n");
        goto cleanup;
    }

    if (bench_trace_build_partitioned_workloads(
            plan->workload,
            plan->timed_ops,
            plan->thread_count,
            plan->trace_seed,
            fixture->initial_keys,
            fixture->initial_count,
            timed_insert_keys,
            plan->timed_ops,
            plan->keyspace_mode,
            &fixture->thread_ops,
            &fixture->thread_op_counts
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent workload traces\n");
        goto cleanup;
    }

    rc = 0;

cleanup:
    free(timed_insert_keys);
    free(warmup_insert_keys);
    return rc;
}

static int bench_fixture_generate_populated_keys(
    const bench_plan *plan,
    size_t           *live_out,
    ht_key_t        **keys_out
) {
    if (bench_dataset_generate_populated_keys(
            plan->planned_capacity,
            plan->target_alpha,
            plan->key_seed,
            live_out,
            keys_out
        ) != 0) {
        return -1;
    }

    return (*live_out > 0) ? 0 : -1;
}

static int bench_fixture_build_ops_from_keys(
    bench_op_kind   kind,
    const ht_key_t *keys,
    size_t          key_count,
    size_t          op_count,
    bench_op      **ops_out
) {
    bench_op *ops = NULL;
    size_t i;

    if (ops_out == NULL) {
        return -1;
    }
    *ops_out = NULL;

    if (op_count == 0) {
        return 0;
    }
    if (keys == NULL || key_count == 0 ||
        op_count > SIZE_MAX / sizeof(*ops)) {
        return -1;
    }

    ops = calloc(op_count, sizeof(*ops));
    if (ops == NULL) {
        return -1;
    }

    for (i = 0; i < op_count; i++) {
        ops[i].kind = kind;
        ops[i].key = keys[i % key_count];
        ops[i].value = keys[i % key_count];
    }

    *ops_out = ops;
    return 0;
}

static int bench_fixture_build_phase_ops(
    const bench_plan *plan,
    bench_fixture    *fixture,
    bench_op_kind     kind,
    const ht_key_t   *keys,
    size_t            key_count
) {
    if (bench_fixture_build_ops_from_keys(
            kind,
            keys,
            key_count,
            plan->warmup_ops,
            &fixture->warmup_ops
        ) != 0 ||
        bench_fixture_build_ops_from_keys(
            kind,
            keys,
            key_count,
            plan->timed_ops,
            &fixture->timed_ops
        ) != 0) {
        return -1;
    }

    fixture->warmup_op_count = plan->warmup_ops;
    fixture->timed_op_count = plan->timed_ops;
    return 0;
}

static size_t bench_fixture_max_size(
    size_t a,
    size_t b
) {
    return (a > b) ? a : b;
}

static void bench_fixture_free_thread_ops(
    bench_op **ops,
    size_t     thread_count
) {
    size_t i;

    if (ops == NULL) {
        return;
    }

    for (i = 0; i < thread_count; i++) {
        bench_trace_free(ops[i]);
    }
    free(ops);
}
