/**
 * @file    bench_trace.c
 * @brief   Workload trace construction helpers.
 *
 * Builds deterministic pre-generated workload traces so benchmark runners can
 * replay them without doing key selection or workload decisions inside the
 * timed loop.
 *
 * @author  J.W Moolman
 * @date    2026-03-31
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bench_dataset.h"
#include "bench_random.h"
#include "bench_trace.h"

/* --- type definitions ----------------------------------------------------- */

typedef struct {
    size_t lookup;
    size_t insert;
    size_t remove;
} bench_workload_ratio;

static const bench_workload_ratio bench_workload_ratios[] = {
    [WORKLOAD_READ_ONLY]  = {1000, 0, 0},
    [WORKLOAD_READ_HEAVY] = {950, 25, 25},
    [WORKLOAD_MIXED]      = {800, 100, 100},
    [WORKLOAD_BALANCED]   = {500, 250, 250},
};

/* --- function prototypes: helpers ----------------------------------------- */

/**
 * @brief Convert workload weights into exact timed operation counts.
 *
 * @param workload Workload profile to decode.
 * @param timed_ops Total number of timed operations requested.
 * @param lookup_count_out Output location for the lookup count.
 * @param insert_count_out Output location for the insert count.
 * @param remove_count_out Output location for the remove count.
 */
static void bench_trace_workload_counts(
    workload_kind workload,
    size_t        timed_ops,
    size_t        *lookup_count_out,
    size_t        *insert_count_out,
    size_t        *remove_count_out
);

/**
 * @brief Shuffle a prebuilt operation array deterministically.
 *
 * @param ops Operation array to shuffle.
 * @param count Number of operations in `ops`.
 * @param seed Seed used by the deterministic shuffler.
 */
static void bench_trace_shuffle_ops(
    bench_op *ops,
    size_t   count,
    uint64_t seed
);

static int bench_trace_build_mutating_workload(
    workload_kind   workload,
    size_t          timed_ops,
    uint64_t        trace_seed,
    const ht_key_t *initial_keys,
    size_t          initial_live,
    const ht_key_t *insert_keys,
    size_t          insert_key_count,
    bench_op      **ops_out
);

static int bench_trace_build_shared_read_workload(
    size_t          timed_ops,
    uint64_t        trace_seed,
    const ht_key_t *initial_keys,
    size_t          initial_live,
    bench_op      **ops_out
);

static int bench_trace_build_thread_workload(
    workload_kind       workload,
    size_t              timed_ops,
    uint64_t            trace_seed,
    const ht_key_t     *initial_keys,
    size_t              initial_live,
    const ht_key_t     *insert_keys,
    size_t              insert_key_count,
    bench_keyspace_mode keyspace_mode,
    bench_op          **ops_out
);

int bench_trace_build_workload(
    workload_kind    workload,
    size_t           timed_ops,
    uint64_t         trace_seed,
    const ht_key_t  *initial_keys,
    size_t           initial_live,
    bench_op       **ops_out
) {
    ht_key_t *insert_keys = NULL;
    size_t lookup_count;
    size_t insert_count;
    size_t remove_count;
    int rc = -1;

    if (ops_out == NULL) {
        return -1;
    }

    *ops_out = NULL;

    if (timed_ops == 0) {
        return 0;
    }

    if (initial_keys == NULL && initial_live > 0) {
        return -1;
    }

    bench_trace_workload_counts(
        workload,
        timed_ops,
        &lookup_count,
        &insert_count,
        &remove_count
    );

    if (insert_count > 0 &&
        bench_dataset_generate_missing_keys(
            initial_keys,
            initial_live,
            trace_seed,
            insert_count,
            &insert_keys
        ) != 0) {
        goto cleanup;
    } /* Inserts need keys that are absent from the initial live set. */

    rc = bench_trace_build_mutating_workload(
        workload,
        timed_ops,
        trace_seed,
        initial_keys,
        initial_live,
        insert_keys,
        insert_count,
        ops_out
    );

cleanup:
    free(insert_keys);
    return rc;
}

static int bench_trace_build_thread_workload(
    workload_kind       workload,
    size_t              timed_ops,
    uint64_t            trace_seed,
    const ht_key_t     *initial_keys,
    size_t              initial_live,
    const ht_key_t     *insert_keys,
    size_t              insert_key_count,
    bench_keyspace_mode keyspace_mode,
    bench_op          **ops_out
) {
    if (keyspace_mode == BENCH_KEYSPACE_SHARED_READ) {
        return bench_trace_build_shared_read_workload(
            timed_ops,
            trace_seed,
            initial_keys,
            initial_live,
            ops_out
        );
    } /* Shared-read traces are lookup-only and never partition keys. */

    if (keyspace_mode != BENCH_KEYSPACE_DISJOINT) {
        return -1;
    }

    return bench_trace_build_mutating_workload(
        workload,
        timed_ops,
        trace_seed,
        initial_keys,
        initial_live,
        insert_keys,
        insert_key_count,
        ops_out
    );
}

