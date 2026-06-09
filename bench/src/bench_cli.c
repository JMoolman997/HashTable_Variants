/**
 * @file    bench_cli.c
 * @brief   Command-line parsing and benchmark dispatch for htbench.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_backend_pthread.h"
#include "bench_backend_single.h"
#include "bench_cli.h"
#include "bench_config.h"
#include "bench_fixture.h"
#include "bench_metric.h"
#include "bench_names.h"
#include "bench_output.h"
#include "bench_plan.h"
#include "ht_registry.h"

enum {
    BENCH_CLI_HELP_EXIT = 2
};

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
    BENCH_OPT_STATS,
    BENCH_OPT_FORMAT,
    BENCH_OPT_CSV,
    BENCH_OPT_HELP
} bench_option_kind;

typedef int (*bench_value_parser_fn)(
    const char *text,
    void *out
);

typedef struct {
    int takes_value;
    size_t field_offset;
    bench_value_parser_fn parser;
    const char *opt_name;
    uint64_t option_bit;
} bench_opt_desc;

#define BENCH_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
#define BENCH_OPT_VALUE(field, parser_fn, name, bit) \
    {1, offsetof(bench_plan, field), (parser_fn), (name), (bit)}
#define BENCH_OPT_FLAG(name, bit) \
    {0, 0, NULL, (name), (bit)}
#define BENCH_DEFINE_VALUE_PARSER(name, parse_fn) \
    static int name(const char *text, void *out) { return parse_fn(text, out); }

static int bench_parse_ull(
    const char *arg,
    unsigned long long *out
);

static int bench_parse_size(
    const char *arg,
    size_t *out
);

static int bench_parse_u64(
    const char *arg,
    uint64_t *out
);

static int bench_parse_double(
    const char *arg,
    double *out
);

BENCH_DEFINE_VALUE_PARSER(bench_parse_impl_value, bench_parse_impl)
BENCH_DEFINE_VALUE_PARSER(bench_parse_size_value, bench_parse_size)
BENCH_DEFINE_VALUE_PARSER(bench_parse_double_value, bench_parse_double)
BENCH_DEFINE_VALUE_PARSER(bench_parse_u64_value, bench_parse_u64)
BENCH_DEFINE_VALUE_PARSER(bench_parse_workload_value, bench_parse_workload)
BENCH_DEFINE_VALUE_PARSER(bench_parse_resize_mode_value, bench_parse_resize_mode)
BENCH_DEFINE_VALUE_PARSER(bench_parse_keyspace_value, bench_parse_keyspace)
BENCH_DEFINE_VALUE_PARSER(bench_parse_stats_mode_value, bench_parse_stats_mode)
BENCH_DEFINE_VALUE_PARSER(bench_parse_format_value, bench_parse_output_format)

static const bench_opt_desc bench_opt_descs[] = {
    [BENCH_OPT_IMPL] = BENCH_OPT_VALUE(
        impl_kind,
        bench_parse_impl_value,
        "--impl",
        BENCH_OPTBIT_IMPL
    ),
    [BENCH_OPT_DATASET_SIZE] = BENCH_OPT_VALUE(
        dataset_size,
        bench_parse_size_value,
        "--dataset-size",
        BENCH_OPTBIT_DATASET_SIZE
    ),
    [BENCH_OPT_ALPHA] = BENCH_OPT_VALUE(
        target_alpha,
        bench_parse_double_value,
        "--alpha",
        BENCH_OPTBIT_ALPHA
    ),
    [BENCH_OPT_TIMED_OPS] = BENCH_OPT_VALUE(
        timed_ops,
        bench_parse_size_value,
        "--timed-ops",
        BENCH_OPTBIT_TIMED_OPS
    ),
    [BENCH_OPT_WARMUP_OPS] = BENCH_OPT_VALUE(
        warmup_ops,
        bench_parse_size_value,
        "--warmup-ops",
        BENCH_OPTBIT_WARMUP_OPS
    ),
    [BENCH_OPT_REPETITIONS] = BENCH_OPT_VALUE(
        repetitions,
        bench_parse_size_value,
        "--repetitions",
        BENCH_OPTBIT_REPETITIONS
    ),
    [BENCH_OPT_HASH_SEED] = BENCH_OPT_VALUE(
        hash_seed,
        bench_parse_u64_value,
        "--hash-seed",
        BENCH_OPTBIT_HASH_SEED
    ),
    [BENCH_OPT_KEY_SEED] = BENCH_OPT_VALUE(
        key_seed,
        bench_parse_u64_value,
        "--key-seed",
        BENCH_OPTBIT_KEY_SEED
    ),
    [BENCH_OPT_TRACE_SEED] = BENCH_OPT_VALUE(
        trace_seed,
        bench_parse_u64_value,
        "--trace-seed",
        BENCH_OPTBIT_TRACE_SEED
    ),
    [BENCH_OPT_WORKLOAD] = BENCH_OPT_VALUE(
        workload,
        bench_parse_workload_value,
        "--workload",
        BENCH_OPTBIT_WORKLOAD
    ),
    [BENCH_OPT_INITIAL_CAPACITY] = BENCH_OPT_VALUE(
        initial_capacity,
        bench_parse_size_value,
        "--initial-capacity",
        BENCH_OPTBIT_INITIAL_CAPACITY
    ),
    [BENCH_OPT_RESIZE] = BENCH_OPT_VALUE(
        resize_mode,
        bench_parse_resize_mode_value,
        "--resize",
        BENCH_OPTBIT_RESIZE
    ),
    [BENCH_OPT_GROW_ALPHA] = BENCH_OPT_VALUE(
        grow_alpha,
        bench_parse_double_value,
        "--grow-alpha",
        BENCH_OPTBIT_GROW_ALPHA
    ),
    [BENCH_OPT_SHRINK_ALPHA] = BENCH_OPT_VALUE(
        shrink_alpha,
        bench_parse_double_value,
        "--shrink-alpha",
        BENCH_OPTBIT_SHRINK_ALPHA
    ),
    [BENCH_OPT_PREFILL_ALPHA] = BENCH_OPT_VALUE(
        prefill_alpha,
        bench_parse_double_value,
        "--prefill-alpha",
        BENCH_OPTBIT_PREFILL_ALPHA
    ),
    [BENCH_OPT_THREAD_COUNT] = BENCH_OPT_VALUE(
        thread_count,
        bench_parse_size_value,
        "--thread-count",
        BENCH_OPTBIT_THREAD_COUNT
    ),
    [BENCH_OPT_CONCURRENT_KEYSPACE] = BENCH_OPT_VALUE(
        keyspace_mode,
        bench_parse_keyspace_value,
        "--concurrent-keyspace",
        BENCH_OPTBIT_CONCURRENT_KEYSPACE
    ),
    [BENCH_OPT_CONCURRENT_RESIZE] = BENCH_OPT_VALUE(
        concurrent_resize_mode,
        bench_parse_resize_mode_value,
        "--concurrent-resize",
        BENCH_OPTBIT_CONCURRENT_RESIZE
    ),
    [BENCH_OPT_STATS] = BENCH_OPT_VALUE(
        stats_mode,
        bench_parse_stats_mode_value,
        "--stats",
        BENCH_OPTBIT_STATS
    ),
    [BENCH_OPT_FORMAT] = BENCH_OPT_VALUE(
        output_format,
        bench_parse_format_value,
        "--format",
        BENCH_OPTBIT_FORMAT
    ),
    [BENCH_OPT_CSV] = BENCH_OPT_FLAG(
        "--csv",
        BENCH_OPTBIT_FORMAT
    ),
    [BENCH_OPT_HELP] = BENCH_OPT_FLAG(
        "--help",
        0
    ),
};

static int bench_parse_plan(
    int argc,
    char **argv,
    bench_plan *plan
);

static void bench_plan_init_defaults(
    bench_plan *plan
);

static void bench_plan_apply_case(
    bench_plan *plan,
    const bench_case *bench_case
);

static bench_option_kind bench_parse_option_kind(
    const char *arg
);

static int bench_apply_option_desc(
    const bench_opt_desc *desc,
    int argc,
    char **argv,
    int *idx,
    bench_plan *plan,
    const char *opt_arg,
    const char *prog
);

static void bench_print_usage(
    FILE *stream,
    const char *prog
);

static void bench_print_impl_list(
    FILE *stream
);

static int bench_run_repetitions(
    const bench_plan *plan
);

static void bench_report_repetition_status(
    const bench_plan *plan,
    size_t repetition,
    const char *status
);

int bench_cli_run(
    int argc,
    char **argv
) {
    bench_plan plan;
    int rc;

    rc = bench_parse_plan(argc, argv, &plan);
    if (rc == BENCH_CLI_HELP_EXIT) {
        return 0;
    }
    if (rc != 0) {
        return rc;
    }

    if (bench_plan_validate(&plan) != 0) {
        bench_print_usage(stderr, argv[0]);
        return 1;
    }

    return bench_run_repetitions(&plan);
}

static int bench_parse_plan(
    int argc,
    char **argv,
    bench_plan *plan
) {
    const bench_case *bench_case;
    int i;
    int rc;

    if (argc < 2 || argv == NULL || plan == NULL) {
        bench_print_usage(stderr, (argc > 0 && argv != NULL) ? argv[0] : "htbench");
        return 1;
    }

    bench_plan_init_defaults(plan);

    if (bench_parse_option_kind(argv[1]) == BENCH_OPT_HELP) {
        bench_print_usage(stdout, argv[0]);
        return BENCH_CLI_HELP_EXIT;
    }

    bench_case = bench_case_find(argv[1]);
    if (bench_case == NULL) {
        fprintf(stderr, "Unknown benchmark: %s\n", argv[1]);
        bench_print_usage(stderr, argv[0]);
        return 1;
    }
    bench_plan_apply_case(plan, bench_case);

    for (i = 2; i < argc; i++) {
        const bench_opt_desc *desc;
        bench_option_kind opt_kind;

        opt_kind = bench_parse_option_kind(argv[i]);
        desc = NULL;
        if (opt_kind > BENCH_OPT_UNKNOWN && opt_kind <= BENCH_OPT_HELP) {
            desc = &bench_opt_descs[opt_kind];
        }
        if (desc == NULL || desc->opt_name == NULL) {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            bench_print_usage(stderr, argv[0]);
            return 1;
        }

        rc = bench_apply_option_desc(
            desc,
            argc,
            argv,
            &i,
            plan,
            argv[i],
            argv[0]
        );
        if (rc != 0) {
            return (rc == BENCH_CLI_HELP_EXIT) ? BENCH_CLI_HELP_EXIT : 1;
        }
    }

    return 0;
}

static void bench_plan_init_defaults(
    bench_plan *plan
) {
    memset(plan, 0, sizeof(*plan));
    plan->workload = WORKLOAD_MIXED;
    plan->impl_kind = HT_IMPL_OPEN_ADDRESSING;
    plan->target_alpha = 0.70;
    plan->repetitions = 1;
    plan->resize_mode = BENCH_RESIZE_DISABLED;
    plan->stats_mode = BENCH_STATS_OFF;
    plan->concurrent_resize_mode = BENCH_RESIZE_DISABLED;
    plan->keyspace_mode = BENCH_KEYSPACE_DISJOINT;
    plan->output_format = BENCH_FORMAT_TEXT;
    plan->thread_count = 1;
}

static void bench_plan_apply_case(
    bench_plan *plan,
    const bench_case *bench_case
) {
    plan->bench_case = bench_case;
    plan->benchmark_name = bench_case->name;
    plan->scenario = bench_case->scenario;
    plan->backend = bench_case->backend;
    plan->capacity_policy = bench_case->capacity_policy;
    plan->resize_mode = bench_case->default_resize_mode;
    plan->keyspace_mode = bench_case->default_keyspace_mode;
}

static int bench_apply_option_desc(
    const bench_opt_desc *desc,
    int argc,
    char **argv,
    int *idx,
    bench_plan *plan,
    const char *opt_arg,
    const char *prog
) {
    void *field_out;
    const char *value;
    const char *display_name;

    if (desc == NULL || plan == NULL || idx == NULL) {
        return -1;
    }
    display_name = (opt_arg != NULL) ? opt_arg : desc->opt_name;
    if (strcmp(desc->opt_name, "--help") == 0) {
        bench_print_usage(stdout, prog);
        return BENCH_CLI_HELP_EXIT;
    }

    if (desc->takes_value) {
        if (argv == NULL || desc->parser == NULL || *idx + 1 >= argc) {
            fprintf(stderr, "Invalid value for %s\n", display_name);
            bench_print_usage(stderr, prog);
            return -1;
        }

        *idx += 1;
        value = argv[*idx];
        field_out = (void *)((char *)plan + desc->field_offset);
        if (desc->parser(value, field_out) != 0) {
            fprintf(stderr, "Invalid value for %s\n", display_name);
            bench_print_usage(stderr, prog);
            return -1;
        }
    } else if (strcmp(desc->opt_name, "--csv") == 0) {
        plan->output_format = BENCH_FORMAT_CSV;
    }

    plan->set_options |= desc->option_bit;
    return 0;
}

static bench_option_kind bench_parse_option_kind(
    const char *arg
) {
    size_t i;

    if (arg == NULL) {
        return BENCH_OPT_UNKNOWN;
    }
    if (strcmp(arg, "-h") == 0) {
        return BENCH_OPT_HELP;
    }
    if (strcmp(arg, "--threads") == 0) {
        return BENCH_OPT_THREAD_COUNT;
    }
    for (i = 1; i < BENCH_ARRAY_LEN(bench_opt_descs); i++) {
        if (bench_opt_descs[i].opt_name != NULL &&
            strcmp(arg, bench_opt_descs[i].opt_name) == 0) {
            return (bench_option_kind)i;
        }
    }

    return BENCH_OPT_UNKNOWN;
}

static int bench_run_repetitions(
    const bench_plan *plan
) {
    size_t repetition;

    if (plan == NULL) {
        return 1;
    }

    if (plan->output_format == BENCH_FORMAT_CSV) {
        bench_output_write_canonical_csv_header(stdout);
    }

    for (repetition = 0; repetition < plan->repetitions; repetition++) {
        bench_fixture fixture;
        bench_sample sample;
        int rc;

        memset(&fixture, 0, sizeof(fixture));
        bench_sample_init(&sample, repetition);
        bench_report_repetition_status(plan, repetition, "starting");

        if (bench_fixture_build(plan, &fixture) != 0) {
            bench_report_repetition_status(plan, repetition, "failed");
            bench_fixture_destroy(&fixture, plan);
            return 1;
        }

        if (plan->backend == BENCH_BACKEND_SINGLE) {
            rc = bench_backend_single_run(plan, &fixture, &sample);
        } else {
            rc = bench_backend_pthread_run(plan, &fixture, &sample);
        }

        if (rc != 0) {
            bench_report_repetition_status(plan, repetition, "failed");
            bench_fixture_destroy(&fixture, plan);
            return 1;
        }

        if (plan->output_format == BENCH_FORMAT_CSV) {
            bench_output_write_canonical_csv_sample(stdout, plan, &sample);
        } else {
            bench_output_write_text_sample(stdout, plan, &sample);
        }

        bench_fixture_destroy(&fixture, plan);
        bench_report_repetition_status(plan, repetition, "done");
    }

    return 0;
}

static void bench_report_repetition_status(
    const bench_plan *plan,
    size_t repetition,
    const char *status
) {
    if (plan == NULL || status == NULL) {
        return;
    }

    if (plan->scenario == BENCH_SCENARIO_WORKLOAD) {
        fprintf(
            stderr,
            "[htbench] %s workload=%s impl=%s repetition=%zu/%zu %s\n",
            plan->benchmark_name,
            bench_workload_name(plan->workload),
            ht_impl_name(plan->impl_kind),
            repetition + 1,
            plan->repetitions,
            status
        );
    } else {
        fprintf(
            stderr,
            "[htbench] %s impl=%s repetition=%zu/%zu %s\n",
            plan->benchmark_name,
            ht_impl_name(plan->impl_kind),
            repetition + 1,
            plan->repetitions,
            status
        );
    }

    fflush(stderr);
}

static void bench_print_usage(
    FILE *stream,
    const char *prog
) {
    const bench_case *cases;
    size_t case_count;
    size_t i;

    if (stream == NULL) {
        return;
    }

    fprintf(
        stream,
        "Usage: %s <subcommand> [options]\n",
        (prog != NULL) ? prog : "htbench"
    );

    fprintf(stream, "Subcommands:\n");
    cases = bench_cases(&case_count);
    for (i = 0; i < case_count; i++) {
        fprintf(stream, "  %s\n", cases[i].name);
    }

    fprintf(stream, "Options:\n");
    bench_print_impl_list(stream);
    fprintf(stream, "  --dataset-size <n>\n");
    fprintf(stream, "  --alpha <0<x<1>\n");
    fprintf(stream, "  --timed-ops <n>\n");
    fprintf(stream, "  --warmup-ops <n>\n");
    fprintf(stream, "  --repetitions <n>\n");
    fprintf(stream, "  --hash-seed <n>\n");
    fprintf(stream, "  --key-seed <n>\n");
    fprintf(stream, "  --trace-seed <n>\n");
    fprintf(stream, "  --workload <read-only|read-heavy|mixed|balanced>\n");
    fprintf(stream, "  --initial-capacity <n>\n");
    fprintf(stream, "  --resize <disabled|grow-only|grow-shrink>\n");
    fprintf(stream, "  --grow-alpha <0<x<1>\n");
    fprintf(stream, "  --shrink-alpha <0<x<1>\n");
    fprintf(stream, "  --prefill-alpha <0<x<1>\n");
    fprintf(stream, "  --thread-count|--threads <n>\n");
    fprintf(stream, "  --concurrent-keyspace <disjoint>\n");
    fprintf(stream, "  --concurrent-resize <disabled|grow-only>\n");
    fprintf(stream, "  --stats <off|on>\n");
    fprintf(stream, "  --format <text|csv>\n");
    fprintf(stream, "  --csv\n");
    fprintf(stream, "  --help\n");
}

static void bench_print_impl_list(
    FILE *stream
) {
    const ht_registry_entry *entries;
    size_t count;
    size_t i;

    entries = ht_registry_entries(&count);
    fprintf(stream, "  --impl <");
    for (i = 0; i < count; i++) {
        fprintf(stream, "%s%s", (i == 0) ? "" : "|", entries[i].name);
    }
    fprintf(stream, ">\n");
}

static int bench_parse_ull(
    const char *arg,
    unsigned long long *out
) {
    unsigned long long parsed;
    char *end;

    if (arg == NULL || out == NULL) {
        return -1;
    }

    errno = 0;
    parsed = strtoull(arg, &end, 10);
    if (errno != 0 || end == arg || *end != '\0') {
        return -1;
    }

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

    errno = 0;
    parsed = strtod(arg, &end);
    if (errno != 0 || end == arg || *end != '\0') {
        return -1;
    }

    *out = parsed;
    return 0;
}
