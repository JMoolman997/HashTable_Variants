# Hash Table Variants

C hash-table backends behind one public API, with shared conformance tests and
Zig-built benchmark binaries.

## Quick Start

```bash
zig build check
```

Use a repo-local Zig cache when the default global cache is not writable:

```bash
ZIG_GLOBAL_CACHE_DIR=.zig-cache/global zig build check
```

## Common Commands

| Task | Command |
|---|---|
| Build libraries, benchmarks, and tests | `zig build all` |
| Build and run the C test suite | `zig build check` |
| Run only the C test suite | `zig build test` |
| Build the fixed-capacity library | `zig build libht` |
| Build the resize-instrumented library | `zig build libht_resize` |
| Build the steady benchmark binary | `zig build htbench` |
| Build the resize benchmark binary | `zig build htbench_resize` |
| Use the Make wrapper | `make check`, `make test`, `make htbench` |
| Remove build outputs | `make clean` |

## Requirements

- Zig
- C11 toolchain
- POSIX shell
- `make` for wrapper targets
- pthread-compatible platform

## Repository Map

| Path | Purpose |
|---|---|
| `include/ht.h` | Public map API |
| `include/ht_types.h` | Public config, result, stats, key, and value types |
| `include/ht_bench.h` | Benchmark-facing direct binding API |
| `src/core/` | API dispatcher, implementation registry, internal vtable contract |
| `src/util/` | Shared C helpers |
| `implementations/open_addressing/` | Open-addressing family |
| `implementations/separate_chaining/` | Separate-chaining family |
| `implementations/linear_hashing/` | Linear hashing backend |
| `implementations/hopscotch/` | Hopscotch backend |
| `implementations/concurrent/` | Concurrent backends |
| `bench/` | Benchmark CLI, plans, runners, output, datasets |
| `tests/` | Public API conformance matrix |
| `docs/` | Algorithm, benchmark, and history notes |
| `results/` | Ignored local benchmark output |
| `build.zig` | Canonical build file |
| `Makefile` | Thin Zig build wrapper |

## Public API

Client code includes `ht.h`, chooses an `ht_impl`, and uses the generic
`ht_*` calls. Backend details stay behind `src/core/ht.c` and
`src/core/ht_registry.c`.

```c
#include "ht.h"

int main(void) {
    ht_config cfg;
    ht_map   *map = NULL;
    ht_val_t  value;

    cfg                 = ht_config_resizing(HT_IMPL_OPEN_ADDRESSING, 1024);
    cfg.max_load_factor = 0.70;

    if (ht_create_ex(&cfg, &map) != HT_OK) {
        return 1;
    }

    if (ht_insert(map, 42, 9001) != HT_OK) {
        ht_destroy(map);
        return 1;
    }

    if (ht_get(map, 42, &value) != HT_OK) {
        ht_destroy(map);
        return 1;
    }

    (void)value;
    ht_remove(map, 42);
    ht_destroy(map);
    return 0;
}
```

| Function | Use |
|---|---|
| `ht_config_default` | Backend defaults, grow-only resizing |
| `ht_config_fixed` | Fixed-capacity table |
| `ht_config_resizing` | Grow/shrink-capable table |
| `ht_create_ex` | Create with a diagnostic `ht_result` |
| `ht_create` | Create with `NULL` on failure |
| `ht_insert`, `ht_get`, `ht_remove`, `ht_contains` | Core operations |
| `ht_size`, `ht_capacity`, `ht_load_factor` | Table state |
| `ht_reserve`, `ht_rehash` | Capacity control |
| `ht_get_stats`, `ht_reset_stats` | Operation and memory counters |
| `ht_impl_name`, `ht_result_name` | Stable names for output and diagnostics |

## Implementations

Implementation labels are the names accepted by `htbench --impl` and returned
by `ht_impl_name`.

