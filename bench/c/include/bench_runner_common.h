/**
 * @file    bench_runner_common.h
 * @brief   Shared benchmark execution helpers.
 *
 * Declares the low-level table setup, warm-up, timing, and result-filling
 * utilities shared by all benchmark execution families.
 *
 * @author  J.W Moolman
 * @date    2026-04-20
 */

#ifndef BENCH_RUNNER_COMMON_H
#define BENCH_RUNNER_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "bench_plan.h"
#include "ht.h"
#include "ht_bench.h"
#include "bench_trace.h"

typedef struct {
    ht_map      *map;
    bench_iface  bi;
    ht_stats     stats;
    ht_config    cfg;
} bench_env;

typedef int (*bench_timed_step_fn)(
    const bench_iface *bi,
    size_t             index,
    const void         *ctx
);

typedef int (*bench_public_step_fn)(
    ht_map         *map,
    size_t          index,
    const void     *ctx
);

typedef enum {
    BENCH_KEY_OP_INSERT = 0,
    BENCH_KEY_OP_REMOVE = 1
} bench_key_op_kind;

typedef enum {
    BENCH_EXEC_PUBLIC = 0, /* Warm-up uses the public ht_* API. */
    BENCH_EXEC_DIRECT = 1  /* Timed loops use the bound bench_iface. */
} bench_exec_kind;

typedef struct {
    bench_exec_kind     kind;
    ht_map             *map;
    const bench_iface  *bi;
} bench_exec_iface;

typedef struct {
    const ht_key_t   *keys;
    bench_key_op_kind  op;
    const char       *error_msg;
} bench_key_step_ctx;

typedef struct {
    const ht_key_t *keys;
    size_t          key_count;
    ht_result       expected_result;
    const char     *bench_name;
} bench_lookup_ctx;

typedef struct {
    const bench_op *ops;
    const char     *error_msg;
} bench_workload_step_ctx;

/**
 * @brief Build the hashtable config for a normalized benchmark plan.
 */
int bench_build_ht_config_from_plan(
    const bench_plan *plan,
    ht_config        *cfg,
    int               collect_stats
);

/**
 * @brief Insert an existing key set into a table before a benchmark phase.
 */
int bench_prepopulate_table(
    ht_map          *map,
    const ht_key_t *keys,
    size_t           key_count
);

/**
 * @brief Generate the populated keyset used by lookup and erase benchmarks.
 */
int bench_generate_populated_dataset(
    const bench_plan *plan,
    const char       *bench_name,
    size_t           *live_out,
    ht_key_t        **keys_out
);

/**
 * @brief Run an untimed public-API warm-up phase on a fresh table.
 */
int bench_run_warmup(
    const bench_plan   *plan,
    const ht_key_t     *prepopulate_keys,
    size_t              prepopulate_count,
    bench_public_step_fn step,
    const void         *ctx
);

/**
 * @brief Warm up lookups against an already populated table.
 */
int bench_run_lookup_warmup(
    const ht_map    *map,
    const ht_key_t  *keys,
    size_t           key_count,
    size_t           warmup_ops,
    ht_result        expected_result,
    const char      *bench_name
);

/**
 * @brief Apply one insert/remove key-array operation through the public API.
 */
int bench_step_key_array_warmup(
    ht_map         *map,
    size_t          index,
    const void     *ctx
);

/**
 * @brief Apply one insert/remove key-array operation through bench_iface.
 */
int bench_step_key_array_timed(
    const bench_iface *bi,
    size_t             index,
    const void        *ctx
);

/**
 * @brief Apply one lookup operation through the public API.
 */
int bench_step_lookup_warmup(
    ht_map         *map,
    size_t          index,
    const void     *ctx
);

/**
 * @brief Apply one lookup operation through bench_iface.
 */
int bench_step_lookup(
    const bench_iface *bi,
    size_t             index,
    const void        *ctx
);

/**
 * @brief Apply one prebuilt workload operation through the public API.
 */
int bench_step_workload_warmup(
    ht_map         *map,
    size_t          index,
    const void     *ctx
);

/**
 * @brief Apply one prebuilt workload operation through bench_iface.
 */
int bench_step_workload(
    const bench_iface *bi,
    size_t             index,
    const void        *ctx
);

/**
 * @brief Create a benchmark table environment from a plan.
 */
int bench_env_init(
    const bench_plan *plan,
    bench_env        *env
);

/**
 * @brief Destroy and clear a benchmark table environment.
 */
void bench_env_destroy(
    bench_env *env
);

/**
 * @brief Reserve, reset stats, and bind direct calls for the timed phase.
 */
int bench_env_prepare_timed_phase(
    bench_env *env
);

/**
 * @brief Read benchmark stats from the environment table.
 */
int bench_env_collect_stats(
    bench_env *env
);

/**
 * @brief Measure a fixed-count direct-call timed loop.
 */
int bench_measure_timed_loop(
    size_t               op_count,
    bench_timed_step_fn  step,
    const bench_iface    *bi,
    const void           *ctx,
    uint64_t             *elapsed_ns
);

/**
 * @brief Run the common timed phase and fill the core result record.
 */
int bench_run_timed_repetition(
    bench_env          *env,
    const bench_plan   *plan,
    size_t              initial_live,
    bench_timed_step_fn step,
    const void         *ctx,
    bench_result       *result
);

/**
 * @brief Populate derived metrics in a core benchmark result.
 */
void bench_fill_result(
    const bench_plan *plan,
    const ht_map     *map,
    const ht_stats   *stats,
    size_t            initial_live,
    uint64_t          elapsed_ns,
    bench_result     *result
);

/**
 * @brief Apply one prebuilt workload operation through public or direct calls.
 */
int bench_apply_workload_op(
    const bench_exec_iface *iface,
    const bench_op         *op
);

#endif /* BENCH_RUNNER_COMMON_H */
