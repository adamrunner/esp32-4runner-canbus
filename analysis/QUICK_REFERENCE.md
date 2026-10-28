# Quick Reference: Broadcast Decoding Status

Decoded signals live in [`dbc/toyota_4runner_2018.dbc`](../dbc/toyota_4runner_2018.dbc).
This page is the evidence behind them and the list of what is still open.

Source: CANExplorer's automatic analysis (`cx_fields.py`, `cx_consensus.py`,
`cx_obdref.py`) of all 24 logs, Dec 2025 - Oct 2026, about 5.5 h of traffic, on
2026-10-28.

---

## Confirmed

### Same layout as opendbc's Toyota 2017 DBCs

These fields were found in most logs at exactly the bits of opendbc's
`toyota_rav4_2017` / `toyota_highlander_2017` messages (`~/Code/opendbc`):

| ID | Message | Signals |
|----|---------|---------|
| `0x024` | KINEMATICS | YAW_RATE `1\|10`, STEERING_TORQUE `17\|10`, ACCEL_Y `33\|10` |
| `0x025` | STEER_ANGLE_SENSOR | STEER_ANGLE `3\|12` signed, x1.5 deg |
| `0x0AA` | WHEEL_SPEEDS | four 16-bit speeds, x0.01 - 67.67 km/h (exactly 0 at rest) |
| `0x0B4` | SPEED | SPEED `47\|16` (bytes 5-6), x0.01 km/h |
| `0x224` | BRAKE_MODULE | BRAKE_PRESSED `5\|1` |
| `0x2C1` | GAS_PEDAL | GAS_RELEASED `3\|1`, GAS_PEDAL `55\|8` x0.005 |

### Matched to the polled OBD-II PIDs

`cx_obdref.py` uses the PID replies already in the logs (`0x7E8`) as references.
The DBC decodes then agree with the PIDs on `CAN_20260102_123449` (drive) and
`CAN_20260125_132609` (idle warm-up):

| Signal | Field | Fit | r | Decoded vs PID |
|--------|-------|-----|---|----------------|
| RPM | `0x1C4` bytes 0-1 BE | x0.78125 | 1.000, 5 logs | median +0.1 rpm, 95% within 11 rpm |
| Vehicle speed | `0x3D0` byte 0 | x1 km/h | 1.000, 4 logs | exact (leads the PID by ~0.2 s) |
| Throttle | `0x361` byte 3 | x0.343 + 15.8 % | 0.994-0.996, 3 of 4 logs | within 1.6 % |
| Coolant temp | `0x498` byte 3 | x1.25 C | 0.998-1.000, 2 warm-up logs | within 0.75 C (32-51 C) |
| Intake air temp | `0x49B` byte 7 | x1.25 C | 0.97-0.999 in 3 logs, 0.89 in 1 | within 1 C |

The temperatures fit with no offset, which is unusual; a cold start below
~30 C would show whether that holds.

### Checksums

Toyota's checksum, `(id_hi + id_lo + DLC + bytes 0..DLC-2) & 0xFF` in the last
byte, holds on every frame of every log (5.5 M frames) for `0x024`, `0x025`,
`0x0B4`, `0x1C4`, `0x1D0`, `0x237` (DLC 6) and `0x2C1`. Leave these bytes out of
correlation searches.

### Corrections to earlier notes

- `0x024` is not RPM and `0x025` is not gear (earlier priorities 1 and 2):
  they are kinematics and the steering angle sensor.
- `0x1C4` is RPM, not throttle; `0x1D0` is not RPM.
- `0x2C1` byte 0 is a constant (8), not coolant; the firmware showed -32 C.
- Coolant is `0x498` byte 3, not the `0x3BB` byte 2 candidate.

The firmware and `scripts/decode_with_obdb.py` now follow the DBC.

---

## Open

| ID | What the analysis sees | Next step |
|----|------------------------|-----------|
| `0x3BC` | Flags/enum near opendbc's GEAR_PACKET GEAR `13\|6`, partial overlap | Log P/R/N/D shifts while parked, then `cx_markers.py` at each shift |
| `0x1D0` | 13-bit field `4\|13M`, 3-bit enum `32\|3`, flags; firmware treats byte 4 as a gear candidate | Same gear log |
| `0x3B3`, `0x3D3` | 12-14-bit fields (`3\|12M`, `5\|14M`) | `cx_obdref.py` with more PIDs (MAF, load) in the poll rotation |
| `0x498` byte 4 | Tracks module voltage (PID 0x42) at r = 0.92-0.975, x0.077 | Log with headlights/blower toggled for a wider voltage swing |
| `0x361` bytes 1-2 | Physical fields beside throttle | Correlate with pedal (`0x2C1` GAS_PEDAL) |
| `0x49x`, `0x4A7`, `0x4A8` | Many 6-8-bit physical fields at ~2 Hz; not TPMS (TPMS_DECODING_NOTES) | Ambient temp, fuel level (meter `21 29`) as references |
| `0x620`-`0x63B` | Body flags; `0x621` carries two AUTOSAR CRC-8 bytes | Door/lock/light events with `cx_markers.py` |

---

## Workflow

```bash
# 1. Convert logs (SD card or logs/) to candump
python analysis/bin_to_candump.py logs -d logs/candump

# 2. Per-log field analysis, then what holds across logs
uv run --project ~/Code/CANExplorer python -I analysis/cx_fields.py \
    logs/candump/*.log -o analysis/decoded/cx
uv run --project ~/Code/CANExplorer python -I analysis/cx_consensus.py analysis/decoded/cx \
    --dbc dbc/toyota_4runner_2018.dbc --dbc ~/Code/opendbc/toyota_rav4_2017_pt_generated.dbc

# 3. Match broadcasts to the PIDs the firmware polled during the drive
uv run --project ~/Code/CANExplorer python -I analysis/cx_obdref.py logs/candump/CAN_*.log

# 4. Events you noted the time of (button, lamp, shift): convert with
#    --clock-offset so the log is on your clock, then
uv run --project ~/Code/CANExplorer python -I analysis/cx_markers.py \
    logs/candump/run.log 2026-10-25T14:02:20 2026-10-25T14:02:51
```

The `cx_*` scripts run in CANExplorer's environment
(`git clone https://github.com/AKJ7/CANExplorer ~/Code/CANExplorer && uv sync`).

### During a logging drive, note the time of

- Engine start/stop
- Gear shifts (P/R/N/D, while parked for the gear log)
- Mode changes (4WD H/L, ECT power/snow)
- Lock/unlock, doors, lights, wipers
- Brake and hard acceleration events

A match is trustworthy when it holds across several logs with the same factor,
and when the decoded value agrees with the PID over a wide range (a warm-up or
a drive, not idle only: on an idle log several RPM-like fields score r = 0.999).
