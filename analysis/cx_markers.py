#!/usr/bin/env python3
"""
Find the bits that change around moments you mark (a button press, a lamp
coming on), with CANExplorer's marker search (core/reverse/markers.py).

Each bit scores the share of markers it changes near, minus the chance of that
happening by coincidence given how often it changes elsewhere, so busy bits
and counters rank low unless they really line up.

    uv run --project ~/Code/CANExplorer python -I analysis/cx_markers.py \
        tpms.log 2026-10-25T14:02:20 2026-10-25T14:02:51 2026-10-25T14:05:26

Marker times are local ISO times on the log's clock: convert the log with
bin_to_candump.py --clock-offset so it matches the clock you noted them on.
"""

import argparse
import datetime as dt

import can
from can_explorer.core.reverse.analysis import collect
from can_explorer.core.reverse.markers import Marker, find_marker_changes

MAX_FRAMES = 200_000


def main():
    parser = argparse.ArgumentParser(description="Bits changing around marked moments")
    parser.add_argument("log", help="candump .log file")
    parser.add_argument("times", nargs="+", help="Marker times, e.g. 2026-10-25T14:02:20")
    parser.add_argument("--before", type=float, default=5.0, help="Window before each marker (s)")
    parser.add_argument("--after", type=float, default=10.0, help="Window after each marker (s)")
    parser.add_argument("--top", type=int, default=30)
    args = parser.parse_args()

    markers = [Marker(dt.datetime.fromisoformat(t).timestamp(), t) for t in args.times]
    frames = collect(can.CanutilsLogReader(args.log), max_frames=MAX_FRAMES)
    matches = find_marker_changes(frames, markers, before=args.before, after=args.after, top=args.top)
    if not matches:
        print("No bits change around most markers more than chance")
    for m in matches:
        print(f"0x{m.field.frame_id:03X} bits {m.field.start}:{m.field.length}  "
              f"hits {m.hits}/{m.markers}  background {m.background:.3f}/s  "
              f"score {m.score:.2f}  {', '.join(m.transitions)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
