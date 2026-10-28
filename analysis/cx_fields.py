#!/usr/bin/env python3
"""
Run CANExplorer's automatic field analysis on candump logs.

For every ID, CANExplorer finds checksums, counters, multiplexers, flags,
enumerations and physical values (core/reverse/analysis.py). Results go to one
JSON file per log, which cx_consensus.py combines across logs.

Runs in CANExplorer's environment (https://github.com/AKJ7/CANExplorer):

    uv run --project ~/Code/CANExplorer python -I analysis/cx_fields.py \
        logs/candump/*.log -o analysis/decoded/cx

Convert .bin/.csv logs first with bin_to_candump.py.
"""

import argparse
import json
import os

import can
from can_explorer.core.reverse.analysis import analyse_all, collect

# CANExplorer analyses only the most recent 20,000 frames of each ID by default,
# which on a 100 Hz ID is the last few minutes of a drive
MAX_FRAMES = 200_000


def analyse_log(path):
    msgs = list(can.CanutilsLogReader(path))
    if not msgs:
        return None
    frames = collect(msgs, max_frames=MAX_FRAMES)
    ids = {}
    for analysis in analyse_all(frames):
        fields = []
        for found in analysis.fields:
            if found.kind == "constant":
                continue
            fields.append({
                "kind": found.kind,
                "start": found.field.start,
                "length": found.field.length,
                "order": "M" if found.field.byte_order == "big_endian" else "I",
                "signed": found.field.signed,
                "mux": found.mux_value,
                "details": found.details,
                "min": found.minimum,
                "max": found.maximum,
                "distinct": found.distinct,
                "confidence": round(found.confidence, 2),
            })
        times = frames[analysis.key].times
        rate = (len(times) - 1) / max(1e-3, times[-1] - times[0]) if len(times) > 1 else 0.0
        ids[f"{analysis.key[0]:03X}"] = {
            "frames": analysis.frames,
            "length": analysis.length,
            "rate_hz": round(rate, 2),
            "note": analysis.note,
            "fields": fields,
        }
    return {
        "log": os.path.splitext(os.path.basename(path))[0],
        "frames": len(msgs),
        "duration_s": msgs[-1].timestamp - msgs[0].timestamp,
        "ids": ids,
    }


def main():
    parser = argparse.ArgumentParser(description="CANExplorer field analysis of candump logs")
    parser.add_argument("logs", nargs="+", help="candump .log files")
    parser.add_argument("-o", "--out-dir", required=True, help="Directory for the per-log JSON")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    for path in args.logs:
        result = analyse_log(path)
        if result is None:
            print(f"Skipped {path}: no frames")
            continue
        out_path = os.path.join(args.out_dir, result["log"] + ".json")
        with open(out_path, "w", encoding="utf-8") as dst:
            json.dump(result, dst, indent=1)
        print(f"{result['log']}: {result['frames']} frames, "
              f"{result['duration_s'] / 60:.1f} min, {len(result['ids'])} IDs", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
