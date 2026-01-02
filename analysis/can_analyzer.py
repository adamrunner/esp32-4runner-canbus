#!/usr/bin/env python3
"""
CAN Log Analysis Tools

Tools for analyzing Toyota 4Runner CAN bus logs to identify broadcast messages
and correlate them with diagnostic data.
"""

import pandas as pd
import numpy as np
from collections import defaultdict, Counter
import json
import sys
from pathlib import Path


class CANLogAnalyzer:
    """Analyzes CAN bus logs to identify patterns and broadcast messages."""

    def __init__(self, log_file):
        """Initialize analyzer with a log file path."""
        self.log_file = Path(log_file)
        self.df = None
        self.message_stats = None
        self.unique_ids = None

    def load_log(self):
        """Load CSV log file into pandas DataFrame."""
        print(f"Loading {self.log_file}...")
        self.df = pd.read_csv(self.log_file, dtype={
            'timestamp_us': 'int64',
            'can_id': 'str',
            'dlc': 'int',
            'b0': 'str', 'b1': 'str', 'b2': 'str', 'b3': 'str',
            'b4': 'str', 'b5': 'str', 'b6': 'str', 'b7': 'str'
        })
        print(f"Loaded {len(self.df)} messages")
        return self.df

    def analyze_message_frequency(self, top_n=50):
        """Analyze frequency of CAN IDs."""
        if self.df is None:
            self.load_log()

        print("\n=== Message Frequency Analysis ===")
        msg_counts = self.df['can_id'].value_counts()
        total_msgs = len(self.df)

        print(f"\nTotal unique CAN IDs: {len(msg_counts)}")
        print(f"Total messages: {total_msgs}")

        print(f"\nTop {top_n} most frequent CAN IDs:")
        print(f"{'CAN ID':<10} {'Count':>12} {'% of Total':>12} {'Msgs/Sec':>12}")
        print("-" * 50)

        # Calculate duration in seconds
        duration_sec = (self.df['timestamp_us'].max() - self.df['timestamp_us'].min()) / 1_000_000

        for can_id, count in msg_counts.head(top_n).items():
            percentage = (count / total_msgs) * 100
            msgs_per_sec = count / duration_sec
            print(f"{can_id:<10} {count:>12,} {percentage:>11.2f}% {msgs_per_sec:>12.2f}")

        self.message_stats = msg_counts
        self.unique_ids = set(msg_counts.index)
        return msg_counts

    def analyze_data_patterns(self, can_id, sample_size=100):
        """Analyze data patterns for a specific CAN ID."""
        if self.df is None:
            self.load_log()

        msgs = self.df[self.df['can_id'] == can_id].head(sample_size)

        if len(msgs) == 0:
            print(f"No messages found for CAN ID {can_id}")
            return

        print(f"\n=== Data Pattern Analysis for CAN ID {can_id} ===")
        print(f"Showing first {len(msgs)} messages")

        # Convert hex bytes to integers for analysis
        byte_cols = [f'b{i}' for i in range(8)]
        for col in byte_cols:
            msgs[f'{col}_int'] = msgs[col].apply(lambda x: int(x, 16) if pd.notna(x) and x != '' else 0)

        print(f"\nTimestamp, Hex Data (bytes 0-7)")
        print("-" * 80)
        for _, row in msgs.iterrows():
            hex_data = ' '.join([row[col] for col in byte_cols])
            print(f"{row['timestamp_us']:12} {hex_data}")

        # Analyze byte value ranges
        print(f"\nByte Value Statistics (integers):")
        print(f"{'Byte':<6} {'Min':>6} {'Max':>6} {'Mean':>8} {'Std':>8} {'Unique':>8}")
        print("-" * 50)
        for i in range(8):
            col = f'b{i}_int'
            print(f"{col:<6} {msgs[col].min():>6} {msgs[col].max():>6} "
                  f"{msgs[col].mean():>8.2f} {msgs[col].std():>8.2f} {msgs[col].nunique():>8}")

    def find_high_frequency_messages(self, min_msgs_per_sec=10):
        """Find messages with high frequency (potential broadcasts)."""
        if self.message_stats is None:
            self.analyze_message_frequency()

        duration_sec = (self.df['timestamp_us'].max() - self.df['timestamp_us'].min()) / 1_000_000

        high_freq = {}
        for can_id, count in self.message_stats.items():
            msgs_per_sec = count / duration_sec
            if msgs_per_sec >= min_msgs_per_sec:
                high_freq[can_id] = {
                    'count': count,
                    'msgs_per_sec': msgs_per_sec,
                    'percentage': (count / len(self.df)) * 100
                }

        print(f"\n=== High Frequency Messages (>= {min_msgs_per_sec} msgs/sec) ===")
        print(f"{'CAN ID':<10} {'Msgs/Sec':>12} {'% of Total':>12} {'Count':>12}")
        print("-" * 50)
        for can_id in sorted(high_freq.keys(), key=lambda x: high_freq[x]['msgs_per_sec'], reverse=True):
            stats = high_freq[can_id]
            print(f"{can_id:<10} {stats['msgs_per_sec']:>12.2f} {stats['percentage']:>11.2f}% "
                  f"{stats['count']:>12,}")

        return high_freq

    def analyze_temporal_patterns(self, can_id, window_ms=1000):
        """Analyze temporal patterns for a specific CAN ID."""
        if self.df is None:
            self.load_log()

        msgs = self.df[self.df['can_id'] == can_id].copy()
        if len(msgs) < 2:
            print(f"Not enough messages for temporal analysis of {can_id}")
            return

        msgs['timestamp_ms'] = msgs['timestamp_us'] // 1000
        msgs['time_diff_ms'] = msgs['timestamp_ms'].diff()

        print(f"\n=== Temporal Pattern Analysis for CAN ID {can_id} ===")
        print(f"Total messages: {len(msgs)}")
        print(f"Duration: {(msgs['timestamp_ms'].max() - msgs['timestamp_ms'].min()) / 1000:.2f} seconds")
        print(f"\nTime between messages (ms):")
        print(f"{'Min':>10} {'Max':>10} {'Mean':>10} {'Median':>10} {'Std':>10}")
        print("-" * 52)
        print(f"{msgs['time_diff_ms'].min():>10.2f} {msgs['time_diff_ms'].max():>10.2f} "
              f"{msgs['time_diff_ms'].mean():>10.2f} {msgs['time_diff_ms'].median():>10.2f} "
              f"{msgs['time_diff_ms'].std():>10.2f}")

        # Calculate messages per window
        msgs['window'] = (msgs['timestamp_ms'] // window_ms) * window_ms
        msgs_per_window = msgs.groupby('window').size()

        print(f"\nMessages per {window_ms}ms window:")
        print(f"{'Min':>6} {'Max':>6} {'Mean':>8} {'Median':>8}")
        print("-" * 32)
        print(f"{msgs_per_window.min():>6} {msgs_per_window.max():>6} "
              f"{msgs_per_window.mean():>8.2f} {msgs_per_window.median():>8.2f}")

    def find_constant_messages(self, min_count=10):
        """Find messages with constant data (potential static status)."""
        if self.df is None:
            self.load_log()

        print("\n=== Constant Data Messages ===")
        constant_msgs = {}

        for can_id in self.unique_ids:
            msgs = self.df[self.df['can_id'] == can_id]
            if len(msgs) < min_count:
                continue

            # Check if all messages have identical data
            byte_cols = [f'b{i}' for i in range(8)]
            unique_combinations = msgs[byte_cols].drop_duplicates()

            if len(unique_combinations) == 1:
                constant_msgs[can_id] = {
                    'count': len(msgs),
                    'data': unique_combinations.iloc[0].tolist()
                }

        print(f"Found {len(constant_msgs)} CAN IDs with constant data:")
        print(f"{'CAN ID':<10} {'Count':>12} {'Data (b0-b7)'}")
        print("-" * 50)
        for can_id in sorted(constant_msgs.keys()):
            msg = constant_msgs[can_id]
            data_str = ' '.join(msg['data'])
            print(f"{can_id:<10} {msg['count']:>12,} {data_str}")

        return constant_msgs

    def find_changing_messages(self, min_changes=5):
        """Find messages with frequently changing data."""
        if self.df is None:
            self.load_log()

        print("\n=== Frequently Changing Messages ===")
        changing_msgs = {}

        for can_id in self.unique_ids:
            msgs = self.df[self.df['can_id'] == can_id]
            if len(msgs) < 10:
                continue

            byte_cols = [f'b{i}' for i in range(8)]
            unique_combinations = msgs[byte_cols].drop_duplicates()

            if len(unique_combinations) >= min_changes:
                changing_msgs[can_id] = {
                    'count': len(msgs),
                    'unique_data': len(unique_combinations),
                    'change_rate': len(unique_combinations) / len(msgs)
                }

        print(f"Found {len(changing_msgs)} CAN IDs with >= {min_changes} unique data combinations:")
        print(f"{'CAN ID':<10} {'Count':>10} {'Unique':>10} {'Change %':>10}")
        print("-" * 42)
        for can_id in sorted(changing_msgs.keys(), key=lambda x: changing_msgs[x]['unique_data'], reverse=True):
            msg = changing_msgs[can_id]
            print(f"{can_id:<10} {msg['count']:>10,} {msg['unique_data']:>10} "
                  f"{msg['change_rate']*100:>9.2f}%")

        return changing_msgs

    def export_message_summary(self, output_file):
        """Export summary of all CAN IDs to a file."""
        if self.df is None:
            self.load_log()

        duration_sec = (self.df['timestamp_us'].max() - self.df['timestamp_us'].min()) / 1_000_000

        summary = []
        for can_id in sorted(self.unique_ids):
            msgs = self.df[self.df['can_id'] == can_id]
            byte_cols = [f'b{i}' for i in range(8)]
            unique_combinations = msgs[byte_cols].drop_duplicates()

            # Get sample data
            sample = msgs.iloc[0]
            data_bytes = [sample[col] for col in byte_cols]

            summary.append({
                'can_id': can_id,
                'count': len(msgs),
                'msgs_per_sec': len(msgs) / duration_sec,
                'percentage': (len(msgs) / len(self.df)) * 100,
                'unique_data': len(unique_combinations),
                'dlc': sample['dlc'],
                'sample_data': ' '.join(data_bytes)
            })

        summary_df = pd.DataFrame(summary)
        summary_df = summary_df.sort_values('count', ascending=False)
        summary_df.to_csv(output_file, index=False)
        print(f"\nExported message summary to {output_file}")

        return summary_df


def main():
    """Main entry point for command-line usage."""
    if len(sys.argv) < 2:
        print("Usage: python can_analyzer.py <log_file.csv> [options]")
        print("\nOptions:")
        print("  --freq N         Show top N most frequent messages (default: 50)")
        print("  --analyze ID     Analyze data patterns for specific CAN ID")
        print("  --high-freq N    Find messages with >= N msgs/sec (default: 10)")
        print("  --temporal ID    Analyze temporal patterns for specific CAN ID")
        print("  --constant       Find messages with constant data")
        print("  --changing N     Find messages with >= N unique data combos (default: 5)")
        print("  --summary FILE   Export message summary to FILE")
        sys.exit(1)

    log_file = sys.argv[1]
    analyzer = CANLogAnalyzer(log_file)
    analyzer.load_log()

    # Parse options
    i = 2
    while i < len(sys.argv):
        arg = sys.argv[i]

        if arg == '--freq':
            top_n = int(sys.argv[i+1])
            analyzer.analyze_message_frequency(top_n)
            i += 2

        elif arg == '--analyze':
            can_id = sys.argv[i+1]
            analyzer.analyze_data_patterns(can_id)
            i += 2

        elif arg == '--high-freq':
            min_freq = int(sys.argv[i+1])
            analyzer.find_high_frequency_messages(min_freq)
            i += 2

        elif arg == '--temporal':
            can_id = sys.argv[i+1]
            analyzer.analyze_temporal_patterns(can_id)
            i += 2

        elif arg == '--constant':
            analyzer.find_constant_messages()
            i += 1

        elif arg == '--changing':
            min_changes = int(sys.argv[i+1])
            analyzer.find_changing_messages(min_changes)
            i += 2

        elif arg == '--summary':
            output_file = sys.argv[i+1]
            analyzer.export_message_summary(output_file)
            i += 2

        else:
            print(f"Unknown option: {arg}")
            sys.exit(1)

    # If no specific analysis requested, run default analysis
    if len(sys.argv) == 2:
        analyzer.analyze_message_frequency(30)
        analyzer.find_high_frequency_messages(10)
        analyzer.find_constant_messages()
        analyzer.find_changing_messages(10)


if __name__ == '__main__':
    main()
