/**
 * @file    main.c
 * @brief   Entry point for the htbench benchmark harness.
 *
 * Delegates process startup directly to the benchmark CLI module.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#include "bench_cli.h"

/**
 * @brief Run the benchmark CLI entry point.
 *
 * @param argc Argument count supplied by the process entry point.
 * @param argv Argument vector supplied by the process entry point.
 *
 * @return The exit code returned by bench_cli_run().
 */
int main(
    int argc,
    char **argv
) {
    return bench_cli_run(argc, argv); /* Delegate startup to the CLI module. */
}