static int bench_trace_build_shared_read_workload(
    size_t          timed_ops,
    uint64_t        trace_seed,
    const ht_key_t *initial_keys,
    size_t          initial_live,
    bench_op      **ops_out
) {
    static const uint64_t BENCH_TRACE_PICK_OFFSET = 0xd1b54a32d192ed03ULL;
    bench_op *ops = NULL;
    size_t i;
    size_t idx;
    int rc = -1;

    if (ops_out == NULL) {
        return -1;
    }

    *ops_out = NULL;

    if (timed_ops == 0) {
        return 0;
    }

    if (initial_keys == NULL || initial_live == 0) {
        return -1;
    }

    if (timed_ops > SIZE_MAX / sizeof(*ops)) {
        return -1;
    }

    ops = calloc(timed_ops, sizeof(*ops));
    if (ops == NULL) {
        return -1;
    }

    for (i = 0; i < timed_ops; i++) {
        idx = (size_t)(
            bench_splitmix64(
                trace_seed + BENCH_TRACE_PICK_OFFSET + (uint64_t)i
            ) % (uint64_t)initial_live
        );
        ops[i].kind  = OP_GET_HIT;
        ops[i].key   = initial_keys[idx];
        ops[i].value = initial_keys[idx];
    } /* Reads choose existing keys without changing live state. */

    rc = 0;
    if (rc != 0) {
        free(ops);
        ops = NULL;
    }
    *ops_out = ops;
    return rc;
}

