#!/usr/bin/env python3
"""
TPMS RF Recorder
Polls the esp32-4runner-tpms receiver (http://tpms.local/api/state) and appends
each state snapshot, stamped with this machine's clock, to a JSONL file. The
file is the ground truth for tpms_correlate.py.

Usage:
    ./scripts/tpms_rf_record.py logs/rf_20261025.jsonl
    ./scripts/tpms_rf_record.py logs/rf.jsonl --url http://tpms.local --interval 5
"""

import argparse
import json
import sys
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description="Record TPMS receiver state to JSONL")
    parser.add_argument("output", help="JSONL file to append to")
    parser.add_argument("--url", default="http://tpms.local", help="Receiver base URL")
    parser.add_argument("--interval", type=float, default=5.0, help="Poll interval (s)")
    args = parser.parse_args()

    state_url = args.url.rstrip("/") + "/api/state"
    print(f"Recording {state_url} every {args.interval:g}s to {args.output} (Ctrl-C to stop)")

    last_counts = {}
    with open(args.output, "a", encoding="utf-8", buffering=1) as out:
        while True:
            try:
                with urllib.request.urlopen(state_url, timeout=4) as resp:
                    state = json.load(resp)
                now = time.time()
                out.write(json.dumps({"host_unix": now, "state": state}) + "\n")

                # Print only new sensor reports for the mapped wheels
                for sensor in state.get("sensors", []):
                    pos = sensor.get("pos")
                    if pos and last_counts.get(sensor["id"]) != sensor.get("count"):
                        last_counts[sensor["id"]] = sensor.get("count")
                        print(f"{time.strftime('%H:%M:%S')} {pos} {sensor['psi']:.1f} PSI "
                              f"(age {sensor.get('age_s')}s)")
            except KeyboardInterrupt:
                raise
            except Exception as exc:  # receiver unplugged, Wi-Fi drop, etc.
                print(f"{time.strftime('%H:%M:%S')} poll failed: {exc}", file=sys.stderr)
            time.sleep(args.interval)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
