#!/usr/bin/env python3
"""
Wheel Speed Analysis

Validates wheel speed broadcast messages (0x0AA) and compares with diagnostic data.
"""

import sys
import pandas as pd
import numpy as np
from pathlib import Path


class WheelSpeedAnalyzer:
    """Analyzes wheel speed data from CAN logs."""

    def __init__(self, log_file):
        """Initialize analyzer with a log file path."""
        self.log_file = Path(log_file)
        self.df = None
        self.wheel_speed_msgs = None

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

    def parse_wheel_speed_broadcast(self):
        """
        Parse wheel speed broadcast messages (CAN ID 0x0AA).

        Format: 4x 16-bit big-endian values (one per wheel)
        Raw value at 0 kph is approximately 6750 (0x1A5E)
        km/h = (raw_value - 6750) / 100.0
        Wheel order: FR, FL, RR, RL
        """
        if self.df is None:
            self.load_log()

        msgs = self.df[self.df['can_id'] == '0AA'].copy()
        if len(msgs) == 0:
            print("No wheel speed broadcast messages found (CAN ID 0x0AA)")
            return None

        print(f"Found {len(msgs)} wheel speed broadcast messages")

        # Parse 16-bit big-endian wheel speed values
        msgs['raw_fr'] = msgs.apply(lambda row: int(row['b0'], 16) * 256 + int(row['b1'], 16), axis=1)
        msgs['raw_fl'] = msgs.apply(lambda row: int(row['b2'], 16) * 256 + int(row['b3'], 16), axis=1)
        msgs['raw_rr'] = msgs.apply(lambda row: int(row['b4'], 16) * 256 + int(row['b5'], 16), axis=1)
        msgs['raw_rl'] = msgs.apply(lambda row: int(row['b6'], 16) * 256 + int(row['b7'], 16), axis=1)

        # Convert to km/h (offset 6750, scale 0.01)
        offset = 6750
        msgs['wheel_fr_kph'] = (msgs['raw_fr'] - offset) / 100.0
        msgs['wheel_fl_kph'] = (msgs['raw_fl'] - offset) / 100.0
        msgs['wheel_rr_kph'] = (msgs['raw_rr'] - offset) / 100.0
        msgs['wheel_rl_kph'] = (msgs['raw_rl'] - offset) / 100.0

        # Calculate average wheel speed
        msgs['avg_wheel_kph'] = msgs[['wheel_fr_kph', 'wheel_fl_kph',
                                       'wheel_rr_kph', 'wheel_rl_kph']].mean(axis=1)

        # Calculate wheel speed differences (for turning detection)
        msgs['lr_diff'] = msgs['wheel_fl_kph'] - msgs['wheel_fr_kph']
        msgs['lr_diff_rear'] = msgs['wheel_rl_kph'] - msgs['wheel_rr_kph']

        self.wheel_speed_msgs = msgs
        return msgs

    def analyze_wheel_speed_stats(self):
        """Analyze wheel speed statistics."""
        if self.wheel_speed_msgs is None:
            self.parse_wheel_speed_broadcast()

        print("\n=== Wheel Speed Statistics ===")
        print(f"{'Metric':<20} {'Min':>10} {'Max':>10} {'Mean':>10} {'Std':>10}")
        print("-" * 62)

        for wheel in ['fr', 'fl', 'rr', 'rl']:
            col = f'wheel_{wheel}_kph'
            print(f"{wheel.upper():<20} "
                  f"{self.wheel_speed_msgs[col].min():>10.2f} "
                  f"{self.wheel_speed_msgs[col].max():>10.2f} "
                  f"{self.wheel_speed_msgs[col].mean():>10.2f} "
                  f"{self.wheel_speed_msgs[col].std():>10.2f}")

        print(f"\n{'Average':<20} "
              f"{self.wheel_speed_msgs['avg_wheel_kph'].min():>10.2f} "
              f"{self.wheel_speed_msgs['avg_wheel_kph'].max():>10.2f} "
              f"{self.wheel_speed_msgs['avg_wheel_kph'].mean():>10.2f} "
              f"{self.wheel_speed_msgs['avg_wheel_kph'].std():>10.2f}")

    def identify_driving_segments(self, min_speed_kph=5):
        """Identify driving segments vs parked/idle segments."""
        if self.wheel_speed_msgs is None:
            self.parse_wheel_speed_broadcast()

        self.wheel_speed_msgs['is_moving'] = self.wheel_speed_msgs['avg_wheel_kph'] > min_speed_kph

        # Find segments
        segments = []
        current_segment = None
        segment_count = 0

        for idx, row in self.wheel_speed_msgs.iterrows():
            is_moving = row['is_moving']

            if current_segment is None:
                current_segment = {
                    'type': 'moving' if is_moving else 'stopped',
                    'start_idx': idx,
                    'count': 0
                }
            elif current_segment['type'] == ('moving' if is_moving else 'stopped'):
                current_segment['count'] += 1
            else:
                # Segment ended
                current_segment['end_idx'] = idx
                segments.append(current_segment)
                segment_count += 1

                current_segment = {
                    'type': 'moving' if is_moving else 'stopped',
                    'start_idx': idx,
                    'count': 0
                }

            current_segment['count'] += 1

        # Don't forget the last segment
        if current_segment is not None:
            current_segment['end_idx'] = self.wheel_speed_msgs.index[-1]
            segments.append(current_segment)

        print(f"\n=== Driving Segments (> {min_speed_kph} kph) ===")
        print(f"Total segments: {len(segments)}")

        moving_segments = [s for s in segments if s['type'] == 'moving']
        stopped_segments = [s for s in segments if s['type'] == 'stopped']

        print(f"Moving segments: {len(moving_segments)}")
        print(f"Stopped segments: {len(stopped_segments)}")

        print(f"\nMoving segments (duration in seconds):")
        for i, seg in enumerate(moving_segments[:5], 1):
            start_time = self.wheel_speed_msgs.loc[seg['start_idx'], 'timestamp_us']
            end_time = self.wheel_speed_msgs.loc[seg['end_idx'], 'timestamp_us']
            duration = (end_time - start_time) / 1_000_000
            avg_speed = self.wheel_speed_msgs.loc[seg['start_idx']:seg['end_idx'], 'avg_wheel_kph'].mean()
            max_speed = self.wheel_speed_msgs.loc[seg['start_idx']:seg['end_idx'], 'avg_wheel_kph'].max()
            print(f"  {i}. {duration:.1f}s  (avg: {avg_speed:.1f} kph, max: {max_speed:.1f} kph)")

        print(f"\nStopped segments (duration in seconds):")
        for i, seg in enumerate(stopped_segments[:5], 1):
            start_time = self.wheel_speed_msgs.loc[seg['start_idx'], 'timestamp_us']
            end_time = self.wheel_speed_msgs.loc[seg['end_idx'], 'timestamp_us']
            duration = (end_time - start_time) / 1_000_000
            avg_speed = self.wheel_speed_msgs.loc[seg['start_idx']:seg['end_idx'], 'avg_wheel_kph'].mean()
            print(f"  {i}. {duration:.1f}s  (avg speed: {avg_speed:.1f} kph)")

        return segments

    def find_turning_events(self, min_diff_kph=2):
        """Find turning events based on left-right wheel speed differences."""
        if self.wheel_speed_msgs is None:
            self.parse_wheel_speed_broadcast()

        self.wheel_speed_msgs['turning'] = (
            (np.abs(self.wheel_speed_msgs['lr_diff']) > min_diff_kph) |
            (np.abs(self.wheel_speed_msgs['lr_diff_rear']) > min_diff_kph)
        )

        turning_msgs = self.wheel_speed_msgs[self.wheel_speed_msgs['turning']]

        print(f"\n=== Turning Events (diff > {min_diff_kph} kph) ===")
        print(f"Found {len(turning_msgs)} turning messages")

        if len(turning_msgs) > 0:
            print(f"Left-Right diff stats:")
            print(f"  Front: mean={turning_msgs['lr_diff'].mean():.2f}, "
                  f"max={turning_msgs['lr_diff'].max():.2f}")
            print(f"  Rear: mean={turning_msgs['lr_diff_rear'].mean():.2f}, "
                  f"max={turning_msgs['lr_diff_rear'].max():.2f}")

        return turning_msgs

    def export_wheel_speed_data(self, output_file):
        """Export parsed wheel speed data to CSV."""
        if self.wheel_speed_msgs is None:
            self.parse_wheel_speed_broadcast()

        cols_to_export = ['timestamp_us', 'raw_fr', 'raw_fl', 'raw_rr', 'raw_rl',
                         'wheel_fr_kph', 'wheel_fl_kph', 'wheel_rr_kph', 'wheel_rl_kph',
                         'avg_wheel_kph', 'lr_diff', 'lr_diff_rear', 'is_moving']

        self.wheel_speed_msgs[cols_to_export].to_csv(output_file, index=False)
        print(f"\nExported wheel speed data to {output_file}")

    def analyze_broadcast_validation(self):
        """Validate broadcast wheel speed decoding."""
        if self.wheel_speed_msgs is None:
            self.parse_wheel_speed_broadcast()

        print("\n=== Broadcast Validation ===")
        print("Checking wheel speed broadcast decoding...")

        # Check for negative values (shouldn't happen with offset)
        neg_count = (self.wheel_speed_msgs['avg_wheel_kph'] < 0).sum()
        if neg_count > 0:
            print(f"WARNING: Found {neg_count} messages with negative wheel speeds")
        else:
            print("✓ No negative wheel speeds")

        # Check for extremely high values (potential decoding errors)
        high_count = (self.wheel_speed_msgs['avg_wheel_kph'] > 200).sum()
        if high_count > 0:
            print(f"WARNING: Found {high_count} messages with wheel speeds > 200 kph")
        else:
            print("✓ No unrealistic wheel speeds")

        # Check wheel speed consistency during stops
        stopped_msgs = self.wheel_speed_msgs[self.wheel_speed_msgs['avg_wheel_kph'] < 1]
        if len(stopped_msgs) > 0:
            stopped_std = stopped_msgs[['wheel_fr_kph', 'wheel_fl_kph',
                                       'wheel_rr_kph', 'wheel_rl_kph']].std().mean()
            print(f"✓ Wheel speed std during stops: {stopped_std:.3f} kph")

        # Check update frequency
        if len(self.wheel_speed_msgs) > 1:
            self.wheel_speed_msgs['timestamp_ms'] = self.wheel_speed_msgs['timestamp_us'] // 1000
            time_diffs = self.wheel_speed_msgs['timestamp_ms'].diff()
            print(f"Update interval: mean={time_diffs.mean():.1f}ms, "
                  f"median={time_diffs.median():.1f}ms, "
                  f"min={time_diffs.min():.0f}ms, "
                  f"max={time_diffs.max():.0f}ms")


