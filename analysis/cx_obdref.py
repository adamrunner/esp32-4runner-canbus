#!/usr/bin/env python3
"""
Find the broadcast fields that carry the OBD-II values the firmware polls.

The PID replies on 0x7E8 are already in the logs. Each PID's values become a
reference for CANExplorer's correlation (core/reverse/reference.py): every
candidate field (byte-aligned 8/16-bit fields plus the fields its analysis
finds) is sampled at the reply times, ranked by Pearson r, searched for a time
lag within +/-1 s, and fitted to factor/offset in the PID's unit.

    uv run --project ~/Code/CANExplorer python -I analysis/cx_obdref.py logs/candump/*.log

A match that holds across several logs with the same factor is a decode; see
dbc/toyota_4runner_2018.dbc for the ones found so far.
"""

import argparse
import os
from collections import defaultdict

import can
from can_explorer.core.reverse.analysis import analyse_all, collect
from can_explorer.core.reverse.obd import PIDS_BY_NUMBER, ObdPoller
from can_explorer.core.reverse.reference import candidates, correlate

MAX_FRAMES = 200_000
MIN_REPLIES = 20


def main():
    parser = argparse.ArgumentParser(description="Match broadcast fields to logged OBD-II replies")
    parser.add_argument("logs", nargs="+", help="candump .log files")
    parser.add_argument("--top", type=int, default=3, help="Matches per PID per log")
    parser.add_argument("--max-lag", type=float, default=1.0, help="Lag search range (s)")
    args = parser.parse_args()

    results = defaultdict(list)
    for path in args.logs:
        msgs = list(can.CanutilsLogReader(path))
        # Not sending anything: the poller only decodes the replies already in the log
        poller = ObdPoller(lambda msg: None, list(PIDS_BY_NUMBER))
        for msg in msgs:
            poller.on_message(msg)
        # Diagnostic requests and replies are not broadcast data
        diagnostic = {(m.arbitration_id, m.is_extended_id) for m in msgs if 0x700 <= m.arbitration_id <= 0x7FF}
        frames = collect(msgs, max_frames=MAX_FRAMES, exclude=diagnostic)
        candidate_fields = candidates(frames, analyse_all(frames))
        for reference in poller.references():
            if len(reference) < MIN_REPLIES or max(reference.values) == min(reference.values):
                continue
            for match in correlate(reference, frames, candidate_fields, max_lag=args.max_lag, top=args.top):
                f = match.field
                order = "M" if f.byte_order == "big_endian" else "I"
                results[reference.name].append((
                    os.path.basename(path), len(reference), match.correlation, match.lag,
                    f"0x{f.frame_id:03X} {f.start}|{f.length}{order}{'s' if f.signed else ''} "
                    f"x{f.scale:g}{f.offset:+g}",
                ))

    for name, rows in results.items():
        print(f"\n== {name}")
        for log, count, r, lag, desc in rows:
            print(f"   {log:<28} n={count:<4} r={r:+.3f} lag={lag:+.2f}s  {desc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
