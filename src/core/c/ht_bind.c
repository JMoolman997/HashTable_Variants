/**
 * @file    ht_bind.c
 * @brief   Binds generic hashtable objects to benchmark-facing direct ops.
 *
 * Implements the conversion from the public opaque hashtable wrapper to a
 * bound benchmark interface containing implementation-direct operation
 * pointers. 
 *
 * This is used by performance-critical timed loops to reduce the
 * overhead of repeatedly traversing the full public dispatch layer.
 *
 * @author  J.W Moolman
 * @date    2026-03-23
 */

#include "ht_bench.h"
#include "ht_internal.h"

int ht_bind_bench_iface(ht_map *map, bench_iface *out) {
    return ht_bind_bench_iface_flags(map, out, 0);
}

int ht_bind_bench_iface_flags(ht_map *map, bench_iface *out, unsigned flags) {
    if (map == NULL || out == NULL || map->vt == NULL || map->impl == NULL) {
        return HT_ERR_INVALID;
    }

    if (map->vt->bind_bench_iface_flags != NULL) {
        return map->vt->bind_bench_iface_flags(map->impl, out, flags);
    }

    if (map->vt->bind_bench_iface == NULL) {
        return HT_ERR_UNSUPPORTED;
    }

    return map->vt->bind_bench_iface(map->impl, out);
}
