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
  --csv > results/lookup-hit-open-addressing.csv
```

## Resize Example

```bash
./zig-out/bin/htbench_resize resize-build \
  --impl adv_open_addressing \
  --dataset-size 1048576 \
  --timed-ops 1048576 \
  --repetitions 5 \
  --csv > results/resize-build-adv-open-addressing.csv
```

## Concurrent Example

```bash
./zig-out/bin/htbench concurrent-lookup \
  --impl p_open_addressing \
  --dataset-size 1048576 \
  --timed-ops 1048576 \
  --threads 8 \
  --repetitions 5 \
  --csv > results/concurrent-lookup-p-open-addressing.csv
```

## Measurement Rules

- Prefer `ReleaseFast` for comparative runs.
- Use the same dataset size, load factor, keyspace mode, seed, and repetition
  count when comparing implementations.
- Keep raw CSVs in `results/`; summarize stable conclusions in the relevant
  documentation or report notes.
- Treat small smoke runs as build validation only, not performance evidence.
