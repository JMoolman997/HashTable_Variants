/**
 * @file    bench_cli.c
 * @brief   Command-line parsing and benchmark dispatch for htbench.
 *
 * Parses benchmark subcommands and options, derives the benchmark
 * specification and dispatches supported benchmark runners.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_cli.h"
#include "bench_config.h"
#include "bench_plan.h"
#include "bench_names.h"
#include "bench_output.h"
#include "bench_runner_concurrent.h"
#include "bench_runner_resize.h"
#include "bench_runner_steady.h"

enum {
    BENCH_CLI_HELP_EXIT = 2
};

/* --- type definitions ---------------------------------------------------- */

typedef enum {
    BENCH_OPT_UNKNOWN = 0,
    BENCH_OPT_IMPL,
    BENCH_OPT_DATASET_SIZE,
    BENCH_OPT_ALPHA,
    BENCH_OPT_TIMED_OPS,
    BENCH_OPT_WARMUP_OPS,
    BENCH_OPT_REPETITIONS,
    BENCH_OPT_HASH_SEED,
    BENCH_OPT_KEY_SEED,
    BENCH_OPT_TRACE_SEED,
    BENCH_OPT_WORKLOAD,
    BENCH_OPT_INITIAL_CAPACITY,
    BENCH_OPT_RESIZE,
    BENCH_OPT_GROW_ALPHA,
    BENCH_OPT_SHRINK_ALPHA,
    BENCH_OPT_PREFILL_ALPHA,
    BENCH_OPT_THREAD_COUNT,
    BENCH_OPT_CONCURRENT_KEYSPACE,
    BENCH_OPT_CONCURRENT_RESIZE,
    BENCH_OPT_CSV,
    BENCH_OPT_HASH_FN,
    BENCH_OPT_STATS,
    BENCH_OPT_HELP
} bench_option_kind;

typedef int (*bench_runner_fn)(
    const bench_plan *plan,
    bench_result *result
);

typedef int (*bench_resize_runner_fn)(
    const bench_plan *plan,
    bench_run_result *result
);

typedef int (*bench_concurrent_runner_fn)(
    const bench_plan *plan,
    bench_run_result *result
);

typedef int (*bench_value_parser_fn)(
    const char *text,
    void *out
);

typedef union {
    bench_runner_fn            steady;
    bench_resize_runner_fn     resize;
    bench_concurrent_runner_fn concurrent;
} bench_runner_dispatch;

typedef struct {
    const char *name;
    bench_option_kind kind;
} bench_option_map;

typedef struct {
    int takes_value;
    size_t field_offset;
    bench_value_parser_fn parser;
    const char *opt_name;
    unsigned parse_flags;
    unsigned spec_flags;
} bench_opt_desc;

typedef struct {
    unsigned flags;
} bench_parse_state;

enum {
    BENCH_PARSE_INITIAL_CAPACITY = 1u << 0,
    BENCH_PARSE_RESIZE_MODE = 1u << 1,
    BENCH_PARSE_GROW_ALPHA = 1u << 2,
    BENCH_PARSE_SHRINK_ALPHA = 1u << 3,
    BENCH_PARSE_THREAD_OPTION = 1u << 4,
    BENCH_PARSE_THREAD_COUNT = 1u << 5,
    BENCH_PARSE_CONCURRENT_KEYSPACE = 1u << 6,
    BENCH_PARSE_CONCURRENT_RESIZE = 1u << 7,
};

enum {
    BENCH_SPEC_SET_CSV = 1u << 0,
    BENCH_SPEC_SET_RESIZE_MODE = 1u << 1,
    BENCH_SPEC_SET_PREFILL_ALPHA = 1u << 2,
    BENCH_SPEC_PRINT_HELP = 1u << 3,
};

#define BENCH_PARSE_HAS(state, flag) \
    ((state) != NULL && (((state)->flags & (flag)) != 0))

#define BENCH_ALPHA_VALID(alpha) ((alpha) > 0.0 && (alpha) < 1.0)

#define BENCH_OPT_VALUE(field, parser_fn, name, parse, spec) \
    {1, offsetof(bench_spec, field), (parser_fn), (name), (parse), (spec)}

#define BENCH_OPT_FLAG(name, parse, spec) \
    {0, 0, NULL, (name), (parse), (spec)}

#define BENCH_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

