/**
 * @file    bench_backend_pthread.c
 * @brief   pthread benchmark backend.
 */

#include <inttypes.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_backend_pthread.h"
#include "bench_names.h"
#include "bench_runner_common.h"
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
    pthread_t thread;
    bench_thread_ctx ctx;
} bench_worker;

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

static void *bench_pthread_worker_main(
    void *arg
);

static int bench_pthread_run_ops(
    const bench_plan *plan,
    const bench_fixture *fixture,
    int serialize_mutations,
    bench_sample *sample
);

static int bench_pthread_run_warmup(
    const bench_plan *plan,
    const bench_fixture *fixture,
    int serialize_mutations
);

static int bench_pthread_run_phase(
    const bench_plan *plan,
    const bench_iface *bi,
    bench_op **ops_by_thread,
    const size_t *op_counts,
    int serialize_mutations,
    uint64_t *elapsed_ns_out,
    uint64_t *worker_failures_out
);

static unsigned bench_pthread_iface_flags(
    const bench_plan *plan
);

static int bench_pthread_append_metrics(
    const bench_plan *plan,
    int serialize_mutations,
    uint64_t wall_elapsed_ns,
    uint64_t worker_failures,
    bench_sample *sample
);

int bench_backend_pthread_run(
    const bench_plan *plan,
    const bench_fixture *fixture,
    bench_sample *sample
) {
    int serialize_mutations;

    if (plan == NULL || fixture == NULL || sample == NULL) {
        return -1;
    }

    /* Non-concurrent implementations share one table; serialize mutating
       workload replay so only thread scheduling remains concurrent. */
    serialize_mutations =
        (plan->scenario == BENCH_SCENARIO_WORKLOAD &&
         plan->thread_count > 1 &&
         !bench_impl_supports_concurrent_mutation(plan->impl_kind));

    return bench_pthread_run_ops(plan, fixture, serialize_mutations, sample);
}

