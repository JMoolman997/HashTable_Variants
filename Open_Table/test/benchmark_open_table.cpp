#include <benchmark/benchmark.h>
extern "C" {
    #include "open_table.h"
}
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

// Benchmark the insertion of keys into the hash table
static void BM_OpenTableInsert(benchmark::State& state) {
    int size = (int)state.range(0);
    float load_factor = state.range(1) / 100.0f;  // Load factor as percentage

    HTConfig config = HT_DEFAULT_CONFIG;
    config.load_factor = load_factor;  // Override default load factor

    for (auto _ : state) {
        HashTab* ht = ht_create(&config);
        for (int i = 0; i < size; i++) {
            int key = rand();
            int value = i;
            ht_insert(ht, &key, sizeof(int), &value);
        }
        ht_destroy(ht);
    }
}

// Benchmark searching in the hash table
static void BM_OpenTableSearch(benchmark::State& state) {
    int size = (int)state.range(0);
    float load_factor = state.range(1) / 100.0f;

    HTConfig config = HT_DEFAULT_CONFIG;
    config.load_factor = load_factor;

    // Pre-populate the table
    HashTab* ht = ht_create(&config);
    for (int i = 0; i < size; i++) {
        int key = rand();
        int value = i;
        ht_insert(ht, &key, sizeof(int), &value);
    }

    for (auto _ : state) {
        for (int i = 0; i < size; i++) {
            int key = i;
            benchmark::DoNotOptimize(ht_search(ht, &key, sizeof(int)));
        }
    }
    ht_destroy(ht);
}

// Benchmark removing keys from the hash table
static void BM_OpenTableRemove(benchmark::State& state) {
    int size = (int)state.range(0);
    float load_factor = state.range(1) / 100.0f;

    HTConfig config = HT_DEFAULT_CONFIG;
    config.load_factor = load_factor;

    for (auto _ : state) {
        HashTab* ht = ht_create(&config);
        for (int i = 0; i < size; i++) {
            int key = rand();
            int value = i;
            ht_insert(ht, &key, sizeof(int), &value);
        }
        for (int i = 0; i < size; i++) {
            int key = i;
            ht_remove(ht, &key, sizeof(int));
        }
        ht_destroy(ht);
    }
}

// Benchmark Registration
static void RegisterInsertBenchmarks() {
    std::vector<int> sizes = {1000, 10000, 100000};
    std::vector<int> load_factors = {75, 80, 90};  // 0.75, 0.80, 0.90 as percentages

    for (int sz : sizes) {
        for (int lf : load_factors) {
            std::string name = "Insert/" + std::to_string(sz) + "/LF" + std::to_string(lf);
            benchmark::RegisterBenchmark(name.c_str(), BM_OpenTableInsert)
                ->Args({sz, lf});
        }
    }
}

static void RegisterSearchBenchmarks() {
    std::vector<int> sizes = {1000, 10000, 100000, 1000000};
    std::vector<int> load_factors = {75, 80, 90};

    for (int sz : sizes) {
        for (int lf : load_factors) {
            std::string name = "Search/" + std::to_string(sz) + "/LF" + std::to_string(lf);
            benchmark::RegisterBenchmark(name.c_str(), BM_OpenTableSearch)
                ->Args({sz, lf});
        }
    }
}

static void RegisterRemoveBenchmarks() {
    std::vector<int> sizes = {1000, 10000, 100000};
    std::vector<int> load_factors = {75, 80, 90};

    for (int sz : sizes) {
        for (int lf : load_factors) {
            std::string name = "Remove/" + std::to_string(sz) + "/LF" + std::to_string(lf);
            benchmark::RegisterBenchmark(name.c_str(), BM_OpenTableRemove)
                ->Args({sz, lf});
        }
    }
}

// Custom main to register benchmarks
int main(int argc, char** argv) {
    RegisterInsertBenchmarks();
    RegisterSearchBenchmarks();
    RegisterRemoveBenchmarks();

    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    return 0;
}
