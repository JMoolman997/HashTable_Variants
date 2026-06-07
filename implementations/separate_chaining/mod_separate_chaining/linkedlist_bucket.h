/**
 * @file    linkedlist_bucket.h
 * @brief   Linked-list bucket implementation for `mod_separate_chaining`.
 *
 * Declares the bucket-local operations used by the modified separate-chaining
 * backend when `linked_mod_separate_chaining` is selected. Nodes are private
 * to the implementation and allocated from a slab pool.
 *
 * @author  J.W. Moolman
 * @date    2026-05-07
 */

#ifndef MOD_SEPARATE_CHAINING_LINKEDLIST_BUCKET_H
#define MOD_SEPARATE_CHAINING_LINKEDLIST_BUCKET_H

#include <stddef.h>
#include <stdint.h>

#include "mod_separate_chaining_impl.h"

typedef struct linkedlist_bucket_node *linkedlist_bucket;
typedef struct linkedlist_bucket_ctx linkedlist_bucket_ctx;

extern const mod_separate_chaining_bucket_ops linkedlist_bucket_ops;

#endif /* MOD_SEPARATE_CHAINING_LINKEDLIST_BUCKET_H */
