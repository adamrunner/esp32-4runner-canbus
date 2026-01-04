#!/usr/bin/env python3
"""
Decode CAN logs using DBC files (cantools).

Outputs one CSV per decoded message ID with timestamp + decoded signals.
"""

import argparse
from pathlib import Path

import cantools
import numpy as np
import pandas as pd

DBC_HEADER = """VERSION \"\"


NS_ :
    NS_DESC_
    CM_
    BA_DEF_
    BA_
    VAL_
    CAT_DEF_
    CAT_
    FILTER
    BA_DEF_DEF_
    EV_DATA_
    ENVVAR_DATA_
    SGTYPE_
    SGTYPE_VAL_
    BA_DEF_SGTYPE_
    BA_SGTYPE_
    SIG_TYPE_REF_
    VAL_TABLE_
    SIG_GROUP_
    SIG_VALTYPE_
    SIGTYPE_VALTYPE_
    BO_TX_BU_
    BA_DEF_REL_
    BA_REL_
    BA_DEF_DEF_REL_
    BU_SG_REL_
    BU_EV_REL_
    BU_BO_REL_
    SG_MUL_VAL_

BS_:

BU_: XXX
"""


def parse_can_id(value):
    value = value.strip().upper()
    if value.startswith("0X"):
        value = value[2:]
    return int(value, 16)


def load_databases(paths):
    databases = []
    for path in paths:
        content = Path(path).read_text(errors="ignore")
        lines = []
        for line in content.splitlines():
            stripped = line.strip()
            if stripped.startswith("CM_ \"") and not stripped.endswith(";"):
                continue
            if stripped.startswith("CM BO_"):
                line = line.replace("CM BO_", "CM_ BO_", 1)
                stripped = line.strip()
            if stripped.startswith("CM_ BO_"):
                continue
            lines.append(line)
        content = "\n".join(lines)
        if "VERSION" not in content.splitlines()[:5]:
            content = f"{DBC_HEADER}\n{content}"
            databases.append(cantools.database.load_string(content, strict=False))
        else:
            databases.append(cantools.database.load_file(str(path), strict=False))
    return databases


def build_message_map(databases):
    msg_map = {}
    for db in databases:
        for msg in db.messages:
            if msg.frame_id not in msg_map:
                msg_map[msg.frame_id] = msg
    return msg_map


def decode_message_rows(msg, rows):
    decoded_rows = []
    errors = 0

    for row in rows.itertuples(index=False):
        data = bytes([
            int(row.b0, 16),
            int(row.b1, 16),
            int(row.b2, 16),
            int(row.b3, 16),
            int(row.b4, 16),
            int(row.b5, 16),
            int(row.b6, 16),
            int(row.b7, 16),
        ])
        try:
            decoded = msg.decode(data, decode_choices=False)
        except Exception:
            errors += 1
            continue

        decoded["timestamp_us"] = row.timestamp_us
        if "ACCEL_Y" in decoded:
            decoded["ACCEL_Y_G_EST"] = (decoded["ACCEL_Y"] * -0.002121) + -0.0126
        decoded_rows.append(decoded)

    df_out = pd.DataFrame(decoded_rows)
    if not df_out.empty:
        df_out = df_out.sort_values("timestamp_us")

    return df_out, errors


def main():
    parser = argparse.ArgumentParser(description="Decode CAN log CSV using DBC files")
    parser.add_argument("log_file", help="Path to CAN log CSV")
    parser.add_argument("--dbc", action="append", required=True, help="DBC file path")
    parser.add_argument("--ids", nargs="*", default=None, help="CAN IDs to decode (hex)")
    parser.add_argument("--out-dir", default="analysis/decoded", help="Output directory")
    parser.add_argument("--compare-obd", action="store_true",
                        help="Compare decoded 0x024/0x025 against OBD PID 0x47 signals")
    args = parser.parse_args()

    log_path = Path(args.log_file)
    if not log_path.exists():
        raise SystemExit(f"Log file not found: {log_path}")

    dbcs = [Path(path) for path in args.dbc]
    for path in dbcs:
        if not path.exists():
            raise SystemExit(f"DBC file not found: {path}")

    databases = load_databases(dbcs)
    msg_map = build_message_map(databases)

    if args.ids:
        target_ids = [parse_can_id(cid) for cid in args.ids]
    else:
        target_ids = sorted(msg_map.keys())

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

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    diag_df = None
    if args.compare_obd:
        abs_mask = (df["can_id"] == "7B8") & (df["b1"] == "61") & (df["b2"] == "47")
        abs_df = df.loc[abs_mask, ["timestamp_us", "b3", "b4", "b5", "b6", "b7"]].copy()
        if not abs_df.empty:
            for col in ["b3", "b4", "b5", "b6", "b7"]:
                abs_df[col] = abs_df[col].apply(lambda x: int(x, 16))
            lat_g = abs_df["b3"].where(abs_df["b3"] <= 127, abs_df["b3"] - 256) / 50.0
            yaw_rate = abs_df["b5"].astype(float) - 128.0
            steering_angle = (((abs_df["b6"] * 256) + abs_df["b7"]).astype(float) / 10.0) - 3276.8
            diag_df = pd.DataFrame({
                "timestamp_us": abs_df["timestamp_us"].astype("int64"),
                "lateral_g": lat_g,
                "yaw_rate_deg_sec": yaw_rate,
                "steering_angle_deg": steering_angle,
            }).sort_values("timestamp_us")

    def compare_with_obd(decoded_df, message_name):
        if diag_df is None or decoded_df.empty:
            return
        decoded_df = decoded_df.sort_values("timestamp_us")

        def correlate(signal_col, diag_col, label):
            merged = pd.merge_asof(
                diag_df[["timestamp_us", diag_col]],
                decoded_df[["timestamp_us", signal_col]],
                on="timestamp_us",
                direction="nearest",
                tolerance=20000,
            ).dropna()
            if merged.empty:
                print(f"{message_name}: no aligned samples for {label}")
                return
            corr = np.corrcoef(merged[signal_col], merged[diag_col])[0, 1]
            print(f"{message_name}: {label} corr={corr:.3f} n={len(merged)}")

        if "YAW_RATE" in decoded_df.columns:
            correlate("YAW_RATE", "yaw_rate_deg_sec", "YAW_RATE vs diag yaw_rate")
        if "STEER_ANGLE" in decoded_df.columns:
            correlate("STEER_ANGLE", "steering_angle_deg", "STEER_ANGLE vs diag steering_angle")
        if "ACCEL_Y_G_EST" in decoded_df.columns:
            correlate("ACCEL_Y_G_EST", "lateral_g", "ACCEL_Y_G_EST vs diag lateral_g")

    for frame_id in target_ids:
        msg = msg_map.get(frame_id)
        if msg is None:
            continue

        hex_id = f"{frame_id:03X}"
        subset = df[df["can_id"] == hex_id]
        if subset.empty:
            continue

        decoded_df, errors = decode_message_rows(msg, subset)
        if decoded_df.empty:
            print(f"{hex_id}: no decodable rows")
            continue

        output_name = f"{log_path.stem}_{hex_id}_{msg.name}.csv"
        output_path = out_dir / output_name
        decoded_df.to_csv(output_path, index=False)
        print(f"{hex_id}: decoded {len(decoded_df)} rows -> {output_path} (errors={errors})")

        if args.compare_obd:
            compare_with_obd(decoded_df, f"0x{hex_id} {msg.name}")


if __name__ == "__main__":
    main()
