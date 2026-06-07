/**
 * @file    bench_runner_concurrent.c
 * @brief   Concurrent benchmark runners.
 *
 * Implements wall-clock concurrent lookup and workload benchmarks using
 * deterministic per-thread traces generated before the timed phase.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#include <pthread.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_dataset.h"
#include "bench_runner_concurrent.h"
#include "bench_time.h"

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    size_t          thread_count;
    size_t          ready_count;
    int             start;
    int             abort;
} bench_start_gate;

typedef struct {
    const bench_op    *ops;
    size_t             op_count;
    int                rc;
    const bench_iface *bi;
    bench_start_gate  *start_gate;
    pthread_mutex_t   *op_lock;
} bench_thread_ctx;

typedef struct {
    pthread_t       thread;
    bench_thread_ctx ctx;
} bench_worker;

typedef struct {
    bench_op **ops;
    size_t    *counts;
} bench_partitioned_traces;

static int bench_start_gate_init(
    bench_start_gate *gate,
    size_t            thread_count
);

static void bench_start_gate_destroy(
    bench_start_gate *gate
);

static int bench_start_gate_wait(
    bench_start_gate *gate
);

static int bench_start_gate_release(
    bench_start_gate *gate,
    uint64_t         *start_ns_out
);

static void bench_start_gate_abort(
    bench_start_gate *gate
);

static void *bench_concurrent_worker_main(
    void *arg
);

static int bench_run_concurrent_ops(
    const bench_plan *plan,
    const ht_key_t   *initial_keys,
    size_t            initial_live,
    bench_op        **warmup_ops_by_thread,
    const size_t     *warmup_op_counts,
    bench_op        **ops_by_thread,
    const size_t     *op_counts,
    int               use_op_lock,
    bench_run_result *result
);

static int bench_run_concurrent_warmup(
    const bench_plan *plan,
    const ht_key_t   *initial_keys,
    size_t            initial_live,
    bench_op        **ops_by_thread,
    const size_t     *op_counts,
    int               use_op_lock
);

static int bench_run_concurrent_phase(
    const bench_plan *plan,
    const bench_iface *bi,
    bench_op        **ops_by_thread,
    const size_t     *op_counts,
    int               use_op_lock,
    uint64_t         *elapsed_ns_out,
    uint64_t         *worker_failures_out
);

static unsigned bench_concurrent_iface_flags(
    const bench_plan *plan
);

static int bench_partitioned_traces_build(
    workload_kind       workload,
    size_t              op_count,
    const bench_plan   *plan,
    uint64_t            trace_seed,
    const ht_key_t     *initial_keys,
    size_t              initial_live,
    const ht_key_t     *insert_keys,
    size_t              insert_key_count,
    bench_keyspace_mode keyspace_mode,
    bench_partitioned_traces *traces
);

static void bench_partitioned_traces_free(
    bench_partitioned_traces *traces,
    size_t                   thread_count
);

int bench_run_concurrent_lookup(
    const bench_plan *plan,
    bench_run_result *result
) {
    ht_key_t *initial_keys = NULL;
    bench_partitioned_traces warmup_traces = {0};
    bench_partitioned_traces timed_traces = {0};
    size_t initial_live = 0;
    uint64_t trace_seed;
    int rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (bench_dataset_generate_populated_keys(
            plan->planned_capacity,
            plan->target_alpha,
            plan->key_seed,
            &initial_live,
            &initial_keys
        ) != 0 ||
        initial_live == 0) {
        fprintf(stderr, "Failed to generate concurrent lookup dataset\n");
        goto cleanup;
    }

    trace_seed = (plan->trace_seed != 0) ? plan->trace_seed : plan->key_seed;
    if (plan->warmup_ops > 0 &&
        bench_partitioned_traces_build(
            WORKLOAD_READ_ONLY,
            plan->warmup_ops,
            plan,
            trace_seed,
            initial_keys,
            initial_live,
            NULL,
            0,
            BENCH_KEYSPACE_SHARED_READ,
            &warmup_traces
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent lookup warm-up traces\n");
        goto cleanup;
    } /* Lookup can reuse the key seed when no trace seed is provided. */

    if (bench_partitioned_traces_build(
            WORKLOAD_READ_ONLY,
            plan->timed_ops,
            plan,
            trace_seed,
            initial_keys,
            initial_live,
            NULL,
            0,
            BENCH_KEYSPACE_SHARED_READ,
            &timed_traces
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent lookup traces\n");
        goto cleanup;
    }

    rc = bench_run_concurrent_ops(
        plan,
        initial_keys,
        initial_live,
        warmup_traces.ops,
        warmup_traces.counts,
        timed_traces.ops,
        timed_traces.counts,
        0,
        result
    );

cleanup:
    bench_partitioned_traces_free(&warmup_traces, plan ? plan->thread_count : 0);
    bench_partitioned_traces_free(&timed_traces, plan ? plan->thread_count : 0);
    free(initial_keys);
    return rc;
}

