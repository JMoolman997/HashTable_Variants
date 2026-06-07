/**
 * @file    bench_names.c
 * @brief   Shared benchmark name helpers.
 *
 * Keeps CLI parsing and CSV output on the same set of names.
 *
 * @author  J.W Moolman
 * @date    2026-04-17
 */

#include <stddef.h>
#include <string.h>

#include "ht.h"
#include "bench_names.h"

#define BENCH_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    const char *name;
    int value;
} bench_name_map;

static const bench_name_map bench_kind_maps[] = {
    {"insert-build", BENCH_INSERT_BUILD},
    {"lookup-hit", BENCH_LOOKUP_HIT},
    {"lookup-miss", BENCH_LOOKUP_MISS},
    {"erase-existing", BENCH_ERASE_EXISTING},
    {"workload", BENCH_WORKLOAD},
    {"resize-build", BENCH_RESIZE_BUILD},
    {"resize-lookup-hit", BENCH_RESIZE_LOOKUP_HIT},
    {"resize-lookup-miss", BENCH_RESIZE_LOOKUP_MISS},
    {"resize-erase-existing", BENCH_RESIZE_ERASE_EXISTING},
    {"resize-workload", BENCH_RESIZE_WORKLOAD},
    {"concurrent-lookup", BENCH_CONCURRENT_LOOKUP},
    {"concurrent-workload", BENCH_CONCURRENT_WORKLOAD},
};

static const bench_name_map bench_impl_maps[] = {
    {"open_addressing", HT_IMPL_OPEN_ADDRESSING},
    {"robin_hood", HT_IMPL_ROBIN_HOOD},
    {"separate_chaining", HT_IMPL_SEPARATE_CHAINING},
    {"hopscotch", HT_IMPL_HOPSCOTCH},
    {"adv_open_addressing", HT_IMPL_ADV_OPEN_ADDRESSING},
    {"p_open_addressing", HT_IMPL_P_OPEN_ADDRESSING},
    {"backshift", HT_IMPL_BACKSHIFT},
    {"metadata", HT_IMPL_METADATA},
    {"simd", HT_IMPL_SIMD},
    {"bucket_mod_separate_chaining", HT_IMPL_BUCKET_MOD_SEPARATE_CHAINING},
    {"linked_mod_separate_chaining", HT_IMPL_LINKED_MOD_SEPARATE_CHAINING},
    {"segmented_mod_separate_chaining", HT_IMPL_SEGMENTED_MOD_SEPARATE_CHAINING},
    {"p_separate_chaining", HT_IMPL_P_SEPARATE_CHAINING},
    {"fingerprint", HT_IMPL_FINGERPRINT},
    {"linear_hashing", HT_IMPL_LINEAR_HASHING},
    {"adv_separate_chaining", HT_IMPL_ADV_SEPARATE_CHAINING},
    {"lf_hopscotch", HT_IMPL_LF_HOPSCOTCH},
};

static const bench_name_map bench_workload_maps[] = {
    {"read-only", WORKLOAD_READ_ONLY},
    {"read-heavy", WORKLOAD_READ_HEAVY},
    {"mixed", WORKLOAD_MIXED},
    {"balanced", WORKLOAD_BALANCED},
};

static const bench_name_map bench_keyspace_maps[] = {
    {"disjoint", BENCH_KEYSPACE_DISJOINT},
    {"shared", BENCH_KEYSPACE_SHARED_MIXED},
    {"shared-mixed", BENCH_KEYSPACE_SHARED_MIXED},
}; /* CLI accepts shared as the canonical mixed read/write keyspace. */

static const bench_name_map bench_keyspace_names[] = {
    {"disjoint", BENCH_KEYSPACE_DISJOINT},
    {"shared-read", BENCH_KEYSPACE_SHARED_READ},
    {"shared", BENCH_KEYSPACE_SHARED_MIXED},
}; /* CSV output keeps the internal shared-read mode visible. */

static const bench_name_map bench_resize_mode_maps[] = {
    {"disabled", BENCH_RESIZE_DISABLED},
    {"grow-only", BENCH_RESIZE_GROW_ONLY},
    {"grow-shrink", BENCH_RESIZE_GROW_SHRINK},
    {"impl-default", BENCH_RESIZE_IMPL_DEFAULT},
};

static const bench_name_map bench_stats_mode_maps[] = {
    {"off", BENCH_STATS_OFF},
    {"on", BENCH_STATS_ON},
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
        } /* First exact match wins; aliases map to the same enum value. */
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
        } /* The first value entry is the canonical output spelling. */
    }

    return "unknown";
}

const char *bench_kind_name(
    bench_kind kind
) {
    return bench_lookup_value_name(
        (int)kind,
        bench_kind_maps,
        BENCH_ARRAY_LEN(bench_kind_maps)
    );
}

const char *bench_impl_name(
    ht_impl impl
) {
    return ht_impl_name(impl);
}

int bench_impl_is_known(
    ht_impl impl
) {
    size_t i;

    for (i = 0; i < BENCH_ARRAY_LEN(bench_impl_maps); i++) {
        if (bench_impl_maps[i].value == (int)impl) {
            return 1;
        }
    }

    return 0;
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
        bench_keyspace_names,
        BENCH_ARRAY_LEN(bench_keyspace_names)
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

int bench_parse_kind(
    const char *text,
    bench_kind *out
) {
    int value;

    if (text == NULL || out == NULL) {
        return -1;
    }

    if (bench_lookup_name_map(
            text,
            bench_kind_maps,
            BENCH_ARRAY_LEN(bench_kind_maps),
            &value
        ) != 0) {
        return -1;
    }

    *out = (bench_kind)value;
    return 0;
}

int bench_parse_impl(
    const char *text,
    ht_impl *out
) {
    int value;

    if (text == NULL || out == NULL) {
        return -1;
    }

    if (bench_lookup_name_map(
            text,
            bench_impl_maps,
            BENCH_ARRAY_LEN(bench_impl_maps),
            &value
        ) != 0) {
        return -1;
    }

    *out = (ht_impl)value;
    return 0;
}

int bench_parse_workload(
    const char *text,
    workload_kind *out
) {
    int value;

    if (text == NULL || out == NULL) {
        return -1;
    }

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

    if (text == NULL || out == NULL) {
        return -1;
    }

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

    if (text == NULL || out == NULL) {
        return -1;
    }

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

    if (text == NULL || out == NULL) {
        return -1;
    }

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
