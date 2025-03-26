#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <basic_func.h>

/**
 * djb2 Hash Function
 *
 * Created by Daniel J. Bernstein.
 */
uint32_t djb2_hash(void *key, size_t len) {
    unsigned char *str = (unsigned char *)key;
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + str[i]; /* hash * 33 + c */
    }
    return hash;
}

/**
 * sdbm Hash Function
 *
 * Known for good distribution properties.
 */
uint32_t sdbm_hash(void *key, size_t len) {
    unsigned char *str = (unsigned char *)key;
    uint32_t hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash = str[i] + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

/**
 * FNV-1a Hash Function
 *
 * Fowler–Noll–Vo hash function, variant 1a.
 */
uint32_t fnv1a_hash(void *key, size_t len) {
    unsigned char *data = (unsigned char *)key;
    uint32_t hash = 2166136261u; // FNV offset basis for 32-bit
    uint32_t prime = 16777619u;

    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= prime;
    }
    return hash;
}

/**
 * MurmurHash3 (32-bit) Function
 *
 */
uint32_t murmur3_32_hash(void *key, size_t len) {
    unsigned char *data = (unsigned char *)key;
    uint32_t hash = 0;
    uint32_t c1 = 0xcc9e2d51;
    uint32_t c2 = 0x1b873593;
    size_t rounded_end = (len & ~0x3);

    // Body
    for (size_t i = 0; i < rounded_end; i += 4) {
        uint32_t k = data[i] | (data[i+1] << 8) | (data[i+2] << 16) | (data[i+3] << 24);
        k *= c1;
        k = (k << 15) | (k >> (32 - 15));
        k *= c2;

        hash ^= k;
        hash = (hash << 13) | (hash >> (32 - 13));
        hash = hash * 5 + 0xe6546b64;
    }

    // Tail
    uint32_t k1 = 0;
    switch (len & 0x3) {
        case 3:
            k1 ^= data[rounded_end + 2] << 16;
        case 2:
            k1 ^= data[rounded_end + 1] << 8;
        case 1:
            k1 ^= data[rounded_end];
            k1 *= c1;
            k1 = (k1 << 15) | (k1 >> (32 - 15));
            k1 *= c2;
            hash ^= k1;
    }

    // Finalization
    hash ^= len;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

    return hash;
}

/**
 * CRC32 Hash Function
 *
 * Primarily used for error-checking but can serve as a hash function.
 */
uint32_t crc32_hash(void *key, size_t len) {
    unsigned char *data = (unsigned char *)key;
    uint32_t crc = 0xFFFFFFFF;
    static uint32_t table[256];
    static int have_table = 0;

    if (!have_table) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                if (c & 1)
                    c = 0xEDB88320 ^ (c >> 1);
                else
                    c = c >> 1;
            }
            table[i] = c;
        }
        have_table = 1;
    }

    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}
