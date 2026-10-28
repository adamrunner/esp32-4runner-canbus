#!/usr/bin/env python3
"""
Combine cx_fields.py results across logs: the fields CANExplorer found in a
large share of the logs, each matched by bit overlap to the signals of
reference DBCs (e.g. opendbc's Toyota DBCs, or our own dbc/).

    uv run --project ~/Code/CANExplorer python -I analysis/cx_consensus.py \
        analysis/decoded/cx --dbc ~/Code/opendbc/toyota_rav4_2017_pt_generated.dbc

`J` is the Jaccard overlap of the two bit sets; 1.00 is the same bits.
"""

import argparse
import glob
import json
import os
from collections import defaultdict

import cantools


def field_bits(start, length, order):
    """Bit numbers of a field in DBC numbering (Motorola starts at the MSB)."""
    if order == "I":
        return frozenset(range(start, start + length))
    bits, bit = [], start
    for _ in range(length):
        bits.append(bit)
        bit = bit - 1 if bit % 8 else bit + 15
    return frozenset(bits)


def load_dbc_signals(paths):
    signals = defaultdict(list)
    for path in paths:
        # Old opendbc generated files have comment lines cantools rejects; comments aren't needed
        with open(path, encoding="utf-8", errors="replace") as src:
            text = "".join(line for line in src if not line.startswith("CM"))
        db = cantools.database.load_string(text, "dbc", strict=False)
        tag = os.path.splitext(os.path.basename(path))[0].replace("_pt_generated", "")
        for msg in db.messages:
            for sig in msg.signals:
                order = "M" if sig.byte_order == "big_endian" else "I"
                signals[msg.frame_id].append({
                    "name": f"{msg.name}.{sig.name}",
                    "bits": field_bits(sig.start, sig.length, order),
                    "spec": f"{sig.start}|{sig.length}{order} x{sig.scale:g}{sig.offset:+g}"
                            + (f" {sig.unit}" if sig.unit else ""),
                    "tag": tag,
                })
    return signals


def main():
    parser = argparse.ArgumentParser(description="Fields found consistently across logs")
    parser.add_argument("cx_dir", help="Directory of cx_fields.py JSON")
    parser.add_argument("--dbc", action="append", default=[], help="Reference DBC (repeatable)")
    parser.add_argument("--min-share", type=float, default=0.3,
                        help="Show fields found in at least this share of the logs with the ID")
    args = parser.parse_args()

    logs = []
    for path in sorted(glob.glob(os.path.join(args.cx_dir, "*.json"))):
        with open(path, encoding="utf-8") as src:
            logs.append(json.load(src))
    dbc = load_dbc_signals(args.dbc)

    present = defaultdict(int)
    seen = defaultdict(lambda: defaultdict(int))
    example = {}
    for log in logs:
        for cid, item in log["ids"].items():
            present[cid] += 1
            for f in item["fields"]:
                key = (f["kind"], f["start"], f["length"], f["order"], f["mux"])
                seen[cid][key] += 1
                example[(cid, key)] = f

    print(f"{len(logs)} logs\n")
    for cid in sorted(present, key=lambda c: int(c, 16)):
        threshold = max(3, args.min_share * present[cid])
        rows = [(key, count) for key, count in seen[cid].items() if count >= threshold]
        if not rows:
            continue
        print(f"0x{cid} (in {present[cid]}/{len(logs)} logs)")
        for key, count in sorted(rows, key=lambda r: (r[0][4] if r[0][4] is not None else -1, r[0][1])):
            kind, start, length, order, mux = key
            f = example[(cid, key)]
            if kind in ("checksum", "counter"):
                extra = f" {f['details']}"
            else:
                extra = f" raw {f['min']:g}..{f['max']:g}"
            mux_text = f" mux={mux}" if mux is not None else ""
            print(f"   {kind:<9} {start}|{length}{order}{mux_text}{extra}  {count}/{present[cid]}")
            if mux is not None:
                continue
            bits = field_bits(start, length, order)
            matches = set()
            for sig in dbc.get(int(cid, 16), []):
                if bits & sig["bits"]:
                    jaccard = len(bits & sig["bits"]) / len(bits | sig["bits"])
                    matches.add((jaccard, f"{sig['name']} {sig['spec']} [{sig['tag']}]"))
            for jaccard, text in sorted(matches, reverse=True)[:2]:
                print(f"{'':14}~ {text} J={jaccard:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
