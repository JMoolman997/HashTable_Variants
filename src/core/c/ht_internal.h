/**
 * @file    ht_internal.h
 * @brief   Internal dispatch and representation layer for hashtable backends.
 *			
 * Defines the internal virtual table used by the core wrapper to dispatch
 * operations to concrete implementations, together with the internal layout
 * of the generic hashtable wrapper object. 
 *
 * This header is for core and backend implementation code only and is not 
 * part of the public API.
 *
 * @author  J.W Moolman
 * @date    2026-03-23
 */

#ifndef HT_INTERNAL_H
#define HT_INTERNAL_H

#include "ht_types.h"
#include "ht_bench.h"

/* --- internal types ------------------------------------------------------ */

/**
 * @brief Backend dispatch table used by the generic wrapper.
 *
 * Each function pointer corresponds to one public operation exposed through
 * `ht.h`, plus the benchmark-binding hook used by timed benchmark loops.
 */
struct ht_vtable {
    /** Destroy a backend instance created by the corresponding create helper. */
    void (*destroy)(
        void *impl
    );

    /** Insert a key/value pair into the backend instance. */
    ht_result (*insert)(
        void *impl,
        ht_key_t key,
        ht_val_t value
    );

    /** Look up a key in the backend instance. */
    ht_result (*get)(
        const void *impl,
        ht_key_t key,
        ht_val_t *value_out
    );

    /** Remove a key from the backend instance. */
    ht_result (*remove)(
        void *impl,
        ht_key_t key
    );

    /** Return the current number of live entries. */
    size_t (*size)(
        const void *impl
    );

    /** Return the current slot or bucket capacity. */
    size_t (*capacity)(
        const void *impl
    );

    /** Return the current load factor. */
    double (*load_factor)(
        const void *impl
    );

    /** Ensure the backend can hold at least the requested capacity. */
    ht_result (*reserve)(
        void *impl,
        size_t capacity
    );

    /** Rebuild the backend around a requested capacity. */
    ht_result (*rehash)(
        void *impl,
        size_t capacity
    );

    /** Copy the backend statistics snapshot into the caller-provided output. */
    ht_result (*get_stats)(
        const void *impl,
        ht_stats *out
    );

    /** Reset the backend statistics counters. */
    ht_result (*reset_stats)(
        void *impl
    );

    /** Populate the low-overhead benchmark interface for timed loops. */
    int (*bind_bench_iface)(
        void *impl,
        bench_iface *out
    );

    /** Populate benchmark hooks with optional backend-specific hints. */
    int (*bind_bench_iface_flags)(
        void *impl,
        bench_iface *out,
        unsigned flags
    );
};

/**
 * @brief Opaque public wrapper around a concrete backend instance.
 */
typedef struct ht_map {
    /** Dispatch table for the selected backend implementation. */
    const struct ht_vtable *vt;
    /** Pointer to the concrete backend state object. */
    void *impl;
    /** Public implementation identifier used when the wrapper was created. */
    ht_impl kind;
} ht_map;

#endif /* HT_INTERNAL_H */
