/**
 * @file    bench_runner_common.c
 * @brief   Shared benchmark execution helpers.
 *
 * Contains the low-level setup, warm-up, timing, and result filling logic
 * used by the steady, resize-aware, and concurrent benchmark runners.
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
#include "bench_runner_common.h"
#include "bench_time.h"

static int bench_step_key_array_exec(
    const bench_exec_iface *exec,
    size_t                  index,
    const void             *ctx
);

static int bench_step_workload_exec(
    const bench_exec_iface *exec,
    size_t                  index,
    const void             *ctx
);

int bench_build_ht_config_from_plan(
    const bench_plan *plan,
    ht_config        *cfg,
    int               collect_stats
) {
    if (plan == NULL || cfg == NULL) {
        return -1;
    }

    if (plan->capacity_mode == BENCH_CAPACITY_RESIZING ||
        plan->exec_mode == BENCH_EXEC_RESIZE) {
        *cfg = ht_config_resizing(plan->impl_kind, plan->initial_capacity);
    } else {
        *cfg = ht_config_fixed(plan->impl_kind, plan->initial_capacity);
    }
    cfg->min_capacity    = plan->min_capacity;
    cfg->max_load_factor = (plan->kind == BENCH_WORKLOAD ||
                            plan->kind == BENCH_CONCURRENT_WORKLOAD)
        ? 1.0
        : plan->target_alpha;
    /* Workloads may mutate, so growth policy is configured below instead. */
    cfg->min_load_factor = 0.0;
    cfg->rsz_mode        = HT_RESIZE_NONE;
    cfg->hash_fn         = plan->hash_fn;
    cfg->hash_seed       = plan->hash_seed;
    cfg->thread_count    = plan->thread_count;
    cfg->collect_stats   = collect_stats;

    if (plan->exec_mode == BENCH_EXEC_RESIZE) {
        switch (plan->resize_mode) {
        case BENCH_RESIZE_DISABLED:
            cfg->rsz_mode        = HT_RESIZE_NONE;
            cfg->max_load_factor = (plan->kind == BENCH_WORKLOAD ||
                                    plan->kind == BENCH_CONCURRENT_WORKLOAD)
                ? 1.0
                : plan->target_alpha;
            cfg->min_load_factor = 0.0;
            break;

        case BENCH_RESIZE_GROW_ONLY:
            cfg->rsz_mode        = HT_RESIZE_GROW;
            cfg->max_load_factor = (plan->grow_alpha > 0.0)
                ? plan->grow_alpha
                : plan->target_alpha;
            cfg->min_load_factor = 0.0;
            break;

        case BENCH_RESIZE_GROW_SHRINK:
            cfg->rsz_mode        = HT_RESIZE_GROW_SHRINK;
            cfg->max_load_factor = (plan->grow_alpha > 0.0)
                ? plan->grow_alpha
                : plan->target_alpha;
            cfg->min_load_factor = (plan->shrink_alpha > 0.0)
                ? plan->shrink_alpha
                : (cfg->max_load_factor * 0.5);
            break;

        case BENCH_RESIZE_IMPL_DEFAULT:
            cfg->rsz_mode        = HT_RESIZE_GROW_SHRINK;
            cfg->max_load_factor = 0.0;
            cfg->min_load_factor = 0.0;
            /* Zero thresholds ask the implementation to choose defaults. */
            break;
        }
    }

    if (plan->exec_mode == BENCH_EXEC_CONCURRENT &&
        plan->concurrent_resize_mode == BENCH_RESIZE_GROW_ONLY) {
        cfg->rsz_mode        = HT_RESIZE_GROW;
        cfg->max_load_factor = (plan->grow_alpha > 0.0)
            ? plan->grow_alpha
            : plan->target_alpha;
        cfg->min_load_factor = 0.0;
    }

    return 0;
}

int bench_prepopulate_table(
    ht_map          *map,
    const ht_key_t *keys,
    size_t           key_count
) {
    size_t i;

    if (map == NULL) {
        return -1;
    }

    if (key_count == 0) {
        return 0;
    }

    if (keys == NULL) {
        return -1;
    }

    for (i = 0; i < key_count; i++) {
        if (ht_insert(map, keys[i], keys[i]) != HT_OK) {
            fprintf(stderr, "Failed to prepopulate benchmark hashtable\n");
            return -1;
        }
    }

    return 0;
}

