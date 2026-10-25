#!/usr/bin/env python3
"""
TPMS CAN Correlator
Searches every passive CAN signal for one that tracks a tire's pressure, using
the RF receiver's readings (tpms_rf_record.py) as ground truth.

For each CAN ID it tries every byte and every adjacent 16-bit pair (big- and
little-endian), averages the values into fixed time bins, and computes the
Pearson correlation with each wheel's RF pressure over the same bins (r), and
of their changes over --step-window seconds (r_step). A small lag search
absorbs error in the clock offset. Results are ranked by r_step: slowly
drifting signals (temperatures warming up) can reach a high r against a
deflate-then-inflate profile, but only a real pressure signal steps when the
tire does and stays flat in between.

Clocks: CAN records carry the ESP32 RTC's wall time, which can be far off.
Pass --clock-offset = (laptop time - RTC time) in seconds. To measure it, note
the laptop clock when the boot log prints "rtc: Current RTC time: ...", then add
your UTC offset (e.g. +25200 for UTC-7): the firmware stores the RTC's local
time in the .bin header as if it were UTC.

Usage:
    ./scripts/tpms_correlate.py logs/CAN_20261025_121500.bin logs/rf.jsonl --clock-offset 6004.5
    ./scripts/tpms_correlate.py CAN.bin rf.jsonl --clock-offset 6004.5 --wheel FL --top 30
"""

import argparse
import datetime as dt
import json
import struct
import sys
from pathlib import Path

import numpy as np

HEADER_FMT = "<8sHHQQII28s"
HEADER_SIZE = 64
MAGIC_PREFIX = b"CANBIN\x00"
RECORD_DTYPE = np.dtype([
    ("timestamp_us", "<u8"),
    ("can_id", "<u4"),
    ("dlc", "u1"),
    ("flags", "u1"),
    ("data", "u1", (8,)),
    ("reserved", "<u2"),
])
WHEELS = ("FL", "FR", "RL", "RR")


def load_can_bin(path):
    """Returns (unix_seconds, can_id, data[N, 8]) from a CANBIN v1 log."""
    raw = Path(path).read_bytes()
    magic, version, header_size, start_unix_us, start_mono_us, record_size, _, _ = \
        struct.unpack(HEADER_FMT, raw[:HEADER_SIZE])
    if not magic.startswith(MAGIC_PREFIX) or version != 1:
        raise ValueError(f"{path}: not a CANBIN v1 log")
    if record_size != RECORD_DTYPE.itemsize or header_size != HEADER_SIZE:
        raise ValueError(f"{path}: unexpected header/record size")
    if start_unix_us == 0:
        raise ValueError(f"{path}: RTC was invalid at log start; no wall-clock time")

    body = raw[HEADER_SIZE:]
    body = body[:len(body) - len(body) % record_size]
    rec = np.frombuffer(body, dtype=RECORD_DTYPE)
    mono = rec["timestamp_us"].astype(np.int64) - np.int64(start_mono_us)
    # Same naive-local interpretation as analysis/bin_to_csv.py's datetime column
    unix_s = (np.int64(start_unix_us) + mono) / 1e6
    return unix_s, rec["can_id"], rec["data"]


def load_rf_reports(path):
    """Returns {wheel: (times[], psi[])} of distinct sensor reports, in host time."""
    reports = {w: {} for w in WHEELS}
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                snap = json.loads(line)
            except json.JSONDecodeError:
                continue
            state = snap["state"]
            id_to_pos = {v: k for k, v in (state.get("map") or {}).items()}
            for s in state.get("sensors", []):
                pos = s.get("pos") or id_to_pos.get(s["id"])
                if pos not in reports or s.get("age_s") is None:
                    continue
                t = snap["host_unix"] - s["age_s"]
                # One report seen across several polls: key on its (rounded) send time
                reports[pos][round(t / 3)] = (t, s["psi"])
    out = {}
    for wheel, by_key in reports.items():
        if by_key:
            pts = sorted(by_key.values())
            out[wheel] = (np.array([p[0] for p in pts]), np.array([p[1] for p in pts]))
    return out


def candidate_signals(data):
    """Yields (name, values[N]) for each byte and adjacent 16-bit pair."""
    d = data.astype(np.int32)
    for i in range(8):
        yield f"b{i}", d[:, i]
    for i in range(7):
        yield f"b{i}b{i + 1}_be", (d[:, i] << 8) | d[:, i + 1]
        yield f"b{i}b{i + 1}_le", d[:, i] | (d[:, i + 1] << 8)


def bin_mean(bin_idx, values, n_bins):
    counts = np.bincount(bin_idx, minlength=n_bins)
    sums = np.bincount(bin_idx, weights=values, minlength=n_bins)
    with np.errstate(invalid="ignore", divide="ignore"):
        return sums / counts, counts > 0


def window_changes(series, ok, k):
    """series[i + k] - series[i] wherever both bins are valid."""
    both = ok[k:] & ok[:-k]
    return (series[k:] - series[:-k])[both], both


def pearson(x, y):
    x = x - x.mean()
    y = y - y.mean()
    denom = np.sqrt((x * x).sum() * (y * y).sum())
    return float((x * y).sum() / denom) if denom > 0 else float("nan")


