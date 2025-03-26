#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <getopt.h>
#include <basic_func.h>
#include <open_addressing.h>

/* --- Types and Constants ------------------------------------------------- */

#ifndef DEFAULT_LOAD_FACTOR
#define DEFAULT_LOAD_FACTOR 0.75f
#endif

#ifndef DEFAULT_MIN_LOAD_FACTOR
#define DEFAULT_MIN_LOAD_FACTOR 0.2f
#endif

#ifndef DEFAULT_INACTIVE_FACTOR
#define DEFAULT_INACTIVE_FACTOR 0.1f
#endif

#ifndef DEFAULT_BUFFER
#define DEFAULT_BUFFER 1024
#endif

#ifndef DEFAULT_P_INSERT
#define DEFAULT_P_INSERT 0.4
#endif

#ifndef DEFAULT_P_LOOKUP
#define DEFAULT_P_LOOKUP 0.4
#endif

#ifndef DEFAULT_P_REMOVE
#define DEFAULT_P_REMOVE 0.2
#endif

#define OUTPUT_DIR "benchmark/results/"

typedef struct {
    char *description;
    void *func_ptr;
} FunctionEntry;

typedef struct {
    float load_factor;
    float min_load_factor;
    float inactive_factor;
    uint32_t (*hash_func)(void *key, size_t len);
    int (*cmp_func)(const void *key1, const void *key2);
    uint32_t (*p)(uint32_t k, uint32_t i, uint32_t m);
    void (*freekey)(void *k);
    void (*freeval)(void *v);
    const char *output_file;
} BenchConfig;

#define C2HASHFN(func_ptr) ((uint32_t (*)(void *, size_t))func_ptr)
static const FunctionEntry hash_func_arr[] = {
    {"djb2", (void *)djb2_hash},
    {"sdbm", (void *)sdbm_hash},
    {"fnv1a", (void *)fnv1a_hash},
    {"murmur3_32", (void *)murmur3_32_hash},
    {"crc32", (void *)crc32_hash}
};

#define C2PROBEFN(func_ptr) ((uint32_t (*)(uint32_t, uint32_t, uint32_t))func_ptr)
static const FunctionEntry probe_func_arr[] = {
    {"linear", (void *)linear_probe_func},
    {"quadratic", (void *)quadratic_probe_func},
    {"double_hash", (void *)double_hash_probe_func}
};
/* --- Benchmarking Function Prototypes ----------------------------------- */

/**
 * @brief Benchmark average insertion time.
 * 
 * @param config     A pointer to the BenchConfig struct.
 * @param num_tests  Number of key-value pairs to insert/benchmark.
 */
static void avg_insert_benchmark(const BenchConfig *config, size_t num_tests);

/**
 * @brief Benchmark average lookup time.
 *
 * @param config     A BenchConfig struct specifying how to initialize the table (optional).
 * @param num_tests  Number of lookups to perform.
 *
 * Note: This can be adapted to reuse the table from avg_insert_benchmark 
 *       if desired, or it can create a fresh table for demonstration.
 */
static void avg_lookup_benchmark(const BenchConfig *config, size_t num_tests);

static void mixed_benchmark(
        const BenchConfig *config,
        size_t num_ops,
        const double p_insert,
        const double p_lookup,
        const double p_remove
);

/* --- Helper Function Prototypes ------------------------------------------ */

/**
 * @brief Write an array of timing data to a CSV file.
 * 
 * @param filename  Path to CSV file.
 * @param data      Array of timing data.
 * @param size      Number of data points.
 * @param header    A string representing the CSV header line.
 * @return 0 on success, non-zero on failure.
 */
static int write_csv(const char* filename, const double* data, size_t size, const char* header);

