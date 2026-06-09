/**
 * @file    bench_metric.h
 * @brief   Typed benchmark metric collection.
 */

#ifndef BENCH_METRIC_H
#define BENCH_METRIC_H

#include <stddef.h>
#include <stdint.h>

#define BENCH_METRIC_MAX 64u

typedef enum {
    BENCH_METRIC_U64 = 0,
    BENCH_METRIC_F64 = 1
} bench_metric_type;

typedef struct {
    const char *name;
    bench_metric_type type;
    union {
        uint64_t u64;
        double   f64;
    } value;
} bench_metric;

typedef struct {
    bench_metric items[BENCH_METRIC_MAX];
    size_t count;
} bench_metric_set;

typedef struct {
    size_t repetition;
    bench_metric_set metrics;
} bench_sample;

void bench_sample_init(
    bench_sample *sample,
    size_t repetition
);

int bench_metric_add_u64(
    bench_metric_set *set,
    const char *name,
    uint64_t value
);

int bench_metric_add_f64(
    bench_metric_set *set,
    const char *name,
    double value
);

const char *bench_metric_type_name(
    bench_metric_type type
);

#endif /* BENCH_METRIC_H */
