/**
 * @file    bench_backend_single.c
 * @brief   Single-thread benchmark backend.
 */

#include <stddef.h>
#include <stdint.h>

#include "bench_backend_single.h"
#include "bench_runner_common.h"

static unsigned bench_backend_single_iface_flags(
    const bench_plan *plan
);

int bench_backend_single_run(
    const bench_plan    *plan,
    const bench_fixture *fixture,
    bench_sample        *sample
) {
    bench_env env = {0};
    uint64_t elapsed_ns;
    int rc = -1;

    if (plan == NULL || fixture == NULL || sample == NULL ||
        fixture->timed_ops == NULL || fixture->timed_op_count == 0) {
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
    if (bench_run_warmup(
            plan,
            fixture->initial_keys,
            fixture->initial_count,
            fixture->warmup_ops,
            fixture->warmup_op_count
        ) != 0) {
        goto cleanup;
    }
    if (bench_env_prepare_timed_phase(
            &env,
            bench_backend_single_iface_flags(plan)
        ) != 0) {
        goto cleanup;
    }
    if (bench_measure_timed_ops(
            &env.bi,
            fixture->timed_ops,
            fixture->timed_op_count,
            &elapsed_ns
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

static unsigned bench_backend_single_iface_flags(
    const bench_plan *plan
) {
    if (plan == NULL) {
        return 0;
    }

    if (plan->scenario == BENCH_SCENARIO_LOOKUP_HIT ||
        plan->scenario == BENCH_SCENARIO_LOOKUP_MISS ||
        (plan->scenario == BENCH_SCENARIO_WORKLOAD &&
         plan->workload == WORKLOAD_READ_ONLY)) {
        return BENCH_IFACE_FROZEN_READ_ONLY;
    }

    return 0;
}
