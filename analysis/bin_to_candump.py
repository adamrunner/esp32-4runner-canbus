#!/usr/bin/env python3
"""
Convert CAN logs (.bin, or the CSVs from older firmware and bin_to_csv.py) to
candump log format, for CANExplorer, SavvyCAN, can-utils and python-can.

Output lines look like `(1761419837.123456) can0 3BB#0011223344556677`.

Timestamps are the header's log start plus the record's monotonic offset. That
start time is the RTC's local time stored as if it were UTC, and the RTC
drifts (see docs/BINARY_LOGGING.md); pass --clock-offset to shift it, e.g.
6004.5 + 25200 = 31204.5 for the 2026-10-25 logs in PDT. Logs without a valid
RTC time get timestamps relative to the first frame.

The logger records neither the extended-ID nor the RTR flag, so IDs above 0x7FF
are written as extended and every frame as a data frame.
"""

import argparse
import csv
import datetime as dt
import os
import struct
import sys

HEADER_FMT = "<8sHHQQII28s"
HEADER_SIZE = 64
RECORD_FMT = "<QIBB8sH"
RECORD_SIZE = 24
MAGIC_PREFIX = b"CANBIN\x00"
VERSION = 1

MAX_STANDARD_ID = 0x7FF


def format_line(seconds, can_id, dlc, payload, interface):
    if can_id > MAX_STANDARD_ID:
        id_str = f"{can_id:08X}"
    else:
        id_str = f"{can_id:03X}"
    data = payload[:min(dlc, 8)].hex().upper()
    return f"({seconds:.6f}) {interface} {id_str}#{data}\n"


def read_bin(path):
    """Yields (seconds, can_id, dlc, payload); seconds are Unix time from the header, or
    relative to the log start when the RTC was not valid."""
    with open(path, "rb") as src:
        header = src.read(HEADER_SIZE)
        if len(header) < HEADER_SIZE:
            raise ValueError("File too small for header")
        magic, version, header_size, start_unix_us, start_mono_us, record_size, _, _ = \
            struct.unpack(HEADER_FMT, header)
        if not magic.startswith(MAGIC_PREFIX):
            raise ValueError(f"Bad magic: {magic!r}")
        if version != VERSION:
            raise ValueError(f"Unsupported version: {version}")
        if header_size != HEADER_SIZE or record_size != RECORD_SIZE:
            raise ValueError(f"Unexpected header/record size: {header_size}/{record_size}")

        leftover = b""
        while True:
            chunk = src.read(RECORD_SIZE * 4096)
            if not chunk:
                break
            data = leftover + chunk
            end = len(data) // RECORD_SIZE * RECORD_SIZE
            leftover = data[end:]
            for timestamp_us, can_id, dlc, _, payload, _ in struct.iter_unpack(RECORD_FMT, data[:end]):
                offset_us = max(0, timestamp_us - start_mono_us)
                yield (start_unix_us + offset_us) / 1e6, can_id, dlc, payload
        if leftover:
            print(f"Warning: {path}: ignoring {len(leftover)} trailing bytes (truncated file)",
                  file=sys.stderr)


def read_csv(path):
    """Yields (seconds, can_id, dlc, payload) from a CSV log. Absolute times come from the
    first row's `datetime` (whole seconds) plus the monotonic offset; without that column,
    times are relative to the first frame."""
    with open(path, newline="", encoding="utf-8-sig") as src:
        reader = csv.DictReader(src)
        if not reader.fieldnames:
            raise ValueError("Empty file")
        if "timestamp_us" not in reader.fieldnames:
            raise ValueError("Not a CAN CSV log (no timestamp_us column)")
        # The earliest firmware named the payload columns byte0-byte7
        prefix = "b" if "b0" in reader.fieldnames else "byte"
        first_us = None
        start_unix = 0.0
        for row in reader:
            try:
                timestamp_us = int(row["timestamp_us"])
                can_id = int(row["can_id"], 16)
                dlc = int(row["dlc"])
                payload = bytes(int(row[f"{prefix}{i}"], 16) for i in range(8))
            except (KeyError, TypeError, ValueError):
                continue
            if first_us is None:
                first_us = timestamp_us
                stamp = (row.get("datetime") or "").strip()
                if stamp:
                    # bin_to_csv.py wrote it with fromtimestamp(), so undo that the same way
                    start_unix = dt.datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S").timestamp()
            yield start_unix + (timestamp_us - first_us) / 1e6, can_id, dlc, payload


def convert_file(input_path, output_path, clock_offset, interface):
    ext = os.path.splitext(input_path)[1].lower()
    records = read_bin(input_path) if ext == ".bin" else read_csv(input_path)
    count = 0
    with open(output_path, "w", encoding="ascii") as dst:
        for seconds, can_id, dlc, payload in records:
            dst.write(format_line(seconds + clock_offset, can_id, dlc, payload, interface))
            count += 1
    return count


def collect_inputs(paths):
    """Expands directories into their .bin/.csv logs. A CSV next to a .bin of the same name
    is bin_to_csv.py output, so only the .bin is used."""
    inputs = []
    for path in paths:
        if os.path.isdir(path):
            names = sorted(os.listdir(path))
            bins = {os.path.splitext(n)[0].lower() for n in names if n.lower().endswith(".bin")}
            for name in names:
                stem, ext = os.path.splitext(name)
                if ext.lower() == ".bin" or (ext.lower() == ".csv" and stem.lower() not in bins):
                    inputs.append(os.path.join(path, name))
        else:
            inputs.append(path)
    return inputs


def main():
    parser = argparse.ArgumentParser(description="Convert CAN logs (.bin/.csv) to candump format")
    parser.add_argument("inputs", nargs="+", help="Log files or directories of logs")
    parser.add_argument("-o", "--output", help="Output path (single input only)")
    parser.add_argument("-d", "--out-dir", help="Directory for outputs (default: next to each input)")
    parser.add_argument("--clock-offset", type=float, default=0.0,
                        help="Seconds added to every timestamp (true time - RTC time)")
    parser.add_argument("--interface", default="can0", help="Interface name in the output")
    args = parser.parse_args()

    inputs = collect_inputs(args.inputs)
    if args.output and len(inputs) != 1:
        print("--output needs exactly one input", file=sys.stderr)
        return 1
    if args.out_dir:
        os.makedirs(args.out_dir, exist_ok=True)

    failures = 0
    for input_path in inputs:
        if args.output:
            output_path = args.output
        else:
            base = os.path.splitext(os.path.basename(input_path))[0] + ".log"
            output_path = os.path.join(args.out_dir or os.path.dirname(input_path), base)
        try:
            count = convert_file(input_path, output_path, args.clock_offset, args.interface)
        except (OSError, ValueError) as exc:
            print(f"Skipped {input_path}: {exc}", file=sys.stderr)
            failures += 1
            continue
        if count == 0:
            os.remove(output_path)
            print(f"Skipped {input_path}: no frames")
            continue
        print(f"Wrote {count} frames to {output_path}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