#define BENCH_DEFINE_VALUE_PARSER(name, parse_fn) \
    static int name(const char *text, void *out) { return parse_fn(text, out); }

static int bench_parse_ull(
    const char *arg,
    unsigned long long *out
) {
    unsigned long long parsed;
    char *end;

    if (arg == NULL || out == NULL) {
        return -1;
    }

    errno  = 0;
    parsed = strtoull(arg, &end, 10);
    if (errno != 0 || end == arg || *end != '\0') {
        return -1;
    } /* Reject partial numeric tokens such as "10ms". */

    *out = parsed;
    return 0;
}

static int bench_parse_size(
    const char *arg,
    size_t *out
) {
    unsigned long long parsed;

    if (arg == NULL || out == NULL) {
        return -1;
    }

    if (bench_parse_ull(arg, &parsed) != 0 || parsed > SIZE_MAX) {
        return -1;
    }

    *out = (size_t)parsed;
    return 0;
}

static int bench_parse_u64(
    const char *arg,
    uint64_t *out
) {
    unsigned long long parsed;

    if (arg == NULL || out == NULL) {
        return -1;
    }

    if (bench_parse_ull(arg, &parsed) != 0 || parsed > UINT64_MAX) {
        return -1;
    }

    *out = (uint64_t)parsed;
    return 0;
}

static int bench_parse_double(
    const char *arg,
    double *out
) {
    double parsed;
    char *end;

    if (arg == NULL || out == NULL) {
        return -1;
    }

    errno  = 0;
    parsed = strtod(arg, &end);
    if (errno != 0 || end == arg || *end != '\0') {
        return -1;
    } /* Floating options must consume the whole argument. */

    *out = parsed;
    return 0;
}

BENCH_DEFINE_VALUE_PARSER(bench_parse_impl_value, bench_parse_impl)
BENCH_DEFINE_VALUE_PARSER(bench_parse_size_value, bench_parse_size)
BENCH_DEFINE_VALUE_PARSER(bench_parse_double_value, bench_parse_double)
BENCH_DEFINE_VALUE_PARSER(bench_parse_u64_value, bench_parse_u64)
BENCH_DEFINE_VALUE_PARSER(bench_parse_workload_value, bench_parse_workload)
BENCH_DEFINE_VALUE_PARSER(bench_parse_resize_mode_value, bench_parse_resize_mode)
BENCH_DEFINE_VALUE_PARSER(bench_parse_keyspace_value, bench_parse_keyspace)
BENCH_DEFINE_VALUE_PARSER(bench_parse_stats_mode_value, bench_parse_stats_mode)

static int bench_parse_hash_fn_value(
    const char *text,
    void *out
) {
    if (text == NULL || out == NULL) {
        return -1;
    }

    if (strcmp(text, "default") != 0) {
        return -1;
    } /* The CLI exposes only the built-in hash path today. */

    *(ht_hash_fn *)out = NULL;
    return 0;
}

static const bench_option_map bench_option_maps[] = {
    {"--impl", BENCH_OPT_IMPL},
    {"--dataset-size", BENCH_OPT_DATASET_SIZE},
    {"--alpha", BENCH_OPT_ALPHA},
    {"--timed-ops", BENCH_OPT_TIMED_OPS},
    {"--warmup-ops", BENCH_OPT_WARMUP_OPS},
    {"--repetitions", BENCH_OPT_REPETITIONS},
    {"--hash-seed", BENCH_OPT_HASH_SEED},
    {"--key-seed", BENCH_OPT_KEY_SEED},
    {"--trace-seed", BENCH_OPT_TRACE_SEED},
    {"--workload", BENCH_OPT_WORKLOAD},
    {"--initial-capacity", BENCH_OPT_INITIAL_CAPACITY},
    {"--resize", BENCH_OPT_RESIZE},
    {"--grow-alpha", BENCH_OPT_GROW_ALPHA},
    {"--shrink-alpha", BENCH_OPT_SHRINK_ALPHA},
    {"--prefill-alpha", BENCH_OPT_PREFILL_ALPHA},
    {"--thread-count", BENCH_OPT_THREAD_COUNT},
    {"--concurrent-keyspace", BENCH_OPT_CONCURRENT_KEYSPACE},
    {"--concurrent-resize", BENCH_OPT_CONCURRENT_RESIZE},
    {"--csv", BENCH_OPT_CSV},
    {"--hash-fn", BENCH_OPT_HASH_FN},
    {"--stats", BENCH_OPT_STATS},
    {"--help", BENCH_OPT_HELP},
    {"-h", BENCH_OPT_HELP},
};

