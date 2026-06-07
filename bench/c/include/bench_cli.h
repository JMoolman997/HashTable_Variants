/**
 * @file    bench_cli.h
 * @brief   Command-line entry point for the benchmark harness.
 *
 * Declares the CLI driver responsible for parsing arguments and dispatching
 * the requested benchmark command.
 *
 * @author  J.W Moolman
 * @date    2026-03-30
 */

#ifndef BENCH_CLI_H
#define BENCH_CLI_H

/* --- function prototypes -------------------------------------------------- */

/**
 * @brief Parse CLI arguments and run the requested benchmark command.
 *
 * @param argc Argument count from `main`.
 * @param argv Argument vector from `main`.
 *
 * @return `0` on success, or a non-zero exit code if parsing or benchmark
 *         execution fails.
 */
int bench_cli_run(
    int argc,
    char **argv
);

#endif /* BENCH_CLI_H */