/**
 * @brief Generate an output filename based on benchmark parameters.
 *
 * @param mode        The benchmark mode (e.g. "insert", "lookup", "both").
 * @param probe       The probe function name (e.g. "linear", "quadratic", "double_hash").
 * @param hash        The hash function name (e.g. "djb2", "sdbm", "fnv1a", ...).
 * @param load_factor A float representing the load factor.
 * @param buffer      Pointer to a buffer in which to store the filename.
 * @param buff_size   The size of the provided buffer in bytes.
 *
 * @return A pointer to `buffer`, or NULL if it fails.
 */
static char *generate_output_filename(
        const char *mode, 
        const char *probe, 
        const char *hash, 
        float load_factor, 
        char *buffer, 
        size_t buff_size
);

/**
 * @brief Compute the time difference in seconds between two timespecs.
 */
static double time_diff(struct timespec start, struct timespec end);

/* --- CLI Function Prototypes --------------------------------------------- */

static void print_usage(const char *prog_name);

/* --- Helper Function Implementations ------------------------------- */

static double time_diff(struct timespec start, struct timespec end) {
    double start_sec = (double) start.tv_sec + (start.tv_nsec / 1e9);
    double end_sec   = (double) end.tv_sec   + (end.tv_nsec / 1e9);
    return end_sec - start_sec;
}

static int write_csv(const char* filename, const double* data, size_t size, const char* header) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen csv");
        return -1;
    }

    // Write the provided header, e.g. "InsertIndex,InsertTime(sec)\n"
    fprintf(fp, "%s", header);

    // For each timing data point, write an incremental index plus the timing
    for (size_t i = 0; i < size; i++) {
        fprintf(fp, "%zu,%.9f\n", i + 1, data[i]);
    }

    fclose(fp);
    return 0;
}

static char *generate_output_filename(const char *mode, 
                               const char *probe, 
                               const char *hash, 
                               float load_factor, 
                               char *buffer, 
                               size_t buff_size)
{
    // Provide default strings if any pointer is NULL
    if (!mode)  mode  = "testmode";
    if (!probe) probe = "default_probe";
    if (!hash)  hash  = "default_hash";

    // Format example: insert_linear_djb2_lf0.75.csv
    // You can tweak precision or overall format as needed.
    int written = snprintf(buffer, buff_size, "%s_%s_%s_lf%.2f.csv",
                           mode, probe, hash, load_factor);

    // If snprintf returns a value >= buff_size, the output was truncated
    if (written < 0 || (size_t)written >= buff_size) {
        // Indicate failure or handle as you see fit
        return NULL;
    }

    return buffer;
}

/* --- Benchmarking Function Implementations ------------------------------- */ 
static void avg_insert_benchmark(const BenchConfig *config, size_t num_tests) {
    if (!config) {
        fprintf(stderr, "avg_insert_benchmark: Invalid BenchConfig.\n");
        return;
    }

    // Allocate array to store insertion times
    double *insert_times = malloc(sizeof(double) * num_tests);
    if (!insert_times) {
        perror("avg_insert_benchmark: malloc insert_times");
        return;
    }

    // Initialize the hash table
    HashTab *ht = init_ht(
        config->load_factor,
        config->min_load_factor,
        config->inactive_factor,
        config->hash_func,
        config->cmp_func,
        config->p,
        config->freekey,
        config->freeval
    );
    if (!ht) {
        fprintf(stderr, "Failed to initialize hash table.\n");
        free(insert_times);
        return;
    }

    for (size_t i = 0; i < num_tests; i++) {
        // Allocate and initialize key
        int *key = malloc(sizeof(int));
        if (!key) {
            perror("Failed to allocate memory for key");
            insert_times[i] = -1.0;
            continue;
        }
        *key = (int)i;  // unique key

        // Allocate and initialize value
        int *value = malloc(sizeof(int));
        if (!value) {
            perror("Failed to allocate memory for value");
            free(key);
            insert_times[i] = -1.0;
            continue;
        }
        *value = (int)i;  // associated value

        // Record the start time
        struct timespec start, end;
        if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
            perror("Failed to get start time");
            free(key);
            free(value);
            insert_times[i] = -1.0;
            continue;
        }

        // Insert into the hash table
        int res = insert_ht(ht, key, sizeof(int), value);
        if (res != HT_SUCCESS && res != HT_KEY_EXISTS) {
            fprintf(stderr, "Insertion failed for key %d with error code %d\n", *key, res);
            free(key);
            free(value);
            insert_times[i] = -1.0;
            continue;
        }

        // Record the end time
        if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
            perror("Failed to get end time");
            insert_times[i] = -1.0;
            // No free(...) here because the key/value has been inserted
            // or might be in an inconsistent state.
            continue;
        }

        // Calculate time difference
        insert_times[i] = time_diff(start, end);
    }

    // Write the insertion results to CSV
    if (config->output_file && write_csv(config->output_file, insert_times, num_tests,
        "InsertIndex,InsertTime(sec)\n") != 0) 
    {
        fprintf(stderr, "Failed to write insertion CSV to '%s'\n", config->output_file);
    } else {
        printf("Insertion benchmark completed. Results written to '%s'\n", config->output_file);
    }

    // Cleanup
    if (free_ht(ht) != HT_SUCCESS) {
        fprintf(stderr, "Failed to free hash table.\n");
    }
    free(insert_times);
}

