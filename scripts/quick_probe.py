#!/usr/bin/env python3
"""
Quick probe utility to summarize candidate signals from CAN logs (CSV or .bin).

Currently checks:
- Speed candidates: 0x024 (bytes0-1, 4-5) and 0x237 (bytes0-1) for variation
- RPM candidates: 0x1D0 (bytes0-1, 1-2)
- TPMS candidates: 0x4A7 byte2/3 averages vs RPM idle/high

Binary format matches convert_csv_to_bin.py (<QHB8B).
"""

import csv
import struct
import sys
from collections import Counter
from pathlib import Path


RECORD_STRUCT = struct.Struct('<QHB8B')


def iter_msgs(path: Path):
    if path.suffix.lower() == '.bin':
        with path.open('rb') as f:
            while True:
                chunk = f.read(RECORD_STRUCT.size)
                if len(chunk) < RECORD_STRUCT.size:
                    break
                ts, can_id, dlc, *data = RECORD_STRUCT.unpack(chunk)
                yield {'id': can_id, 'dlc': dlc, 'data': data[:dlc]}
    else:
        with path.open() as f:
            first = f.readline()
            f.seek(0)
            if first.lower().startswith('timestamp_us'):
                r = csv.DictReader(f)
                for row in r:
                    try:
                        can_id = int(row['can_id'], 16)
                        dlc = int(row['dlc'])
                        data = [int(row[f'byte{i}'], 16) for i in range(8)]
                    except Exception:
                        continue
                    yield {'id': can_id, 'dlc': dlc, 'data': data[:dlc]}
            else:
                # text log not needed for this probe
                return


def summarize(path: Path):
    speed_024_01 = []
    speed_024_45 = []
    speed_237_01 = []
    rpm_1d0_01 = []
    rpm_1d0_12 = []
    tpms_4a7_low = []
    tpms_4a7_high = []
    rpm_last = None

    for msg in iter_msgs(path):
        cid = msg['id']
        data = msg['data'] + [0] * (8 - len(msg['data']))

        if cid == 0x1D0:
            rpm_1d0_01.append((data[0] << 8) | data[1])
            rpm_1d0_12.append((data[1] << 8) | data[2])
            rpm_last = (data[1] << 8) | data[2]
        elif cid == 0x024:
            speed_024_01.append((data[0] << 8) | data[1])
            speed_024_45.append((data[4] << 8) | data[5])
        elif cid == 0x237:
            speed_237_01.append((data[0] << 8) | data[1])
        elif cid == 0x4A7 and rpm_last is not None:
            target = tpms_4a7_high if rpm_last > 900 else tpms_4a7_low
            target.append((data[2], data[3]))

    def stats(vals):
        if not vals:
            return None
        return min(vals), max(vals), sum(vals) / len(vals), len(set(vals))

    print("0x024 bytes0-1", stats(speed_024_01))
    print("0x024 bytes4-5", stats(speed_024_45))
    print("0x237 bytes0-1", stats(speed_237_01))
    print("0x1D0 bytes0-1", stats(rpm_1d0_01))
    print("0x1D0 bytes1-2", stats(rpm_1d0_12))

    tpms_sets = [
        ("4A7 byte2 low", [x[0] for x in tpms_4a7_low]),
        ("4A7 byte2 high", [x[0] for x in tpms_4a7_high]),
        ("4A7 byte3 low", [x[1] for x in tpms_4a7_low]),
        ("4A7 byte3 high", [x[1] for x in tpms_4a7_high]),
    ]

    for label, arr in tpms_sets:
        print(label + ":", stats(arr))


def main():
    if len(sys.argv) < 2:
        print("Usage: quick_probe.py <log.csv|log.bin>")
        sys.exit(1)
    path = Path(sys.argv[1])
    summarize(path)


if __name__ == "__main__":
    main()
