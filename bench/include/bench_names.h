/**
 * @file    bench_names.h
 * @brief   Shared benchmark name helpers.
 */

#ifndef BENCH_NAMES_H
#define BENCH_NAMES_H

#include "bench_config.h"

const char *bench_scenario_name(
    bench_scenario scenario
);

const char *bench_backend_name(
    bench_backend_kind backend
);

const char *bench_workload_name(
    workload_kind workload
);

const char *bench_keyspace_name(
    bench_keyspace_mode keyspace_mode
);

const char *bench_resize_mode_name(
    bench_resize_mode resize_mode
);

const char *bench_stats_mode_name(
    bench_stats_mode stats_mode
);

int bench_parse_impl(
    const char *text,
    ht_impl *out
);

int bench_parse_workload(
    const char *text,
    workload_kind *out
);

int bench_parse_keyspace(
    const char *text,
    bench_keyspace_mode *out
);

int bench_parse_resize_mode(
    const char *text,
    bench_resize_mode *out
);

int bench_parse_stats_mode(
    const char *text,
    bench_stats_mode *out
);

int bench_parse_output_format(
    const char *text,
    bench_output_format *out
);

#endif /* BENCH_NAMES_H */
