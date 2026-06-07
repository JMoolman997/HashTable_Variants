/**
 * @file    bench_output.c
 * @brief   CSV output helpers for benchmark results.
 *
 * Implements CSV header and row emitters for raw per-repetition benchmark
 * output.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#include <stddef.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "bench_output.h"
#include "bench_names.h"

#define BENCH_CSV_RESULT_COLUMNS \
    "elapsed_ns,ns_per_op,ops_per_sec,"

#define BENCH_CSV_STATS_COLUMNS \
    "inserts,insert_failures,lookups,lookup_misses," \
    "removes,remove_misses,probes,max_probe_len," \
    "cleanup_requested,cleanup_started,cleanup_completed," \
    "cleanup_fallback,cleanup_shadow_publish," \
    "cleanup_shadow_abandon,cleanup_log_peak_entries,"

#define BENCH_CSV_MEMORY_COLUMNS \
    "bytes_used,memory_amplification," \
    "avg_probe_len,final_live,final_capacity,final_alpha," \
    "memory_amp_initial,memory_amp_final"

#define BENCH_CSV_CORE_RESULT_COLUMNS \
    BENCH_CSV_RESULT_COLUMNS \
    BENCH_CSV_STATS_COLUMNS \
    BENCH_CSV_MEMORY_COLUMNS

static const char *bench_hash_name(
    ht_hash_fn hash_fn
) {
    return (hash_fn == NULL) ? "default" : "custom";
}

static uint64_t bench_row_seed(
    const bench_spec *spec
) {
    if (spec == NULL) {
        return 0;
    }

    /* Workload rows identify the trace; fixed-key rows identify the keyset. */
    return (spec->kind == BENCH_WORKLOAD ||
            spec->kind == BENCH_RESIZE_WORKLOAD ||
            spec->kind == BENCH_CONCURRENT_WORKLOAD)
        ? spec->trace_seed
        : spec->key_seed;
}

static const char *bench_concurrent_mutation_mode_name(
    const bench_run_result *result
) {
    const bench_spec *spec;

    if (result == NULL) {
        return "unknown";
    }

    spec = &result->core.spec;
    if (spec->kind == BENCH_CONCURRENT_LOOKUP) {
        return "read-only";
    } /* Lookup workers never mutate the table. */

    if (result->concurrent.thread_count <= 1) {
        return "single-thread";
    } /* A one-thread workload has no concurrent mutation path to report. */

    return result->concurrent.harness_op_lock
        ? "harness-serialized"
        : (result->concurrent.true_concurrent_mutation
            ? "true-concurrent"
            : "unlocked");
}

void bench_output_write_csv_header(
    FILE *stream
) {
    if (stream == NULL) {
        return;
    }

    fprintf(stream,
            "benchmark,impl,hash_fn,hash_seed,"
            "dataset_size,capacity,target_alpha,initial_size,"
            "workload,warmup_ops,timed_ops,repetition,seed,"
            BENCH_CSV_CORE_RESULT_COLUMNS "\n");
    /* Keep header order matched to bench_output_write_csv_row(). */
}

