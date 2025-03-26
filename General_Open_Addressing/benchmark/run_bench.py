#!/usr/bin/env python3
"""
Main module to run benchmarks, process CSV data, compute statistics,
and generate plots.
"""

import os
import json
import subprocess
import numpy as np
import pandas as pd

from plot_utils import plot_cumulative_time, plot_histogram_and_box, plot_multiple_cumulative


def run_benchmarks(config_path, exec_path, results_dir):
    """
    Runs benchmarks based on a JSON config file and returns the list of generated CSV file paths.
    """
    with open(config_path, 'r') as f:
        config = json.load(f)

    benchmarks = config.get('benchmarks', [])
    if not benchmarks:
        print("No benchmarks specified in config.json")
        return []

    os.makedirs(results_dir, exist_ok=True)
    generated_csvs = []

    for bench in benchmarks:
        mode = bench.get('mode', 'insert')
        probe = bench.get('probe', 'linear')
        hashf = bench.get('hash', 'djb2')
        num_tests = str(bench.get('num_tests', 100000))
        load_factor = bench.get('load_factor', 0.75)
        out_csv = bench.get('output_file', '').strip()
        if not out_csv:
            out_csv = f"{mode}_{probe}_{hashf}_{load_factor}.csv"
        out_csv_path = os.path.join(results_dir, out_csv)

        cmd = [
            exec_path,
            '--mode', mode,
            '--probe', probe,
            '--hash', hashf,
            '--num-tests', num_tests,
            '--load-factor', str(load_factor),
            '--output-file', out_csv_path
        ]
        print(f"\nRunning benchmark: {cmd}")
        subprocess.run(cmd, check=False)

        if os.path.exists(out_csv_path):
            print(f"CSV output saved to: {out_csv_path}")
            generated_csvs.append(out_csv_path)
        else:
            print(f"Warning: CSV not created for: {cmd}")

    return generated_csvs


def load_csv_data(csv_path):
    """
    Loads CSV data using Pandas. Assumes the CSV has at least two columns where
    the first is the operation index and the second is the time.
    """
    if not os.path.exists(csv_path):
        print(f"Error: CSV file '{csv_path}' not found.")
        return None

    try:
        df = pd.read_csv(csv_path)
        if df.shape[1] < 2:
            raise ValueError("CSV does not have at least two columns")
        # Standardize column names
        df.columns = ['Index', 'Time']
        return df
    except Exception as e:
        print(f"Error reading '{csv_path}': {e}")
        return None


def print_statistics(df, label=""):
    """
    Computes and prints summary statistics for the 'Time' column in the DataFrame.
    """
    stats = df['Time'].describe(percentiles=[0.95])
    print(f"Statistics for {label}:\n{stats}\n")
    # Additional statistic: 99th percentile
    percentile_99 = np.percentile(df['Time'], 99)
    print(f"99th percentile for {label}: {percentile_99:.6f} sec\n")


def main():
    script_dir = os.path.dirname(__file__)
    config_path = os.path.join(script_dir, 'config.json')
    exec_path = os.path.join('..', 'bin', 'benchmark_hashtab')
    results_dir = os.path.join(script_dir, 'results')
    plots_dir = os.path.join(script_dir, 'plots')

    os.makedirs(plots_dir, exist_ok=True)

    # 1) Run benchmarks and collect CSV file paths
    generated_csvs = run_benchmarks(config_path, exec_path, results_dir)

    # 2) Process each CSV, compute statistics, and create plots
    run_data_list = []
    for csv_file in generated_csvs:
        df = load_csv_data(csv_file)
        if df is not None:
            filename_only = os.path.basename(csv_file)
            label = filename_only.replace('.csv', '')

            # Compute and print statistics
            print_statistics(df, label=label)

            # Plot cumulative time
            cumulative_out = os.path.join(plots_dir, f"{label}_cumulative.png")
            plot_cumulative_time(df, title=f"{label} - Cumulative", out_file=cumulative_out)

            # Plot time distribution: histogram and box plot
            distribution_out_prefix = os.path.join(plots_dir, f"{label}_distribution")
            plot_histogram_and_box(df, title_prefix=f"{label} - Time Distribution", out_file_prefix=distribution_out_prefix)

            run_data_list.append({'data': df, 'label': label})

    # 3) Plot combined cumulative times for all runs
    if run_data_list:
        combined_plot = os.path.join(plots_dir, "all_cumulative.png")
        plot_multiple_cumulative(run_data_list, title="All Benchmarks (Cumulative)", out_file=combined_plot)

    print("\nAll benchmarks processed and plots generated.")


if __name__ == '__main__':
    main()

