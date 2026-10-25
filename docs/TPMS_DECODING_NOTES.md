# TPMS Decoding Notes - Toyota 4Runner

## Status (2026-10-25): TPMS is not on the DLC3 CAN bus

On this 2018 4Runner, tire pressure is not available from the OBD port's CAN
bus, either on request or as a broadcast:

1. **No ECU at OBDb's TPMS address.** Nothing answers at `0x750` ext `2A`, not
   even tester present, while other ECUs on the same bus do.
2. **No other diagnostic sub-ECU carries it.** Only `0x40` (main body ECU) and
   `0xDC` answer behind `0x750`; `0xDC` did not react to a deflated tire.
3. **No broadcast carries it.** Over a 14-minute capture in which FL swung
   between 22.7 and 35.2 PSI (RF receiver as ground truth), no CAN signal
   tracked the pressure.
4. **The TPMS lamp is not on this bus either.** The dash TPMS lamp came on when
   FL went below ~25 PSI, but no bit on the bus changed at those crossings.
   The TPMS ECU reaches the cluster by some other path (another bus or a
   direct line), which also explains (1)-(3).

The `esp32-4runner-tpms` RF receiver (CC1101, 315 MHz) is the source for tire
pressures. The longer-term plan is a separate project CAN network, with a
gateway bridging selected vehicle-CAN data onto it and the RF receiver as
another node there.

Not yet tried:

- **Plain 11-bit request IDs `0x700-0x7F7`.** Only the extended addresses
  behind `0x750` have been swept. Skip IDs already seen on the bus (`0x770`,
  `0x7C6`, `0x7CE` appear unprompted) to avoid colliding with real traffic.
  Less likely to pay off given (3) and (4).
- **K-line on DLC3 pin 7.** OBDb's 2003-2009 4Runner TPMS entry is KWP on
  K-line (header `81 2A`, 9600 baud), and `2A` is the same address OBDb gives
  for CAN. Check pin 7 for ~12 V idle with the ignition on; if present, try
  `21 30` there with a K-line transceiver (L9637D, SI9241) on a spare UART.

## Truck and tools

- 2018 4Runner, 2018 cluster (warning lamp only, no per-wheel display).
- Sensors: PMV-107J at 315 MHz. Mapped IDs FL `01069354`, FR `0101e011`,
  RL `072e7108`, RR `0739510d`; nearby `0241xx31` IDs belong to other cars.
- Lamp threshold: on below ~25 PSI (observed on FL; placard 32 PSI).
- RF receiver: `http://tpms.local`, JSON state at `/api/state` (`sensors[]`
  with `id`, `pos`, `psi`, `temp_c`, `age_s`, `count`; `map` of wheel -> ID).
  While a tire's pressure is changing, its sensor reports about every 15 s.
- `scripts/tpms_rf_record.py`: records `/api/state` to JSONL with laptop time.
- `scripts/tpms_correlate.py`: correlates every byte / 16-bit pair of a CAN
  `.bin` log with the recorded RF pressures (`r` on levels, `r_step` on 10 s
  changes, ranked by `r_step`; see the next section for why).
- Firmware (`main/4runner_canbus_main.cpp`), all off by default:
  `k_tpms_sweep_on_boot` (0x750 address sweep, TPMS PID probe of each
  responder, then `21 00-FF` on `0xDC`) and `k_ecu_ident_on_boot` (KWP `1A`).
  Replies from any `0x750` sub-ECU are logged in full as `Sub-ECU 0xNN reply`,
  and any unrecognized `0x7xx` frame as `Unhandled 0xNNN`.

## Clock alignment for .bin logs

The board's RTC drifts: on 2026-10-25 it was 6004.5 s behind the laptop
(boot log printed `Current RTC time: 12:12:58` at laptop 13:53:02.5).
Separately, the firmware stores the RTC's local time in the `.bin` header as
if it were UTC, so header times are off by the time-zone offset as well. For
`tpms_correlate.py --clock-offset`, add both: 6004.5 + 25200 (UTC-7) =
31204.5. The script reports when the CAN and RF times do not overlap.

## Correlation method: why `r_step`