static int bench_trace_build_mutating_workload(
    workload_kind   workload,
    size_t          timed_ops,
    uint64_t        trace_seed,
    const ht_key_t *initial_keys,
    size_t          initial_live,
    const ht_key_t *insert_keys,
    size_t          insert_key_count,
    bench_op      **ops_out
) {
    static const uint64_t BENCH_TRACE_PICK_OFFSET = 0xd1b54a32d192ed03ULL;
    bench_op *ops = NULL;
    ht_key_t *live_keys = NULL;
    size_t lookup_count;
    size_t insert_count;
    size_t remove_count;
    size_t total_live_capacity;
    size_t live_count;
    size_t insert_cursor;
    uint64_t pick_cursor;
    size_t op_cursor;
    size_t i;
    size_t idx;
    int rc = -1;

    if (ops_out == NULL) {
        return -1;
    }

    *ops_out = NULL;

    if (timed_ops == 0) {
        return 0;
    }

    if (initial_keys == NULL && initial_live > 0) {
        return -1;
    }

    if (timed_ops > SIZE_MAX / sizeof(*ops)) {
        return -1;
    }

    bench_trace_workload_counts(
        workload,
        timed_ops,
        &lookup_count,
        &insert_count,
        &remove_count
    );

    if (insert_count > insert_key_count ||
        (insert_count > 0 && insert_keys == NULL)) {
        goto cleanup;
    }

    if ((lookup_count > 0 || remove_count > 0) && initial_live == 0) {
        goto cleanup;
    }

    ops = calloc(timed_ops, sizeof(*ops));
    if (ops == NULL) {
        goto cleanup;
    }

    op_cursor = 0;
    for (i = 0; i < lookup_count; i++) {
        ops[op_cursor++].kind = OP_GET_HIT;
    }
    for (i = 0; i < insert_count; i++) {
        ops[op_cursor++].kind = OP_INSERT;
    }
    for (i = 0; i < remove_count; i++) {
        ops[op_cursor++].kind = OP_REMOVE;
    } /* Counts are exact before the trace is shuffled. */

    bench_trace_shuffle_ops(ops, timed_ops, trace_seed);
    /* Shuffling keeps the ratio fixed while mixing operation order. */

    if (insert_count > SIZE_MAX - initial_live) {
        goto cleanup;
    }

    total_live_capacity = initial_live + insert_count;
    if (total_live_capacity > 0) {
        if (total_live_capacity > SIZE_MAX / sizeof(*live_keys)) {
            goto cleanup;
        }

        live_keys = malloc(total_live_capacity * sizeof(*live_keys));
        if (live_keys == NULL) {
            goto cleanup;
        }

        if (initial_live > 0) {
            memcpy(
                live_keys,
                initial_keys,
                initial_live * sizeof(*live_keys)
            );
        }
    }

    live_count    = initial_live;
    insert_cursor = 0;
    pick_cursor   = 0;

    for (i = 0; i < timed_ops; i++) {
        switch (ops[i].kind) {
        case OP_GET_HIT:
            if (live_count == 0) {
                goto cleanup;
            }

            idx = (size_t)(
                bench_splitmix64(
                    trace_seed + BENCH_TRACE_PICK_OFFSET + pick_cursor
                ) % (uint64_t)live_count
            );
            pick_cursor++;

            ops[i].key   = live_keys[idx];
            ops[i].value = live_keys[idx];
            break;

        case OP_INSERT:
            if (insert_cursor >= insert_count) {
                goto cleanup;
            }

            ops[i].key   = insert_keys[insert_cursor];
            ops[i].value = insert_keys[insert_cursor];

            live_keys[live_count] = insert_keys[insert_cursor];
            live_count++;
            insert_cursor++;
            /* Mirror successful inserts so later hits/removes can see them. */
            break;

        case OP_REMOVE:
            if (live_count == 0) {
                goto cleanup;
            }

            idx = (size_t)(
                bench_splitmix64(
                    trace_seed + BENCH_TRACE_PICK_OFFSET + pick_cursor
                ) % (uint64_t)live_count
            );
            pick_cursor++;

            ops[i].key   = live_keys[idx];
            ops[i].value = live_keys[idx];

            live_keys[idx] = live_keys[live_count - 1];
            live_count--;
            /* Swap-delete keeps the live-key mirror compact. */
            break;

        case OP_GET_MISS:
        default:
            goto cleanup;
        }
    }

    rc = 0;

cleanup:
    free(live_keys);
    if (rc != 0) {
        free(ops);
        ops = NULL;
    }
    *ops_out = ops;
    return rc;
}

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
) {
    static const uint64_t BENCH_TRACE_THREAD_SEED =
        0x9e3779b97f4a7c15ULL;
    bench_op **ops = NULL;
    size_t *op_counts = NULL;
    size_t op_base;
    size_t op_rem;
    size_t live_base;
    size_t live_rem;
    size_t live_offset;
    size_t shared_live;
    size_t mutable_live;
    size_t insert_offset;
    size_t i;
    size_t j;
    int rc = -1;

    if (ops_out == NULL || op_counts_out == NULL || thread_count == 0) {
        return -1;
    }

    *ops_out       = NULL;
    *op_counts_out = NULL;

    if (thread_count > SIZE_MAX / sizeof(*ops) ||
        thread_count > SIZE_MAX / sizeof(*op_counts)) {
        return -1;
    }

    if (keyspace_mode == BENCH_KEYSPACE_SHARED_READ &&
        (initial_keys == NULL || initial_live == 0)) {
        return -1;
    } /* Shared-read workers all sample from the same populated keyset. */

    if (keyspace_mode == BENCH_KEYSPACE_DISJOINT) {
        if (initial_keys == NULL && initial_live > 0) {
            return -1;
        }
        if (timed_ops > insert_key_count ||
            (timed_ops > 0 && insert_keys == NULL)) {
            return -1;
        }
    } else if (keyspace_mode == BENCH_KEYSPACE_SHARED_MIXED) {
        if (initial_keys == NULL ||
            thread_count > SIZE_MAX / 2 ||
            initial_live < thread_count * 2) {
            return -1;
        }
        if (timed_ops > insert_key_count ||
            (timed_ops > 0 && insert_keys == NULL)) {
            return -1;
        }
    } else if (keyspace_mode != BENCH_KEYSPACE_SHARED_READ) {
        return -1;
    }

    ops = calloc(thread_count, sizeof(*ops));
    if (ops == NULL) {
        goto cleanup;
    }

    op_counts = calloc(thread_count, sizeof(*op_counts));
    if (op_counts == NULL) {
        goto cleanup;
    }

    op_base = timed_ops / thread_count;
    op_rem  = timed_ops % thread_count;
    for (i = 0; i < thread_count; i++) {
        op_counts[i] = op_base + ((i < op_rem) ? 1 : 0);
    }

    shared_live = 0;
    mutable_live = initial_live;
    if (keyspace_mode == BENCH_KEYSPACE_SHARED_MIXED) {
        shared_live = initial_live / 2;
        mutable_live = initial_live - shared_live;
        if (shared_live == 0 || mutable_live < thread_count) {
            goto cleanup;
        }
    } /* Mixed mode reserves a shared read pool plus per-thread mutable keys. */

    live_base     = mutable_live / thread_count;
    live_rem      = mutable_live % thread_count;
    live_offset   = 0;
    insert_offset = 0;

    for (i = 0; i < thread_count; i++) {
        const ht_key_t *thread_initial = initial_keys;
        const ht_key_t *thread_insert = insert_keys;
        size_t thread_initial_live = initial_live;
        size_t thread_insert_count = insert_key_count;
        bench_keyspace_mode thread_keyspace = keyspace_mode;
        uint64_t thread_seed =
            trace_seed + (BENCH_TRACE_THREAD_SEED * (uint64_t)(i + 1));

        if (keyspace_mode == BENCH_KEYSPACE_DISJOINT ||
            keyspace_mode == BENCH_KEYSPACE_SHARED_MIXED) {
            thread_initial_live =
                live_base + ((i < live_rem) ? 1 : 0);
            thread_insert_count = op_counts[i];
            thread_insert =
                (thread_insert_count > 0) ? &insert_keys[insert_offset] : NULL;

            if (keyspace_mode == BENCH_KEYSPACE_SHARED_MIXED) {
                thread_initial = (thread_initial_live > 0)
                    ? &initial_keys[shared_live + live_offset]
                    : NULL;
                thread_keyspace = BENCH_KEYSPACE_DISJOINT;
            } else {
                thread_initial = (thread_initial_live > 0)
                    ? &initial_keys[live_offset]
                    : NULL;
            }
        } /* Disjoint-style modes slice mutable keys by worker. */

        if (bench_trace_build_thread_workload(
                workload,
                op_counts[i],
                thread_seed,
                thread_initial,
                thread_initial_live,
                thread_insert,
                thread_insert_count,
                thread_keyspace,
                &ops[i]
            ) != 0) {
            goto cleanup;
        }

        if (keyspace_mode == BENCH_KEYSPACE_SHARED_MIXED) {
            for (j = 0; j < op_counts[i]; j++) {
                size_t idx;

                if (ops[i][j].kind != OP_GET_HIT) {
                    continue;
                }

                idx = (size_t)(
                    bench_splitmix64(
                        thread_seed + BENCH_TRACE_THREAD_SEED + (uint64_t)j
                    ) % (uint64_t)shared_live
                );
                ops[i][j].key   = initial_keys[idx];
                ops[i][j].value = initial_keys[idx];
            }
        } /* Mixed-mode lookup hits are redirected to the shared read pool. */

        if (keyspace_mode == BENCH_KEYSPACE_DISJOINT ||
            keyspace_mode == BENCH_KEYSPACE_SHARED_MIXED) {
            live_offset += thread_initial_live;
            insert_offset += thread_insert_count;
        }
    }

    rc = 0;

cleanup:
    if (rc != 0) {
        if (ops != NULL) {
            for (i = 0; i < thread_count; i++) {
                free(ops[i]);
            }
        }
        free(ops);
        free(op_counts);
        ops       = NULL;
        op_counts = NULL;
    }

    *ops_out       = ops;
    *op_counts_out = op_counts;
    return rc;
}