static void avg_lookup_benchmark(const BenchConfig *config, size_t num_tests) {
    if (!config) {
        fprintf(stderr, "avg_lookup_benchmark: Invalid BenchConfig.\n");
        return;
    }

    // Allocate an array for lookup times
    double *lookup_times = malloc(sizeof(double) * num_tests);
    if (!lookup_times) {
        perror("avg_lookup_benchmark: malloc lookup_times");
        return;
    }

    // For demonstration, we create and populate a hash table here.
    // If you want to reuse the same table from avg_insert_benchmark,
    // you'd need to pass it in or store it somewhere globally.
    HashTab *ht = init_ht(
        config->load_factor,
        config->min_load_factor,
        config->inactive_factor,
        config->hash_func,
        config->cmp_func,
        config->p,
        config->freekey,
        config->freeval
    );
    if (!ht) {
        fprintf(stderr, "Failed to initialize table for lookup benchmark.\n");
        free(lookup_times);
        return;
    }

    /* 
     * Populate the table so that lookups are meaningful.
     * For example, insert `num_tests` distinct keys:
     */
    for (size_t i = 0; i < num_tests; i++) {
        int *key = malloc(sizeof(int));
        int *val = malloc(sizeof(int));
        if (!key || !val) {
            free(key);
            free(val);
            continue;
        }
        *key = (int)i;
        *val = (int)(i + 123);  // arbitrary value
        if (insert_ht(ht, key, sizeof(int), val) != HT_SUCCESS) {
            // If insertion fails, free the memory (though your code might skip it)
            free(key);
            free(val);
        }
    }

    // Now measure the time for `num_tests` lookups.
    // We'll randomly pick existing keys to look up:
    for (size_t i = 0; i < num_tests; i++) {
        int random_key_val = rand() % num_tests;
        struct timespec start, end;

        if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
            perror("Failed to get start time for lookup");
            lookup_times[i] = -1.0;
            continue;
        }

        // Perform the lookup
        int search_return = search_ht(ht, &random_key_val, sizeof(int));
        if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
            perror("Failed to get end time for lookup");
            lookup_times[i] = -1.0;
            continue;
        }

        // Calculate time difference
        lookup_times[i] = time_diff(start, end);

    }

    // Write lookup data to CSV
    if (config->output_file && write_csv(config->output_file, lookup_times, num_tests,
        "LookupIndex,LookupTime(sec)\n") != 0)
    {
        fprintf(stderr, "Failed to write lookup CSV to '%s'\n", config->output_file);
    } else {
        printf("Lookup benchmark completed. Results written to '%s'\n", config->output_file);
    }

    // Cleanup
    if (free_ht(ht) != HT_SUCCESS) {
        fprintf(stderr, "Failed to free hash table after lookup.\n");
    }
    free(lookup_times);
}

