/**
 * @file    hash_func.h
 * @brief   Standalone hash helper functions.
 *
 * Declares a small set of byte-oriented hash functions that can be reused by
 * experiments or future backends without pulling in the older legacy code.
 *
 * @author  J.W Moolman
 * @date    2026-04-02
 */

#ifndef HASH_FUNC_H
#define HASH_FUNC_H

#include <stddef.h>
#include <stdint.h>

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Hash a byte sequence with the djb2 algorithm.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The 32-bit hash value computed from the input bytes.
 */
uint32_t djb2_hash(
    const void *key,
    size_t      len
);

/**
 * @brief Hash a byte sequence with the sdbm algorithm.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The 32-bit hash value computed from the input bytes.
 */
uint32_t sdbm_hash(
    const void *key,
    size_t      len
);

/**
 * @brief Hash a byte sequence with the FNV-1a algorithm.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The 32-bit hash value computed from the input bytes.
 */
uint32_t fnv1a_hash(
    const void *key,
    size_t      len
);

/**
 * @brief Hash a byte sequence with the 32-bit Murmur3 algorithm.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The 32-bit hash value computed from the input bytes.
 */
uint32_t murmur3_32_hash(
    const void *key,
    size_t      len
);

/**
 * @brief Hash a byte sequence with a software CRC32 implementation.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The 32-bit CRC value computed from the input bytes.
 */
uint32_t crc32_hash(
    const void *key,
    size_t      len
);

#endif /* HASH_FUNC_H */
