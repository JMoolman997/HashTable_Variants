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
src/core/c/                ht_map dispatcher and internal vtable contract
src/util/c/                shared C helpers
implementations/           algorithm-family implementation tree
bench/c/                   benchmark CLI and runners
tests/c/                   C API test matrix
docs/                      algorithm, benchmark, and history notes
results/                   ignored local benchmark output
build.zig                  canonical build file
Makefile                   thin zig build wrapper
```

## API

Client code includes `include/hash_table/ht.h`.

1. Fill `ht_config` from `include/hash_table/ht_types.h`.
2. Set `impl_kind`.
3. Call `ht_create`.
4. Use the generic `ht_*` functions.
5. Call `ht_destroy`.

Core dispatch lives in `src/core/c/ht.c`. Backend details stay out of the public
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

See `docs/algorithms.md` for design notes.

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

The tests use `tests/c/test_registry.c` as the implementation list. Each test
case creates tables through `ht_create()` and exercises the public API.

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

1. Add the backend under `implementations/<family>/c/<variant>/`.
2. Implement private state plus functions matching `src/core/c/ht_internal.h`.
3. Expose:
   - `<variant>_create_impl_ex(const ht_config *cfg, void **out)`
   - `<variant>_vtable(void)`
   - optional `<variant>_bind_bench_iface(...)`
4. Add an `HT_IMPL_*` enum value in `include/hash_table/ht_types.h`.
5. Include the backend header and add a case in `src/core/c/ht.c`.
6. Add include paths and source files to `build.zig`.
7. Add the CLI name in `bench/c/src/bench_names.c`.
8. Add one registry entry in `tests/c/test_registry.c`.
9. Run `zig build check`.

That is enough for the public API tests and benchmark parser to see the backend.

## Style Guide

- Match the style already used in nearby C files.
- Keep function prototypes grouped under explicit section banners.
- Prefer multi-line declarations for non-trivial signatures.
- Use braces for every `if`, `else`, `for`, and `while` body.
- Wrap long expressions; keep wrapped assignments easy to scan.
- Align setup assignments when they form one logical block.
- Keep opening braces on the same line as function signatures.
- Preserve the existing file-header and section-comment style.
- Prefer clear names over comments that restate the code.
