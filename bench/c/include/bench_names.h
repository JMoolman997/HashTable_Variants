/**
 * @file    bench_names.h
 * @brief   Shared benchmark name helpers.
 *
 * Provides one source of truth for benchmark, implementation, and workload
 * names used by the CLI and CSV output.
 *
 * @author  J.W Moolman
 * @date    2026-04-17
 */

#ifndef BENCH_NAMES_H
#define BENCH_NAMES_H

#include "bench_config.h"

/**
 * @brief Return the canonical CLI/CSV name for a benchmark kind.
 */
const char *bench_kind_name(
    bench_kind kind
);

/**
 * @brief Return the canonical CLI/CSV name for an implementation.
 */
const char *bench_impl_name(
    ht_impl impl
);

/**
 * @brief Return non-zero when an implementation enum has a canonical name.
 */
int bench_impl_is_known(
    ht_impl impl
);

/**
 * @brief Return the canonical CLI/CSV name for a workload mix.
 */
const char *bench_workload_name(
    workload_kind workload
);

/**
 * @brief Return the canonical CSV name for a concurrent keyspace mode.
 */
const char *bench_keyspace_name(
    bench_keyspace_mode keyspace_mode
);

/**
 * @brief Return the canonical CLI/CSV name for a resize mode.
 */
const char *bench_resize_mode_name(
    bench_resize_mode resize_mode
);

/**
 * @brief Parse a canonical benchmark name.
 */
int bench_parse_kind(
    const char *text,
    bench_kind *out
);

/**
 * @brief Parse a canonical implementation name.
 */
int bench_parse_impl(
    const char *text,
    ht_impl *out
);

/**
 * @brief Parse a canonical workload name.
 */
int bench_parse_workload(
    const char *text,
    workload_kind *out
);

/**
 * @brief Parse a supported user-facing keyspace name.
 */
int bench_parse_keyspace(
    const char *text,
    bench_keyspace_mode *out
);

/**
 * @brief Parse a canonical resize mode name.
 */
int bench_parse_resize_mode(
    const char *text,
    bench_resize_mode *out
);

#endif /* BENCH_NAMES_H */