int bench_generate_populated_dataset(
    const bench_plan *plan,
    const char       *bench_name,
    size_t           *live_out,
    ht_key_t        **keys_out
) {
    if (plan == NULL || bench_name == NULL ||
        live_out == NULL || keys_out == NULL) {
        return -1;
    }

    if (bench_dataset_generate_populated_keys(
            plan->planned_capacity,
            plan->target_alpha,
            plan->key_seed,
            live_out,
            keys_out
        ) != 0) {
        fprintf(stderr, "Failed to generate %s dataset\n", bench_name);
        return -1;
    }

    if (*live_out == 0) {
        fprintf(stderr, "%s benchmark requires a non-zero dataset\n", bench_name);
        return -1;
    }

    return 0;
}

int bench_run_warmup(
    const bench_plan   *plan,
    const ht_key_t     *prepopulate_keys,
    size_t              prepopulate_count,
    bench_public_step_fn step,
    const void         *ctx
) {
    ht_config cfg;
    ht_map    *map = NULL;
    ht_result create_rc;
    int       rc = -1;
    size_t    i;

    if (plan == NULL || step == NULL) {
        return -1;
    }

    if (plan->warmup_ops == 0) {
        return 0;
    } /* No warm-up means no throwaway table is needed. */

    if (prepopulate_count > 0 && prepopulate_keys == NULL) {
        return -1;
    }

    if (bench_build_ht_config_from_plan(plan, &cfg, 0) != 0) {
        return -1;
    }

    create_rc = ht_create_ex(&cfg, &map);
    if (create_rc != HT_OK) {
        fprintf(
            stderr,
            "Failed to create warm-up hashtable: %s\n",
            ht_result_name(create_rc)
        );
        goto cleanup;
    }

    if (bench_prepopulate_table(map, prepopulate_keys, prepopulate_count) != 0) {
        goto cleanup;
    } /* Warm-up starts from the same live set as the timed phase. */

    for (i = 0; i < plan->warmup_ops; i++) {
        if (step(map, i, ctx) != 0) {
            goto cleanup;
        }
    }

    rc = 0;

cleanup:
    ht_destroy(map);
    return rc;
}

int bench_run_lookup_warmup(
    const ht_map    *map,
    const ht_key_t  *keys,
    size_t           key_count,
    size_t           warmup_ops,
    ht_result        expected_result,
    const char      *bench_name
) {
    ht_val_t value;
    ht_result rc;
    size_t   i;

    if (warmup_ops == 0) {
        return 0;
    }

    if (map == NULL || keys == NULL || key_count == 0 ||
        bench_name == NULL) {
        return -1;
    }

    for (i = 0; i < warmup_ops; i++) {
        rc = ht_get(map, keys[i % key_count], &value);
        if (rc != expected_result) {
            fprintf(stderr, "Warm-up failed during %s benchmark\n", bench_name);
            return -1;
        }
    }

    return 0;
}

int bench_step_key_array_warmup(
    ht_map         *map,
    size_t          index,
    const void     *ctx
) {
    bench_exec_iface exec;

    if (map == NULL) {
        return -1;
    }

    exec.kind = BENCH_EXEC_PUBLIC;
    exec.map  = map;
    exec.bi   = NULL;
    return bench_step_key_array_exec(&exec, index, ctx);
}

int bench_step_key_array_timed(
    const bench_iface *bi,
    size_t             index,
    const void        *ctx
) {
    bench_exec_iface exec;

    if (bi == NULL) {
        return -1;
    }

    exec.kind = BENCH_EXEC_DIRECT;
    exec.map  = NULL;
    exec.bi   = bi;
    return bench_step_key_array_exec(&exec, index, ctx);
}