int bench_run_concurrent_workload(
    const bench_plan *plan,
    bench_run_result *result
) {
    ht_key_t *initial_keys = NULL;
    ht_key_t *warmup_insert_keys = NULL;
    ht_key_t *insert_keys = NULL;
    bench_partitioned_traces warmup_traces = {0};
    bench_partitioned_traces timed_traces = {0};
    int rc = -1;

    if (plan == NULL || result == NULL) {
        return -1;
    }

    if (plan->dataset_size < plan->thread_count) {
        fprintf(
            stderr,
            "concurrent-workload requires --dataset-size >= --thread-count\n"
        );
        return -1;
    } /* Each worker needs at least one starting key for disjoint traces. */

    if (bench_dataset_generate_unique_keys(
            plan->dataset_size,
            plan->key_seed,
            &initial_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent workload dataset\n");
        goto cleanup;
    }

    if (bench_dataset_generate_missing_keys(
            initial_keys,
            plan->dataset_size,
            plan->trace_seed,
            plan->timed_ops,
            &insert_keys
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent workload insert keys\n");
        goto cleanup;
    }

    if (plan->warmup_ops > 0 &&
        bench_dataset_generate_missing_keys(
            initial_keys,
            plan->dataset_size,
            plan->trace_seed,
            plan->warmup_ops,
            &warmup_insert_keys
        ) != 0) {
        fprintf(stderr,
                "Failed to generate concurrent workload warm-up insert keys\n");
        goto cleanup;
    } /* Warm-up inserts use their own keys so timed inserts stay missing. */

    if (plan->warmup_ops > 0 &&
        bench_partitioned_traces_build(
            plan->workload,
            plan->warmup_ops,
            plan,
            plan->trace_seed,
            initial_keys,
            plan->dataset_size,
            warmup_insert_keys,
            plan->warmup_ops,
            plan->concurrent_keyspace_mode,
            &warmup_traces
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent workload warm-up traces\n");
        goto cleanup;
    }

    if (bench_partitioned_traces_build(
            plan->workload,
            plan->timed_ops,
            plan,
            plan->trace_seed,
            initial_keys,
            plan->dataset_size,
            insert_keys,
            plan->timed_ops,
            plan->concurrent_keyspace_mode,
            &timed_traces
        ) != 0) {
        fprintf(stderr, "Failed to generate concurrent workload traces\n");
        goto cleanup;
    }

    rc = bench_run_concurrent_ops(
        plan,
        initial_keys,
        plan->dataset_size,
        warmup_traces.ops,
        warmup_traces.counts,
        timed_traces.ops,
        timed_traces.counts,
        plan->thread_count > 1 &&
            !bench_impl_supports_concurrent_mutation(plan->impl_kind),
        result
    );

cleanup:
    bench_partitioned_traces_free(&warmup_traces, plan ? plan->thread_count : 0);
    free(warmup_insert_keys);
    bench_partitioned_traces_free(&timed_traces, plan ? plan->thread_count : 0);
    free(insert_keys);
    free(initial_keys);
    return rc;
}

static int bench_run_concurrent_ops(
    const bench_plan *plan,
    const ht_key_t   *initial_keys,
    size_t            initial_live,
    bench_op        **warmup_ops_by_thread,
    const size_t     *warmup_op_counts,
    bench_op        **ops_by_thread,
    const size_t     *op_counts,
    int               use_op_lock,
    bench_run_result *result
) {
    ht_config cfg;
    ht_map *map = NULL;
    bench_iface bi;
    ht_stats stats;
    ht_result create_rc;
    uint64_t elapsed_ns = 0;
    uint64_t wall_elapsed_ns;
    uint64_t worker_failures = 0;
    int rc = -1;

    if (plan == NULL || ops_by_thread == NULL || op_counts == NULL ||
        result == NULL) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    memset(&bi, 0, sizeof(bi));
    memset(&stats, 0, sizeof(stats));

    if (bench_build_ht_config_from_plan(plan, &cfg, 1) != 0) {
        return -1;
    }

    create_rc = ht_create_ex(&cfg, &map);
    if (create_rc != HT_OK) {
        fprintf(
            stderr,
            "Failed to create concurrent benchmark hashtable: %s\n",
            ht_result_name(create_rc)
        );
        goto cleanup;
    }

    if (bench_prepopulate_table(map, initial_keys, initial_live) != 0) {
        goto cleanup;
    }

    if (bench_run_concurrent_warmup(
            plan,
            initial_keys,
            initial_live,
            warmup_ops_by_thread,
            warmup_op_counts,
            use_op_lock
        ) != 0) {
        goto cleanup;
    } /* Warm-up uses a throwaway table to keep timed state untouched. */

    if (ht_reserve(map, ht_capacity(map)) != HT_OK) {
        fprintf(stderr, "Failed to reserve concurrent benchmark table\n");
        goto cleanup;
    } /* Do capacity stabilization before workers start the clock. */

    if (ht_reset_stats(map) != HT_OK) {
        fprintf(stderr, "Failed to reset concurrent benchmark stats\n");
        goto cleanup;
    }

    if (ht_bind_bench_iface_flags(
            map,
            &bi,
            bench_concurrent_iface_flags(plan)
        ) != HT_OK) {
        fprintf(stderr, "Failed to bind concurrent benchmark interface\n");
        goto cleanup;
    }

    if (bench_run_concurrent_phase(
            plan,
            &bi,
            ops_by_thread,
            op_counts,
            use_op_lock,
            &elapsed_ns,
            &worker_failures
        ) != 0) {
        goto cleanup;
    }
    wall_elapsed_ns = elapsed_ns;

    if (ht_get_stats(map, &stats) != HT_OK) {
        fprintf(stderr, "Failed to read concurrent benchmark stats\n");
        goto cleanup;
    }

    bench_fill_result(
        plan,
        map,
        &stats,
        initial_live,
        wall_elapsed_ns,
        &result->core
    );
    result->concurrent.thread_count = plan->thread_count;
    result->concurrent.keyspace_mode = plan->concurrent_keyspace_mode;
    result->concurrent.resize_mode = plan->concurrent_resize_mode;
    result->concurrent.harness_op_lock = use_op_lock ? 1 : 0;
    result->concurrent.true_concurrent_mutation =
        (plan->kind == BENCH_CONCURRENT_WORKLOAD &&
         plan->thread_count > 1 &&
         !use_op_lock &&
         bench_impl_supports_concurrent_mutation(plan->impl_kind));
    result->concurrent.wall_elapsed_ns = result->core.elapsed_ns;
    result->concurrent.worker_failures = worker_failures;
    result->concurrent.total_ops_per_sec = result->core.ops_per_sec;
    result->concurrent.ops_per_sec_per_thread =
        result->concurrent.total_ops_per_sec / (double)plan->thread_count;

    rc = 0;

cleanup:
    ht_destroy(map);
    return rc;
}

static int bench_run_concurrent_warmup(
    const bench_plan *plan,
    const ht_key_t   *initial_keys,
    size_t            initial_live,
    bench_op        **ops_by_thread,
    const size_t     *op_counts,
    int               use_op_lock
) {
    ht_config cfg;
    ht_map *map = NULL;
    bench_iface bi;
    ht_result create_rc;
    int rc = -1;

    if (plan == NULL) {
        return -1;
    }

    if (plan->warmup_ops == 0) {
        return 0;
    }

    memset(&bi, 0, sizeof(bi));
    if (bench_build_ht_config_from_plan(plan, &cfg, 0) != 0) {
        return -1;
    }

    create_rc = ht_create_ex(&cfg, &map);
    if (create_rc != HT_OK) {
        fprintf(
            stderr,
            "Failed to create concurrent warm-up hashtable: %s\n",
            ht_result_name(create_rc)
        );
        goto cleanup;
    }

    if (bench_prepopulate_table(map, initial_keys, initial_live) != 0) {
        goto cleanup;
    }

    if (ht_reserve(map, ht_capacity(map)) != HT_OK) {
        fprintf(stderr, "Failed to reserve concurrent warm-up table\n");
        goto cleanup;
    }

    if (ht_bind_bench_iface_flags(
            map,
            &bi,
            bench_concurrent_iface_flags(plan)
        ) != HT_OK) {
        fprintf(stderr, "Failed to bind concurrent warm-up benchmark interface\n");
        goto cleanup;
    }

    if (bench_run_concurrent_phase(
            plan,
            &bi,
            ops_by_thread,
            op_counts,
            use_op_lock,
            NULL,
            NULL
        ) != 0) {
        fprintf(stderr, "Concurrent benchmark warm-up failed\n");
        goto cleanup;
    }

    rc = 0;

cleanup:
    ht_destroy(map);
    return rc;
}

static int bench_run_concurrent_phase(
    const bench_plan *plan,
    const bench_iface *bi,
    bench_op        **ops_by_thread,
    const size_t     *op_counts,
    int               use_op_lock,
    uint64_t         *elapsed_ns_out,
    uint64_t         *worker_failures_out
) {
    bench_start_gate start_gate;
    bench_worker *workers = NULL;
    pthread_mutex_t op_lock;
    pthread_mutex_t *op_lock_ptr = NULL;
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t worker_failures = 0;
    size_t created = 0;
    size_t i;
    int start_gate_ready = 0;
    int op_lock_ready = 0;
    int rc = -1;

    if (plan == NULL || bi == NULL || ops_by_thread == NULL || op_counts == NULL) {
        return -1;
    }

    memset(&start_gate, 0, sizeof(start_gate));
    memset(&op_lock, 0, sizeof(op_lock));
    if (plan->thread_count > SIZE_MAX / sizeof(*workers)) {
        return -1;
    }

    workers = calloc(plan->thread_count, sizeof(*workers));
    if (workers == NULL) {
        goto cleanup;
    }

    if (bench_start_gate_init(&start_gate, plan->thread_count) != 0) {
        goto cleanup;
    }
    start_gate_ready = 1;

    if (use_op_lock) {
        if (pthread_mutex_init(&op_lock, NULL) != 0) {
            goto cleanup;
        }
        op_lock_ready = 1;
        op_lock_ptr = &op_lock;
    } /* Serialize operations only for implementations lacking mutation safety. */

    for (i = 0; i < plan->thread_count; i++) {
        workers[i].ctx.ops        = ops_by_thread[i];
        workers[i].ctx.op_count   = op_counts[i];
        workers[i].ctx.rc         = 0;
        workers[i].ctx.bi         = bi;
        workers[i].ctx.start_gate = &start_gate;
        workers[i].ctx.op_lock    = op_lock_ptr;

        if (pthread_create(
                &workers[i].thread,
                NULL,
                bench_concurrent_worker_main,
                &workers[i].ctx
            ) != 0) {
            bench_start_gate_abort(&start_gate);
            goto cleanup;
        }
        created++;
    }

    if (bench_start_gate_release(&start_gate, &start_ns) != 0) {
        bench_start_gate_abort(&start_gate);
        goto cleanup;
    } /* The release timestamp is the shared start time for all workers. */

    for (i = 0; i < created; i++) {
        if (pthread_join(workers[i].thread, NULL) != 0) {
            worker_failures++;
        }
    }
    created = 0;

    end_ns = bench_now_ns();
    if (end_ns == 0 || end_ns <= start_ns) {
        goto cleanup;
    }

    for (i = 0; i < plan->thread_count; i++) {
        if (workers[i].ctx.rc != 0) {
            worker_failures++;
        }
    }

    if (worker_failures > 0) {
        fprintf(stderr, "Concurrent benchmark worker failure count: %" PRIu64 "\n",
                worker_failures);
        goto cleanup;
    }

    if (elapsed_ns_out != NULL) {
        *elapsed_ns_out = end_ns - start_ns;
    }
    if (worker_failures_out != NULL) {
        *worker_failures_out = worker_failures;
    }

    rc = 0;

cleanup:
    if (created > 0) {
        bench_start_gate_abort(&start_gate);
        for (i = 0; i < created; i++) {
            (void)pthread_join(workers[i].thread, NULL);
        }
    }

    if (op_lock_ready) {
        pthread_mutex_destroy(&op_lock);
    }

    if (start_gate_ready) {
        bench_start_gate_destroy(&start_gate);
    }

    free(workers);
    return rc;
}

static unsigned bench_concurrent_iface_flags(
    const bench_plan *plan
) {
    if (plan == NULL) {
        return 0;
    }

    if (plan->kind == BENCH_CONCURRENT_LOOKUP ||
        (plan->kind == BENCH_CONCURRENT_WORKLOAD &&
         plan->workload == WORKLOAD_READ_ONLY &&
         plan->concurrent_resize_mode == BENCH_RESIZE_DISABLED)) {
        return BENCH_IFACE_FROZEN_READ_ONLY;
    } /* Read-only phases can bind the cheaper frozen interface. */

    return 0;
}

static int bench_partitioned_traces_build(
    workload_kind       workload,
    size_t              op_count,
    const bench_plan   *plan,
    uint64_t            trace_seed,
    const ht_key_t     *initial_keys,
    size_t              initial_live,
    const ht_key_t     *insert_keys,
    size_t              insert_key_count,
    bench_keyspace_mode keyspace_mode,
    bench_partitioned_traces *traces
) {
    if (plan == NULL || traces == NULL) {
        return -1;
    }

    bench_partitioned_traces_free(traces, plan->thread_count);
    /* Rebuild always starts from a cleared trace container. */
    return bench_trace_build_partitioned_workloads(
        workload,
        op_count,
        plan->thread_count,
        trace_seed,
        initial_keys,
        initial_live,
        insert_keys,
        insert_key_count,
        keyspace_mode,
        &traces->ops,
        &traces->counts
    );
}

static void bench_partitioned_traces_free(
    bench_partitioned_traces *traces,
    size_t                   thread_count
) {
    size_t i;

    if (traces == NULL) {
        return;
    }

    if (traces->ops != NULL) {
        for (i = 0; i < thread_count; i++) {
            bench_trace_free(traces->ops[i]);
        }
    }

    free(traces->ops);
    free(traces->counts);
    traces->ops = NULL;
    traces->counts = NULL;
}

static void *bench_concurrent_worker_main(
    void *arg
) {
    bench_thread_ctx *ctx = arg;
    bench_exec_iface exec;
    size_t i;

    if (ctx == NULL) {
        return NULL;
    }

    ctx->rc = -1;
    if (ctx->bi == NULL || ctx->start_gate == NULL) {
        return NULL;
    }

    if (bench_start_gate_wait(ctx->start_gate) != 0) {
        return NULL;
    }

    exec.kind = BENCH_EXEC_DIRECT;
    exec.map  = NULL;
    exec.bi   = ctx->bi;

    for (i = 0; i < ctx->op_count; i++) {
        if (ctx->ops == NULL) {
            return NULL;
        }

        if (ctx->op_lock != NULL &&
            pthread_mutex_lock(ctx->op_lock) != 0) {
            return NULL;
        } /* Optional harness lock protects non-concurrent implementations. */

        if (bench_apply_workload_op(
                &exec,
                &ctx->ops[i]
            ) != 0) {
            if (ctx->op_lock != NULL) {
                pthread_mutex_unlock(ctx->op_lock);
            }
            return NULL;
        }

        if (ctx->op_lock != NULL &&
            pthread_mutex_unlock(ctx->op_lock) != 0) {
            return NULL;
        }
    }
    ctx->rc = 0;
    return NULL;
}

static int bench_start_gate_init(
    bench_start_gate *gate,
    size_t            thread_count
) {
    if (gate == NULL || thread_count == 0) {
        return -1;
    }

    memset(gate, 0, sizeof(*gate));
    gate->thread_count = thread_count;

    if (pthread_mutex_init(&gate->mutex, NULL) != 0) {
        return -1;
    }

    if (pthread_cond_init(&gate->cond, NULL) != 0) {
        pthread_mutex_destroy(&gate->mutex);
        return -1;
    }

    return 0;
}

static void bench_start_gate_destroy(
    bench_start_gate *gate
) {
    if (gate == NULL) {
        return;
    }

    pthread_cond_destroy(&gate->cond);
    pthread_mutex_destroy(&gate->mutex);
    memset(gate, 0, sizeof(*gate));
}

static int bench_start_gate_wait(
    bench_start_gate *gate
) {
    int rc = 0;

    if (gate == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&gate->mutex) != 0) {
        return -1;
    }

    gate->ready_count++;
    if (gate->ready_count == gate->thread_count) {
        (void)pthread_cond_broadcast(&gate->cond);
    }

    while (!gate->start && !gate->abort) {
        if (pthread_cond_wait(&gate->cond, &gate->mutex) != 0) {
            rc = -1;
            break;
        }
    } /* Workers sleep here until the main thread records start time. */

    if (gate->abort) {
        rc = -1;
    }

    if (pthread_mutex_unlock(&gate->mutex) != 0) {
        rc = -1;
    }

    return rc;
}

static int bench_start_gate_release(
    bench_start_gate *gate,
    uint64_t         *start_ns_out
) {
    int rc = 0;
    uint64_t start_ns;

    if (gate == NULL || start_ns_out == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&gate->mutex) != 0) {
        return -1;
    }

    while (gate->ready_count < gate->thread_count && !gate->abort) {
        if (pthread_cond_wait(&gate->cond, &gate->mutex) != 0) {
            rc = -1;
            break;
        }
    }

    if (rc == 0 && !gate->abort) {
        start_ns = bench_now_ns();
        if (start_ns == 0) {
            gate->abort = 1;
            rc = -1;
        } else {
            gate->start = 1;
            *start_ns_out = start_ns;
        }
    } else {
        rc = -1;
    } /* Release either starts every worker or aborts them together. */

    (void)pthread_cond_broadcast(&gate->cond);

    if (pthread_mutex_unlock(&gate->mutex) != 0) {
        rc = -1;
    }

    return rc;
}

static void bench_start_gate_abort(
    bench_start_gate *gate
) {
    if (gate == NULL) {
        return;
    }

    if (pthread_mutex_lock(&gate->mutex) != 0) {
        return;
    }

    gate->abort = 1;
    (void)pthread_cond_broadcast(&gate->cond);
    (void)pthread_mutex_unlock(&gate->mutex);
}
