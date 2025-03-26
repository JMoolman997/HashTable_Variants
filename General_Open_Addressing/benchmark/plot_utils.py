#!/usr/bin/env python3
"""
This module contains functions for plotting performance data.
"""

import os
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns


def plot_cumulative_time(df, title='Cumulative Time Plot', out_file='plot.png'):
    """
    Plots cumulative time from a DataFrame containing 'Index' and 'Time' columns.

    Parameters
    ----------
    df : pandas.DataFrame
        Must contain 'Index' and 'Time' columns.
    title : str, optional
        Plot title.
    out_file : str, optional
        File path to save the plot.
    """
    # Ensure data is sorted by index
    df = df.sort_values('Index')
    df['CumulativeTime'] = df['Time'].cumsum()

    plt.figure(figsize=(10, 6))
    plt.plot(df['Index'], df['CumulativeTime'], linewidth=2)
    plt.title(title, fontsize=14)
    plt.xlabel("Operation Index", fontsize=12)
    plt.ylabel("Cumulative Time (sec)", fontsize=12)
    plt.grid(True, linestyle='--', alpha=0.5)

    # Ensure the directory exists before saving
    os.makedirs(os.path.dirname(out_file), exist_ok=True)
    plt.savefig(out_file, dpi=300)
    plt.close()
    print(f"Cumulative plot saved to {out_file}")


def plot_histogram_and_box(df, title_prefix='Time Distribution', out_file_prefix='distribution'):
    """
    Creates a figure with both histogram and box plot for the 'Time' column in the DataFrame.

    Parameters
    ----------
    df : pandas.DataFrame
        DataFrame containing at least the 'Time' column.
    title_prefix : str, optional
        Title prefix for both plots.
    out_file_prefix : str, optional
        File prefix for saving the plot.
    """
    fig, axs = plt.subplots(1, 2, figsize=(14, 6))

    # Histogram with KDE
    sns.histplot(df['Time'], bins=50, kde=True, ax=axs[0], color='skyblue')
    axs[0].set_title(f"{title_prefix} (Histogram)")
    axs[0].set_xlabel("Time (sec)")
    axs[0].set_ylabel("Frequency")
    axs[0].grid(True, linestyle='--', alpha=0.5)

    # Box plot
    sns.boxplot(x=df['Time'], ax=axs[1], color='lightgreen')
    axs[1].set_title(f"{title_prefix} (Box Plot)")
    axs[1].set_xlabel("Time (sec)")
    axs[1].grid(True, linestyle='--', alpha=0.5)

    out_file = os.path.join(os.path.dirname(out_file_prefix), f"{os.path.basename(out_file_prefix)}.png")
    os.makedirs(os.path.dirname(out_file), exist_ok=True)
    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()
    print(f"Distribution plots saved to {out_file}")


def plot_multiple_cumulative(run_data_list, title='Multiple Cumulative Time Comparison', out_file='combined_cumulative.png'):
    """
    Plots cumulative times for multiple runs on the same figure.

    Parameters
    ----------
    run_data_list : list of dict
        Each dictionary should contain:
            - 'data': a pandas.DataFrame with 'Index' and 'Time' columns.
            - 'label': a label for the run.
    title : str, optional
        Title for the combined plot.
    out_file : str, optional
        File path to save the plot.
    """
    plt.figure(figsize=(10, 6))
    for run_data in run_data_list:
        df = run_data['data'].sort_values('Index')
        df['CumulativeTime'] = df['Time'].cumsum()
        label = run_data.get('label', 'Run')
        plt.plot(df['Index'], df['CumulativeTime'], label=label, linewidth=2)

    plt.title(title, fontsize=14)
    plt.xlabel("Operation Index", fontsize=12)
    plt.ylabel("Cumulative Time (sec)", fontsize=12)
    plt.grid(True, linestyle='--', alpha=0.5)
    plt.legend()
    os.makedirs(os.path.dirname(out_file), exist_ok=True)
    plt.savefig(out_file, dpi=300)
    plt.close()
    print(f"Combined cumulative plot saved to {out_file}")

