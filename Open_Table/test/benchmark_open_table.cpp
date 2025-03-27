#include <benchmark/benchmark.h>
extern "C" {
  #include "open_table.h"
}
#include <cstdlib>
#include <cstdint>

// Benchmark the insertion of keys into the hash table.
// Each iteration creates a new table, inserts state.range(0) keys, and then destroys the table.
static void BM_OpenTableInsert(benchmark::State& state) {
  HTConfig config = HT_DEFAULT_CONFIG;
  for (auto _ : state) {
    HashTab* ht = ht_create(&config);
    for (int i = 0; i < state.range(0); i++) {
      int key = i;
      int value = i;
      // Insert key-value pair (using sizeof(int) for the key length)
      ht_insert(ht, &key, sizeof(int), &value);
    }
    ht_destroy(ht);
  }
}
BENCHMARK(BM_OpenTableInsert)->Arg(1000)->Arg(10000);

// Benchmark searching in the hash table.
// Pre-populate the table once, then for each iteration, search for each key.
static void BM_OpenTableSearch(benchmark::State& state) {
  HTConfig config = HT_DEFAULT_CONFIG;
  HashTab* ht = ht_create(&config);
  for (int i = 0; i < state.range(0); i++) {
    int key = i;
    int value = i;
    ht_insert(ht, &key, sizeof(int), &value);
  }
  for (auto _ : state) {
    for (int i = 0; i < state.range(0); i++) {
      int key = i;
      // Prevent compiler optimizations from removing the call.
      benchmark::DoNotOptimize(ht_search(ht, &key, sizeof(int)));
    }
  }
  ht_destroy(ht);
}
BENCHMARK(BM_OpenTableSearch)->Arg(1000)->Arg(10000);

// Benchmark removing keys from the hash table.
// For each iteration, create and populate a table then remove each key.
static void BM_OpenTableRemove(benchmark::State& state) {
  HTConfig config = HT_DEFAULT_CONFIG;
  for (auto _ : state) {
    HashTab* ht = ht_create(&config);
    for (int i = 0; i < state.range(0); i++) {
      int key = i;
      int value = i;
      ht_insert(ht, &key, sizeof(int), &value);
    }
    for (int i = 0; i < state.range(0); i++) {
      int key = i;
      ht_remove(ht, &key, sizeof(int));
    }
    ht_destroy(ht);
  }
}
BENCHMARK(BM_OpenTableRemove)->Arg(1000)->Arg(10000);

BENCHMARK_MAIN();

