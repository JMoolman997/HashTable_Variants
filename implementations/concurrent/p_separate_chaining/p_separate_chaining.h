/**
 * @file    p_separate_chaining.h
 * @brief   Concurrent segmented separate-chaining backend.
 *
 * This backend stores each bucket as a short chain of fixed-capacity segments.
 * Each segment keeps keys and values in separate contiguous arrays, while
 * table mutation is protected by padded stripe mutexes.
 *
 * This header is intended for the core wrapper and benchmark binding code. It
 * is not part of the public user-facing API.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#ifndef P_SEPARATE_CHAINING_H
#define P_SEPARATE_CHAINING_H

#include "ht_bench.h"
#include "ht_types.h"

struct ht_vtable;

/**
 * @brief Allocate and initialize a concurrent separate-chaining backend.
 *
 * @param cfg Backend configuration used to size and initialize the table.
 *
 * @return An initialized backend object on success, or `NULL` if `cfg` is
 *         invalid or allocation fails.
 */
ht_result p_separate_chaining_create_impl_ex(
    const ht_config *cfg,
    void           **out
);

/**
 * @brief Return the backend vtable used by the core wrapper.
 *
 * @return A pointer to the static concurrent separate-chaining dispatch table.
 */
const struct ht_vtable *p_separate_chaining_vtable(
    void
);

/**
 * @brief Populate a benchmark interface with direct backend operations.
 *
 * @param ctx Backend instance to expose through the benchmark hooks.
 * @param out Output interface to populate.
 *
 * @return `HT_OK` on success, or `HT_ERR_INVALID` if the inputs are invalid.
 */
int p_separate_chaining_bind_bench_iface(
    void *ctx,
    bench_iface *out
);

#endif /* P_SEPARATE_CHAINING_H */