int bench_step_lookup_warmup(
    ht_map         *map,
    size_t          index,
    const void     *ctx
) {
    const bench_lookup_ctx *step_ctx;
    ht_val_t value;
    ht_result rc;

    if (map == NULL || ctx == NULL) {
        return -1;
    }

    step_ctx = ctx;
    if (step_ctx->keys == NULL || step_ctx->key_count == 0 ||
        step_ctx->bench_name == NULL) {
        return -1;
    }

    rc = ht_get(
        map,
        step_ctx->keys[index % step_ctx->key_count],
        &value
    );
    if (rc != step_ctx->expected_result) {
        fprintf(
            stderr,
            "Warm-up failed during %s benchmark\n",
            step_ctx->bench_name
        );
        return -1;
    }

    return 0;
}

int bench_step_lookup(
    const bench_iface *bi,
    size_t             index,
    const void        *ctx
) {
    const bench_lookup_ctx *step_ctx;
    ht_val_t value;
    int      rc;

    if (bi == NULL || ctx == NULL || bi->get == NULL) {
        return -1;
    }

    step_ctx = ctx;
    if (step_ctx->keys == NULL || step_ctx->key_count == 0 ||
        step_ctx->bench_name == NULL) {
        return -1;
    }

    rc = bi->get(
        bi->ctx,
        step_ctx->keys[index % step_ctx->key_count],
        &value
    );
    if (rc != step_ctx->expected_result) {
        fprintf(
            stderr,
            "%s benchmark failed during timed loop\n",
            step_ctx->bench_name
        );
        return -1;
    }

    return 0;
}

int bench_step_workload_warmup(
    ht_map         *map,
    size_t          index,
    const void     *ctx
) {
    bench_exec_iface exec;

    if (map == NULL) {
        return -1;
    }

    exec.kind = BENCH_EXEC_PUBLIC;
    exec.map  = map;
    exec.bi   = NULL;
    return bench_step_workload_exec(&exec, index, ctx);
}

int bench_step_workload(
    const bench_iface *bi,
    size_t             index,
    const void        *ctx
) {
    bench_exec_iface exec;

    if (bi == NULL) {
        return -1;
    }

    exec.kind = BENCH_EXEC_DIRECT;
    exec.map  = NULL;
    exec.bi   = bi;
    return bench_step_workload_exec(&exec, index, ctx);
}

int bench_env_init(
    const bench_plan *plan,
    bench_env        *env
) {
    if (plan == NULL || env == NULL) {
        return -1;
    }

    memset(env, 0, sizeof(*env));
    if (bench_build_ht_config_from_plan(
            plan,
            &env->cfg,
            plan->stats_mode == BENCH_STATS_ON
        ) != 0) {
        return -1;
    }

    {
        ht_result create_rc = ht_create_ex(&env->cfg, &env->map);
        if (create_rc != HT_OK) {
            fprintf(
                stderr,
                "Failed to create benchmark hashtable: %s\n",
                ht_result_name(create_rc)
            );
            return -1;
        }
    }

    if (env->map == NULL) {
        return -1;
    }

    return 0;
}

void bench_env_destroy(
    bench_env *env
) {
    if (env == NULL) {
        return;
    }

    if (env->map != NULL) {
        ht_destroy(env->map);
    }

    memset(env, 0, sizeof(*env));
}

int bench_env_prepare_timed_phase(
    bench_env *env
) {
    if (env == NULL || env->map == NULL) {
        return -1;
    }

    if (ht_reserve(env->map, ht_capacity(env->map)) != HT_OK) {
        fprintf(stderr, "Failed to reserve benchmark table\n");
        return -1;
    } /* Pin capacity before timing so reserve work is not measured. */

    if (ht_reset_stats(env->map) != HT_OK) {
        fprintf(stderr, "Failed to reset benchmark stats\n");
        return -1;
    } /* Timed rows should include only measured-phase operations. */

    if (ht_bind_bench_iface(env->map, &env->bi) != HT_OK) {
        fprintf(stderr, "Failed to bind benchmark interface\n");
        return -1;
    }

    return 0;
}

int bench_env_collect_stats(
    bench_env *env
) {
    if (env == NULL || env->map == NULL) {
        return -1;
    }

    if (ht_get_stats(env->map, &env->stats) != HT_OK) {
        fprintf(stderr, "Failed to read benchmark stats\n");
        return -1;
    }

    return 0;
}

