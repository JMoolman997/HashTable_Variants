/**
 * @file    ht_types.h
 * @brief   Shared public type definitions.
 *
 * This header is part of the public interface and should remain free of
 * implementation-specific details.
 *
 * @author  J.W Moolman
 * @date    2026-03-23
 */

#ifndef HT_TYPES_H
#define HT_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifndef HT_ENABLE_RESIZE_INSTRUMENTATION
#define HT_ENABLE_RESIZE_INSTRUMENTATION 0
#endif

/** Public key type used by every hashtable backend. */
typedef uint64_t ht_key_t;
/** Public value type used by every hashtable backend. */
typedef uint64_t ht_val_t;

/**
 * @brief Result codes returned by public and internal hashtable operations.
 */
typedef enum {
  HT_OK = 0,
  HT_ERR = -1,
  HT_ERR_OOM = -2,
  HT_ERR_NOT_FOUND = -3,
  HT_ERR_EXISTS = -4,
  HT_ERR_FULL = -5,
  HT_ERR_INVALID = -6,
  HT_ERR_UNSUPPORTED = -7
} ht_result;

/**
 * @brief Supported backend implementation kinds.
 */
typedef enum {
  HT_IMPL_OPEN_ADDRESSING = 0,
  HT_IMPL_ROBIN_HOOD = 1,
  HT_IMPL_SEPARATE_CHAINING = 2,
  HT_IMPL_HOPSCOTCH = 3,
  HT_IMPL_ADV_OPEN_ADDRESSING = 4,
  HT_IMPL_P_OPEN_ADDRESSING = 5,
  HT_IMPL_BACKSHIFT = 6,
  HT_IMPL_METADATA = 7,
  HT_IMPL_SIMD = 8,
  HT_IMPL_BUCKET_MOD_SEPARATE_CHAINING = 9,
  HT_IMPL_LINKED_MOD_SEPARATE_CHAINING = 10,
  HT_IMPL_SEGMENTED_MOD_SEPARATE_CHAINING = 11,
  HT_IMPL_P_SEPARATE_CHAINING = 12,
  HT_IMPL_FINGERPRINT = 13,
  HT_IMPL_LINEAR_HASHING = 14,
  HT_IMPL_ADV_SEPARATE_CHAINING = 15,
  HT_IMPL_LF_HOPSCOTCH = 16
} ht_impl;

/**
 * @brief Resize policies supported by the hashtable backends.
 */
typedef enum {
  HT_RESIZE_NONE = 0,
  HT_RESIZE_GROW = 1,
  HT_RESIZE_GROW_SHRINK = 2
} ht_rsz_mode;

/**
 * @brief Hash function signature used by configurable backends.
 *
 * @param key Key to hash.
 * @param seed Seed mixed into the hash computation.
 *
 * @return Mixed 64-bit hash value for `key`.
 */
typedef uint64_t (*ht_hash_fn)(ht_key_t key, uint64_t seed);

/**
 * @brief Aggregate operation and memory statistics recorded by a table.
 */
typedef struct {
  uint64_t inserts;         /**< Number of attempted inserts. */
  uint64_t insert_failures; /**< Number of inserts that failed. */
  uint64_t lookups;         /**< Number of attempted lookups. */
  uint64_t lookup_misses;   /**< Number of failed lookups. */
  uint64_t removes;         /**< Number of attempted removals. */
  uint64_t remove_misses;   /**< Number of failed removals. */

  uint64_t probes;        /**< Total number of probes across operations. */
  uint64_t max_probe_len; /**< Longest single probe sequence observed. */

  /** Cleanup counters are meaningful for concurrent open-addressing backends. */
  uint64_t cleanup_requested_count;     /**< Tombstone cleanup requests. */
  uint64_t cleanup_started_count;       /**< Claimed cleanup passes. */
  uint64_t cleanup_completed_count;     /**< Completed cleanup passes. */
  uint64_t cleanup_fallback_count;      /**< Exclusive fallback rebuilds. */
  uint64_t cleanup_shadow_publish_count; /**< Published shadow cleanups. */
  uint64_t cleanup_shadow_abandon_count; /**< Abandoned shadow cleanups. */
  uint64_t cleanup_log_peak_entries;    /**< Peak replay-log entries. */

  uint64_t resize_count;         /**< Number of resize operations performed. */
  uint64_t grow_count;           /**< Number of capacity-increasing resizes. */
  uint64_t shrink_count;         /**< Number of capacity-decreasing resizes. */
  uint64_t rehash_count;         /**< Number of successful rehash passes. */
  uint64_t resize_entries_moved; /**< Live entries moved during resizes. */
  uint64_t resize_total_ns;      /**< Total time spent in successful resizes. */
  uint64_t resize_max_ns;        /**< Slowest single successful resize. */
  size_t resize_min_capacity;    /**< Minimum capacity observed by resizes. */
  size_t resize_max_capacity;    /**< Maximum capacity observed by resizes. */

  size_t bytes_used; /**< Approximate bytes owned by the backend. */
} ht_stats;

/**
 * @brief Public configuration used to create a hashtable instance.
 */
typedef struct {
  ht_impl impl_kind; /**< Backend implementation to instantiate. */

  size_t init_capacity; /**< Requested starting capacity before rounding. */
  size_t min_capacity;  /**< Minimum capacity allowed after shrinking. */

  double max_load_factor; /**< Upper load factor threshold for growth. */
  double min_load_factor; /**< Lower load factor threshold for shrink. */

  ht_rsz_mode rsz_mode; /**< Resize policy to apply during mutations. */

  ht_hash_fn hash_fn; /**< Optional custom hash function. */
  uint64_t hash_seed; /**< Seed passed into the selected hash function. */

  size_t thread_count; /**< Concurrent backend thread-count sizing hint. */

  int collect_stats; /**< Non-zero when statistics should be tracked. */
} ht_config;

#endif /* HT_TYPES_H */