void bench_trace_free(
    bench_op *ops
) {
    free(ops);
}

static void bench_trace_workload_counts(
    workload_kind workload,
    size_t        timed_ops,
    size_t        *lookup_count_out,
    size_t        *insert_count_out,
    size_t        *remove_count_out
) {
    static const size_t BENCH_TRACE_RATIO_DENOM = 1000;
    const bench_workload_ratio *ratio;
    size_t weights[3];
    size_t counts[3];
    size_t remainders[3];
    size_t remaining;
    size_t best_idx;
    size_t i;
    uint64_t scaled;

    if (lookup_count_out == NULL ||
        insert_count_out == NULL ||
        remove_count_out == NULL) {
        return;
    }

    ratio = &bench_workload_ratios[WORKLOAD_READ_ONLY];
    if (workload >= WORKLOAD_READ_ONLY && workload <= WORKLOAD_BALANCED) {
        ratio = &bench_workload_ratios[workload];
    }

    weights[0] = ratio->lookup;
    weights[1] = ratio->insert;
    weights[2] = ratio->remove;
    for (i = 0; i < 3; i++) {
        scaled        = (uint64_t)timed_ops * (uint64_t)weights[i];
        counts[i]     = (size_t)(scaled / BENCH_TRACE_RATIO_DENOM);
        remainders[i] = (size_t)(scaled % BENCH_TRACE_RATIO_DENOM);
    }

    remaining = timed_ops - (counts[0] + counts[1] + counts[2]);
    while (remaining > 0) {
        best_idx = 0;

        for (i = 1; i < 3; i++) {
            if (remainders[i] > remainders[best_idx]) {
                best_idx = i;
            }
        }

        counts[best_idx]++;
        remainders[best_idx] = 0;
        remaining--;
    } /* Largest remainders receive leftover operations first. */

    *lookup_count_out = counts[0];
    *insert_count_out = counts[1];
    *remove_count_out = counts[2];
}

static void bench_trace_shuffle_ops(
    bench_op *ops,
    size_t   count,
    uint64_t seed
) {
    bench_op tmp;
    size_t   i;
    size_t   j;

    if (ops == NULL || count < 2) {
        return;
    }

    for (i = count - 1; i > 0; i--) {
        j = (size_t)(
            bench_splitmix64(seed + (uint64_t)i) % (uint64_t)(i + 1)
        );

        tmp    = ops[i];
        ops[i] = ops[j];
        ops[j] = tmp;
    }
}