def main():
    parser = argparse.ArgumentParser(description="Correlate CAN signals with RF tire pressures")
    parser.add_argument("can_log", help="CANBIN .bin log from the SD card")
    parser.add_argument("rf_log", help="JSONL from tpms_rf_record.py")
    parser.add_argument("--clock-offset", type=float, required=True,
                        help="Seconds to add to CAN (RTC) time to get laptop time")
    parser.add_argument("--bin", type=float, default=2.0, help="Time bin width (s)")
    parser.add_argument("--step-window", type=float, default=10.0,
                        help="Window (s) over which changes are compared for r_step")
    parser.add_argument("--max-lag", type=float, default=20.0,
                        help="Search CAN-vs-RF lag within +/- this many seconds")
    parser.add_argument("--wheel", choices=WHEELS, help="Only correlate against this wheel")
    parser.add_argument("--min-frames", type=int, default=20, help="Skip IDs with fewer frames")
    parser.add_argument("--top", type=int, default=20, help="Rows to print")
    args = parser.parse_args()

    can_t, can_id, data = load_can_bin(args.can_log)
    can_t = can_t + args.clock_offset
    rf = load_rf_reports(args.rf_log)

    wheels = [w for w in ([args.wheel] if args.wheel else WHEELS) if w in rf]
    if not wheels:
        sys.exit("No RF reports for the requested wheel(s)")

    fmt = lambda t: dt.datetime.fromtimestamp(t).strftime("%H:%M:%S")
    print(f"CAN:  {len(can_t)} frames, {fmt(can_t[0])} - {fmt(can_t[-1])} (laptop time)")
    for w in wheels:
        t, p = rf[w]
        print(f"RF {w}: {len(t)} reports, {fmt(t[0])} - {fmt(t[-1])}, "
              f"{p.min():.1f}-{p.max():.1f} PSI")

    # Common bin grid over the CAN capture; RF pressure is a step function between reports
    t0, t1 = can_t[0], can_t[-1]
    rf_lo = min(rf[w][0][0] for w in wheels)
    rf_hi = max(rf[w][0][-1] for w in wheels)
    if rf_hi < t0 or rf_lo > t1:
        gap_h = ((rf_lo + rf_hi) / 2 - (t0 + t1) / 2) / 3600
        sys.exit(f"CAN and RF times do not overlap (RF is {gap_h:+.2f} h from CAN); "
                 "check --clock-offset, including the time-zone term")
    n_bins = int((t1 - t0) // args.bin) + 1
    centers = t0 + (np.arange(n_bins) + 0.5) * args.bin
    truth = {}
    for w in wheels:
        rt, rp = rf[w]
        idx = np.searchsorted(rt, centers, side="right") - 1
        valid = idx >= 0  # before the first report the pressure is unknown
        if not valid.any() or np.ptp(rp[idx[valid]]) == 0:
            print(f"RF {w}: pressure constant during the CAN capture; skipped")
            continue
        truth[w] = (np.where(valid, rp[np.clip(idx, 0, None)], np.nan), valid)
    if not truth:
        sys.exit("No wheel's pressure changed during the CAN capture; nothing to correlate")

    lag_bins = int(args.max_lag // args.bin)
    step_bins = max(1, int(round(args.step_window / args.bin)))
    results = []
    for cid in np.unique(can_id):
        mask = can_id == cid
        if mask.sum() < args.min_frames:
            continue
        rel = can_t[mask] - t0
        for name, values in candidate_signals(data[mask]):
            if np.ptp(values) == 0:
                continue
            for lag in range(-lag_bins, lag_bins + 1):
                b = np.floor(rel / args.bin).astype(np.int64) + lag
                keep = (b >= 0) & (b < n_bins)
                if not keep.any():
                    continue
                sig, has = bin_mean(b[keep], values[keep].astype(float), n_bins)
                for w, (psi, valid) in truth.items():
                    ok = has & valid
                    if ok.sum() < 10 or np.ptp(psi[ok]) == 0 or np.ptp(sig[ok]) == 0:
                        continue
                    r = pearson(sig[ok], psi[ok])
                    d_sig, both = window_changes(sig, ok, step_bins)
                    d_psi = (psi[step_bins:] - psi[:-step_bins])[both]
                    if np.ptp(d_sig) == 0 or np.ptp(d_psi) == 0:
                        continue
                    r_step = pearson(d_sig, d_psi)
                    slope = np.polyfit(psi[ok], sig[ok], 1)[0]
                    results.append((abs(r_step), r_step, r, int(cid), name, w, lag * args.bin,
                                    int(ok.sum()), int(values.min()), int(values.max()), slope))

    # Keep the best lag per (ID, signal, wheel)
    best = {}
    for row in results:
        key = (row[3], row[4], row[5])
        if key not in best or row[0] > best[key][0]:
            best[key] = row
    ranked = sorted(best.values(), key=lambda r: r[0], reverse=True)

    # A 16-bit pair that does no better than one of its own bytes is just that byte again
    def redundant(row):
        cid, name, w = row[3:6]
        if not name.endswith(("_be", "_le")):
            return False
        for byte in (name[:2], name[2:4]):
            single = best.get((cid, byte, w))
            if single and single[0] >= row[0] - 0.01:
                return True
        return False

    ranked = [row for row in ranked if not redundant(row)]

    print()
    print(f"{'ID':>6} {'signal':<10} {'wheel':<5} {'r_step':>7} {'r':>7} {'lag s':>6} "
          f"{'bins':>5} {'raw range':>13} {'counts/PSI':>10}")
    for _, r_step, r, cid, name, w, lag, n, lo, hi, slope in ranked[:args.top]:
        print(f"0x{cid:03X} {name:<10} {w:<5} {r_step:>7.3f} {r:>7.3f} {lag:>6.0f} {n:>5} "
              f"{lo:>6}-{hi:<6} {slope:>10.2f}")
    print()
    print("A real pressure signal: r_step and r both near +/-1 for one wheel, a few "
          "counts/PSI (OBDb's 21 30 scale is ~4.0), and a lag well inside +/-max-lag.")


if __name__ == "__main__":
    main()