static const char *const bench_usage_lines[] = {
    "Subcommands:",
    "  insert-build",
    "  lookup-hit",
    "  lookup-miss",
    "  erase-existing",
    "  workload",
    "  resize-build",
    "  resize-lookup-hit",
    "  resize-lookup-miss",
    "  resize-erase-existing",
    "  resize-workload",
    "  concurrent-lookup",
    "  concurrent-workload",
    "Options:",
    "  --impl <open_addressing|p_open_addressing|",
    "p_separate_chaining|adv_open_addressing|robin_hood|",
    "separate_chaining|hopscotch|lf_hopscotch|backshift|metadata|simd|",
    "bucket_mod_separate_chaining|linked_mod_separate_chaining|",
    "segmented_mod_separate_chaining|fingerprint|linear_hashing|",
    "adv_separate_chaining>",
    "  --dataset-size <n>",
    "  --alpha <0<x<1>",
    "  --timed-ops <n>",
    "  --warmup-ops <n>",
    "  --repetitions <n>",
    "  --hash-seed <n>",
    "  --key-seed <n>",
    "  --trace-seed <n>",
    "  --workload <read-only|read-heavy|mixed|balanced>",
    "  --initial-capacity <n>",
    "  --resize <disabled|grow-only|grow-shrink|impl-default>",
    "  --grow-alpha <0<x<1>",
    "  --shrink-alpha <0<x<1>",
    "  --prefill-alpha <0<x<1>",
    "  --thread-count <n>",
    "  --concurrent-keyspace <disjoint|shared>",
    "  --concurrent-resize <disabled|grow-only>",
    "  --hash-fn <default>",
    "  --stats <off|on>",
    "  --csv",
    "  --help",
};

static const bench_runner_fn bench_runners[] = {
    bench_run_insert_build,
    bench_run_lookup_hit,
    bench_run_lookup_miss,
    bench_run_erase_existing,
    bench_run_workload,
};

static const bench_resize_runner_fn bench_resize_runners[] = {
    bench_run_resize_build,
    bench_run_resize_lookup_hit,
    bench_run_resize_lookup_miss,
    bench_run_resize_erase_existing,
    bench_run_resize_workload,
};

static const bench_concurrent_runner_fn bench_concurrent_runners[] = {
    bench_run_concurrent_lookup,
    bench_run_concurrent_workload,
};

