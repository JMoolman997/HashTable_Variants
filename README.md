# Hash Table Variants

Research monorepo for hash table implementations. The active code is C, built
with Zig, and organized by algorithm family.

## Quick Start

```bash
zig build check
```

Use the local cache if Zig cannot write to its default cache:

```bash
ZIG_GLOBAL_CACHE_DIR=.zig-cache/global zig build check
```

## Layout

```text
include/hash_table/        public C API
src/core/                  ht_map dispatcher and internal vtable contract
src/util/                  shared C helpers
implementations/           algorithm-family implementation tree
bench/                     benchmark CLI and runners
tests/                     C API test matrix
docs/                      algorithm, benchmark, and history notes
results/                   ignored local benchmark output
build.zig                  canonical build file
Makefile                   thin zig build wrapper
```

## API

Client code includes `include/hash_table/ht.h`.

1. Start from `ht_config_default`, `ht_config_fixed`, or
   `ht_config_resizing`.
2. Call `ht_create` for simple setup or `ht_create_ex` when the caller needs a
   diagnostic `ht_result`.
3. Use the generic `ht_*` functions.
4. Call `ht_destroy`.

Core dispatch lives in `src/core/ht.c`; implementation registration and stable
names live in `src/core/ht_registry.c`. Backend details stay out of the public
headers.

## Implementations

- Open addressing: `open_addressing`, `backshift`, `robin_hood`, `metadata`,
  `simd`, `adv_open_addressing`
- Separate chaining: `separate_chaining`, `bucket_mod_separate_chaining`,
  `linked_mod_separate_chaining`, `segmented_mod_separate_chaining`,
  `fingerprint`, `adv_separate_chaining`
- Linear hashing: `linear_hashing`
- Hopscotch: `hopscotch`
- Concurrent: `p_open_addressing`, `p_separate_chaining`, `lf_hopscotch`

See `docs/algorithms.md` for design notes and the public threading contract.

## Build

```bash
zig build all
zig build check
zig build test
zig build htbench
zig build htbench_resize
zig build libht
zig build libht_resize
```

Make wrapper:

```bash
make check
make test
make htbench
make htbench_resize
make clean
```

## Tests

The tests read the same `src/core/ht_registry.c` implementation table used by
the public API and benchmark parser. Each matrix test creates tables through
the public API and exercises the common `ht_*` calls.

Run:

```bash
zig build test
zig build check
```

## Benchmarks

Build:

```bash
zig build htbench
zig build htbench_resize
```

Example:

```bash
./zig-out/bin/htbench lookup-hit \
  --impl open_addressing \
  --dataset-size 1048576 \
  --alpha 0.70 \
  --timed-ops 1048576 \
  --warmup-ops 65536 \
  --repetitions 3 \
  --stats off \
  --csv
```

See `docs/benchmarking.md` for benchmark rules and examples.

## Connect A New Implementation

1. Add the backend under `implementations/<family>/<variant>/`.
2. Implement private state plus functions matching `src/core/ht_internal.h`.
3. Expose:
   - `<variant>_create_impl_ex(const ht_config *cfg, void **out)`
   - `<variant>_vtable(void)`
   - optional `<variant>_bind_bench_iface(...)`
4. Add an `HT_IMPL_*` enum value in `include/hash_table/ht_types.h`.
5. Add one entry in `src/core/ht_registry.c` with the enum, CLI name,
   constructor, and vtable getter.
6. Add include paths and source files to `build.zig`.
7. Add focused tests only when the backend has behavior not covered by the
   shared matrix.
8. Run `zig build check`.

That is enough for the public API, tests, and benchmark parser to see the
backend.

## Style Guide

- Match the style already used in nearby C files.
- Keep private declarations concise; use section banners only where they make a
  long file easier to scan.
- Prefer multi-line declarations for non-trivial signatures.
- Use braces for every `if`, `else`, `for`, and `while` body.
- Wrap long expressions; keep wrapped assignments easy to scan.
- Align setup assignments when they form one logical block.
- Keep opening braces on the same line as function signatures.
- Preserve the existing file-header and section-comment style.
- Prefer clear names over comments that restate the code.
