/**
 * @file    bench_metric.c
 * @brief   Typed benchmark metric collection.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bench_metric.h"

static int bench_metric_add(
    bench_metric_set *set,
    bench_metric      metric
) {
    if (set == NULL || metric.name == NULL ||
        set->count >= (size_t)BENCH_METRIC_MAX) {
        return -1;
    }

    set->items[set->count] = metric;
    set->count++;
    return 0;
}

void bench_sample_init(
    bench_sample *sample,
    size_t        repetition
) {
    if (sample == NULL) {
        return;
    }

    memset(sample, 0, sizeof(*sample));
    sample->repetition = repetition;
}

int bench_metric_add_u64(
    bench_metric_set *set,
    const char       *name,
    uint64_t          value
) {
    bench_metric metric;

    memset(&metric, 0, sizeof(metric));
    metric.name = name;
    metric.type = BENCH_METRIC_U64;
    metric.value.u64 = value;
    return bench_metric_add(set, metric);
}

int bench_metric_add_f64(
    bench_metric_set *set,
    const char       *name,
    double            value
) {
    bench_metric metric;

    memset(&metric, 0, sizeof(metric));
    metric.name = name;
    metric.type = BENCH_METRIC_F64;
    metric.value.f64 = value;
    return bench_metric_add(set, metric);
}

const char *bench_metric_type_name(
    bench_metric_type type
) {
    switch (type) {
    case BENCH_METRIC_U64:
        return "u64";
    case BENCH_METRIC_F64:
        return "f64";
    default:
        return "unknown";
    }
}
