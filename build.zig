const std = @import("std");

const common_include_dirs = [_][]const u8{
    "include",
    "src/core",
    "src/util",
    "implementations/open_addressing/open_addressing",
    "implementations/open_addressing/advanced",
    "implementations/open_addressing/backshift",
    "implementations/open_addressing/metadata",
    "implementations/open_addressing/robin_hood",
    "implementations/open_addressing/simd",
    "implementations/separate_chaining/separate_chaining",
    "implementations/separate_chaining/advanced",
    "implementations/separate_chaining/fingerprint",
    "implementations/separate_chaining/mod_separate_chaining",
    "implementations/linear_hashing/linear_hashing",
    "implementations/hopscotch/hopscotch",
    "implementations/concurrent/p_open_addressing",
    "implementations/concurrent/p_separate_chaining",
    "implementations/concurrent/lf_hopscotch",
    "bench/include",
};

const fixed_c_flags = [_][]const u8{
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pthread",
    "-DHT_ENABLE_RESIZE_INSTRUMENTATION=0",
};

const resize_c_flags = [_][]const u8{
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pthread",
    "-DHT_ENABLE_RESIZE_INSTRUMENTATION=1",
};

const ht_core_sources = [_][]const u8{
    "src/core/ht.c",
    "src/core/ht_bind.c",
    "src/core/ht_registry.c",
    "src/util/hash_func.c",
    "src/util/backend_config.c",
    "src/util/resize_stats.c",
    "src/util/slab_pool.c",
    "implementations/open_addressing/open_addressing/open_addressing_impl.c",
    "implementations/open_addressing/advanced/adv_open_addressing_impl.c",
    "implementations/open_addressing/backshift/backshift_impl.c",
    "implementations/open_addressing/metadata/metadata_impl.c",
    "implementations/open_addressing/robin_hood/robin_hood_impl.c",
    "implementations/open_addressing/simd/simd_impl.c",
    "implementations/separate_chaining/separate_chaining/separate_chaining_impl.c",
    "implementations/separate_chaining/advanced/adv_separate_chaining.c",
    "implementations/separate_chaining/fingerprint/fingerprint_impl.c",
    "implementations/separate_chaining/mod_separate_chaining/mod_separate_chaining_impl.c",
    "implementations/separate_chaining/mod_separate_chaining/linkedlist_bucket.c",
    "implementations/separate_chaining/mod_separate_chaining/inline_array_bucket.c",
    "implementations/separate_chaining/mod_separate_chaining/segmented_bucket.c",
    "implementations/linear_hashing/linear_hashing/linear_hashing_impl.c",
    "implementations/hopscotch/hopscotch/hopscotch_impl.c",
    "implementations/concurrent/p_open_addressing/p_open_addressing.c",
    "implementations/concurrent/p_separate_chaining/p_separate_chaining.c",
    "implementations/concurrent/lf_hopscotch/lf_hopscotch_impl.c",
};

const bench_sources = [_][]const u8{
    "bench/src/main.c",
    "bench/src/bench_cli.c",
    "bench/src/bench_config.c",
    "bench/src/bench_plan.c",
    "bench/src/bench_dataset.c",
    "bench/src/bench_fixture.c",
    "bench/src/bench_metric.c",
    "bench/src/bench_output.c",
    "bench/src/bench_names.c",
    "bench/src/bench_runner_common.c",
    "bench/src/bench_backend_single.c",
    "bench/src/bench_backend_pthread.c",
    "bench/src/bench_trace.c",
    "bench/src/bench_time.c",
};

const test_sources = [_][]const u8{
    "tests/test_support.c",
    "tests/test_registry.c",
    "tests/ht_test.c",
    "tests/test_runner.c",
    "tests/ht_test_basic.c",
    "tests/ht_test_resize.c",
};

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const libht = addHashTableLibrary(b, target, optimize, false);
    const libht_resize = addHashTableLibrary(b, target, optimize, true);
    const htbench = addBenchmark(b, target, optimize, libht, false);
    const htbench_resize = addBenchmark(b, target, optimize, libht_resize, true);
    const ht_test = addTestBinary(b, target, optimize, libht);

    const libht_install = b.addInstallArtifact(libht, .{});
    const libht_resize_install = b.addInstallArtifact(libht_resize, .{});
    const htbench_install = b.addInstallArtifact(htbench, .{});
    const htbench_resize_install = b.addInstallArtifact(htbench_resize, .{});
    const ht_test_install = b.addInstallArtifact(ht_test, .{});

    const install_step = b.getInstallStep();
    install_step.dependOn(&libht_install.step);
    install_step.dependOn(&libht_resize_install.step);
    install_step.dependOn(&htbench_install.step);
    install_step.dependOn(&htbench_resize_install.step);
    install_step.dependOn(&ht_test_install.step);

    const all_step = b.step("all", "Build libraries, benchmark binaries, and the test binary");
    all_step.dependOn(install_step);

    addInstallStep(b, "libht", "Build the fixed-capacity static library", libht_install);
    addInstallStep(b, "libht_resize", "Build the resize-instrumented static library", libht_resize_install);
    addInstallStep(b, "htbench", "Build the benchmark binary", htbench_install);
    addInstallStep(b, "htbench_resize", "Build the resize-instrumented benchmark binary", htbench_resize_install);
    addInstallStep(b, "ht_test", "Build the C test binary", ht_test_install);

    const run_tests = b.addRunArtifact(ht_test);
    const test_step = b.step("test", "Build and run the C hash table test suite");
    test_step.dependOn(&run_tests.step);

    const check_step = b.step("check", "Build everything and run the C test suite");
    check_step.dependOn(install_step);
    check_step.dependOn(&run_tests.step);
}

fn addHashTableLibrary(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    resize: bool,
) *std.Build.Step.Compile {
    const module = addCModule(b, target, optimize, &ht_core_sources, resize);
    return b.addLibrary(.{
        .name = if (resize) "ht_resize" else "ht",
        .linkage = .static,
        .root_module = module,
    });
}

fn addBenchmark(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    libht: *std.Build.Step.Compile,
    resize: bool,
) *std.Build.Step.Compile {
    const module = addCModule(b, target, optimize, &bench_sources, resize);
    module.linkLibrary(libht);
    return b.addExecutable(.{
        .name = if (resize) "htbench_resize" else "htbench",
        .root_module = module,
    });
}

fn addTestBinary(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    libht: *std.Build.Step.Compile,
) *std.Build.Step.Compile {
    const module = addCModule(b, target, optimize, &test_sources, false);
    module.linkLibrary(libht);
    return b.addExecutable(.{
        .name = "ht_test",
        .root_module = module,
    });
}

fn addCModule(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    sources: []const []const u8,
    resize: bool,
) *std.Build.Module {
    const module = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });
    addCommonIncludes(b, module);
    module.addCSourceFiles(.{
        .files = sources,
        .flags = if (resize) &resize_c_flags else &fixed_c_flags,
        .language = .c,
    });
    module.linkSystemLibrary("c", .{});
    module.linkSystemLibrary("pthread", .{});
    return module;
}

fn addCommonIncludes(
    b: *std.Build,
    module: *std.Build.Module,
) void {
    for (common_include_dirs) |dir| {
        module.addIncludePath(b.path(dir));
    }
}

fn addInstallStep(
    b: *std.Build,
    name: []const u8,
    description: []const u8,
    install_artifact: *std.Build.Step.InstallArtifact,
) void {
    const step = b.step(name, description);
    step.dependOn(&install_artifact.step);
}
