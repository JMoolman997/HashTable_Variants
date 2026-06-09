/**
 * @file    bench_runner_common.c
 * @brief   Shared benchmark execution helpers.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bench_runner_common.h"
#include "bench_time.h"

static int bench_apply_op_public(
    ht_map         *map,
    const bench_op *op
);

static int bench_add_core_metric_values(
    bench_sample   *sample,
    const ht_stats *stats,
    uint64_t        elapsed_ns,
    double          ns_per_op,
    double          ops_per_sec,
    size_t          initial_live,
    size_t          final_live,
    size_t          final_capacity,
    double          final_alpha,
    double          avg_probe_len,
    double          memory_amp_initial,
    double          memory_amp_final
);

int bench_build_ht_config_from_plan(
    const bench_plan *plan,
    ht_config        *cfg,
    int               collect_stats
) {
    if (plan == NULL || cfg == NULL) {
        return -1;
    }

    if (plan->capacity_policy == BENCH_CAP_RESIZING) {
        *cfg = ht_config_resizing(plan->impl_kind, plan->initial_capacity);
    } else {
        *cfg = ht_config_fixed(plan->impl_kind, plan->initial_capacity);
    }

    cfg->min_capacity = plan->initial_capacity;
    cfg->max_load_factor = plan->target_alpha;
    cfg->min_load_factor = 0.0;
    cfg->rsz_mode = HT_RESIZE_NONE;
    cfg->hash_fn = NULL;
    cfg->hash_seed = plan->hash_seed;
    cfg->thread_count = plan->thread_count;
    cfg->collect_stats = collect_stats;

    if (plan->capacity_policy == BENCH_CAP_RESIZING &&
        plan->backend == BENCH_BACKEND_SINGLE) {
        switch (plan->resize_mode) {
        case BENCH_RESIZE_DISABLED:
            cfg->rsz_mode = HT_RESIZE_NONE;
            break;
        case BENCH_RESIZE_GROW_ONLY:
            cfg->rsz_mode = HT_RESIZE_GROW;
            cfg->max_load_factor = plan->grow_alpha;
            cfg->min_load_factor = 0.0;
            break;
        case BENCH_RESIZE_GROW_SHRINK:
            cfg->rsz_mode = HT_RESIZE_GROW_SHRINK;
            cfg->max_load_factor = plan->grow_alpha;
            cfg->min_load_factor = plan->shrink_alpha;
            break;
        }
    }

    if (plan->backend == BENCH_BACKEND_PTHREAD &&
        plan->concurrent_resize_mode == BENCH_RESIZE_GROW_ONLY) {
        cfg->rsz_mode = HT_RESIZE_GROW;
        cfg->max_load_factor = plan->grow_alpha;
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

int bench_run_warmup(
    const bench_plan *plan,
    const ht_key_t   *prepopulate_keys,
    size_t            prepopulate_count,
    const bench_op   *ops,
    size_t            op_count
) {
    ht_config cfg;
    ht_map *map = NULL;
    ht_result create_rc;
    size_t i;
    int rc = -1;

    if (plan == NULL) {
        return -1;
    }
    if (op_count == 0) {
        return 0;
    }
    if (ops == NULL) {
        return -1;
    }
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
    }

    for (i = 0; i < op_count; i++) {
        if (bench_apply_op_public(map, &ops[i]) != 0) {
            fprintf(stderr, "Benchmark warm-up failed during operation replay\n");
            goto cleanup;
        }
    }

    rc = 0;

cleanup:
    ht_destroy(map);
    return rc;
}

int bench_env_init(
    const bench_plan *plan,
    bench_env        *env
) {
    ht_result create_rc;

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

    create_rc = ht_create_ex(&env->cfg, &env->map);
    if (create_rc != HT_OK) {
        fprintf(stderr, "Failed to create benchmark hashtable: %s\n",
                ht_result_name(create_rc));
        return -1;
    }

    return (env->map != NULL) ? 0 : -1;
}

void bench_env_destroy(
    bench_env *env
) {
    if (env == NULL) {
        return;
    }

    ht_destroy(env->map);
    memset(env, 0, sizeof(*env));
}

int bench_env_prepare_timed_phase(
    bench_env *env,
    unsigned   iface_flags
) {
    if (env == NULL || env->map == NULL) {
        return -1;
    }

    if (ht_reserve(env->map, ht_capacity(env->map)) != HT_OK) {
        fprintf(stderr, "Failed to reserve benchmark table\n");
        return -1;
    }
    if (ht_reset_stats(env->map) != HT_OK) {
        fprintf(stderr, "Failed to reset benchmark stats\n");
        return -1;
    }
    if (ht_bind_bench_iface_flags(env->map, &env->bi, iface_flags) != HT_OK) {
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

int bench_measure_timed_ops(
    const bench_iface *bi,
    const bench_op    *ops,
    size_t             op_count,
    uint64_t          *elapsed_ns
) {
    uint64_t start_ns;
    uint64_t end_ns;
    size_t i;

    if (bi == NULL || ops == NULL || elapsed_ns == NULL || op_count == 0) {
        return -1;
    }

    start_ns = bench_now_ns();
    if (start_ns == 0) {
        return -1;
    }

    for (i = 0; i < op_count; i++) {
        if (bench_apply_op_direct(bi, &ops[i]) != 0) {
            fprintf(stderr, "Benchmark timed replay failed\n");
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

int bench_apply_op_direct(
    const bench_iface *bi,
    const bench_op    *op
) {
    ht_val_t value;
    ht_result rc;

    if (bi == NULL || op == NULL ||
        bi->get == NULL || bi->insert == NULL || bi->remove == NULL) {
        return -1;
    }

    switch (op->kind) {
    case OP_GET_HIT:
        rc = bi->get(bi->ctx, op->key, &value);
        return (rc == HT_OK) ? 0 : -1;
    case OP_GET_MISS:
        rc = bi->get(bi->ctx, op->key, &value);
        return (rc == HT_ERR_NOT_FOUND) ? 0 : -1;
    case OP_INSERT:
        rc = bi->insert(bi->ctx, op->key, op->value);
        return (rc == HT_OK) ? 0 : -1;
    case OP_REMOVE:
        rc = bi->remove(bi->ctx, op->key);
        return (rc == HT_OK) ? 0 : -1;
    default:
        return -1;
    }
}

int bench_append_core_metrics(
    const bench_plan *plan,
    const ht_map     *map,
    const ht_stats   *stats,
    size_t            initial_live,
    uint64_t          elapsed_ns,
    bench_sample     *sample
) {
    uint64_t total_ops;
    size_t final_live;
    size_t final_capacity;
    double ns_per_op = 0.0;
    double ops_per_sec = 0.0;
    double final_alpha = 0.0;
    double avg_probe_len = 0.0;
    double memory_amp_initial = 0.0;
    double memory_amp_final = 0.0;
    const double entry_bytes = (double)(sizeof(ht_key_t) + sizeof(ht_val_t));

    if (plan == NULL || map == NULL || stats == NULL || sample == NULL) {
        return -1;
    }

    final_live = ht_size(map);
    final_capacity = ht_capacity(map);
    if (plan->timed_ops > 0 && elapsed_ns > 0) {
        ns_per_op = (double)elapsed_ns / (double)plan->timed_ops;
        ops_per_sec = ((double)plan->timed_ops * 1e9) / (double)elapsed_ns;
    }
    if (final_capacity > 0) {
        final_alpha = (double)final_live / (double)final_capacity;
    }

    total_ops = stats->lookups + stats->inserts + stats->removes;
    if (total_ops > 0) {
        avg_probe_len = (double)stats->probes / (double)total_ops;
    }
    if (initial_live > 0) {
        memory_amp_initial =
            (double)stats->bytes_used / ((double)initial_live * entry_bytes);
    }
    if (final_live > 0) {
        memory_amp_final =
            (double)stats->bytes_used / ((double)final_live * entry_bytes);
    }

    return bench_add_core_metric_values(
        sample,
        stats,
        elapsed_ns,
        ns_per_op,
        ops_per_sec,
        initial_live,
        final_live,
        final_capacity,
        final_alpha,
        avg_probe_len,
        memory_amp_initial,
        memory_amp_final
    );
}

int bench_append_resize_metrics(
    const bench_plan *plan,
    const ht_stats   *stats,
    size_t            final_capacity,
    bench_sample     *sample
) {
    size_t fallback_min;
    size_t fallback_max;
    uint64_t resize_events = 0;
    uint64_t grow_events = 0;
    uint64_t shrink_events = 0;
    uint64_t resize_entries_moved = 0;
    uint64_t total_resize_ns = 0;
    uint64_t max_resize_ns = 0;
    size_t min_capacity_seen;
    size_t max_capacity_seen;

    if (plan == NULL || stats == NULL || sample == NULL) {
        return -1;
    }

    fallback_min = (plan->initial_capacity < final_capacity)
        ? plan->initial_capacity
        : final_capacity;
    fallback_max = (plan->initial_capacity > final_capacity)
        ? plan->initial_capacity
        : final_capacity;
    min_capacity_seen = fallback_min;
    max_capacity_seen = fallback_max;

#if HT_ENABLE_RESIZE_INSTRUMENTATION
    resize_events = stats->resize_count;
    grow_events = stats->grow_count;
    shrink_events = stats->shrink_count;
    resize_entries_moved = stats->resize_entries_moved;
    total_resize_ns = stats->resize_total_ns;
    max_resize_ns = stats->resize_max_ns;
    min_capacity_seen = (stats->resize_min_capacity != 0)
        ? stats->resize_min_capacity
        : fallback_min;
    max_capacity_seen = (stats->resize_max_capacity != 0)
        ? stats->resize_max_capacity
        : fallback_max;
#endif

    if (bench_metric_add_u64(&sample->metrics, "resize_events",
            resize_events) != 0 ||
        bench_metric_add_u64(&sample->metrics, "grow_events",
            grow_events) != 0 ||
        bench_metric_add_u64(&sample->metrics, "shrink_events",
            shrink_events) != 0 ||
        bench_metric_add_u64(&sample->metrics, "resize_entries_moved",
            resize_entries_moved) != 0 ||
        bench_metric_add_u64(&sample->metrics, "total_resize_ns",
            total_resize_ns) != 0 ||
        bench_metric_add_u64(&sample->metrics, "max_resize_ns",
            max_resize_ns) != 0 ||
        bench_metric_add_u64(&sample->metrics, "min_capacity_seen",
            (uint64_t)min_capacity_seen) != 0 ||
        bench_metric_add_u64(&sample->metrics, "max_capacity_seen",
            (uint64_t)max_capacity_seen) != 0) {
        return -1;
    }

    return 0;
}

static int bench_apply_op_public(
    ht_map         *map,
    const bench_op *op
) {
    ht_val_t value;
    ht_result rc;

    if (map == NULL || op == NULL) {
        return -1;
    }

    switch (op->kind) {
    case OP_GET_HIT:
        rc = ht_get(map, op->key, &value);
        return (rc == HT_OK) ? 0 : -1;
    case OP_GET_MISS:
        rc = ht_get(map, op->key, &value);
        return (rc == HT_ERR_NOT_FOUND) ? 0 : -1;
    case OP_INSERT:
        rc = ht_insert(map, op->key, op->value);
        return (rc == HT_OK) ? 0 : -1;
    case OP_REMOVE:
        rc = ht_remove(map, op->key);
        return (rc == HT_OK) ? 0 : -1;
    default:
        return -1;
    }
}

static int bench_add_core_metric_values(
    bench_sample   *sample,
    const ht_stats *stats,
    uint64_t        elapsed_ns,
    double          ns_per_op,
    double          ops_per_sec,
    size_t          initial_live,
    size_t          final_live,
    size_t          final_capacity,
    double          final_alpha,
    double          avg_probe_len,
    double          memory_amp_initial,
    double          memory_amp_final
) {
    if (bench_metric_add_u64(&sample->metrics, "elapsed_ns", elapsed_ns) != 0 ||
        bench_metric_add_f64(&sample->metrics, "ns_per_op", ns_per_op) != 0 ||
        bench_metric_add_f64(&sample->metrics, "ops_per_sec", ops_per_sec) != 0 ||
        bench_metric_add_u64(&sample->metrics, "initial_live",
            (uint64_t)initial_live) != 0 ||
        bench_metric_add_u64(&sample->metrics, "final_live",
            (uint64_t)final_live) != 0 ||
        bench_metric_add_u64(&sample->metrics, "final_capacity",
            (uint64_t)final_capacity) != 0 ||
        bench_metric_add_f64(&sample->metrics, "final_alpha", final_alpha) != 0 ||
        bench_metric_add_f64(&sample->metrics, "avg_probe_len",
            avg_probe_len) != 0 ||
        bench_metric_add_u64(&sample->metrics, "max_probe_len",
            stats->max_probe_len) != 0 ||
        bench_metric_add_f64(&sample->metrics, "memory_amplification_initial",
            memory_amp_initial) != 0 ||
        bench_metric_add_f64(&sample->metrics, "memory_amplification_final",
            memory_amp_final) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_lookups",
            stats->lookups) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_lookup_misses",
            stats->lookup_misses) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_inserts",
            stats->inserts) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_insert_failures",
            stats->insert_failures) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_removes",
            stats->removes) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_remove_misses",
            stats->remove_misses) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_probes",
            stats->probes) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_bytes_used",
            (uint64_t)stats->bytes_used) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_cleanup_requested",
            stats->cleanup_requested_count) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_cleanup_started",
            stats->cleanup_started_count) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_cleanup_completed",
            stats->cleanup_completed_count) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_cleanup_fallback",
            stats->cleanup_fallback_count) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_cleanup_shadow_publish",
            stats->cleanup_shadow_publish_count) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_cleanup_shadow_abandon",
            stats->cleanup_shadow_abandon_count) != 0 ||
        bench_metric_add_u64(&sample->metrics, "ht_stat_cleanup_log_peak_entries",
            stats->cleanup_log_peak_entries) != 0) {
        return -1;
    }

    return 0;
}