static void mixed_benchmark(
        const BenchConfig *config,
        size_t num_ops,
        const double p_insert,
        const double p_lookup,
        const double p_remove
) {
    double *op_times = malloc(num_ops * sizeof(double));
    if (!op_times) {
        perror("malloc op_times failed");
        return;
    }
    srand(time(NULL));
    /* Preallocate keys/values for num_ops. */
    int **keys = malloc(num_ops * sizeof(int *));
    int **vals = malloc(num_ops * sizeof(int *));
    if (!keys || !vals) {
        perror("malloc keys/vals");
        free(op_times);
        return;
    }

    for (size_t i = 0; i < num_ops; i++) {
        keys[i] = malloc(sizeof(int));
        vals[i] = malloc(sizeof(int));
        if (!keys[i] || !vals[i]) {
            perror("malloc key/val element");
            /* TODO: add cleanup */
            return;
        }
        *keys[i] = (int)i;
        *vals[i] = (int)(i + 500);
    }
    /* Initialize hash table. */
    HashTab *ht = init_ht(
            config->load_factor,
            config->min_load_factor,
            config->inactive_factor,
            config->hash_func,
            config->cmp_func,
            config->p,
            config->freekey,
            config->freeval
    );

    if (!ht) {
        fprintf(stderr, "Failed to initialize hash table.\n");
        goto cleanup_mixed;
    }

    /* Warm-up phase: perform a few operations. */
    size_t warmup = num_ops / 10;
    for (size_t i = 0; i < warmup; i++) {
        insert_ht(ht, keys[i], sizeof(int), vals[i]);
        search_ht(ht, keys[i], sizeof(int));
        remove_ht(ht, keys[i], sizeof(int));
    }

    size_t num_inserts = 0;  // Track number of successful inserts.

    /* Benchmark mixed operations. */
    for (size_t i = 0; i < num_ops; i++) {
        double op_choice = (double)rand() / RAND_MAX;
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        if (op_choice < p_insert) {
            /* Insert (only if key not already inserted). */
            if (num_inserts < num_ops) {
                int res = insert_ht(ht, keys[num_inserts], sizeof(int), vals[num_inserts]);
                if (res == HT_SUCCESS || res == HT_KEY_EXISTS) {
                    num_inserts++;
                }
            }
        } else if (op_choice < p_insert + p_lookup) {
            /* Lookup from the already inserted keys. */
            if (num_inserts > 0) {
                size_t idx = (rand() % num_inserts);
                search_ht(ht, keys[idx], sizeof(int));
            }
        } else {
            /* Remove from the table. */
            if (num_inserts > 0) {
                size_t idx =rand() % num_inserts;
                //fprintf(stderr, "&keys[idx]:%p idx:%zu i:%zu num_in:%zu\n",
                //        (void *)&keys[idx], idx, i, num_inserts);
                if (remove_ht(ht, keys[idx], sizeof(int)) == HT_SUCCESS) {
                    keys[idx] = NULL;
                    vals[idx] = NULL;
                }
                /* TODO:adjust num_inserts, or allow duplicate removals. */
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        op_times[i] = time_diff(start, end);
    }

    /* Write CSV for mixed benchmark. */
    if (config->output_file && write_csv(config->output_file, op_times, num_ops,
                                         "OpIndex,OpTime(sec)\n") != 0) {
        fprintf(stderr, "Failed to write CSV to '%s'\n", config->output_file);
    } else {
        printf("Mixed benchmark completed. CSV written to '%s'\n", config->output_file);
    }

    free_ht(ht);
    
cleanup_mixed:
    for (size_t i = 0; i < num_ops; i++) {
        if (keys[i] != NULL) {
            free(keys[i]);
        }
        if (vals[i] != NULL) {
            free(vals[i]);
        }
    }
    free(keys);
    free(vals);
    free(op_times);
}   

/* --- CLI Functions ------------------------------------------------------- */
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [OPTIONS]\n", prog_name);
    fprintf(stderr, "  --help, -h               Print this help message\n");
    fprintf(stderr, "  --mode, -m <insert|lookup|both>  Benchmark mode\n");
    fprintf(stderr, "  --probe, -p <STR>        Probe function to use\n");
    fprintf(stderr, "  --hash, -H <STR>         Hash function to use\n");
    fprintf(stderr, "  --load-factor, -l <F>    Load factor (float),"
                    "default=%.2f\n", DEFAULT_LOAD_FACTOR);
    fprintf(stderr, "  --num-tests, -n <N>      Number of operations, e.g. 100000\n");
    fprintf(stderr, "  --output-file, -o <FILE> Where to write CSV\n");

    // Print available probes from probe_func_arr
    fprintf(stderr, "\nAvailable probes:\n");
    // Example:   linear, quadratic, double_hash
    for (size_t i = 0; i < sizeof(probe_func_arr) / sizeof(probe_func_arr[0]); i++) {
        fprintf(stderr, "  %s\n", probe_func_arr[i].description);
    }

    // Print available hashes from hash_func_arr
    fprintf(stderr, "\nAvailable hash functions:\n");
    // Example:   djb2, sdbm, fnv1a, murmur3_32, crc32
    for (size_t i = 0; i < sizeof(hash_func_arr) / sizeof(hash_func_arr[0]); i++) {
        fprintf(stderr, "  %s\n", hash_func_arr[i].description);
    }

    fprintf(stderr, "\nExample:\n");
    fprintf(stderr, "  %s --mode insert --probe linear --hash djb2"
                    " --num-tests 100000 --output-file my_insert.csv\n", prog_name);
    fprintf(stderr, "  %s --mode lookup --probe double_hash --hash crc32"
                    " --num-tests 50000 --output-file my_lookup.csv\n", prog_name);
}
/* --- Main Function ------------------------------------------------------- */

int main(int argc, char *argv[]) {
    // Default values
    const char *mode_str       = "lookup";
    const char *probe_str      = NULL;
    const char *hash_str       = NULL;     
    float load_factor          = DEFAULT_LOAD_FACTOR;
    float min_load_factor      = DEFAULT_MIN_LOAD_FACTOR;
    float inactive_factor      = DEFAULT_INACTIVE_FACTOR;
    size_t num_tests           = 100000;
    const char *output_file    = NULL;

    // Prepare for getopt_long
    static struct option long_opts[] = {
        {"help",        no_argument,       NULL, 'h'},
        {"mode",        required_argument, NULL, 'm'},
        {"probe",       required_argument, NULL, 'p'},
        {"hash",        required_argument, NULL, 'H'},
        {"load-factor", required_argument, NULL, 'l'},
        {"num-tests",   required_argument, NULL, 'n'},
        {"output-file", required_argument, NULL, 'o'},
        {0, 0, 0, 0}
    };

    const char *short_opts = "hm:p:H:l:n:o:"; 
    int opt, long_index = 0;

    while ((opt = getopt_long(argc, argv, short_opts, long_opts, &long_index)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'm':
                mode_str = optarg;
                break;
            case 'p':
                probe_str = optarg;
                break;
            case 'H':
                hash_str = optarg;
                break;
            case 'l':
                load_factor = strtof(optarg, NULL);
                break;
            case 'n': {
                long val = strtol(optarg, NULL, 10);
                if (val <= 0) {
                    fprintf(stderr, "Error: --num-tests must be > 0\n");
                    return 1;
                }
                num_tests = (size_t)val;
                break;
            }
            case 'o':
                output_file = optarg;
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Validate mode
    int do_insert = 0;
    int do_lookup = 0;
    int do_mixed = 0;
    if (strcmp(mode_str, "insert") == 0) {
        do_insert = 1;
    } else if (strcmp(mode_str, "lookup") == 0) {
        do_lookup = 1;
    } else if (strcmp(mode_str, "mixed") == 0) {
        do_mixed = 1;
    } else {
        fprintf(
                stderr,
                "Unknown mode '%s'. Must be 'insert', 'lookup', or 'mixed'.\n",
                mode_str
        );
        return 1;
    }

    // Find the requested probe function
    uint32_t (*probe_fn)(uint32_t, uint32_t, uint32_t) = NULL;
    if (probe_str) {
        size_t found = 0;
        for (size_t i = 0; i < sizeof(probe_func_arr)/sizeof(probe_func_arr[0]); i++) {
            if (strcmp(probe_str, probe_func_arr[i].description) == 0) {
                probe_fn = C2PROBEFN(probe_func_arr[i].func_ptr);
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Error: Unrecognized probe '%s'\n", probe_str);
            return 1;
        }
    } else {
        // If user didn't specify, pick the first or some default
        probe_fn = C2PROBEFN(probe_func_arr[0].func_ptr);
    }

    // Find the requested hash function
    uint32_t (*hash_fn)(void *, size_t) = NULL;
    if (hash_str) {
        size_t found = 0;
        for (size_t i = 0; i < sizeof(hash_func_arr)/sizeof(hash_func_arr[0]); i++) {
            if (strcmp(hash_str, hash_func_arr[i].description) == 0) {
                hash_fn = C2HASHFN(hash_func_arr[i].func_ptr);
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Error: Unrecognized hash '%s'\n", hash_str);
            return 1;
        }
    } else {
        // If user didn't specify, pick the first or some default
        hash_fn = C2HASHFN(hash_func_arr[0].func_ptr);
    }

    // If no output file is specified, build one automatically
    if (!output_file) {
        static char default_filename[256];

        const char *actual_probe = probe_str ? probe_str : probe_func_arr[0].description;
        const char *actual_hash  = hash_str  ? hash_str  : hash_func_arr[0].description;

        if (!generate_output_filename(mode_str,
                                      actual_probe,
                                      actual_hash,
                                      load_factor,
                                      default_filename,
                                      sizeof(default_filename)))
        {
            fprintf(stderr, "Error: Failed to generate default filename.\n");
            return 1;
        }
        output_file = default_filename;
    }

    // Build the BenchConfig
    BenchConfig config = {
        .load_factor      = load_factor,
        .min_load_factor  = min_load_factor,
        .inactive_factor  = inactive_factor,
        .hash_func        = hash_fn,
        .cmp_func         = int_cmp,
        .p                = probe_fn,
        .freekey          = free,
        .freeval          = free,
        .output_file      = output_file
    };

    // Info message
    printf("Running benchmark:\n");
    printf("  Mode          : %s\n", mode_str);
    printf("  Probe         : %s\n", probe_str ? probe_str : probe_func_arr[0].description);
    printf("  Hash          : %s\n", hash_str ? hash_str : hash_func_arr[0].description);
    printf("  Load Factor   : %.2f\n", load_factor);
    printf("  Num Tests     : %zu\n", num_tests);
    printf("  Output File   : %s\n", output_file);

    // Execute selected benchmarks
    if (do_insert) {
        avg_insert_benchmark(&config, num_tests);
    }
    if (do_lookup) {
        avg_lookup_benchmark(&config, num_tests);
    }
    if (do_mixed) {
        mixed_benchmark(
                &config,
                num_tests,
                DEFAULT_P_INSERT,
                DEFAULT_P_LOOKUP,
                DEFAULT_P_REMOVE
        );
    }
    return 0;
}