static int bench_pthread_run_ops(
    const bench_plan *plan,
    const bench_fixture *fixture,
    int serialize_mutations,
    bench_sample *sample
) {
    bench_env env = {0};
    uint64_t elapsed_ns = 0;
    uint64_t worker_failures = 0;
    int rc = -1;

    if (fixture->thread_ops == NULL || fixture->thread_op_counts == NULL) {
        return -1;
    }

    if (bench_env_init(plan, &env) != 0) {
        return -1;
    }
    if (bench_prepopulate_table(
            env.map,
            fixture->initial_keys,
            fixture->initial_count
        ) != 0) {
        goto cleanup;
    }

    if (bench_pthread_run_warmup(plan, fixture, serialize_mutations) != 0) {
        goto cleanup;
    }
    if (bench_env_prepare_timed_phase(
            &env,
            bench_pthread_iface_flags(plan)
        ) != 0) {
        goto cleanup;
    }

    if (bench_pthread_run_phase(
            plan,
            &env.bi,
            fixture->thread_ops,
            fixture->thread_op_counts,
            serialize_mutations,
            &elapsed_ns,
            &worker_failures
        ) != 0) {
        goto cleanup;
    }

    if (bench_env_collect_stats(&env) != 0) {
        goto cleanup;
    }
    if (bench_append_core_metrics(
            plan,
            env.map,
            &env.stats,
            fixture->initial_count,
            elapsed_ns,
            sample
        ) != 0) {
        goto cleanup;
    }
    if (bench_pthread_append_metrics(
            plan,
            serialize_mutations,
            elapsed_ns,
            worker_failures,
            sample
        ) != 0) {
        goto cleanup;
    }
    if (plan->capacity_policy == BENCH_CAP_RESIZING &&
        bench_append_resize_metrics(
            plan,
            &env.stats,
            ht_capacity(env.map),
            sample
        ) != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    bench_env_destroy(&env);
    return rc;
}

static int bench_pthread_run_warmup(
    const bench_plan *plan,
    const bench_fixture *fixture,
    int serialize_mutations
) {
    ht_config cfg;
    ht_map *map = NULL;
    bench_iface bi;
    ht_result create_rc;
    uint64_t ignored_elapsed_ns = 0;
    uint64_t ignored_failures = 0;
    int rc = -1;

    if (plan->warmup_ops == 0) {
        return 0;
    }
    if (fixture->warmup_thread_ops == NULL ||
        fixture->warmup_thread_op_counts == NULL) {
        return -1;
    }

    memset(&bi, 0, sizeof(bi));
    if (bench_build_ht_config_from_plan(plan, &cfg, 0) != 0) {
        return -1;
    }

    create_rc = ht_create_ex(&cfg, &map);
    if (create_rc != HT_OK) {
        fprintf(stderr, "Failed to create pthread warm-up hashtable: %s\n",
                ht_result_name(create_rc));
        goto cleanup;
    }
    if (bench_prepopulate_table(
            map,
            fixture->initial_keys,
            fixture->initial_count
        ) != 0) {
        goto cleanup;
    }
    if (ht_reserve(map, ht_capacity(map)) != HT_OK) {
        fprintf(stderr, "Failed to reserve pthread warm-up table\n");
        goto cleanup;
    }
    if (ht_bind_bench_iface_flags(
            map,
            &bi,
            bench_pthread_iface_flags(plan)
        ) != HT_OK) {
        fprintf(stderr, "Failed to bind pthread warm-up interface\n");
        goto cleanup;
    }
    if (bench_pthread_run_phase(
            plan,
            &bi,
            fixture->warmup_thread_ops,
            fixture->warmup_thread_op_counts,
            serialize_mutations,
            &ignored_elapsed_ns,
            &ignored_failures
        ) != 0) {
        fprintf(stderr, "pthread benchmark warm-up failed\n");
        goto cleanup;
    }

    rc = 0;

cleanup:
    ht_destroy(map);
    return rc;
}

static int bench_pthread_run_phase(
    const bench_plan *plan,
    const bench_iface *bi,
    bench_op **ops_by_thread,
    const size_t *op_counts,
    int serialize_mutations,
    uint64_t *elapsed_ns_out,
    uint64_t *worker_failures_out
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

    if (plan == NULL || bi == NULL || ops_by_thread == NULL ||
        op_counts == NULL || elapsed_ns_out == NULL ||
        worker_failures_out == NULL) {
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

    if (serialize_mutations) {
        if (pthread_mutex_init(&op_lock, NULL) != 0) {
            goto cleanup;
        }
        op_lock_ready = 1;
        op_lock_ptr = &op_lock;
    }

    for (i = 0; i < plan->thread_count; i++) {
        workers[i].ctx.ops = ops_by_thread[i];
        workers[i].ctx.op_count = op_counts[i];
        workers[i].ctx.rc = 0;
        workers[i].ctx.bi = bi;
        workers[i].ctx.start_gate = &start_gate;
        workers[i].ctx.op_lock = op_lock_ptr;

        if (pthread_create(
                &workers[i].thread,
                NULL,
                bench_pthread_worker_main,
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
    }

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
        fprintf(stderr, "pthread benchmark worker failure count: %" PRIu64 "\n",
                worker_failures);
        goto cleanup;
    }

    *elapsed_ns_out = end_ns - start_ns;
    *worker_failures_out = worker_failures;
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

static unsigned bench_pthread_iface_flags(
    const bench_plan *plan
) {
    if (plan == NULL) {
        return 0;
    }

    if (plan->scenario == BENCH_SCENARIO_LOOKUP_HIT ||
        (plan->scenario == BENCH_SCENARIO_WORKLOAD &&
         plan->workload == WORKLOAD_READ_ONLY &&
         plan->concurrent_resize_mode == BENCH_RESIZE_DISABLED)) {
        return BENCH_IFACE_FROZEN_READ_ONLY;
    }

    return 0;
}

static int bench_pthread_append_metrics(
    const bench_plan *plan,
    int serialize_mutations,
    uint64_t wall_elapsed_ns,
    uint64_t worker_failures,
    bench_sample *sample
) {
    double total_ops_per_sec;
    double ops_per_sec_per_thread;
    int true_concurrent_mutation;

    total_ops_per_sec =
        ((double)plan->timed_ops * 1e9) / (double)wall_elapsed_ns;
    ops_per_sec_per_thread = total_ops_per_sec / (double)plan->thread_count;
    true_concurrent_mutation =
        (plan->scenario == BENCH_SCENARIO_WORKLOAD &&
         plan->workload != WORKLOAD_READ_ONLY &&
         plan->thread_count > 1 &&
         !serialize_mutations &&
         bench_impl_supports_concurrent_mutation(plan->impl_kind));

    if (bench_metric_add_u64(&sample->metrics, "thread_count",
            (uint64_t)plan->thread_count) != 0 ||
        bench_metric_add_u64(&sample->metrics, "keyspace_mode",
            (uint64_t)plan->keyspace_mode) != 0 ||
        bench_metric_add_u64(&sample->metrics, "resize_mode",
            (uint64_t)plan->concurrent_resize_mode) != 0 ||
        bench_metric_add_u64(&sample->metrics, "true_concurrent_mutation",
            (uint64_t)(true_concurrent_mutation ? 1 : 0)) != 0 ||
        bench_metric_add_u64(&sample->metrics, "harness_op_lock",
            (uint64_t)(serialize_mutations ? 1 : 0)) != 0 ||
        bench_metric_add_u64(&sample->metrics, "wall_elapsed_ns",
            wall_elapsed_ns) != 0 ||
        bench_metric_add_f64(&sample->metrics, "total_ops_per_sec",
            total_ops_per_sec) != 0 ||
        bench_metric_add_f64(&sample->metrics, "ops_per_sec_per_thread",
            ops_per_sec_per_thread) != 0 ||
        bench_metric_add_u64(&sample->metrics, "worker_failures",
            worker_failures) != 0) {
        return -1;
    }

    return 0;
}

static void *bench_pthread_worker_main(
    void *arg
) {
    bench_thread_ctx *ctx = arg;
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

    for (i = 0; i < ctx->op_count; i++) {
        if (ctx->ops == NULL) {
            return NULL;
        }
        if (ctx->op_lock != NULL &&
            pthread_mutex_lock(ctx->op_lock) != 0) {
            return NULL;
        }
        if (bench_apply_op_direct(ctx->bi, &ctx->ops[i]) != 0) {
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
    }
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
    uint64_t start_ns;
    int rc = 0;

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
    }

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