int bench_measure_timed_loop(
    size_t              op_count,
    bench_timed_step_fn step,
    const bench_iface   *bi,
    const void          *ctx,
    uint64_t            *elapsed_ns
) {
    uint64_t start_ns;
    uint64_t end_ns;
    size_t   i;

    if (step == NULL || bi == NULL || elapsed_ns == NULL) {
        return -1;
    }

    start_ns = bench_now_ns();
    if (start_ns == 0) {
        return -1;
    } /* A zero timestamp is the clock helper's failure sentinel. */

    for (i = 0; i < op_count; i++) {
        if (step(bi, i, ctx) != 0) {
            return -1;
        }
    }

    end_ns = bench_now_ns();
    if (end_ns == 0 || end_ns <= start_ns) {
        return -1;
    }

    *elapsed_ns = end_ns - start_ns;
    return 0;
}

int bench_run_timed_repetition(
    bench_env          *env,
    const bench_plan   *plan,
    size_t              initial_live,
    bench_timed_step_fn step,
    const void         *ctx,
    bench_result       *result
) {
    uint64_t elapsed_ns;

    if (env == NULL || plan == NULL || step == NULL || result == NULL) {
        return -1;
    }

    if (bench_env_prepare_timed_phase(env) != 0) {
        return -1;
    }

    if (bench_measure_timed_loop(
            plan->timed_ops,
            step,
            &env->bi,
            ctx,
            &elapsed_ns
        ) != 0) {
        return -1;
    }

    if (bench_env_collect_stats(env) != 0) {
        return -1;
    }

    bench_fill_result(
        plan,
        env->map,
        &env->stats,
        initial_live,
        elapsed_ns,
        result
    );
    return 0;
}