| Family | Labels |
|---|---|
| Open addressing | `open_addressing`, `backshift`, `robin_hood`, `metadata`, `simd`, `adv_open_addressing` |
| Separate chaining | `separate_chaining`, `bucket_mod_separate_chaining`, `linked_mod_separate_chaining`, `segmented_mod_separate_chaining`, `fingerprint`, `adv_separate_chaining` |
| Linear hashing | `linear_hashing` |
| Hopscotch | `hopscotch` |
| Concurrent | `p_open_addressing`, `p_separate_chaining`, `lf_hopscotch` |

See `docs/algorithms.md` for backend notes and the public threading contract.

## Tests

The tests read the same registry used by the public API and benchmark parser.
Most coverage is shared public-API behavior; backend-specific tests are reserved
for behavior the matrix cannot express.

```bash
zig build test
zig build check
```

| File | Purpose |
|---|---|
| `tests/ht_test.c` | Suite entry point and registry checks |
| `tests/ht_test_basic.c` | Public API behavior |
| `tests/ht_test_resize.c` | Resize behavior |
| `tests/test_registry.c` | Test-side implementation list |
| `tests/test_runner.c` | Shared PASS/FAIL runner |

## Benchmarks

Build optimized benchmark binaries for real measurements:

```bash
zig build -Doptimize=ReleaseFast htbench
zig build -Doptimize=ReleaseFast htbench_resize
```

Steady-state example:

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

Resize example:

```bash
./zig-out/bin/htbench_resize resize-build \
  --impl adv_open_addressing \
  --dataset-size 1048576 \
  --timed-ops 1048576 \
  --repetitions 3 \
  --stats off \
  --csv
```

Benchmark commands:

- `insert-build`
- `lookup-hit`
- `lookup-miss`
- `erase-existing`
- `workload`
- `resize-build`
- `resize-lookup-hit`
- `resize-lookup-miss`
- `resize-erase-existing`
- `resize-workload`
- `concurrent-lookup`
- `concurrent-workload`

See `docs/benchmarking.md` for measurement rules and more examples.

## Add An Implementation

1. Add the backend under `implementations/<family>/<variant>/`.
2. Implement private state plus functions matching `src/core/ht_internal.h`.
3. Expose `<variant>_create_impl_ex(...)` and `<variant>_vtable(void)`.
4. Expose `<variant>_bind_bench_iface(...)` only when direct benchmark binding is useful.
5. Add an `HT_IMPL_*` enum value in `include/ht_types.h`.
6. Add one `src/core/ht_registry.c` entry with the enum, label, constructor, and vtable getter.
7. Add include paths and source files to `build.zig`.
8. Add focused tests only for behavior outside the shared matrix.
9. Run `zig build check`.

## Docs

| File | Use |
|---|---|
| `docs/algorithms.md` | Backend design notes and threading contract |
| `docs/benchmarking.md` | Benchmark rules and command examples |
| `docs/history.md` | Project history notes |

## Style Guide

- Match nearby C code before introducing a new local pattern.
- Use multi-line declarations for non-trivial signatures.
- Use braces for every `if`, `else`, `for`, and `while` body.
- Wrap long expressions before lines become hard to scan.
- Align setup assignments when they form one logical block.
- Keep opening braces on the same line as function signatures.
- Keep comments for invariants, tradeoffs, and non-obvious decisions.
- Prefer clear names over comments that restate the code.

Style example:

```c
static void bench_fill_timing_summary(
    const bench_plan *plan,
    const ht_stats   *stats,
    uint64_t          elapsed_ns,
    bench_result     *result
) {
    uint64_t total_ops;

    if (plan == NULL || stats == NULL || result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->elapsed_ns = elapsed_ns;
    result->stats      = *stats;

    if (plan->timed_ops > 0) {
        result->ns_per_op = (double)elapsed_ns / (double)plan->timed_ops;
        result->ops_per_sec =
            ((double)plan->timed_ops * 1e9) / (double)elapsed_ns;
    }

    total_ops = stats->lookups + stats->inserts + stats->removes;
    if (total_ops > 0) {
        result->avg_probe_len = (double)stats->probes / (double)total_ops;
    }
}
```