static const bench_opt_desc bench_opt_descs[] = {
    [BENCH_OPT_IMPL] = BENCH_OPT_VALUE(
        impl_kind,
        bench_parse_impl_value,
        "--impl",
        0,
        0
    ),
    [BENCH_OPT_DATASET_SIZE] = BENCH_OPT_VALUE(
        dataset_size,
        bench_parse_size_value,
        "--dataset-size",
        0,
        0
    ),
    [BENCH_OPT_ALPHA] = BENCH_OPT_VALUE(
        target_alpha,
        bench_parse_double_value,
        "--alpha",
        0,
        0
    ),
    [BENCH_OPT_TIMED_OPS] = BENCH_OPT_VALUE(
        timed_ops,
        bench_parse_size_value,
        "--timed-ops",
        0,
        0
    ),
    [BENCH_OPT_WARMUP_OPS] = BENCH_OPT_VALUE(
        warmup_ops,
        bench_parse_size_value,
        "--warmup-ops",
        0,
        0
    ),
    [BENCH_OPT_REPETITIONS] = BENCH_OPT_VALUE(
        repetitions,
        bench_parse_size_value,
        "--repetitions",
        0,
        0
    ),
    [BENCH_OPT_HASH_SEED] = BENCH_OPT_VALUE(
        hash_seed,
        bench_parse_u64_value,
        "--hash-seed",
        0,
        0
    ),
    [BENCH_OPT_KEY_SEED] = BENCH_OPT_VALUE(
        key_seed,
        bench_parse_u64_value,
        "--key-seed",
        0,
        0
    ),
    [BENCH_OPT_TRACE_SEED] = BENCH_OPT_VALUE(
        trace_seed,
        bench_parse_u64_value,
        "--trace-seed",
        0,
        0
    ),
    [BENCH_OPT_WORKLOAD] = BENCH_OPT_VALUE(
        workload,
        bench_parse_workload_value,
        "--workload",
        0,
        0
    ),
    [BENCH_OPT_INITIAL_CAPACITY] = BENCH_OPT_VALUE(
        initial_capacity,
        bench_parse_size_value,
        "--initial-capacity",
        BENCH_PARSE_INITIAL_CAPACITY,
        0
    ),
    [BENCH_OPT_RESIZE] = BENCH_OPT_VALUE(
        resize_mode,
        bench_parse_resize_mode_value,
        "--resize",
        BENCH_PARSE_RESIZE_MODE,
        BENCH_SPEC_SET_RESIZE_MODE
    ),
    [BENCH_OPT_GROW_ALPHA] = BENCH_OPT_VALUE(
        grow_alpha,
        bench_parse_double_value,
        "--grow-alpha",
        BENCH_PARSE_GROW_ALPHA,
        0
    ),
    [BENCH_OPT_SHRINK_ALPHA] = BENCH_OPT_VALUE(
        shrink_alpha,
        bench_parse_double_value,
        "--shrink-alpha",
        BENCH_PARSE_SHRINK_ALPHA,
        0
    ),
    [BENCH_OPT_PREFILL_ALPHA] = BENCH_OPT_VALUE(
        prefill_alpha,
        bench_parse_double_value,
        "--prefill-alpha",
        0,
        BENCH_SPEC_SET_PREFILL_ALPHA
    ),
    [BENCH_OPT_THREAD_COUNT] = BENCH_OPT_VALUE(
        thread_count,
        bench_parse_size_value,
        "--thread-count",
        BENCH_PARSE_THREAD_OPTION | BENCH_PARSE_THREAD_COUNT,
        0
    ),
    [BENCH_OPT_CONCURRENT_KEYSPACE] = BENCH_OPT_VALUE(
        concurrent_keyspace_mode,
        bench_parse_keyspace_value,
        "--concurrent-keyspace",
        BENCH_PARSE_THREAD_OPTION | BENCH_PARSE_CONCURRENT_KEYSPACE,
        0
    ),
    [BENCH_OPT_CONCURRENT_RESIZE] = BENCH_OPT_VALUE(
        concurrent_resize_mode,
        bench_parse_resize_mode_value,
        "--concurrent-resize",
        BENCH_PARSE_THREAD_OPTION | BENCH_PARSE_CONCURRENT_RESIZE,
        0
    ),
    [BENCH_OPT_CSV] = BENCH_OPT_FLAG(
        "--csv",
        0,
        BENCH_SPEC_SET_CSV
    ),
    [BENCH_OPT_HASH_FN] = BENCH_OPT_VALUE(
        hash_fn,
        bench_parse_hash_fn_value,
        "--hash-fn",
        0,
        0
    ),
    [BENCH_OPT_STATS] = BENCH_OPT_VALUE(
        stats_mode,
        bench_parse_stats_mode_value,
        "--stats",
        0,
        0
    ),
    [BENCH_OPT_HELP] = BENCH_OPT_FLAG(
        "--help",
        0,
        BENCH_SPEC_PRINT_HELP
    ),
};

/* --- function prototypes: parsing ----------------------------------------- */

static int bench_parse_spec(
    int argc,
    char **argv,
    bench_spec *spec
);

static void bench_spec_init(
    bench_spec *spec
);

static bench_option_kind bench_parse_option_kind(
    const char *arg
);

static int bench_apply_option_desc(
    const bench_opt_desc *desc,
    int argc,
    char **argv,
    int *idx,
    bench_spec *spec,
    bench_parse_state *state,
    const char *prog
);

/* --- function prototypes: helpers ----------------------------------------- */

static int bench_validate_spec(
    const bench_spec *spec,
    const bench_parse_state *state
);

static void bench_print_usage(
    FILE *stream,
    const char *prog
);

static int bench_run_repetitions(
    const bench_spec *spec,
    const bench_plan *plan,
    bench_runner_dispatch runner
);

static void bench_report_repetition_status(
    const bench_plan *plan,
    size_t            repetition,
    const char       *status
);

