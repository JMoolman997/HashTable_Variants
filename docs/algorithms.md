# Algorithms

The active code keeps variants that demonstrate a distinct table strategy,
storage layout, deletion policy, metadata layout, resize model, or concurrency
model.

## Open Addressing

- `open_addressing`: baseline linear probing with tombstones and cached hashes.
- `backshift`: linear probing with backward-shift deletion instead of
  tombstones.
- `robin_hood`: open addressing with Robin Hood insertion and backward-shift
  deletion.
- `metadata`: scalar open addressing with separate control/tag metadata.
- `simd`: SIMD-assisted metadata probing using SSE2 control-byte groups.
- `adv_open_addressing`: combined control metadata, SIMD filtering, Robin Hood
  placement, and backshift deletion.

These variants intentionally overlap at the API level but isolate different
probe and deletion techniques.

## Separate Chaining

- `separate_chaining`: baseline linked-list bucket chains.
- `bucket_mod_separate_chaining`: fixed inline bucket arrays with overflow
  buckets.
- `linked_mod_separate_chaining`: slab-backed linked-list bucket operations.
- `segmented_mod_separate_chaining`: lazily allocated fixed-size bucket
  segments.
- `fingerprint`: segmented chains with compact hash fingerprints.
- `adv_separate_chaining`: cache-oriented inline entries, overflow segments,
  tags, and move-to-front promotion.

The separate-chaining family is the main place where memory layout changes are
compared while keeping collision resolution conceptually chained.

## Linear Hashing

- `linear_hashing`: incremental split/merge growth with fingerprinted
  segments.

This backend is retained because its resize behavior is algorithmically
different from whole-table rehashing.

## Hopscotch

- `hopscotch`: single-threaded hopscotch hashing with 64-bit neighborhood
  bitmaps, tags, relocation, and fixed-mode overflow fallback.

Hopscotch is kept as its own family because it combines open-addressed storage
with bounded neighborhood invariants.

## Concurrent Variants

- `p_open_addressing`: parallel open addressing with striped writers, reader
  protection, resize/cleanup coordination, and tombstone cleanup instrumentation.
- `p_separate_chaining`: concurrent segmented separate chaining with stripe
  locks and per-stripe slab pools.
- `lf_hopscotch`: C11-atomic hopscotch backend with wait-free lookups relative
  to resize, writer-stop generation publishing, and fixed-mode overflow.

These are grouped together because their primary distinction is concurrency
control rather than the base collision-resolution family alone.
