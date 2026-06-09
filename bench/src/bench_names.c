/**
 * @file    bench_names.c
 * @brief   Shared benchmark name helpers.
 */

#include <stddef.h>
#include <string.h>

#include "ht_registry.h"
#include "bench_names.h"

#define BENCH_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    const char *name;
    int value;
} bench_name_map;

static const bench_name_map bench_scenario_maps[] = {
    {"insert-build", BENCH_SCENARIO_INSERT_BUILD},
    {"lookup-hit", BENCH_SCENARIO_LOOKUP_HIT},
    {"lookup-miss", BENCH_SCENARIO_LOOKUP_MISS},
    {"erase-existing", BENCH_SCENARIO_ERASE_EXISTING},
    {"workload", BENCH_SCENARIO_WORKLOAD},
};

static const bench_name_map bench_backend_maps[] = {
    {"single", BENCH_BACKEND_SINGLE},
    {"pthread", BENCH_BACKEND_PTHREAD},
};

static const bench_name_map bench_workload_maps[] = {
    {"read-only", WORKLOAD_READ_ONLY},
    {"read-heavy", WORKLOAD_READ_HEAVY},
    {"mixed", WORKLOAD_MIXED},
    {"balanced", WORKLOAD_BALANCED},
};

static const bench_name_map bench_keyspace_maps[] = {
    {"disjoint", BENCH_KEYSPACE_DISJOINT},
    {"shared-read", BENCH_KEYSPACE_SHARED_READ},
};

static const bench_name_map bench_resize_mode_maps[] = {
    {"disabled", BENCH_RESIZE_DISABLED},
    {"grow-only", BENCH_RESIZE_GROW_ONLY},
    {"grow-shrink", BENCH_RESIZE_GROW_SHRINK},
};

static const bench_name_map bench_stats_mode_maps[] = {
    {"off", BENCH_STATS_OFF},
    {"on", BENCH_STATS_ON},
};

static const bench_name_map bench_output_format_maps[] = {
    {"text", BENCH_FORMAT_TEXT},
    {"csv", BENCH_FORMAT_CSV},
};

static int bench_lookup_name_map(
    const char *text,
    const bench_name_map *map,
    size_t count,
    int *value_out
) {
    size_t i;

    if (text == NULL || map == NULL || value_out == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (strcmp(text, map[i].name) == 0) {
            *value_out = map[i].value;
            return 0;
        }
    }

    return -1;
}

static const char *bench_lookup_value_name(
    int value,
    const bench_name_map *map,
    size_t count
) {
    size_t i;

    if (map == NULL) {
        return "unknown";
    }

    for (i = 0; i < count; i++) {
        if (map[i].value == value) {
            return map[i].name;
        }
    }

    return "unknown";
}

const char *bench_scenario_name(
    bench_scenario scenario
) {
    return bench_lookup_value_name(
        (int)scenario,
        bench_scenario_maps,
        BENCH_ARRAY_LEN(bench_scenario_maps)
    );
}

const char *bench_backend_name(
    bench_backend_kind backend
) {
    return bench_lookup_value_name(
        (int)backend,
        bench_backend_maps,
        BENCH_ARRAY_LEN(bench_backend_maps)
    );
}

const char *bench_workload_name(
    workload_kind workload
) {
    return bench_lookup_value_name(
        (int)workload,
        bench_workload_maps,
        BENCH_ARRAY_LEN(bench_workload_maps)
    );
}

const char *bench_keyspace_name(
    bench_keyspace_mode keyspace_mode
) {
    return bench_lookup_value_name(
        (int)keyspace_mode,
        bench_keyspace_maps,
        BENCH_ARRAY_LEN(bench_keyspace_maps)
    );
}

const char *bench_resize_mode_name(
    bench_resize_mode resize_mode
) {
    return bench_lookup_value_name(
        (int)resize_mode,
        bench_resize_mode_maps,
        BENCH_ARRAY_LEN(bench_resize_mode_maps)
    );
}

const char *bench_stats_mode_name(
    bench_stats_mode stats_mode
) {
    return bench_lookup_value_name(
        (int)stats_mode,
        bench_stats_mode_maps,
        BENCH_ARRAY_LEN(bench_stats_mode_maps)
    );
}

int bench_parse_impl(
    const char *text,
    ht_impl *out
) {
    const ht_registry_entry *entries;
    size_t count;
    size_t i;

    if (text == NULL || out == NULL) {
        return -1;
    }

    entries = ht_registry_entries(&count);
    for (i = 0; i < count; i++) {
        if (strcmp(text, entries[i].name) == 0) {
            *out = entries[i].impl;
            return 0;
        }
    }

    return -1;
}

int bench_parse_workload(
    const char *text,
    workload_kind *out
) {
    int value;

    if (bench_lookup_name_map(
            text,
            bench_workload_maps,
            BENCH_ARRAY_LEN(bench_workload_maps),
            &value
        ) != 0) {
        return -1;
    }

    *out = (workload_kind)value;
    return 0;
}

int bench_parse_keyspace(
    const char *text,
    bench_keyspace_mode *out
) {
    int value;

    if (bench_lookup_name_map(
            text,
            bench_keyspace_maps,
            BENCH_ARRAY_LEN(bench_keyspace_maps),
            &value
        ) != 0) {
        return -1;
    }

    *out = (bench_keyspace_mode)value;
    return 0;
}

int bench_parse_resize_mode(
    const char *text,
    bench_resize_mode *out
) {
    int value;

    if (bench_lookup_name_map(
            text,
            bench_resize_mode_maps,
            BENCH_ARRAY_LEN(bench_resize_mode_maps),
            &value
        ) != 0) {
        return -1;
    }

    *out = (bench_resize_mode)value;
    return 0;
}

int bench_parse_stats_mode(
    const char *text,
    bench_stats_mode *out
) {
    int value;

    if (bench_lookup_name_map(
            text,
            bench_stats_mode_maps,
            BENCH_ARRAY_LEN(bench_stats_mode_maps),
            &value
        ) != 0) {
        return -1;
    }

    *out = (bench_stats_mode)value;
    return 0;
}

int bench_parse_output_format(
    const char *text,
    bench_output_format *out
) {
    int value;

    if (bench_lookup_name_map(
            text,
            bench_output_format_maps,
            BENCH_ARRAY_LEN(bench_output_format_maps),
            &value
        ) != 0) {
        return -1;
    }

    *out = (bench_output_format)value;
    return 0;
}