void bench_output_write_csv_row(
    FILE *stream,
    const bench_result *result,
    size_t repetition
) {
    const bench_spec *spec;

    if (stream == NULL || result == NULL) {
        return;
    }

    spec = &result->spec;

    fprintf(stream,
            "%s,%s,%s,%" PRIu64 ","
            "%zu,%zu,%.6f,%zu,"
            "%s,%zu,%zu,%zu,%" PRIu64 ","
            "%" PRIu64 ",%.6f,%.6f,"
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%zu,%.6f,"
            "%.6f,%zu,%zu,%.6f,%.6f,%.6f\n",
            bench_kind_name(spec->kind),
            bench_impl_name(spec->impl_kind),
            bench_hash_name(spec->hash_fn),
            spec->hash_seed,
            spec->dataset_size,
            spec->capacity,
            spec->target_alpha,
            result->initial_live,
            (spec->kind == BENCH_WORKLOAD ||
             spec->kind == BENCH_RESIZE_WORKLOAD)
                ? bench_workload_name(spec->workload)
                : "n/a",
            spec->warmup_ops,
            spec->timed_ops,
            repetition,
            bench_row_seed(spec),
            result->elapsed_ns,
            result->ns_per_op,
            result->ops_per_sec,
            result->stats.inserts,
            result->stats.insert_failures,
            result->stats.lookups,
            result->stats.lookup_misses,
            result->stats.removes,
            result->stats.remove_misses,
            result->stats.probes,
            result->stats.max_probe_len,
            result->stats.cleanup_requested_count,
            result->stats.cleanup_started_count,
            result->stats.cleanup_completed_count,
            result->stats.cleanup_fallback_count,
            result->stats.cleanup_shadow_publish_count,
            result->stats.cleanup_shadow_abandon_count,
            result->stats.cleanup_log_peak_entries,
            result->stats.bytes_used,
            result->memory_amplification,
            result->avg_probe_len,
            result->final_live,
            result->final_capacity,
            result->final_alpha,
            result->memory_amp_initial,
            result->memory_amp_final);
    /* Column order is part of the benchmark archive contract. */
}

void bench_output_write_resize_csv_header(
    FILE *stream
) {
    if (stream == NULL) {
        return;
    }

    /* Resize rows record both the starting capacity and the post-run capacity. */
    fprintf(stream,
            "benchmark,impl,hash_fn,hash_seed,"
            "dataset_size,initial_capacity,capacity,target_alpha,initial_size,"
            "workload,resize_mode,grow_alpha,shrink_alpha,prefill_alpha,"
            "warmup_ops,timed_ops,repetition,seed,"
            BENCH_CSV_CORE_RESULT_COLUMNS ","
            "resize_events,grow_events,shrink_events,resize_entries_moved,"
            "total_resize_ns,"
            "max_resize_ns,min_capacity_seen,max_capacity_seen\n");
}

void bench_output_write_resize_csv_row(
    FILE *stream,
    const bench_run_result *result,
    size_t repetition
) {
    const bench_spec *spec;

    if (stream == NULL || result == NULL) {
        return;
    }

    spec = &result->core.spec;

    fprintf(stream,
            "%s,%s,%s,%" PRIu64 ","
            "%zu,%zu,%zu,%.6f,%zu,"
            "%s,%s,%.6f,%.6f,%.6f,"
            "%zu,%zu,%zu,%" PRIu64 ","
            "%" PRIu64 ",%.6f,%.6f,"
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%zu,%.6f,"
            "%.6f,%zu,%zu,%.6f,%.6f,%.6f,"
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ","
            "%" PRIu64 ",%zu,%zu\n",
            bench_kind_name(spec->kind),
            bench_impl_name(spec->impl_kind),
            bench_hash_name(spec->hash_fn),
            spec->hash_seed,
            spec->dataset_size,
            spec->initial_capacity,
            result->core.final_capacity,
            spec->target_alpha,
            result->core.initial_live,
            (spec->kind == BENCH_WORKLOAD ||
             spec->kind == BENCH_RESIZE_WORKLOAD)
                ? bench_workload_name(spec->workload)
                : "n/a",
            bench_resize_mode_name(spec->resize_mode),
            spec->grow_alpha,
            spec->shrink_alpha,
            spec->prefill_alpha,
            spec->warmup_ops,
            spec->timed_ops,
            repetition,
            bench_row_seed(spec),
            result->core.elapsed_ns,
            result->core.ns_per_op,
            result->core.ops_per_sec,
            result->core.stats.inserts,
            result->core.stats.insert_failures,
            result->core.stats.lookups,
            result->core.stats.lookup_misses,
            result->core.stats.removes,
            result->core.stats.remove_misses,
            result->core.stats.probes,
            result->core.stats.max_probe_len,
            result->core.stats.cleanup_requested_count,
            result->core.stats.cleanup_started_count,
            result->core.stats.cleanup_completed_count,
            result->core.stats.cleanup_fallback_count,
            result->core.stats.cleanup_shadow_publish_count,
            result->core.stats.cleanup_shadow_abandon_count,
            result->core.stats.cleanup_log_peak_entries,
            result->core.stats.bytes_used,
            result->core.memory_amplification,
            result->core.avg_probe_len,
            result->core.final_live,
            result->core.final_capacity,
            result->core.final_alpha,
            result->core.memory_amp_initial,
            result->core.memory_amp_final,
            result->resize.resize_events,
            result->resize.grow_events,
            result->resize.shrink_events,
            result->resize.resize_entries_moved,
            result->resize.total_resize_ns,
            result->resize.max_resize_ns,
            result->resize.min_capacity_seen,
            result->resize.max_capacity_seen);
}

