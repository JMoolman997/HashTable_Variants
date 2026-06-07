/**
 * @file    bench_trace.h
 * @brief   Workload trace construction helpers.
 *
 * Declares the benchmark operation trace types and helpers used to build a
 * deterministic workload trace before entering the timed hot loop.
 *
 * @author  J.W Moolman
 * @date    2026-03-31
 */

#ifndef BENCH_TRACE_H
#define BENCH_TRACE_H

#include <stddef.h>
#include <stdint.h>

#include "bench_config.h"
#include "ht_types.h"

/* --- type definitions ----------------------------------------------------- */

typedef enum {
    OP_GET_HIT = 0,
    OP_GET_MISS = 1,
    OP_INSERT = 2,
    OP_REMOVE = 3
} bench_op_kind;

typedef struct {
    bench_op_kind kind;
    ht_key_t      key;
    ht_val_t      value;
} bench_op;

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Build a deterministic workload operation trace.
 *
 * @param workload Workload mix to generate.
 * @param timed_ops Number of timed operations to place in the trace.
 * @param trace_seed Seed used to randomize operation ordering and picks.
 * @param initial_keys Keys present in the table before the timed phase.
 * @param initial_live Number of live keys in `initial_keys`.
 * @param ops_out Output pointer that receives the allocated operation array.
 *
 * @return `0` on success, or `-1` if validation or allocation fails.
 */
int bench_trace_build_workload(
    workload_kind    workload,
    size_t           timed_ops,
    uint64_t         trace_seed,
    const ht_key_t  *initial_keys,
    size_t           initial_live,
    bench_op       **ops_out
);

/**
 * @brief Build one operation trace per concurrent worker.
 *
 * @param workload Workload mix to generate for mutable worker traces.
 * @param timed_ops Total operations to split across workers.
 * @param thread_count Number of worker traces to build.
 * @param trace_seed Seed used to randomize operation ordering and picks.
 * @param initial_keys Keys present before the timed phase.
 * @param initial_live Number of live keys in `initial_keys`.
 * @param insert_keys Absent keys available for insert operations.
 * @param insert_key_count Number of keys in `insert_keys`.
 * @param keyspace_mode Worker keyspace sharing mode.
 * @param ops_out Output pointer that receives the worker trace array.
 * @param op_counts_out Output pointer that receives per-worker op counts.
 *
 * @return `0` on success, or `-1` if validation or allocation fails.
 */
int bench_trace_build_partitioned_workloads(
    workload_kind       workload,
    size_t              timed_ops,
    size_t              thread_count,
    uint64_t            trace_seed,
    const ht_key_t     *initial_keys,
    size_t              initial_live,
    const ht_key_t     *insert_keys,
    size_t              insert_key_count,
    bench_keyspace_mode keyspace_mode,
    bench_op         ***ops_out,
    size_t            **op_counts_out
);

/**
 * @brief Free a workload trace produced by bench_trace_build_workload().
 *
 * @param ops Operation array to free. `NULL` is allowed.
 */
void bench_trace_free(
    bench_op *ops
);

#endif /* BENCH_TRACE_H */