int bench_cli_run(
    int argc,
    char **argv
) {
    bench_spec spec;
    bench_plan plan;
    bench_runner_dispatch runner = {0};
    int rc;

    rc = bench_parse_spec(argc, argv, &spec);
    if (rc == BENCH_CLI_HELP_EXIT) {
        return 0;
    }
    if (rc != 0) {
        return rc;
    }

    if (bench_plan_build(&spec, &plan) != 0) {
        return 1;
    }

    if (bench_plan_validate(&plan) != 0) {
        bench_print_usage(stderr, argv[0]);
        return 1;
    }

    if (plan.exec_mode == BENCH_EXEC_RESIZE) {
        if (plan.kind < BENCH_RESIZE_BUILD ||
            plan.kind > BENCH_RESIZE_WORKLOAD) {
            fprintf(stderr, "Unsupported benchmark: %s\n", argv[1]);
            return 1;
        } /* Kind is outside the resize runner table. */

        runner.resize = bench_resize_runners[plan.kind - BENCH_RESIZE_BUILD];

        return bench_run_repetitions(
            &spec,
            &plan,
            runner
        );
    }

    if (plan.exec_mode == BENCH_EXEC_CONCURRENT) {
        if (plan.kind < BENCH_CONCURRENT_LOOKUP ||
            plan.kind > BENCH_CONCURRENT_WORKLOAD) {
            fprintf(stderr, "Unsupported benchmark: %s\n", argv[1]);
            return 1;
        } /* Kind is outside the concurrent runner table. */

        runner.concurrent =
            bench_concurrent_runners[plan.kind - BENCH_CONCURRENT_LOOKUP];

        return bench_run_repetitions(
            &spec,
            &plan,
            runner
        );
    }

    if (plan.exec_mode != BENCH_EXEC_STEADY) {
        fprintf(stderr, "Unsupported benchmark execution mode\n");
        return 1;
    } /* Plan validation should leave only known execution modes. */

    if (plan.kind < BENCH_INSERT_BUILD || plan.kind > BENCH_WORKLOAD) {
        fprintf(stderr, "Unsupported benchmark: %s\n", argv[1]);
        return 1;
    } /* Kind is outside the steady runner table. */

    runner.steady = bench_runners[plan.kind];

    return bench_run_repetitions(
        &spec,
        &plan,
        runner
    );
}

static int bench_parse_spec(
    int argc,
    char **argv,
    bench_spec *spec
) {
    int i;
    int rc;
    bench_parse_state state;

    if (argc < 2 || argv == NULL || spec == NULL) {
        bench_print_usage(stderr, (argc > 0 && argv != NULL) ? argv[0] : "htbench");
        return 1;
    }

    bench_spec_init(spec);
    memset(&state, 0, sizeof(state));

    if (bench_parse_option_kind(argv[1]) == BENCH_OPT_HELP) {
        bench_print_usage(stdout, argv[0]);
        return BENCH_CLI_HELP_EXIT;
    } /* Allow `htbench --help` without requiring a subcommand. */

    if (bench_parse_kind(argv[1], &spec->kind) != 0) {
        fprintf(stderr, "Unknown benchmark: %s\n", argv[1]);
        bench_print_usage(stderr, argv[0]);
        return 1;
    }

    for (i = 2; i < argc; i++) {
        const bench_opt_desc *desc;
        bench_option_kind opt_kind;

        opt_kind = bench_parse_option_kind(argv[i]);
        desc = NULL;
        if (opt_kind > BENCH_OPT_UNKNOWN && opt_kind <= BENCH_OPT_HELP) {
            desc = &bench_opt_descs[opt_kind];
        } /* Option kind is safe to index into the descriptor table. */

        if (desc == NULL || desc->opt_name == NULL) {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            bench_print_usage(stderr, argv[0]);
            return 1;
        }

        rc = bench_apply_option_desc(desc, argc, argv, &i, spec, &state, argv[0]);
        if (rc != 0) {
            if (rc == BENCH_CLI_HELP_EXIT) {
                return BENCH_CLI_HELP_EXIT;
            }
            return 1;
        }
    }

    if (bench_validate_spec(spec, &state) != 0) {
        bench_print_usage(stderr, argv[0]);
        return 1;
    }

    return 0;
}

