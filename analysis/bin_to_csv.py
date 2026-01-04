#!/usr/bin/env python3
"""
Convert CAN binary logs (.bin) to CSV.
"""

import argparse
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

CSV_HEADER = "datetime,timestamp_us,can_id,dlc,b0,b1,b2,b3,b4,b5,b6,b7\n"


def parse_header(data):
    if len(data) < HEADER_SIZE:
        raise ValueError("File too small for header")

    magic, version, header_size, log_start_unix_us, log_start_mono_us, record_size, flags, _ = \
        struct.unpack(HEADER_FMT, data[:HEADER_SIZE])

    if not magic.startswith(MAGIC_PREFIX):
        raise ValueError(f"Bad magic: {magic!r}")
    if version != VERSION:
        raise ValueError(f"Unsupported version: {version}")
    if header_size != HEADER_SIZE:
        raise ValueError(f"Unexpected header size: {header_size}")
    if record_size != RECORD_SIZE:
        raise ValueError(f"Unexpected record size: {record_size}")

    return {
        "log_start_unix_us": log_start_unix_us,
        "log_start_monotonic_us": log_start_mono_us,
        "flags": flags,
    }


def format_datetime(log_start_unix_us, log_start_mono_us, timestamp_us, cache):
    if log_start_unix_us == 0:
        return ""

    if timestamp_us < log_start_mono_us:
        unix_us = log_start_unix_us
    else:
        unix_us = log_start_unix_us + (timestamp_us - log_start_mono_us)

    seconds_int = int(unix_us // 1_000_000)
    cached = cache.get(seconds_int)
    if cached is not None:
        return cached

    formatted = dt.datetime.fromtimestamp(seconds_int).strftime("%Y-%m-%d %H:%M:%S")
    cache[seconds_int] = formatted
    return formatted


def convert_file(input_path, output_path):
    with open(input_path, "rb") as src:
        header_data = src.read(HEADER_SIZE)
        header = parse_header(header_data)

        with open(output_path, "w", encoding="utf-8") as dst:
            dst.write(CSV_HEADER)

            leftover = b""
            records_written = 0
            datetime_cache = {}

            while True:
                chunk = src.read(RECORD_SIZE * 1024)
                if not chunk:
                    break

                data = leftover + chunk
                record_count = len(data) // RECORD_SIZE
                end = record_count * RECORD_SIZE
                leftover = data[end:]

                for i in range(record_count):
                    offset = i * RECORD_SIZE
                    rec = data[offset:offset + RECORD_SIZE]
                    timestamp_us, can_id, dlc, flags, payload, _ = struct.unpack(RECORD_FMT, rec)

                    datetime_str = format_datetime(
                        header["log_start_unix_us"],
                        header["log_start_monotonic_us"],
                        timestamp_us,
                        datetime_cache,
                    )

                    can_id_str = f"{can_id:03X}"
                    bytes_hex = [f"{b:02X}" for b in payload]

                    line = ",".join([
                        datetime_str,
                        str(timestamp_us),
                        can_id_str,
                        str(dlc),
                        *bytes_hex,
                    ])
                    dst.write(line + "\n")
                    records_written += 1

            if leftover:
                print(
                    f"Warning: ignoring {len(leftover)} trailing bytes (truncated file)",
                    file=sys.stderr,
                )

    return records_written


def main():
    parser = argparse.ArgumentParser(description="Convert CAN binary logs to CSV")
    parser.add_argument("input", help="Path to .bin log file")
    parser.add_argument("output", nargs="?", help="Output CSV path")
    args = parser.parse_args()

    input_path = args.input
    if not os.path.isfile(input_path):
        print(f"Input file not found: {input_path}", file=sys.stderr)
        return 1

    if args.output:
        output_path = args.output
    else:
        base, _ = os.path.splitext(input_path)
        output_path = base + ".csv"

    try:
        records = convert_file(input_path, output_path)
    except ValueError as exc:
        print(f"Invalid binary log file: {exc}", file=sys.stderr)
        print("Ensure the input is a valid CAN binary log (.bin) file.", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"Conversion failed: {exc}", file=sys.stderr)
        return 1

    print(f"Wrote {records} records to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