void bench_output_write_concurrent_csv_header(
    FILE *stream
) {
    if (stream == NULL) {
        return;
    }

    fprintf(stream,
            "benchmark,impl,hash_fn,hash_seed,"
            "dataset_size,capacity,target_alpha,initial_size,"
            "workload,concurrent_mutation_mode,keyspace_mode,"
            "harness_op_lock,concurrent_resize,"
            "warmup_ops,timed_ops,repetition,seed,"
            BENCH_CSV_CORE_RESULT_COLUMNS ","
            "thread_count,wall_elapsed_ns,total_ops_per_sec,"
            "ops_per_sec_per_thread,worker_failures\n");
    /* Concurrent rows append thread-level wall-clock metrics. */
}

void bench_output_write_concurrent_csv_row(
    FILE *stream,
    const bench_run_result *result,
    size_t repetition
) {
    const bench_spec *spec;

    if (stream == NULL || result == NULL) {
        return;
    }

    spec = &result->core.spec;

    fprintf(stream,
            "%s,%s,%s,%" PRIu64 ","
            "%zu,%zu,%.6f,%zu,"
            "%s,%s,%s,%d,%s,%zu,%zu,%zu,%" PRIu64 ","
            "%" PRIu64 ",%.6f,%.6f,"
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%zu,%.6f,"
            "%.6f,%zu,%zu,%.6f,%.6f,%.6f,"
            "%zu,%" PRIu64 ",%.6f,%.6f,%" PRIu64 "\n",
            bench_kind_name(spec->kind),
            bench_impl_name(spec->impl_kind),
            bench_hash_name(spec->hash_fn),
            spec->hash_seed,
            spec->dataset_size,
            spec->capacity,
            spec->target_alpha,
            result->core.initial_live,
            (spec->kind == BENCH_WORKLOAD ||
             spec->kind == BENCH_CONCURRENT_WORKLOAD)
                ? bench_workload_name(spec->workload)
                : "n/a",
            bench_concurrent_mutation_mode_name(result),
            bench_keyspace_name(result->concurrent.keyspace_mode),
            result->concurrent.harness_op_lock,
            bench_resize_mode_name(result->concurrent.resize_mode),
            spec->warmup_ops,
            spec->timed_ops,
            repetition,
            bench_row_seed(spec),
            result->core.elapsed_ns,
            result->core.ns_per_op,
            result->core.ops_per_sec,
            result->core.stats.inserts,
            result->core.stats.insert_failures,
            result->core.stats.lookups,
            result->core.stats.lookup_misses,
            result->core.stats.removes,
            result->core.stats.remove_misses,
            result->core.stats.probes,
            result->core.stats.max_probe_len,
            result->core.stats.cleanup_requested_count,
            result->core.stats.cleanup_started_count,
            result->core.stats.cleanup_completed_count,
            result->core.stats.cleanup_fallback_count,
            result->core.stats.cleanup_shadow_publish_count,
            result->core.stats.cleanup_shadow_abandon_count,
            result->core.stats.cleanup_log_peak_entries,
            result->core.stats.bytes_used,
            result->core.memory_amplification,
            result->core.avg_probe_len,
            result->core.final_live,
            result->core.final_capacity,
            result->core.final_alpha,
            result->core.memory_amp_initial,
            result->core.memory_amp_final,
            result->concurrent.thread_count,
            result->concurrent.wall_elapsed_ns,
            result->concurrent.total_ops_per_sec,
            result->concurrent.ops_per_sec_per_thread,
            result->concurrent.worker_failures);
}
