#ifndef BASIC_FUNC_H
#define BASIC_FUNC_H

#include <stdint.h> // For uint32_t
#include <stddef.h> // For size_t

/**
 * Hash Functions
 *
 * These functions generate hash values for a given key.
 */
uint32_t djb2_hash(void *key, size_t len);       // DJB2 hashing algorithm
uint32_t sdbm_hash(void *key, size_t len);       // SDBM hashing algorithm
uint32_t fnv1a_hash(void *key, size_t len);      // FNV-1a hashing algorithm
uint32_t murmur3_32_hash(void *key, size_t len); // Murmur3 hashing algorithm
uint32_t crc32_hash(void *key, size_t len);      // CRC32 hashing algorithm

/**
 * Probing Functions
 *
 * These functions determine the probing strategy for open addressing in hash tables.
 */
uint32_t linear_probe_func(uint32_t k, uint32_t i, uint32_t m);       // Linear probing
uint32_t quadratic_probe_func(uint32_t k, uint32_t i, uint32_t m);    // Quadratic probing
uint32_t double_hash_probe_func(uint32_t k, uint32_t i, uint32_t m);  // Double hashing

/**
 * Comparison Functions
 *
 * These functions compare two elements of various data types.
 */
int int_cmp(const void *a, const void *b);        // Compare integers
int long_cmp(const void *a, const void *b);       // Compare long integers
int float_cmp(const void *a, const void *b);      // Compare floats
int double_cmp(const void *a, const void *b);     // Compare doubles
int char_cmp(const void *a, const void *b);       // Compare characters
int string_cmp(const void *a, const void *b);     // Compare strings

#endif // BASIC_FUNC_H