Level correlation alone gives false positives: on a real drive log with a
made-up deflate/inflate profile (no pressure data in it at all), slowly
drifting signals such as the `0x3BB` coolant-temperature candidate reached
|r| = 0.94. Correlating changes over 10 s windows separates them: the same
test peaked at `r_step` 0.34, while a planted pressure byte scored 1.000 with
the expected 4.0 counts/PSI. A real pressure signal needs both high.

## Evidence

### 2018: no ECU at 0x750 / 2A

OBDb's 4Runner signal set lists these at `0x750` ext `2A`, reply `0x758`:

| Request | Meaning (OBDb) |
|---|---|
| `21 30` | FL, FR, RL, RR, spare: bar = raw / 58 - 0.5 (0 = no data); lists 2018 |
| `21 15` | "set 2" pressures, bar = raw * 69 / 4000 - 0.5; all years |
| `21 16` | temperatures, C = raw - 40; 2020+ |
| `22 1005` | FR, RR, FL, RL, spare, PSI = raw / 4.125 - 5.697; 2026+ |

`components/tpms_diag` implements framing (extended addressing, ISO-TP flow
control and reassembly) and decoders for all four, host-tested.

Engine running, 2026-10-25: no reply to `3E 00`, `21 30`, `21 15`, `22 1005`
or `21 16` at ext `2A` (95 requests in 60 s, `tx_fail=0`), while the meter at
`0x7C0` answered all 19 of its polls. No unrecognized `0x7xx` frames either.

### 0x750 address sweep

Tester present (`3E 00`) to all 256 extended addresses found two sub-ECUs,
both answering `7F 3E 12` (they want a different tester-present form; any
diagnostic ECU at `2A` would have answered the same way). Byte 0 of each reply
is the responder's address.

| Ext | 21 30 | 21 15 | 22 1005 | 21 16 | 1A 81 |
|---|---|---|---|---|---|
| `0x40` | NRC 12 | `61 15 30 00 40 00` | NRC 11 | NRC 12 | "35240" |
| `0xDC` | NRC 12 | NRC 12 | NRC 11 | NRC 12 | "35010" |

NRC 12 = sub-function/PID not supported, NRC 11 = service not supported (no
UDS). `0x40` is the main body ECU by Toyota convention; its `21 15` is body
data that shares the PID number. KWP `1A 80-9F`: each answers only `1A 81`
(the reply does not echo the option byte), NRC 12 to the rest. Neither code
is a Toyota TPMS part number (89769-xxxxx).

### 0xDC is not the TPMS ECU

`21 00/20/.../E0` return supported-PID bitmaps (as in OBD mode 01): `0xDC`
supports `21 03, 05, 07, 08, E1, E2`. Polled every ~3 s for 10 minutes while
FL went 30.2 -> 28.4 PSI: nothing moved in the 7.5 minutes after the drop.

| PID | Contents |
|---|---|
| `08` | Supply voltage, 0.1 V/count (`8B` = 13.9 V, matches a battery voltmeter; flickers `8A`/`8B`) |
| `E1` | `00 36 90 00 xx xx`: last two bytes a free-running 100 ms counter that survives our reboots |
| `03` | `00 00` |
| `05` | `80 00 00` |
| `07` | 18 bytes, all zero (multi-frame; needs ~250 ms before the next request to `0xDC`) |
| `E2` | `00 3E 99 01 00` |

### Passive correlation against RF ground truth

`CAN_20261025_121704.bin` (933k frames, 13:57-14:11) alongside
`tpms_rf_record.py`. FL, engine running:
28.8 -> 35.2 -> 24.5 -> 34.9 -> 22.7 -> 34.2 PSI, 24 reports. Lamp came on
below ~25 PSI: first crossing at 14:02:20 (24.5), back above at 14:02:51
(26.3), below again at 14:05:26 (22.7).

- `tpms_correlate.py`: best `r_step` 0.58 (`0x3B1` b3, `0x620` b2, `0x639` b2)
  with `r` <= 0.24. All three are flags that change only at the start
  (13:57-13:58) and end (14:10:3x) of the session, not at the swings.
- Bit-level scan for the lamp: no quiet bit changes near both sub-25 PSI
  crossings. Bits that do are counters or busy signals (`0x2C1`, `0x3D3`,
  `0x49E` b1, `0x4A7` b3, `0x63B` b6 on a ~25 s/51 s/3.4 min cycle).
