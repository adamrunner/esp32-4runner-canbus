# 4Runner CAN Passive RE Notes (LOG_0001)

This summarizes what we can infer from the new CSV log (`logs/LOG_0001.CSV` / `logs/LOG_0001.bin`) and what to do next when resuming.

## Current Signals of Interest

- **Candidate speed:**
  - `0x024` bytes0–1: values 415–24,162 (209 uniques), averages ~542. Shows wide variation; likely speed or movement-related.
  - `0x024` bytes4–5: values 25,012–27,137 (132 uniques); possibly another scale/offset channel.
  - `0x237` bytes0–1: very wide range 0–65,463 (374 uniques); varies a lot, could be composite/bitfield but changes with motion.

- **Candidate RPM:**
  - `0x1D0` bytes0–1: values 0–4,724 (2,489 uniques), average ~2,007. This looks more plausible than bytes1–2 (which span 0–65k). Needs a scale factor (try /4, /8, etc.).

- **TPMS-like:**
  - `0x0AA`: all four 16-bit fields identical per message, ~33 psi with the old formula. Not per-wheel pressure.
  - `0x4A7`: bytes0–3 vary; byte2/3 rise with engine activity. Averages: low RPM ~16–17, high RPM ~30–35 (range 1–43). Best TPMS candidate for pressure/temp, but scaling unknown.
  - `0x4A8`: mostly `FF FF FF FF 00 C0 60 00/01`; not useful for pressure.
  - `0x498/49C/49D/49E/49F`: present (~1.8–1.9k msgs each), but byte4/5 combos look odd (wide ranges, sometimes zeros). No clear pressure correlation yet.

- **Steering:**
  - `0x1AA` appears constant zero in this log.

- **Speed/steering gaps:**
  - `0x0B4` is all zeros; no broadcast speed found there in this capture.

## Binary Pipeline

- Convert CSV to binary for faster analysis:
  - `./scripts/convert_csv_to_bin.py logs/LOG_0001.CSV` → `logs/LOG_0001.bin`
- All scripts accept `.bin` automatically (`decode_with_obdb.py`, `decode_can.py`, `find_tpms.py`, `validate_can.py`, `quick_probe.py`).
- Quick summaries: `./scripts/quick_probe.py logs/LOG_0001.bin`

## Next Steps for a Fresh Session

1) **Lock down RPM:**
   - Assume RPM in `0x1D0` bytes0–1. Try scales (raw/4, raw/8, raw/16) and look for idle clustering ~600–800 and max reasonable revs.
   - Cross-check with any known idle rev periods if available.

2) **Find usable speed:**
   - Focus on `0x024` bytes0–1 as primary speed candidate; plot vs time to see smooth ramps. Test scales like raw/100, raw/256, raw/32, etc.
   - Compare `0x024` bytes4–5 and `0x237` bytes0–1; see if either correlates tightly to the same motion pattern.
   - Capture a short drive with a known speed (e.g., hold 20–30 mph for a few seconds) to map scale factors.

3) **Revisit TPMS:**
   - Treat `0x4A7` byte2/3 as pressure or temperature candidates; monitor stability over time. Since this drive was short, true tire pressure/temp shouldn’t change much—so large swings likely mean it’s something else.
   - If you can: log after tire pressure changes (e.g., adjust one tire a few PSI) and compare `0x4A7` values.

4) **Steering:**
   - In a new capture, turn the wheel lock-to-lock while stationary; watch `0x1AA` and other candidates (e.g., `0x025`, `0x237` bytes4–5) for signed variation.

5) **More data with known events:**
   - Record: start → idle → steady low-speed roll → brief higher speed → stop. Note approximate speeds/RPMs and any HVAC/lighting interactions. Short, annotated captures help nail scaling quickly.

6) **If OBD polling is acceptable:**
   - Capture a short session while issuing OBDb queries to bring in known response IDs (0x750/0x758/0x7B0/0x7B8/0x780). Then the existing OBDb map can decode those signals directly.

## Handy Commands

- Binary summary probe: `./scripts/quick_probe.py logs/LOG_0001.bin`
- Detailed decode of a specific ID: `./scripts/decode_can.py logs/LOG_0001.bin --id 0x024` (or other ID)
- TPMS search: `./scripts/find_tpms.py logs/LOG_0001.bin`
- OBDb decode (for completeness): `./scripts/decode_with_obdb.py logs/LOG_0001.bin --id 0x4A7`

Use these notes as the starting point when new captures are available.
