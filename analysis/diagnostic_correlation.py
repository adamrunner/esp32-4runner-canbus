#!/usr/bin/env python3
"""
Diagnostic Correlation Analyzer

Correlate broadcast CAN IDs with diagnostic PID values from OBD responses.
"""

import argparse
from pathlib import Path

import numpy as np
import pandas as pd


DEFAULT_CANDIDATE_IDS = [
    "024",
    "025",
    "0AA",
    "0B4",
    "1C4",
    "1D0",
    "2C1",
    "3B3",
    "3D3",
    "498",
    "499",
    "49E",
    "49F",
    "4A7",
    "4A8",
]


def _hex_to_uint(series):
    return series.fillna("00").apply(lambda x: int(x, 16) if isinstance(x, str) else int(x))


def _to_int8(values):
    return values.where(values <= 127, values - 256)


def _to_int16(values):
    return values.where(values <= 32767, values - 65536)


def _window_series(timestamps_us, values, window_ms):
    window_us = window_ms * 1000
    windows = (timestamps_us // window_us) * window_ms
    df = pd.DataFrame({"window": windows, "value": values})
    return df.groupby("window")["value"].mean()


def _window_features(timestamps_us, features, window_ms):
    window_us = window_ms * 1000
    windows = (timestamps_us // window_us) * window_ms
    data = {"window": windows}
    data.update(features)
    df = pd.DataFrame(data)
    return df.groupby("window").mean()


def load_log(log_path):
    cols = [
        "timestamp_us",
        "can_id",
        "b0",
        "b1",
        "b2",
        "b3",
        "b4",
        "b5",
        "b6",
        "b7",
    ]
    df = pd.read_csv(log_path, usecols=cols, dtype=str)
    df["timestamp_us"] = df["timestamp_us"].astype("int64")
    df["can_id"] = df["can_id"].str.upper()
    for col in ["b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7"]:
        df[col] = df[col].fillna("00").str.upper()
    return df


def extract_diag_signals(df):
    diag_ids = {"7E8", "7B8", "7C8"}
    diag = df[df["can_id"].isin(diag_ids)].copy()
    if diag.empty:
        return {}

    diag["service"] = diag["b1"].str.upper()
    diag["pid"] = diag["b2"].str.upper()
    diag["length"] = _hex_to_uint(diag["b0"])

    b3 = _hex_to_uint(diag["b3"])
    b4 = _hex_to_uint(diag["b4"])
    b5 = _hex_to_uint(diag["b5"])
    b6 = _hex_to_uint(diag["b6"])
    b7 = _hex_to_uint(diag["b7"])

    signals = {}

    def add_signal(name, mask, values):
        subset = diag.loc[mask, ["timestamp_us"]].copy()
        if subset.empty:
            return
        subset["value"] = values[mask]
        signals[name] = subset

    # Standard OBD-II (0x41) from 0x7E8
    std_mask = (diag["can_id"] == "7E8") & (diag["service"] == "41")
    add_signal("rpm", std_mask & (diag["pid"] == "0C"), (b3 * 256 + b4) / 4.0)
    add_signal("vehicle_speed_kph", std_mask & (diag["pid"] == "0D"), b3.astype(float))
    add_signal("vbatt_v", std_mask & (diag["pid"] == "42"), (b3 * 256 + b4) / 1000.0)
    add_signal("iat_c", std_mask & (diag["pid"] == "0F"), b3.astype(float) - 40.0)
    add_signal("baro_kpa", std_mask & (diag["pid"] == "33"), b3.astype(float))

    # Extended (0x61) responses
    ext_mask = diag["service"] == "61"

    # ECM extended responses (0x7E8)
    ecm_mask = ext_mask & (diag["can_id"] == "7E8")
    add_signal("atf_pan_c", ecm_mask & (diag["pid"] == "82"), (b3 * 256 + b4) / 256.0 - 40.0)
    add_signal("atf_tqc_c", ecm_mask & (diag["pid"] == "82"), (b5 * 256 + b6) / 256.0 - 40.0)
    add_signal("gear", ecm_mask & (diag["pid"] == "85"), b3.astype(float))
    add_signal("odo_km", ecm_mask & (diag["pid"] == "28"), ((b3 * 65536) + (b4 * 256) + b5).astype(float))

    # ABS extended responses (0x7B8)
    abs_mask = ext_mask & (diag["can_id"] == "7B8")
    add_signal("diag_wheel_fr_kph", abs_mask & (diag["pid"] == "03"), (b3 * 256.0) / 200.0)
    add_signal("diag_wheel_fl_kph", abs_mask & (diag["pid"] == "03"), (b4 * 256.0) / 200.0)
    add_signal("diag_wheel_rr_kph", abs_mask & (diag["pid"] == "03"), (b5 * 256.0) / 200.0)
    add_signal("diag_wheel_rl_kph", abs_mask & (diag["pid"] == "03"), (b6 * 256.0) / 200.0)
    add_signal(
        "diag_wheel_avg_kph",
        abs_mask & (diag["pid"] == "03"),
        ((b3 + b4 + b5 + b6) * 256.0) / (200.0 * 4.0),
    )

    # Orientation live data (PID 0x47)
    lat_g = _to_int8(b3) / 50.0
    long_g = _to_int8(b4) / 50.0
    yaw_rate = b5.astype(float) - 128.0
    steering_angle = (((b6 * 256) + b7).astype(float) / 10.0) - 3276.8
    add_signal("lateral_g", abs_mask & (diag["pid"] == "47"), lat_g)
    add_signal("longitudinal_g", abs_mask & (diag["pid"] == "47"), long_g)
    add_signal("yaw_rate_deg_sec", abs_mask & (diag["pid"] == "47"), yaw_rate)
    add_signal("steering_angle_deg", abs_mask & (diag["pid"] == "47"), steering_angle)

    # Meter extended responses (0x7C8)
    meter_mask = ext_mask & (diag["can_id"] == "7C8")
    add_signal("fuel_gal", meter_mask & (diag["pid"] == "29"), (b3 * 500.0) / 3785.0)

    return signals


def compute_feature_windows(msgs, window_ms):
    timestamps_us = msgs["timestamp_us"]
    bytes_u8 = {}
    for i in range(8):
        bytes_u8[f"b{i}_u8"] = _hex_to_uint(msgs[f"b{i}"])

    features = {}
    for i in range(8):
        u8 = bytes_u8[f"b{i}_u8"].astype(float)
        features[f"b{i}_u8"] = u8
        features[f"b{i}_s8"] = _to_int8(u8)

    for i in range(7):
        hi = bytes_u8[f"b{i}_u8"]
        lo = bytes_u8[f"b{i+1}_u8"]
        u16_be = (hi * 256) + lo
        u16_le = (lo * 256) + hi
        features[f"b{i}{i+1}_u16_be"] = u16_be.astype(float)
        features[f"b{i}{i+1}_s16_be"] = _to_int16(u16_be.astype(float))
        features[f"b{i}{i+1}_u16_le"] = u16_le.astype(float)
        features[f"b{i}{i+1}_s16_le"] = _to_int16(u16_le.astype(float))

    return _window_features(timestamps_us, features, window_ms)


def correlate_signals(df, candidate_ids, window_ms, min_windows, top_n, signal_filter=None):
    signals = extract_diag_signals(df)
    if not signals:
        print("No diagnostic signals found.")
        return

    if signal_filter:
        filtered = {}
        for name in signal_filter:
            if name in signals:
                filtered[name] = signals[name]
            else:
                print(f"Warning: diagnostic signal not found: {name}")
        signals = filtered

    print("Diagnostic signal samples:")
    for name, signal_df in sorted(signals.items()):
        print(f"- {name}: {len(signal_df)} samples")

    for signal_name, signal_df in sorted(signals.items()):
        diag_series = _window_series(signal_df["timestamp_us"], signal_df["value"], window_ms)
        if diag_series.std() == 0 or len(diag_series) < min_windows:
            print(f"\n=== {signal_name} ===\nSkipped (insufficient variation or samples)")
            continue

        results = []
        for can_id in candidate_ids:
            msgs = df[df["can_id"] == can_id].copy()
            if msgs.empty:
                continue

            features = compute_feature_windows(msgs, window_ms)
            aligned = features.join(diag_series, how="inner")
            if len(aligned) < min_windows:
                continue

            diag_values = aligned["value"]
            if diag_values.std() == 0:
                continue

            feature_cols = [col for col in aligned.columns if col != "value"]
            feature_std = aligned[feature_cols].std()
            feature_cols = feature_std[feature_std > 0].index.tolist()
            if not feature_cols:
                continue

            corr = aligned[feature_cols].corrwith(diag_values)
            for feature_name, corr_value in corr.dropna().items():
                results.append({
                    "can_id": can_id,
                    "feature": feature_name,
                    "corr": float(corr_value),
                    "windows": len(aligned),
                })

        results.sort(key=lambda x: abs(x["corr"]), reverse=True)

        print(f"\n=== {signal_name} ===")
        if not results:
            print("No correlations found.")
            continue

        print(f"Top correlations (window={window_ms}ms, min_windows={min_windows}):")
        print(f"{'CAN ID':<6} {'Feature':<14} {'Corr':>8} {'Windows':>8}")
        print("-" * 44)
        for entry in results[:top_n]:
            print(f"{entry['can_id']:<6} {entry['feature']:<14} {entry['corr']:>8.3f} {entry['windows']:>8}")


def main():
    parser = argparse.ArgumentParser(description="Correlate broadcast IDs with diagnostic PIDs.")
    parser.add_argument("log_file", help="Path to CAN log CSV")
    parser.add_argument("--ids", nargs="*", default=None, help="CAN IDs to analyze")
    parser.add_argument("--window-ms", type=int, default=200, help="Window size for correlation")
    parser.add_argument("--min-windows", type=int, default=20, help="Minimum aligned windows")
    parser.add_argument("--top", type=int, default=10, help="Top correlations to display per signal")
    parser.add_argument("--signals", nargs="*", default=None, help="Diagnostic signals to analyze")
    args = parser.parse_args()

    log_path = Path(args.log_file)
    if not log_path.exists():
        raise SystemExit(f"Log file not found: {log_path}")

    candidate_ids = []
    for cid in (args.ids or DEFAULT_CANDIDATE_IDS):
        normalized = cid.strip().upper()
        if normalized.startswith("0X"):
            normalized = normalized[2:]
        candidate_ids.append(normalized)
    df = load_log(log_path)
    correlate_signals(df, candidate_ids, args.window_ms, args.min_windows, args.top, args.signals)


if __name__ == "__main__":
    main()