static int bench_apply_option_desc(
    const bench_opt_desc *desc,
    int argc,
    char **argv,
    int *idx,
    bench_spec *spec,
    bench_parse_state *state,
    const char *prog
) {
    void *field_out;
    const char *value;

    if (desc == NULL || spec == NULL || state == NULL) {
        return -1;
    }

    if (desc->takes_value) {
        if (argv == NULL || idx == NULL || desc->parser == NULL) {
            return -1;
        }

        if (*idx + 1 >= argc) {
            fprintf(stderr, "Invalid value for %s\n", desc->opt_name);
            bench_print_usage(stderr, prog);
            return -1;
        } /* Value-taking options require one following token. */

        *idx += 1;
        value = argv[*idx];
        field_out = (void *)((char *)spec + desc->field_offset);
        if (desc->parser(value, field_out) != 0) {
            fprintf(stderr, "Invalid value for %s\n", desc->opt_name);
            bench_print_usage(stderr, prog);
            return -1;
        }
    }

    state->flags |= desc->parse_flags;

    if ((desc->spec_flags & BENCH_SPEC_SET_CSV) != 0) {
        spec->csv_output = 1;
    }
    if ((desc->spec_flags & BENCH_SPEC_SET_RESIZE_MODE) != 0) {
        spec->resize_mode_set = 1;
    }
    if ((desc->spec_flags & BENCH_SPEC_SET_PREFILL_ALPHA) != 0) {
        spec->prefill_alpha_set = 1;
    }
    if ((desc->spec_flags & BENCH_SPEC_PRINT_HELP) != 0) {
        bench_print_usage(stdout, prog);
        return BENCH_CLI_HELP_EXIT;
    } /* Help is treated as a successful early exit. */

    return 0;
}

static void bench_spec_init(
    bench_spec *spec
) {
    memset(spec, 0, sizeof(*spec));
    spec->workload     = WORKLOAD_MIXED;
    spec->impl_kind    = HT_IMPL_OPEN_ADDRESSING;
    spec->target_alpha = 0.70;
    spec->repetitions  = 1;
    spec->capacity_mode = BENCH_CAPACITY_FIXED;
    spec->resize_mode   = BENCH_RESIZE_DISABLED;
    spec->stats_mode    = BENCH_STATS_OFF;
    spec->concurrent_resize_mode = BENCH_RESIZE_DISABLED;
    spec->concurrent_keyspace_mode = BENCH_KEYSPACE_DISJOINT;
    /* Defaults describe the smallest fixed-capacity steady benchmark. */
}

static bench_option_kind bench_parse_option_kind(
    const char *arg
) {
    size_t i;

    if (arg == NULL) {
        return BENCH_OPT_UNKNOWN;
    }

    for (i = 0; i < BENCH_ARRAY_LEN(bench_option_maps); i++) {
        if (strcmp(arg, bench_option_maps[i].name) == 0) {
            return bench_option_maps[i].kind;
        } /* Short and long help names both map to BENCH_OPT_HELP. */
    }

    return BENCH_OPT_UNKNOWN;
}

static int bench_run_repetitions(
    const bench_spec *spec,
    const bench_plan *plan,
    bench_runner_dispatch runner
) {
    bench_run_result result;
    size_t repetition;

    if (spec == NULL || plan == NULL) {
        return 1;
    }

    if (spec->csv_output) {
        switch (plan->exec_mode) {
        case BENCH_EXEC_STEADY:
            bench_output_write_csv_header(stdout);
            break;
        case BENCH_EXEC_RESIZE:
            bench_output_write_resize_csv_header(stdout);
            break;
        case BENCH_EXEC_CONCURRENT:
            bench_output_write_concurrent_csv_header(stdout);
            break;
        }
    }

    for (repetition = 0; repetition < plan->repetitions; repetition++) {
        memset(&result, 0, sizeof(result));
        bench_report_repetition_status(plan, repetition, "starting");

        switch (plan->exec_mode) {
        case BENCH_EXEC_STEADY:
            if (runner.steady == NULL ||
                runner.steady(plan, &result.core) != 0) {
                bench_report_repetition_status(plan, repetition, "failed");
                return 1;
            }
            break;
        case BENCH_EXEC_RESIZE:
            if (runner.resize == NULL || runner.resize(plan, &result) != 0) {
                bench_report_repetition_status(plan, repetition, "failed");
                return 1;
            }
            break;
        case BENCH_EXEC_CONCURRENT:
            if (runner.concurrent == NULL ||
                runner.concurrent(plan, &result) != 0) {
                bench_report_repetition_status(plan, repetition, "failed");
                return 1;
            }
            break;
        default:
            bench_report_repetition_status(plan, repetition, "failed");
            return 1;
        } /* Dispatch the selected runner family for this repetition. */

        if (spec->csv_output) {
            switch (plan->exec_mode) {
            case BENCH_EXEC_STEADY:
                bench_output_write_csv_row(stdout, &result.core, repetition);
                break;
            case BENCH_EXEC_RESIZE:
                bench_output_write_resize_csv_row(stdout, &result, repetition);
                break;
            case BENCH_EXEC_CONCURRENT:
                bench_output_write_concurrent_csv_row(
                    stdout,
                    &result,
                    repetition
                );
                break;
            }
        }
        bench_report_repetition_status(plan, repetition, "done");
    }

    return 0;
}

