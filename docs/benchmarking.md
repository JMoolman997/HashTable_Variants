# Benchmarking

Benchmarks are built and run from the root Zig build.

## Build

```bash
zig build htbench
zig build htbench_resize
```

Use optimized binaries for real measurements:

```bash
zig build -Doptimize=ReleaseFast htbench
zig build -Doptimize=ReleaseFast htbench_resize
```

## Steady-State Example

```bash
./zig-out/bin/htbench lookup-hit \
  --impl open_addressing \
  --dataset-size 1048576 \
  --alpha 0.70 \
  --timed-ops 1048576 \
  --warmup-ops 65536 \
  --repetitions 5 \
  --stats off \
  --csv > results/lookup_hit_open_addressing.csv
```

## Resize Example

```bash
./zig-out/bin/htbench_resize resize-build \
  --impl adv_open_addressing \
  --dataset-size 1048576 \
  --timed-ops 1048576 \
  --repetitions 5 \
  --stats off \
  --csv > results/resize_build_adv_open_addressing.csv
```

## Concurrent Example

```bash
./zig-out/bin/htbench concurrent-lookup \
  --impl p_open_addressing \
  --dataset-size 1048576 \
  --timed-ops 1048576 \
  --thread-count 8 \
  --repetitions 5 \
  --stats off \
  --csv > results/concurrent_lookup_p_open_addressing.csv
```

## Measurement Rules

- Prefer `ReleaseFast` for comparative runs.
- Use `--stats off` for clean timing measurements.
- Use `--stats on` for diagnostic/probe/memory rows, and report that mode with
  the result.
- Use the same dataset size, load factor, keyspace mode, seed, and repetition
  count when comparing implementations.
- Use the exact implementation names shown by `--help`; these names come from
  the central backend registry and are also used in CSV output.
- Keep raw CSVs in `results/`; summarize stable conclusions in the relevant
  documentation or report notes.
- Treat small smoke runs as build validation only, not performance evidence.
