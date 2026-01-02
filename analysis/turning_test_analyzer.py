#!/usr/bin/env python3
"""
Turning Test Analyzer

Finds candidate CAN IDs/bytes correlated with left-right wheel speed differences
to identify steering angle or yaw-related signals.
"""

import argparse
from pathlib import Path

import numpy as np
import pandas as pd


DEFAULT_CANDIDATE_IDS = ["0B4", "2C1", "1D0", "1C4", "024", "025"]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Analyze turning behavior using wheel speed differences (CAN 0x0AA)."
    )
    parser.add_argument("log_file", help="Path to CAN log CSV")
    parser.add_argument(
        "--candidates",
        default=",".join(DEFAULT_CANDIDATE_IDS),
        help="Comma-separated CAN IDs to analyze (default: %(default)s)",
    )
    parser.add_argument("--min-speed", type=float, default=5.0, help="Min speed (kph)")
    parser.add_argument(
        "--min-diff",
        type=float,
        default=1.5,
        help="Min left-right diff (kph) to count as turning",
    )
    parser.add_argument(
        "--straight-max-diff",
        type=float,
        default=0.5,
        help="Max diff (kph) to count as straight",
    )
    parser.add_argument(
        "--tolerance-ms",
        type=int,
        default=20,
        help="Max time alignment tolerance in ms (default: %(default)s)",
    )
    parser.add_argument("--top", type=int, default=5, help="Top N results per metric")
    parser.add_argument(
        "--max-samples",
        type=int,
        default=10,
        help="Max turning samples to print (default: %(default)s)",
    )
    return parser.parse_args()


def normalize_ids(raw_ids):
    ids = []
    for item in raw_ids.split(","):
        item = item.strip().upper()
        if item.startswith("0X"):
            item = item[2:]
        if item:
            ids.append(item)
    return ids


def hex_to_uint(series):
    return series.fillna("00").apply(lambda x: int(x, 16) if isinstance(x, str) else int(x)).astype("int64")


def be_pair(a, b):
    return (np.left_shift(a, 8) | b).astype("int64")


def load_log(log_path, candidate_ids):
    cols = ["timestamp_us", "can_id"] + [f"b{i}" for i in range(8)]
    df = pd.read_csv(log_path, usecols=cols, dtype=str)
    df["timestamp_us"] = df["timestamp_us"].astype("int64")
    df["can_id"] = df["can_id"].str.upper().str.replace("0X", "", regex=False)
    for col in [f"b{i}" for i in range(8)]:
        df[col] = df[col].fillna("00").str.upper()
    return df[df["can_id"].isin(["0AA"] + candidate_ids)].copy()


def build_wheel_df(df):
    wheel = df[df["can_id"] == "0AA"].copy()
    if wheel.empty:
        return None

    for i in range(8):
        wheel[f"b{i}"] = hex_to_uint(wheel[f"b{i}"])

    fr = be_pair(wheel["b0"].to_numpy(), wheel["b1"].to_numpy())
    fl = be_pair(wheel["b2"].to_numpy(), wheel["b3"].to_numpy())
    rr = be_pair(wheel["b4"].to_numpy(), wheel["b5"].to_numpy())
    rl = be_pair(wheel["b6"].to_numpy(), wheel["b7"].to_numpy())

    fr_kph = (fr - 6750) / 100.0
    fl_kph = (fl - 6750) / 100.0
    rr_kph = (rr - 6750) / 100.0
    rl_kph = (rl - 6750) / 100.0

    speed_avg = (fr_kph + fl_kph + rr_kph + rl_kph) / 4.0
    lr_diff = ((fl_kph + rl_kph) / 2.0) - ((fr_kph + rr_kph) / 2.0)

    wheel_df = pd.DataFrame(
        {
            "timestamp_us": wheel["timestamp_us"].to_numpy(),
            "speed_avg": speed_avg,
            "lr_diff": lr_diff,
        }
    )
    wheel_df["lr_diff_abs"] = wheel_df["lr_diff"].abs()
    return wheel_df.sort_values("timestamp_us")


def print_turning_summary(wheel_df, min_speed, min_diff, straight_max_diff, max_samples):
    total = len(wheel_df)
    if total == 0:
        print("No wheel speed samples found.")
        return

    duration = (wheel_df["timestamp_us"].max() - wheel_df["timestamp_us"].min()) / 1_000_000
    moving_mask = wheel_df["speed_avg"] >= min_speed
    turning_mask = moving_mask & (wheel_df["lr_diff_abs"] >= min_diff)
    straight_mask = moving_mask & (wheel_df["lr_diff_abs"] <= straight_max_diff)

    print("\n=== Turning Summary (0x0AA) ===")
    print(f"Samples: {total:,} over {duration:.1f} seconds")
    print(f"Moving samples (>{min_speed:.1f} kph): {moving_mask.sum():,}")
    print(f"Turning samples (>{min_diff:.1f} kph diff): {turning_mask.sum():,}")
    print(f"Straight samples (<={straight_max_diff:.1f} kph diff): {straight_mask.sum():,}")

    if turning_mask.sum() > 0:
        subset = wheel_df[turning_mask]
        print(
            "Turning diff stats (kph): "
            f"mean={subset['lr_diff'].mean():.2f}, "
            f"max={subset['lr_diff'].max():.2f}, "
            f"min={subset['lr_diff'].min():.2f}"
        )

        top_turns = subset.sort_values("lr_diff_abs", ascending=False).head(max_samples)
        print("\nTop turning samples (timestamp_us, speed_kph, lr_diff_kph):")
        for _, row in top_turns.iterrows():
            print(f"  {row['timestamp_us']:>12}  {row['speed_avg']:>6.1f}  {row['lr_diff']:>7.2f}")


