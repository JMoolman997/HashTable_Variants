/**
 * @file    hash_func.c
 * @brief   Standalone hash helper functions.
 *
 * Implements byte-oriented hash functions that can be reused independently of
 * the current hashtable backends.
 *
 * @author  J.W Moolman
 * @date    2026-04-02
 */

#include "hash_func.h"

/* --- function definitions ------------------------------------------------- */

/**
 * @brief Hash a byte sequence with the djb2 algorithm.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The computed 32-bit hash value.
 */
uint32_t djb2_hash(
    const void *key,
    size_t      len
) {
    const unsigned char *data = key;
    uint32_t             hash = 5381U;
    size_t               i;

    for (i = 0; i < len; i++) {
        /* djb2 multiplies by 33 using a shift-and-add step. */
        hash = ((hash << 5) + hash) + (uint32_t)data[i];
    }

    return hash;
}

/**
 * @brief Hash a byte sequence with the sdbm algorithm.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The computed 32-bit hash value.
 */
uint32_t sdbm_hash(
    const void *key,
    size_t      len
) {
    const unsigned char *data = key;
    uint32_t             hash = 0U;
    size_t               i;

    for (i = 0; i < len; i++) {
        /* sdbm uses two shifts to approximate its multiply-and-subtract mix. */
        hash = (uint32_t)data[i] + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

/**
 * @brief Hash a byte sequence with the FNV-1a algorithm.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The computed 32-bit hash value.
 */
uint32_t fnv1a_hash(
    const void *key,
    size_t      len
) {
    const unsigned char *data = key;
    uint32_t             hash = 2166136261U;
    const uint32_t       prime = 16777619U;
    size_t               i;

    for (i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i];
        hash *= prime;
    }

    return hash;
}

/**
 * @brief Hash a byte sequence with the 32-bit Murmur3 algorithm.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The computed 32-bit hash value.
 */
uint32_t murmur3_32_hash(
    const void *key,
    size_t      len
) {
    const unsigned char *data = key;
    const uint32_t       c1 = 0xcc9e2d51U;
    const uint32_t       c2 = 0x1b873593U;
    const size_t         rounded_end = len & ~(size_t)0x3U;
    uint32_t             hash = 0U;
    uint32_t             k1 = 0U;
    size_t               i;

    for (i = 0; i < rounded_end; i += 4) {
        /* Pack four bytes into one little-endian 32-bit block before mixing. */
        uint32_t k = (uint32_t)data[i] |
                     ((uint32_t)data[i + 1] << 8) |
                     ((uint32_t)data[i + 2] << 16) |
                     ((uint32_t)data[i + 3] << 24);

        k *= c1;
        k = (k << 15) | (k >> 17);
        k *= c2;

        hash ^= k;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5U + 0xe6546b64U;
    }

    /* Fold any trailing 1-3 bytes into the final partial block. */
    switch (len & 0x3U) {
    case 3:
        k1 ^= (uint32_t)data[rounded_end + 2] << 16;
        /* fall through */
    case 2:
        k1 ^= (uint32_t)data[rounded_end + 1] << 8;
        /* fall through */
    case 1:
        k1 ^= (uint32_t)data[rounded_end];
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        hash ^= k1;
        break;
    default:
        break;
    }

    hash ^= (uint32_t)len;
    hash ^= hash >> 16;
    hash *= 0x85ebca6bU;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35U;
    hash ^= hash >> 16;

    return hash;
}

/**
 * @brief Hash a byte sequence with a software CRC32 implementation.
 *
 * @param key Pointer to the input bytes.
 * @param len Number of bytes to hash.
 *
 * @return The computed 32-bit CRC value.
 */
uint32_t crc32_hash(
    const void *key,
    size_t      len
) {
    const unsigned char *data = key;
    static uint32_t      table[256];
    static int           have_table = 0;
    uint32_t             crc = 0xFFFFFFFFU;
    uint32_t             i;
    size_t               j;

    if (!have_table) {
        for (i = 0; i < 256U; i++) {
            uint32_t c = i;

            for (j = 0; j < 8U; j++) {
                /* Shift one bit at a time and inject the reflected CRC polynomial. */
                if ((c & 1U) != 0U) {
                    c = 0xEDB88320U ^ (c >> 1);
                } else {
                    c >>= 1;
                }
            }

            table[i] = c;
        }

        have_table = 1;
    }

    for (j = 0; j < len; j++) {
        /* Use the low byte as the table index and shift in the next byte. */
        crc = table[(crc ^ (uint32_t)data[j]) & 0xFFU] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFU;
}
