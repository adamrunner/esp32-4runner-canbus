#!/usr/bin/env python3
"""
Simple CAN ID Analyzer

Quick analysis of CAN IDs to find potentially interesting broadcasts.
"""

import sys
import pandas as pd
from pathlib import Path


def main():
    if len(sys.argv) < 2:
        print("Usage: python simple_analyzer.py <log_file.csv>")
        return

    log_file = Path(sys.argv[1])
    print(f"Loading {log_file}...")

    df = pd.read_csv(log_file, dtype={
        'timestamp_us': 'int64',
        'can_id': 'str',
        'dlc': 'int',
        'b0': 'str', 'b1': 'str', 'b2': 'str', 'b3': 'str',
        'b4': 'str', 'b5': 'str', 'b6': 'str', 'b7': 'str'
    })

    print(f"Loaded {len(df)} messages\n")

    # Get message frequency
    msg_counts = df['can_id'].value_counts()
    duration_sec = (df['timestamp_us'].max() - df['timestamp_us'].min()) / 1_000_000

    print(f"Total unique CAN IDs: {len(msg_counts)}")
    print(f"Duration: {duration_sec:.1f} seconds\n")

    print("=== High Frequency Messages (> 10 Hz) ===\n")
    print(f"{'CAN ID':<8} {'Count':>10} {'Hz':>8} {'Sample Data':>30}")
    print("-" * 58)

    for can_id, count in msg_counts.head(20).items():
        hz = count / duration_sec
        if hz < 10:
            continue

        msgs = df[df['can_id'] == can_id].head(1)
        data = ' '.join([msgs.iloc[0][f'b{i}'] for i in range(8)])
        print(f"{can_id:<8} {count:>10,} {hz:>8.1f} {data}")

    print("\n=== Messages with High Variation (unique data > 100) ===\n")
    print(f"{'CAN ID':<8} {'Count':>10} {'Unique':>8} {'Change Rate':>12}")

    for can_id in msg_counts.index:
        msgs = df[df['can_id'] == can_id]
        byte_cols = [f'b{i}' for i in range(8)]
        unique_combos = msgs[byte_cols].drop_duplicates()

        if len(unique_combos) > 100:
            change_rate = len(unique_combos) / len(msgs)
            print(f"{can_id:<8} {len(msgs):>10,} {len(unique_combos):>8} {change_rate*100:>11.1f}%")


if __name__ == '__main__':
    main()