- `0x49D` b0 / `0x498` b4.5 pulse for ~2 s at 13:59:27, 14:00:07, 14:02:47,
  14:03:44 and 14:05:44. Three line up with FL reports, but about 20 FL
  reports have no pulse and two pulses have no report, so they are not
  "packet received" events.

### Earlier passive decode (2026-10-24): not supported

The original passive "decode" below rests on a single calibration point (all
four tires at 38 PSI) and does not hold up against the drive logs:

- `0x4A7` bytes 0/1 jump by 20+ counts (~4.6 PSI at the claimed scale) between
  consecutive frames 97 times in one 15-minute drive (`CAN_20260102_123449`),
  range 36-255, and hit 255 at the end. Real TPMS values change slowly and
  step at most about once a minute (the sensor transmit interval).
- Bytes 4/5 move in lockstep with bytes 0/1 (e.g. 197/213 -> 142/144), which
  looks like a dynamic left/right pair, not tire pressure.
- `0x49E` byte 0 is also a constant 166, so "byte near 166 at 38 PSI" does not
  single anything out. `0x498` byte 4 drifts *down* over drives as tires warm.

The 2026-10-25 correlation confirms it: none of `0x4A7`, `0x498` or `0x49E`
track FL pressure.

---

## Original notes (December 2024)

Originally believed to decode **3 of 4** tire pressure sensors from passive CAN bus broadcast messages.

**Calibration Date:** December 12, 2024
**Known Tire Pressure:** 38 PSI (all 4 tires)
**Ambient Temperature:** ~55°F (13°C)

## Decoded Signals

### CAN ID 0x4A7 - Front Axle TPMS
**DLC:** 8 bytes
**Update Rate:** ~41 messages in capture

| Byte | Signal | Formula | Unit | Notes |
|------|--------|---------|------|-------|
| 0 | FL Pressure | `x * 1.583` | kPa | ✅ Calibrated (38 PSI = 262 kPa) |
| 1 | FR Pressure | `x * 1.583` | kPa | ✅ Calibrated (38 PSI = 262 kPa) |
| 2 | Unknown | - | - | Low variation (39-44), possibly sensor ID |
| 3 | Unknown | - | - | Low variation (39-43), possibly sensor ID |
| 4 | Unknown | - | - | Values 108-122, not matching ambient temp |
| 5 | Unknown | - | - | Values 106-123, not matching ambient temp |
| 6 | Unknown | - | - | Low variation (64-70) |
| 7 | Unknown | - | - | Low variation (64-70) |

**Typical Message:**
`A6 A5 29 27 74 73 40 43`
- FL: 166 → 263 kPa (38.1 PSI) ✓
- FR: 165 → 261 kPa (37.9 PSI) ✓

### CAN ID 0x498 - Rear Axle TPMS (Partial)
**DLC:** 8 bytes
**Update Rate:** ~35 messages in capture

| Byte | Signal | Formula | Unit | Notes |
|------|--------|---------|------|-------|
| 0 | Constant | - | - | Always 0x40 (64) |
| 1 | Unknown | - | - | Values 2-7 |
| 2 | Unknown | - | - | High variation (5-255), not pressure |
| 3 | Unknown | - | - | Values 39-46, similar to 0x4A7 bytes 2-3 |
| 4 | **Rear Pressure** | `x * 1.583` | kPa | ✅ Calibrated (40 PSI = 276 kPa) |
| 5 | Unknown | - | - | Values 0-22 |
| 6 | Unknown | - | - | Values 0-13 |
| 7 | Constant | - | - | Always 0x00 |

**Typical Message:**
`40 03 9D 2C AF 0C 00 00`
- Byte 4: 175 → 277 kPa (40.2 PSI) ✓

**Question:** Which rear wheel? RL or RR?
**Hypothesis:** Byte 3 might indicate wheel position (similar to 0x4A7)

## Missing Data

### 4th Tire Pressure
The second rear tire pressure has not been located yet. Possibilities:
1. **On a different CAN ID** in the 0x49x-0x4Ax range
2. **Alternating messages** with the first rear tire on 0x498
3. **Different message format** (16-bit values, different scaling)
4. **Only broadcasts when different** from the other rear tire

### Temperature Data
The bytes in 0x4A7 (positions 4-7) were initially thought to be temperature using formula `(x - 40)°C`, but:
- Values 108-122 → 68-82°C (154-180°F)
- This is too hot for ambient temperature (55°F actual)
- Might be:
  - Internal sensor temperature (after driving)
  - Different encoding method
  - Not temperature at all