static void bench_report_repetition_status(
    const bench_plan *plan,
    size_t            repetition,
    const char       *status
) {
    if (plan == NULL || status == NULL) {
        return;
    }

    if (plan->kind == BENCH_WORKLOAD ||
        plan->kind == BENCH_RESIZE_WORKLOAD ||
        plan->kind == BENCH_CONCURRENT_WORKLOAD) {
        fprintf(
            stderr,
            "[htbench] %s workload=%s impl=%s repetition=%zu/%zu %s\n",
            bench_kind_name(plan->kind),
            bench_workload_name(plan->workload),
            bench_impl_name(plan->impl_kind),
            repetition + 1,
            plan->repetitions,
            status
        );
    } else {
        fprintf(
            stderr,
            "[htbench] %s impl=%s repetition=%zu/%zu %s\n",
            bench_kind_name(plan->kind),
            bench_impl_name(plan->impl_kind),
            repetition + 1,
            plan->repetitions,
            status
        );
    }

    fflush(stderr);
}

static int bench_validate_spec(
    const bench_spec *spec,
    const bench_parse_state *state
) {
    int is_resize;
    int is_concurrent;

    if (spec == NULL) {
        fprintf(stderr, "Missing benchmark specification\n");
        return -1;
    }

    is_resize = (spec->kind >= BENCH_RESIZE_BUILD &&
                 spec->kind <= BENCH_RESIZE_WORKLOAD);
    is_concurrent = (spec->kind == BENCH_CONCURRENT_LOOKUP ||
                     spec->kind == BENCH_CONCURRENT_WORKLOAD);

    /* Common required values. */
    if (spec->dataset_size == 0) {
        fprintf(stderr, "--dataset-size must be greater than zero\n");
        return -1;
    }

    if (!BENCH_ALPHA_VALID(spec->target_alpha)) {
        fprintf(stderr, "--alpha must be greater than 0 and less than 1\n");
        return -1;
    }

    if (spec->timed_ops == 0) {
        fprintf(stderr, "--timed-ops must be greater than zero\n");
        return -1;
    }

    if (spec->repetitions == 0) {
        fprintf(stderr, "--repetitions must be greater than zero\n");
        return -1;
    }

    /* Option scope checks. */
    if (BENCH_PARSE_HAS(state, BENCH_PARSE_RESIZE_MODE) && !is_resize) {
        fprintf(stderr,
                "--resize is only valid for resize benchmarks; use "
                "--concurrent-resize for concurrent workloads\n");
        return -1;
    }

    if ((BENCH_PARSE_HAS(state, BENCH_PARSE_INITIAL_CAPACITY) ||
         BENCH_PARSE_HAS(state, BENCH_PARSE_GROW_ALPHA)) &&
        !is_resize && !is_concurrent) {
        fprintf(stderr,
                "--initial-capacity and --grow-alpha are only valid for "
                "resize or concurrent benchmarks\n");
        return -1;
    }

    if (BENCH_PARSE_HAS(state, BENCH_PARSE_SHRINK_ALPHA) && !is_resize) {
        fprintf(stderr, "--shrink-alpha is only valid for resize benchmarks\n");
        return -1;
    }

    if (spec->prefill_alpha_set && spec->kind != BENCH_RESIZE_WORKLOAD) {
        fprintf(stderr, "--prefill-alpha is only valid for resize-workload\n");
        return -1;
    } /* Prefill density has meaning only for resize-workload setup. */

    if (BENCH_PARSE_HAS(state, BENCH_PARSE_THREAD_OPTION) && !is_concurrent) {
        fprintf(stderr, "Thread options are only valid for concurrent benchmarks\n");
        return -1;
    }

    if (BENCH_PARSE_HAS(state, BENCH_PARSE_CONCURRENT_KEYSPACE) &&
        spec->kind != BENCH_CONCURRENT_WORKLOAD) {
        fprintf(stderr,
                "--concurrent-keyspace is only valid for concurrent-workload\n");
        return -1;
    }

    if (BENCH_PARSE_HAS(state, BENCH_PARSE_CONCURRENT_RESIZE) &&
        spec->kind != BENCH_CONCURRENT_WORKLOAD) {
        fprintf(stderr,
                "--concurrent-resize is only valid for concurrent-workload\n");
        return -1;
    }

    /* Resize-only constraints. */
    if (is_resize &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_RESIZE_MODE) &&
        spec->resize_mode == BENCH_RESIZE_DISABLED) {
        fprintf(stderr, "--resize disabled is not valid for resize benchmarks\n");
        return -1;
    } /* Explicit disabled mode contradicts resize benchmark selection. */

    if (is_resize &&
        spec->kind == BENCH_RESIZE_WORKLOAD &&
        spec->prefill_alpha_set &&
        !BENCH_ALPHA_VALID(spec->prefill_alpha)) {
        fprintf(stderr, "--prefill-alpha must be greater than 0 and less than 1\n");
        return -1;
    }

    if (is_resize &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_INITIAL_CAPACITY) &&
        spec->initial_capacity == 0) {
        fprintf(stderr, "--initial-capacity must be greater than zero\n");
        return -1;
    }

    if (is_resize &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_GROW_ALPHA) &&
        !BENCH_ALPHA_VALID(spec->grow_alpha)) {
        fprintf(stderr, "--grow-alpha must be greater than 0 and less than 1\n");
        return -1;
    }

    if (is_resize &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_SHRINK_ALPHA) &&
        !BENCH_ALPHA_VALID(spec->shrink_alpha)) {
        fprintf(stderr, "--shrink-alpha must be greater than 0 and less than 1\n");
        return -1;
    }

    if (is_resize &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_GROW_ALPHA) &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_SHRINK_ALPHA) &&
        spec->shrink_alpha >= spec->grow_alpha) {
        fprintf(stderr, "--shrink-alpha must be less than --grow-alpha\n");
        return -1;
    }

    /* Concurrent-only constraints. */
    if (is_concurrent &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_CONCURRENT_RESIZE) &&
        spec->concurrent_resize_mode != BENCH_RESIZE_DISABLED &&
        spec->concurrent_resize_mode != BENCH_RESIZE_GROW_ONLY) {
        fprintf(stderr,
                "--concurrent-resize currently supports disabled or grow-only\n");
        return -1;
    }

    if (is_concurrent &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_THREAD_COUNT) &&
        spec->thread_count == 0) {
        fprintf(stderr, "--thread-count must be greater than zero\n");
        return -1;
    }

    if (is_concurrent &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_INITIAL_CAPACITY) &&
        spec->initial_capacity == 0) {
        fprintf(stderr, "--initial-capacity must be greater than zero\n");
        return -1;
    }

    if (is_concurrent &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_GROW_ALPHA) &&
        !BENCH_ALPHA_VALID(spec->grow_alpha)) {
        fprintf(stderr, "--grow-alpha must be greater than 0 and less than 1\n");
        return -1;
    }

    if (is_concurrent &&
        BENCH_PARSE_HAS(state, BENCH_PARSE_GROW_ALPHA) &&
        spec->concurrent_resize_mode == BENCH_RESIZE_DISABLED) {
        fprintf(stderr,
                "--grow-alpha for concurrent benchmarks requires "
                "--concurrent-resize grow-only\n");
        return -1;
    } /* Grow threshold only matters when concurrent growth is enabled. */

    /* Workload benchmarks need deterministic trace generation. */
    if ((spec->kind == BENCH_WORKLOAD ||
         spec->kind == BENCH_RESIZE_WORKLOAD ||
         spec->kind == BENCH_CONCURRENT_WORKLOAD) &&
        spec->trace_seed == 0) {
        fprintf(stderr, "--trace-seed must be provided for workload benchmarks\n");
        return -1;
    }

    return 0;
}

static void bench_print_usage(
    FILE *stream,
    const char *prog
) {
    size_t i;

    if (stream == NULL) {
        return;
    }

    fprintf(
        stream,
        "Usage: %s <subcommand> [options]\n",
        (prog != NULL) ? prog : "htbench"
    );
    for (i = 0; i < BENCH_ARRAY_LEN(bench_usage_lines); i++) {
        fprintf(stream, "%s\n", bench_usage_lines[i]);
    }
}