def main():
    """Main entry point for command-line usage."""
    if len(sys.argv) < 2:
        print("Usage: python wheel_speed_analyzer.py <log_file.csv> [options]")
        print("\nOptions:")
        print("  --stats           Show wheel speed statistics")
        print("  --segments        Identify driving segments")
        print("  --turning         Find turning events")
        print("  --validate        Validate broadcast decoding")
        print("  --export FILE     Export parsed data to FILE")
        return

    log_file = sys.argv[1]
    analyzer = WheelSpeedAnalyzer(log_file)

    # Parse options
    i = 2
    while i < len(sys.argv):
        arg = sys.argv[i]

        if arg == '--stats':
            analyzer.load_log()
            analyzer.parse_wheel_speed_broadcast()
            analyzer.analyze_wheel_speed_stats()
            i += 1

        elif arg == '--segments':
            analyzer.load_log()
            analyzer.parse_wheel_speed_broadcast()
            analyzer.identify_driving_segments()
            i += 1

        elif arg == '--turning':
            analyzer.load_log()
            analyzer.parse_wheel_speed_broadcast()
            analyzer.find_turning_events()
            i += 1

        elif arg == '--validate':
            analyzer.load_log()
            analyzer.parse_wheel_speed_broadcast()
            analyzer.analyze_broadcast_validation()
            i += 1

        elif arg == '--export':
            output_file = sys.argv[i+1]
            analyzer.load_log()
            analyzer.parse_wheel_speed_broadcast()
            analyzer.export_wheel_speed_data(output_file)
            i += 2

        else:
            print(f"Unknown option: {arg}")
            sys.exit(1)

    # If no specific analysis requested, run all analyses
    if len(sys.argv) == 2:
        analyzer.load_log()
        analyzer.parse_wheel_speed_broadcast()
        analyzer.analyze_wheel_speed_stats()
        analyzer.identify_driving_segments()
        analyzer.find_turning_events()
        analyzer.analyze_broadcast_validation()


if __name__ == '__main__':
    main()
