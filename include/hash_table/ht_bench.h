/**
 * @file    ht_bench.h
 * @brief   Benchmark binding interface for low-overhead hot-path execution.
 *
 * Defines the bound benchmark interface used to call backend operations
 * directly after a generic hashtable instance has been created.
 *
 * Timed benchmark loops use this to avoid repeated public-wrapper dispatch.
 *
 * @author  J.W Moolman
 * @date    2026-03-23
 */

#ifndef HT_BENCH_H
#define HT_BENCH_H

#include "ht.h"

/**
 * @brief Backend operation hooks bound to one concrete table instance.
 */
typedef struct {
    void *ctx; /**< Backend instance passed to each operation hook. */

    ht_result (*insert)(
        void *ctx,
        ht_key_t key,
        ht_val_t value
    ); /**< Direct insert operation for benchmark loops. */

    ht_result (*get)(
        const void *ctx,
        ht_key_t key,
        ht_val_t *value_out
    ); /**< Direct lookup operation for benchmark loops. */

    ht_result (*remove)(
        void *ctx,
        ht_key_t key
    ); /**< Direct remove operation for benchmark loops. */
} bench_iface;

/** Hint that the benchmark phase will perform only reads after setup. */
#define BENCH_IFACE_FROZEN_READ_ONLY (1u << 0)

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Bind a generic hashtable instance to benchmark operation hooks.
 *
 * @param map Hashtable instance whose backend operations should be exposed.
 * @param out Output interface populated with backend insert/get/remove hooks.
 *
 * @return `HT_OK` on success, `HT_ERR_INVALID` for bad inputs, or
 *         `HT_ERR_UNSUPPORTED` if the backend does not expose a benchmark
 *         interface.
 */
int ht_bind_bench_iface(
    ht_map *map,
    bench_iface *out
);

/**
 * @brief Bind benchmark operations with optional backend hints.
 *
 * Unsupported optimization hints may be ignored by a backend; the returned
 * interface remains semantically equivalent to ht_bind_bench_iface().
 *
 * @param map Hashtable instance whose backend operations should be exposed.
 * @param out Output interface populated with backend operation hooks.
 * @param flags Bitset of BENCH_IFACE_* hints.
 *
 * @return `HT_OK` on success, `HT_ERR_INVALID` for bad inputs, or
 *         `HT_ERR_UNSUPPORTED` if the backend has no benchmark interface.
 */
int ht_bind_bench_iface_flags(
    ht_map *map,
    bench_iface *out,
    unsigned flags
);

#endif /* HT_BENCH_H */
