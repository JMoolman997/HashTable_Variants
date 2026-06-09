/**
 * @file    bench_output.c
 * @brief   Benchmark result output helpers.
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "bench_names.h"
#include "bench_output.h"
#include "ht.h"

static const char *bench_output_workload_field(
    const bench_plan *plan
);

static const char *bench_output_resize_mode_field(
    const bench_plan *plan
);

static void bench_output_write_metric_value(
    FILE               *stream,
    const bench_metric *metric
);

void bench_output_write_canonical_csv_header(
    FILE *stream
) {
    if (stream == NULL) {
        return;
    }

    fprintf(
        stream,
        "benchmark,scenario,backend,impl,repetition,workload,"
        "dataset_size,timed_ops,warmup_ops,target_alpha,"
        "initial_capacity,planned_capacity,resize_mode,thread_count,"
        "keyspace_mode,key_seed,trace_seed,hash_seed,stats_mode,"
        "metric,type,value\n"
    );
}

void bench_output_write_canonical_csv_sample(
    FILE               *stream,
    const bench_plan   *plan,
    const bench_sample *sample
) {
    size_t i;

    if (stream == NULL || plan == NULL || sample == NULL) {
        return;
    }

    for (i = 0; i < sample->metrics.count; i++) {
        const bench_metric *metric = &sample->metrics.items[i];

        fprintf(
            stream,
            "%s,%s,%s,%s,%zu,%s,"
            "%zu,%zu,%zu,%.6f,"
            "%zu,%zu,%s,%zu,"
            "%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,"
            "%s,%s,",
            plan->benchmark_name,
            bench_scenario_name(plan->scenario),
            bench_backend_name(plan->backend),
            ht_impl_name(plan->impl_kind),
            sample->repetition,
            bench_output_workload_field(plan),
            plan->dataset_size,
            plan->timed_ops,
            plan->warmup_ops,
            plan->target_alpha,
            plan->initial_capacity,
            plan->planned_capacity,
            bench_output_resize_mode_field(plan),
            plan->thread_count,
            bench_keyspace_name(plan->keyspace_mode),
            plan->key_seed,
            plan->trace_seed,
            plan->hash_seed,
            bench_stats_mode_name(plan->stats_mode),
            metric->name,
            bench_metric_type_name(metric->type)
        );
        bench_output_write_metric_value(stream, metric);
        fputc('\n', stream);
    }
}

void bench_output_write_text_sample(
    FILE               *stream,
    const bench_plan   *plan,
    const bench_sample *sample
) {
    size_t i;

    if (stream == NULL || plan == NULL || sample == NULL) {
        return;
    }

    fprintf(
        stream,
        "%s scenario=%s backend=%s impl=%s repetition=%zu\n",
        plan->benchmark_name,
        bench_scenario_name(plan->scenario),
        bench_backend_name(plan->backend),
        ht_impl_name(plan->impl_kind),
        sample->repetition + 1u
    );
    fprintf(
        stream,
        "  workload=%s dataset_size=%zu timed_ops=%zu warmup_ops=%zu "
        "target_alpha=%.6f initial_capacity=%zu planned_capacity=%zu\n",
        bench_output_workload_field(plan),
        plan->dataset_size,
        plan->timed_ops,
        plan->warmup_ops,
        plan->target_alpha,
        plan->initial_capacity,
        plan->planned_capacity
    );
    fprintf(
        stream,
        "  resize_mode=%s thread_count=%zu keyspace_mode=%s "
        "key_seed=%" PRIu64 " trace_seed=%" PRIu64 " hash_seed=%" PRIu64
        " stats_mode=%s\n",
        bench_output_resize_mode_field(plan),
        plan->thread_count,
        bench_keyspace_name(plan->keyspace_mode),
        plan->key_seed,
        plan->trace_seed,
        plan->hash_seed,
        bench_stats_mode_name(plan->stats_mode)
    );

    for (i = 0; i < sample->metrics.count; i++) {
        if ((i % 4u) == 0u) {
            fputs("  ", stream);
        }
        fprintf(stream, "%s=", sample->metrics.items[i].name);
        bench_output_write_metric_value(stream, &sample->metrics.items[i]);
        if ((i % 4u) == 3u || i + 1u == sample->metrics.count) {
            fputc('\n', stream);
        } else {
            fputc(' ', stream);
        }
    }
}

static const char *bench_output_workload_field(
    const bench_plan *plan
) {
    if (plan != NULL && plan->scenario == BENCH_SCENARIO_WORKLOAD) {
        return bench_workload_name(plan->workload);
    }

    return "n/a";
}

static const char *bench_output_resize_mode_field(
    const bench_plan *plan
) {
    if (plan == NULL) {
        return "unknown";
    }
    if (plan->backend == BENCH_BACKEND_PTHREAD) {
        return bench_resize_mode_name(plan->concurrent_resize_mode);
    }

    return bench_resize_mode_name(plan->resize_mode);
}

static void bench_output_write_metric_value(
    FILE               *stream,
    const bench_metric *metric
) {
    if (stream == NULL || metric == NULL) {
        return;
    }

    switch (metric->type) {
    case BENCH_METRIC_U64:
        fprintf(stream, "%" PRIu64, metric->value.u64);
        break;
    case BENCH_METRIC_F64:
        fprintf(stream, "%.6f", metric->value.f64);
        break;
    default:
        fputs("unknown", stream);
        break;
    }
}