## Calibration Formula

### Discovered Formula
```
Pressure (kPa) = raw_value × 1.583
Pressure (PSI) = raw_value × 0.2296
```

### Verification
| Raw | kPa | PSI | Target | Match |
|-----|-----|-----|--------|-------|
| 166 | 263 | 38.1 | 38 | ✅ |
| 165 | 261 | 37.9 | 38 | ✅ |
| 175 | 277 | 40.2 | 38 | ⚠️ +2 PSI |

The rear tire reading 40 PSI instead of 38 PSI could indicate:
- Actual pressure difference
- Temperature compensation
- Different calibration per axle
- Measurement at different time

## Comparison with OBDb Data

The OBDb database contains **diagnostic request/response** formulas (OBD-II):
```
Pressure = (raw / 58) - 0.5 bars
```

This is **different** from the passive broadcast formula we discovered:
```
Pressure = raw × 1.583 kPa = raw × 0.2296 PSI
```

**Why the difference?**
- OBDb formulas are for **ECU query responses** (request/response protocol)
- We're capturing **passive broadcast messages** on the CAN bus
- Different message formats use different encoding

## ScanGauge II Reference

ScanGauge XGauge commands for Toyota TPMS:
- **Method 1** (ISO): `TXD: 822AF02162`, multiple formula options
- **Method 2** (CAN): `TXD: 07502A2190`, different formulas

These are diagnostic commands, not passive messages.

## Other Observed CAN IDs with Similar Values

IDs that might contain the 4th tire pressure (byte values 150-180 range):

| CAN ID | Byte | Range | Avg | Notes |
|--------|------|-------|-----|-------|
| 0x49E | 0 | 166-166 | 166.0 | ⭐ Always 166 (constant) |
| 0x638 | 4 | 170-170 | 170.0 | ⭐ Always 170 (constant) |
| 0x617 | 5 | 108-180 | 139.1 | ⚠️ High variation |
| 0x1AA | 5 | 177-177 | 177.0 | Always 177 (steering angle msg) |

**Candidates to investigate:**
- 0x49E: Byte 0 constant at 166 (38.2 PSI with our formula)
- 0x638: Byte 4 constant at 170 (39.1 PSI with our formula)

If these are tire pressures, they're suspiciously constant (no variation).

## Next Steps for Finding 4th Tire

### Option 1: Drive and Capture
1. Capture data while **stationary**
2. Capture data while **driving** (pressure increases with heat)
3. Capture data after **changing one tire pressure**
4. Compare which bytes change

### Option 2: Analyze Message Timing
Look for messages that:
- Broadcast at same rate as 0x4A7 and 0x498
- Have byte values in the ~165-175 range
- Change slightly over time (like real tire pressure)

### Option 3: Check for Multi-Message Protocol
The 4th tire might be:
- In the same message but **16-bit encoding** (spans 2 bytes)
- Sent in **alternating messages** with the 3rd tire
- Only sent when **pressure changes**

### Option 4: Search Remaining 0x49x IDs
Systematically check:
- 0x499, 0x49A, 0x49B, 0x49C, 0x49D, 0x49E, 0x49F
- Look for bytes with low variation around 165-175 range

## Conclusion

We've successfully reverse-engineered the TPMS encoding for **front axle and 1 rear tire** using empirical calibration. The formula `x * 1.583 kPa` (or `x * 0.2296 PSI`) accurately decodes tire pressure from the raw CAN bus data.

The 4th tire pressure remains elusive but is likely in one of the other 0x49x messages or encoded differently in 0x498.

## Tools Created

- `investigate_tpms.py` - Analyzes TPMS byte patterns and formulas
- `calibrate_tpms.py` - Interactive calibration tool using known tire pressure
- `find_rear_tpms.py` - Searches for rear tire data in all CAN IDs
- `scripts/decode_with_obdb.py` - Updated with calibrated TPMS formulas

## References

- [OBDb Toyota 4Runner Database](https://github.com/brendan-w/obdb)
- [ScanGauge II XGauge Commands](https://www.scangauge.com/support/x-gauge-commands/toyota-lexus-scion/)
- Toyota 4Runner generation: Fifth Generation (N280), 2010-2023