def build_feature_table(aligned):
    features = {}
    for i in range(8):
        features[f"b{i}"] = aligned[f"b{i}"].astype("int64")

    for i in range(7):
        hi = features[f"b{i}"]
        lo = features[f"b{i+1}"]
        features[f"b{i}{i+1}_u16_be"] = (hi * 256 + lo).astype("int64")
        features[f"b{i}{i+1}_u16_le"] = (lo * 256 + hi).astype("int64")

    return features


def print_top_correlations(feature_table, metric_series, top_n, label):
    results = []
    for name, values in feature_table.items():
        corr = values.corr(metric_series)
        if corr is None or np.isnan(corr):
            continue
        results.append((name, corr))

    results.sort(key=lambda item: abs(item[1]), reverse=True)
    print(f"{label}:")
    if not results:
        print("  (no valid correlations)")
        return
    for name, corr in results[:top_n]:
        print(f"  {name:<12} {corr:+.3f}")


def print_top_deltas(feature_table, turning_mask, straight_mask, top_n):
    results = []
    for name, values in feature_table.items():
        turn_mean = values[turning_mask].mean()
        straight_mean = values[straight_mask].mean()
        if np.isnan(turn_mean) or np.isnan(straight_mean):
            continue
        delta = turn_mean - straight_mean
        results.append((name, delta))

    results.sort(key=lambda item: abs(item[1]), reverse=True)
    print("Top turning vs straight deltas (turning - straight):")
    if not results:
        print("  (no valid delta results)")
        return
    for name, delta in results[:top_n]:
        print(f"  {name:<12} {delta:+.2f}")


def analyze_candidates(
    df,
    wheel_df,
    candidate_ids,
    min_speed,
    min_diff,
    straight_max_diff,
    tolerance_ms,
    top_n,
):
    tolerance_us = tolerance_ms * 1000

    for can_id in candidate_ids:
        msgs = df[df["can_id"] == can_id].copy()
        if msgs.empty:
            print(f"\n-- {can_id}: no data")
            continue

        for i in range(8):
            msgs[f"b{i}"] = hex_to_uint(msgs[f"b{i}"])

        msgs = msgs.sort_values("timestamp_us")
        aligned = pd.merge_asof(
            msgs,
            wheel_df,
            on="timestamp_us",
            direction="nearest",
            tolerance=tolerance_us,
        )
        aligned = aligned.dropna(subset=["speed_avg", "lr_diff", "lr_diff_abs"])
        if len(aligned) < 200:
            print(f"\n-- {can_id}: insufficient aligned samples ({len(aligned)})")
            continue

        moving_mask = aligned["speed_avg"] >= min_speed
        turning_mask = moving_mask & (aligned["lr_diff_abs"] >= min_diff)
        straight_mask = moving_mask & (aligned["lr_diff_abs"] <= straight_max_diff)

        print(f"\n-- {can_id} --")
        print(
            f"Aligned samples: {len(aligned):,} "
            f"(turning: {turning_mask.sum():,}, straight: {straight_mask.sum():,})"
        )

        feature_table = build_feature_table(aligned)
        print_top_correlations(feature_table, aligned["lr_diff"], top_n, "Top correlations vs lr_diff")
        print_top_correlations(
            feature_table, aligned["lr_diff_abs"], top_n, "Top correlations vs lr_diff_abs"
        )

        if turning_mask.sum() >= 50 and straight_mask.sum() >= 50:
            print_top_deltas(feature_table, turning_mask, straight_mask, top_n)
        else:
            print("Top turning vs straight deltas: insufficient turning/straight samples")


def main():
    args = parse_args()
    log_file = Path(args.log_file)
    candidate_ids = normalize_ids(args.candidates)

    print(f"Loading {log_file}...")
    df = load_log(log_file, candidate_ids)
    if df.empty:
        print("No matching CAN IDs found in log.")
        return

    wheel_df = build_wheel_df(df)
    if wheel_df is None:
        print("No wheel speed data (0x0AA) found; cannot analyze turning.")
        return

    print_turning_summary(
        wheel_df, args.min_speed, args.min_diff, args.straight_max_diff, args.max_samples
    )

    print("\n=== Candidate Turning Correlations ===")
    analyze_candidates(
        df,
        wheel_df,
        candidate_ids,
        args.min_speed,
        args.min_diff,
        args.straight_max_diff,
        args.tolerance_ms,
        args.top,
    )


if __name__ == "__main__":
    main()
