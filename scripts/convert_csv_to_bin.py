#!/usr/bin/env python3
"""
Convert CAN CSV logs to fixed-size binary records for faster analysis.

Binary record layout (little-endian):
  uint64  timestamp_us
  uint16  can_id
  uint8   dlc
  uint8   data[8]

Total size per record: 19 bytes. No file header is included.
"""

import csv
import struct
import sys
from pathlib import Path


RECORD_STRUCT = struct.Struct("<QHB8B")


def convert(csv_path: Path, bin_path: Path):
    written = 0
    with csv_path.open() as f_in, bin_path.open("wb") as f_out:
        reader = csv.DictReader(f_in)
        for row in reader:
            try:
                timestamp = int(row.get("timestamp_us", 0))
                can_id = int(str(row.get("can_id", "0")), 16)
                dlc = int(row.get("dlc", 0))
            except Exception:
                continue

            data_bytes = []
            for i in range(8):
                val = str(row.get(f"byte{i}", "")).strip()
                if val == "":
                    data_bytes.append(0)
                    continue
                try:
                    data_bytes.append(int(val, 16))
                except ValueError:
                    try:
                        data_bytes.append(int(val))
                    except ValueError:
                        data_bytes.append(0)

            record = RECORD_STRUCT.pack(timestamp, can_id, dlc, *data_bytes[:8])
            f_out.write(record)
            written += 1

    print(f"Converted {written} records to {bin_path}")


def main():
    if len(sys.argv) < 2:
        print("Usage: convert_csv_to_bin.py <input.csv> [output.bin]")
        sys.exit(1)

    csv_path = Path(sys.argv[1])
    if not csv_path.exists():
        print(f"❌ Input file not found: {csv_path}")
        sys.exit(1)

    if len(sys.argv) >= 3:
        bin_path = Path(sys.argv[2])
    else:
        bin_path = csv_path.with_suffix(".bin")

    convert(csv_path, bin_path)


if __name__ == "__main__":
    main()