void bench_fill_result(
    const bench_plan *plan,
    const ht_map     *map,
    const ht_stats   *stats,
    size_t            initial_live,
    uint64_t          elapsed_ns,
    bench_result     *result
) {
    uint64_t total_ops;
    const double entry_bytes = (double)(sizeof(ht_key_t) + sizeof(ht_val_t));

    if (plan == NULL || map == NULL || stats == NULL || result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->spec.kind            = plan->kind;
    result->spec.workload        = plan->workload;
    result->spec.impl_kind       = plan->impl_kind;
    result->spec.hash_fn         = plan->hash_fn;
    result->spec.hash_seed       = plan->hash_seed;
    result->spec.dataset_size    = plan->dataset_size;
    result->spec.target_alpha    = plan->target_alpha;
    result->spec.capacity        = plan->initial_capacity;
    result->spec.resize_mode     = plan->resize_mode;
    result->spec.stats_mode      = plan->stats_mode;
    result->spec.concurrent_resize_mode =
        plan->concurrent_resize_mode;
    result->spec.prefill_alpha   = plan->prefill_alpha;
    result->spec.grow_alpha      = plan->grow_alpha;
    result->spec.shrink_alpha    = plan->shrink_alpha;
    result->spec.initial_capacity = plan->initial_capacity;
    result->spec.warmup_ops      = plan->warmup_ops;
    result->spec.timed_ops       = plan->timed_ops;
    result->spec.repetitions     = plan->repetitions;
    result->spec.key_seed        = plan->key_seed;
    result->spec.trace_seed      = plan->trace_seed;
    result->elapsed_ns           = elapsed_ns;
    result->initial_live         = initial_live;
    result->final_live           = ht_size(map);
    result->final_capacity       = ht_capacity(map);
    result->stats                = *stats;
    if (result->final_capacity > 0) {
        result->final_alpha =
            (double)result->final_live / (double)result->final_capacity;
    }

    if (plan->timed_ops > 0) {
        result->ns_per_op = (double)elapsed_ns / (double)plan->timed_ops;
        result->ops_per_sec =
            ((double)plan->timed_ops * 1e9) / (double)elapsed_ns;
    }

    total_ops = stats->lookups + stats->inserts + stats->removes;
    if (total_ops > 0) {
        result->avg_probe_len = (double)stats->probes / (double)total_ops;
    } /* Probe average is meaningful only when the table saw operations. */

    if (result->initial_live > 0) {
        result->memory_amp_initial =
            (double)stats->bytes_used / ((double)result->initial_live * entry_bytes);
        result->memory_amplification = result->memory_amp_initial;
    }

    if (result->final_live > 0) {
        result->memory_amp_final =
            (double)stats->bytes_used / ((double)result->final_live * entry_bytes);
    }
}

static int bench_step_key_array_exec(
    const bench_exec_iface *exec,
    size_t                  index,
    const void             *ctx
) {
    const bench_key_step_ctx *step_ctx = ctx;
    ht_key_t key;
    int rc = -1;

    if (exec == NULL || step_ctx == NULL ||
        step_ctx->keys == NULL || step_ctx->error_msg == NULL) {
        return -1;
    }

    key = step_ctx->keys[index];
    if (exec->kind == BENCH_EXEC_PUBLIC && exec->map != NULL) {
        if (step_ctx->op == BENCH_KEY_OP_INSERT) {
            rc = ht_insert(exec->map, key, key);
        } else if (step_ctx->op == BENCH_KEY_OP_REMOVE) {
            rc = ht_remove(exec->map, key);
        }
    } else if (exec->kind == BENCH_EXEC_DIRECT && exec->bi != NULL) {
        if (step_ctx->op == BENCH_KEY_OP_INSERT && exec->bi->insert != NULL) {
            rc = exec->bi->insert(exec->bi->ctx, key, key);
        } else if (step_ctx->op == BENCH_KEY_OP_REMOVE &&
                   exec->bi->remove != NULL) {
            rc = exec->bi->remove(exec->bi->ctx, key);
        }
    } /* Key-array steps support only insert and remove operations. */

    if (rc != HT_OK) {
        fprintf(stderr, "%s\n", step_ctx->error_msg);
        return -1;
    }

    return 0;
}

static int bench_step_workload_exec(
    const bench_exec_iface *exec,
    size_t                  index,
    const void             *ctx
) {
    const bench_workload_step_ctx *step_ctx = ctx;

    if (exec == NULL || step_ctx == NULL ||
        step_ctx->ops == NULL || step_ctx->error_msg == NULL) {
        return -1;
    }

    if (bench_apply_workload_op(exec, &step_ctx->ops[index]) != 0) {
        fprintf(stderr, "%s\n", step_ctx->error_msg);
        return -1;
    }

    return 0;
}

int bench_apply_workload_op(
    const bench_exec_iface *iface,
    const bench_op         *op
) {
    ht_val_t value;
    int      rc;

    if (iface == NULL || op == NULL) {
        return -1;
    }

    if (iface->kind != BENCH_EXEC_PUBLIC &&
        iface->kind != BENCH_EXEC_DIRECT) {
        return -1;
    }

    if (iface->kind == BENCH_EXEC_PUBLIC && iface->map == NULL) {
        return -1;
    }

    if (iface->kind == BENCH_EXEC_DIRECT &&
        (iface->bi == NULL || iface->bi->get == NULL ||
            iface->bi->insert == NULL || iface->bi->remove == NULL)) {
        return -1;
    }

    switch (op->kind) {
    case OP_GET_HIT:
        rc = (iface->kind == BENCH_EXEC_PUBLIC)
            ? ht_get(iface->map, op->key, &value)
            : iface->bi->get(iface->bi->ctx, op->key, &value);
        return (rc == HT_OK) ? 0 : -1;

    case OP_GET_MISS:
        rc = (iface->kind == BENCH_EXEC_PUBLIC)
            ? ht_get(iface->map, op->key, &value)
            : iface->bi->get(iface->bi->ctx, op->key, &value);
        return (rc == HT_ERR_NOT_FOUND) ? 0 : -1;

    case OP_INSERT:
        rc = (iface->kind == BENCH_EXEC_PUBLIC)
            ? ht_insert(iface->map, op->key, op->value)
            : iface->bi->insert(iface->bi->ctx, op->key, op->value);
        return (rc == HT_OK) ? 0 : -1;

    case OP_REMOVE:
        rc = (iface->kind == BENCH_EXEC_PUBLIC)
            ? ht_remove(iface->map, op->key)
            : iface->bi->remove(iface->bi->ctx, op->key);
        return (rc == HT_OK) ? 0 : -1;

    default:
        return -1;
    }
}
