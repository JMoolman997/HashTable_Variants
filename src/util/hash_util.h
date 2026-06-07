/**
 * @file hash_util.h
 * @brief Hash indexing, tags, and the default integer hash.
 *
 * @author  J.W. Moolman
 * @date    2026-06-07
 */

#ifndef HT_UTIL_HASH_UTIL_H
#define HT_UTIL_HASH_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "ht_types.h"

#define HT_INDEX_FOR_U64(hash, capacity) ((size_t)(hash) & ((capacity) - 1u))

static inline uint8_t ht_hash_tag_u8(uint64_t hash) {
    return (uint8_t)(hash >> 56);
}

static inline uint64_t default_hash(ht_key_t key, uint64_t seed) {
    uint64_t x = key ^ seed;

    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;

    return x;
}

#endif /* HT_UTIL_HASH_UTIL_H */
